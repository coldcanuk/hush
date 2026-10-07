/* hush_keyfile.c: encrypts nsecs into $HUSH_HOME/keys.vault. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hush_home.h"
#include "hush_keyfile.h"
#include "hush_pass.h"

#define HUSH_KEYFILE_NAME "keys.vault"
#define HUSH_KEYFILE_MAGIC "HUSHVAULT1\n"

enum {
    HUSH_KEYFILE_SALT = 16,
    HUSH_KEYFILE_IV = 12,
    HUSH_KEYFILE_TAG = 16,
    HUSH_KEYFILE_KEY = 32,
    HUSH_KEYFILE_ROUNDS = 20000,
    HUSH_KEYFILE_PHRASE_MAX = 128,
    HUSH_KEYFILE_PLAIN_MAX = 4096,
    HUSH_KEYFILE_FILE_MAX = 8192,
    HUSH_KEYFILE_HEX_MAX = 8192,
    HUSH_KEYFILE_MODE = 0600,
    HUSH_KEYFILE_NIBBLE = 0x0f,
    HUSH_KEYFILE_HEX_LETTER = 10
};

/* Ciphertext and the header that opens it. blob_n is the ciphertext length. */
typedef struct hush_keyfile_parts {
    unsigned char salt[HUSH_KEYFILE_SALT];
    unsigned char iv[HUSH_KEYFILE_IV];
    unsigned char tag[HUSH_KEYFILE_TAG];
    unsigned char blob[HUSH_KEYFILE_PLAIN_MAX];
    size_t blob_n;
} hush_keyfile_parts_t;

/* Four hex lines of one vault file. ct holds the ciphertext line. */
typedef struct hush_keyfile_hexlines {
    char salt[HUSH_KEYFILE_SALT * 2 + 1];
    char iv[HUSH_KEYFILE_IV * 2 + 1];
    char tag[HUSH_KEYFILE_TAG * 2 + 1];
    char ct[HUSH_KEYFILE_HEX_MAX];
} hush_keyfile_hexlines_t;

/* Pointers into a mutable vault text. Not owned. */
typedef struct hush_keyfile_fields {
    char *salt;
    char *iv;
    char *tag;
    char *ct;
} hush_keyfile_fields_t;

/* One GCM operation. tag is written on seal and read on open. */
typedef struct hush_keyfile_crypt {
    unsigned char *out;
    size_t *out_n;
    const unsigned char *in;
    size_t in_n;
    const unsigned char *key;
    const unsigned char *iv;
    unsigned char *tag;
} hush_keyfile_crypt_t;

/* One path/secret record inside the plaintext. next is the byte after it. */
typedef struct hush_keyfile_pair {
    const char *path;
    size_t path_n;
    const char *secret;
    size_t secret_n;
    const char *next;
} hush_keyfile_pair_t;

/* Plaintext body passed into find. bytes is not owned. */
typedef struct hush_keyfile_body {
    const unsigned char *bytes;
    size_t n;
} hush_keyfile_body_t;

/* Growing replacement plaintext. dst is owned by the caller. */
typedef struct hush_keyfile_edit {
    unsigned char *dst;
    size_t dstsz;
    size_t used;
    int replaced;
} hush_keyfile_edit_t;

/* 1 when the phrase is set, 0 when the vault is off, -1 when it does not fit. */
static int hush_keyfile_phrase(char *out, size_t outsz);

/* Writes the vault path. 0 on overflow. */
static int hush_keyfile_vault_path(char *out, size_t outsz);

/* Fills buf from /dev/urandom. Fails HUSH_ERR_IO. */
static hush_status_t hush_keyfile_random(unsigned char *buf, size_t n);

/* Derives a 32-byte key. Fails HUSH_ERR_CRYPTO. */
static hush_status_t hush_keyfile_derive(unsigned char *key,
                                        const unsigned char *salt,
                                        const char *phrase);

/* Starts AES-256-GCM on ctx. 0 when OpenSSL refuses. */
static int hush_keyfile_gcm_ok(EVP_CIPHER_CTX *ctx, const unsigned char *key,
                              const unsigned char *iv, int encrypt);

/* Seals box->in into box->out, including the tag. Fails HUSH_ERR_CRYPTO. */
static hush_status_t hush_keyfile_seal(hush_keyfile_crypt_t *box);

/* Opens box->in into box->out. CRYPTO when the tag does not match. */
static hush_status_t hush_keyfile_open(hush_keyfile_crypt_t *box);

/* Encodes n bytes as hex. 0 when it does not fit. */
static int hush_keyfile_hex_put(char *dst, size_t dstsz,
                               const unsigned char *src, size_t n);

/* Maps one hex digit to 0..15. -1 when the digit is not hex. */
static int hush_keyfile_hex_val(char ch);

/* Decodes hex into dst. 0 on a bad digit or odd length. */
static int hush_keyfile_hex_get(unsigned char *dst, size_t dstsz,
                               size_t *out_n, const char *hex);

/* Reads the vault file into text. NOT_FOUND when absent. */
static hush_status_t hush_keyfile_slurp(char *text, size_t textsz);

/* Cuts one line out of *cursor. 0 when no newline remains. */
static int hush_keyfile_cut_line(char **cursor, char **line);

/* Splits text into the four vault lines. CRYPTO on a bad file. */
static hush_status_t hush_keyfile_fields(char *text, hush_keyfile_fields_t *out);

/* Decodes a hex line of exactly expect bytes. 0 otherwise. */
static int hush_keyfile_take_hex(unsigned char *dst, size_t expect,
                                const char *hex);

/* Decodes the four lines into parts. CRYPTO on a bad line. */
static hush_status_t hush_keyfile_decode(hush_keyfile_parts_t *out,
                                        const hush_keyfile_fields_t *fields);

/* Decrypts parts into plain. CRYPTO when the phrase is wrong. */
static hush_status_t hush_keyfile_decrypt(unsigned char *plain, size_t *plain_n,
                                         const hush_keyfile_parts_t *parts,
                                         const char *phrase);

/* Reads the vault. NOT_FOUND when absent. CRYPTO when the phrase fails. */
static hush_status_t hush_keyfile_read(unsigned char *plain, size_t *plain_n,
                                      const char *phrase);

/* Seals plain under a fresh salt and iv. */
static hush_status_t hush_keyfile_encrypt(hush_keyfile_parts_t *sealed,
                                         const unsigned char *plain,
                                         size_t plain_n, const char *phrase);

/* Hex-encodes sealed into lines. FULL when a line does not fit. */
static hush_status_t hush_keyfile_encode(hush_keyfile_hexlines_t *lines,
                                        const hush_keyfile_parts_t *sealed);

/* Opens path.tmp at mode 0600. The caller owns *out. */
static hush_status_t hush_keyfile_open_tmp(const char *tmp, FILE **out);

/* Closes fp. Unlinks tmp when wrote is 0 or the close fails. */
static hush_status_t hush_keyfile_close_tmp(FILE *fp, const char *tmp, int wrote);

/* Replaces the vault with lines. 0600. */
static hush_status_t hush_keyfile_commit(const char *path,
                                        const hush_keyfile_hexlines_t *lines);

/* Replaces the vault with plain. 0600. */
static hush_status_t hush_keyfile_write(const unsigned char *plain,
                                       size_t plain_n, const char *phrase);

/* Points pair at the next record. 0 when the record is cut off. */
static int hush_keyfile_next_rec(const char *p, const char *end,
                                hush_keyfile_pair_t *pair);

/* Copies secret into out. FULL when it does not fit. */
static hush_status_t hush_keyfile_copy_secret(char *out, size_t outsz,
                                             const char *secret, size_t n);

/* Copies the secret for path. NOT_FOUND when path is absent. */
static hush_status_t hush_keyfile_find(char *out, size_t outsz,
                                      const hush_keyfile_body_t *body,
                                      const char *path);

/* Appends path and secret. FULL when the edit does not fit. */
static hush_status_t hush_keyfile_append(hush_keyfile_edit_t *edit,
                                        const char *path, const char *secret);

/* Copies one existing record into the edit. FULL when it does not fit. */
static hush_status_t hush_keyfile_keep(hush_keyfile_edit_t *edit,
                                      const char *rec, size_t rec_n);

/* Copies the edit back over plain, then wipes the edit buffer. */
static void hush_keyfile_apply(unsigned char *plain, size_t *plain_n,
                              hush_keyfile_edit_t *edit);

/* Inserts or replaces path. FULL when the result does not fit. */
static hush_status_t hush_keyfile_upsert(unsigned char *plain, size_t *plain_n,
                                        const char *path, const char *secret);

hush_status_t hush_keyfile_save(const char *path, const char *secret)
{
    char phrase[HUSH_KEYFILE_PHRASE_MAX] = {0};
    unsigned char plain[HUSH_KEYFILE_PLAIN_MAX] = {0};
    size_t plain_n = 0;
    int have = 0;
    hush_status_t st = HUSH_OK;

    if (path == NULL || secret == NULL || path[0] == '\0' || secret[0] == '\0')
        return HUSH_ERR_ARG;
    if (strchr(path, '\n') != NULL || strchr(secret, '\n') != NULL)
        return HUSH_ERR_ARG;
    have = hush_keyfile_phrase(phrase, sizeof(phrase));
    if (have < 0)
        return HUSH_ERR_ARG;
    if (have == 0)
        return HUSH_OK;
    st = hush_keyfile_read(plain, &plain_n, phrase);
    if (st == HUSH_ERR_NOT_FOUND)
        st = HUSH_OK;
    if (st != HUSH_OK) {
        OPENSSL_cleanse(phrase, sizeof(phrase));
        return st;
    }
    st = hush_keyfile_upsert(plain, &plain_n, path, secret);
    if (st == HUSH_OK)
        st = hush_keyfile_write(plain, plain_n, phrase);
    OPENSSL_cleanse(plain, sizeof(plain));
    OPENSSL_cleanse(phrase, sizeof(phrase));
    return st;
}

hush_status_t hush_keyfile_load(char *out, size_t outsz, const char *path)
{
    char phrase[HUSH_KEYFILE_PHRASE_MAX] = {0};
    char vault[HUSH_HOME_PATH_MAX] = {0};
    unsigned char plain[HUSH_KEYFILE_PLAIN_MAX] = {0};
    size_t plain_n = 0;
    hush_keyfile_body_t body = {0};
    int have = 0;
    hush_status_t st = HUSH_OK;

    if (out == NULL || outsz == 0 || path == NULL || path[0] == '\0')
        return HUSH_ERR_ARG;
    out[0] = '\0';
    if (!hush_keyfile_vault_path(vault, sizeof(vault)))
        return HUSH_ERR_NOT_FOUND;
    if (access(vault, F_OK) != 0)
        return HUSH_ERR_NOT_FOUND;
    have = hush_keyfile_phrase(phrase, sizeof(phrase));
    if (have <= 0)
        return HUSH_ERR_CRYPTO;
    st = hush_keyfile_read(plain, &plain_n, phrase);
    OPENSSL_cleanse(phrase, sizeof(phrase));
    if (st != HUSH_OK)
        return st;
    body.bytes = plain;
    body.n = plain_n;
    st = hush_keyfile_find(out, outsz, &body, path);
    OPENSSL_cleanse(plain, sizeof(plain));
    return st;
}

static int hush_keyfile_phrase(char *out, size_t outsz)
{
    const char *env = NULL;
    size_t n = 0;

    assert(out != NULL && outsz > 0);
    out[0] = '\0';
    env = getenv(HUSH_KEYFILE_ENV);
    if (env == NULL || env[0] == '\0')
        return 0;
    n = strlen(env);
    if (n + 1 > outsz)
        return -1;
    memcpy(out, env, n + 1);
    return 1;
}

static int hush_keyfile_vault_path(char *out, size_t outsz)
{
    char root[HUSH_HOME_PATH_MAX] = {0};

    assert(out != NULL && outsz > 0);
    hush_home_root(root, sizeof(root));
    if (root[0] == '\0')
        return 0;
    hush_home_join(out, outsz, root, HUSH_KEYFILE_NAME);
    return out[0] != '\0';
}

static hush_status_t hush_keyfile_random(unsigned char *buf, size_t n)
{
    int fd = -1;
    size_t got = 0;

    assert(buf != NULL && n > 0);
    fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0)
        return HUSH_ERR_IO;
    while (got < n) {
        ssize_t nread = read(fd, buf + got, n - got);

        if (nread <= 0)
            break;
        got += (size_t)nread;
    }
    close(fd);
    if (got != n)
        return HUSH_ERR_IO;
    return HUSH_OK;
}

static hush_status_t hush_keyfile_derive(unsigned char *key,
                                        const unsigned char *salt,
                                        const char *phrase)
{
    int ok = 0;

    assert(key != NULL && salt != NULL && phrase != NULL);
    ok = PKCS5_PBKDF2_HMAC(phrase, (int)strlen(phrase), salt,
                           HUSH_KEYFILE_SALT, HUSH_KEYFILE_ROUNDS,
                           EVP_sha256(), HUSH_KEYFILE_KEY, key);
    if (ok != 1)
        return HUSH_ERR_CRYPTO;
    return HUSH_OK;
}

static int hush_keyfile_gcm_ok(EVP_CIPHER_CTX *ctx, const unsigned char *key,
                              const unsigned char *iv, int encrypt)
{
    int started = 0;

    assert(ctx != NULL && key != NULL && iv != NULL);
    if (encrypt)
        started = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL);
    else
        started = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL);
    if (started != 1)
        return 0;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, HUSH_KEYFILE_IV,
                            NULL) != 1)
        return 0;
    if (encrypt)
        return EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv) == 1;
    return EVP_DecryptInit_ex(ctx, NULL, NULL, key, iv) == 1;
}

static hush_status_t hush_keyfile_seal(hush_keyfile_crypt_t *box)
{
    EVP_CIPHER_CTX *ctx = NULL;
    int n = 0;
    int fin = 0;
    hush_status_t st = HUSH_ERR_CRYPTO;

    assert(box != NULL && box->out != NULL && box->out_n != NULL);
    assert(box->in != NULL && box->key != NULL && box->iv != NULL);
    assert(box->tag != NULL);
    assert(box->in_n <= (size_t)HUSH_KEYFILE_PLAIN_MAX);
    ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL)
        return HUSH_ERR_CRYPTO;
    if (hush_keyfile_gcm_ok(ctx, box->key, box->iv, 1) &&
        EVP_EncryptUpdate(ctx, box->out, &n, box->in, (int)box->in_n) == 1 &&
        EVP_EncryptFinal_ex(ctx, box->out + n, &fin) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, HUSH_KEYFILE_TAG,
                            box->tag) == 1) {
        *box->out_n = (size_t)n + (size_t)fin;
        st = HUSH_OK;
    }
    EVP_CIPHER_CTX_free(ctx);
    return st;
}

static hush_status_t hush_keyfile_open(hush_keyfile_crypt_t *box)
{
    EVP_CIPHER_CTX *ctx = NULL;
    int n = 0;
    int fin = 0;
    hush_status_t st = HUSH_ERR_CRYPTO;

    assert(box != NULL && box->out != NULL && box->out_n != NULL);
    assert(box->in != NULL && box->key != NULL && box->iv != NULL);
    assert(box->tag != NULL);
    assert(box->in_n <= (size_t)HUSH_KEYFILE_PLAIN_MAX);
    ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL)
        return HUSH_ERR_CRYPTO;
    if (hush_keyfile_gcm_ok(ctx, box->key, box->iv, 0) &&
        EVP_DecryptUpdate(ctx, box->out, &n, box->in, (int)box->in_n) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, HUSH_KEYFILE_TAG,
                            box->tag) == 1 &&
        EVP_DecryptFinal_ex(ctx, box->out + n, &fin) == 1) {
        *box->out_n = (size_t)n + (size_t)fin;
        st = HUSH_OK;
    }
    EVP_CIPHER_CTX_free(ctx);
    return st;
}

static int hush_keyfile_hex_put(char *dst, size_t dstsz,
                               const unsigned char *src, size_t n)
{
    static const char hex[] = "0123456789abcdef";
    size_t i = 0;

    assert(dst != NULL && src != NULL);
    if (n * 2 + 1 > dstsz)
        return 0;
    for (i = 0; i < n; i++) {
        dst[i * 2] = hex[(src[i] >> 4) & HUSH_KEYFILE_NIBBLE];
        dst[i * 2 + 1] = hex[src[i] & HUSH_KEYFILE_NIBBLE];
    }
    dst[n * 2] = '\0';
    return 1;
}

static int hush_keyfile_hex_val(char ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + HUSH_KEYFILE_HEX_LETTER;
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + HUSH_KEYFILE_HEX_LETTER;
    return -1;
}

static int hush_keyfile_hex_get(unsigned char *dst, size_t dstsz,
                               size_t *out_n, const char *hex)
{
    size_t n = 0;
    size_t i = 0;

    assert(dst != NULL && out_n != NULL && hex != NULL);
    n = strlen(hex);
    if ((n % 2) != 0 || n / 2 > dstsz)
        return 0;
    for (i = 0; i < n; i += 2) {
        int hi = hush_keyfile_hex_val(hex[i]);
        int lo = hush_keyfile_hex_val(hex[i + 1]);

        if (hi < 0 || lo < 0)
            return 0;
        dst[i / 2] = (unsigned char)((hi << 4) | lo);
    }
    *out_n = n / 2;
    return 1;
}

static hush_status_t hush_keyfile_slurp(char *text, size_t textsz)
{
    char path[HUSH_HOME_PATH_MAX] = {0};
    FILE *fp = NULL;
    size_t nread = 0;

    assert(text != NULL && textsz > 1);
    if (!hush_keyfile_vault_path(path, sizeof(path)))
        return HUSH_ERR_IO;
    fp = fopen(path, "r");
    if (fp == NULL)
        return HUSH_ERR_NOT_FOUND;
    nread = fread(text, 1, textsz - 1, fp);
    fclose(fp);
    text[nread] = '\0';
    return HUSH_OK;
}

static int hush_keyfile_cut_line(char **cursor, char **line)
{
    char *nl = NULL;

    assert(cursor != NULL && *cursor != NULL && line != NULL);
    *line = *cursor;
    nl = strchr(*cursor, '\n');
    if (nl == NULL)
        return 0;
    *nl = '\0';
    *cursor = nl + 1;
    return 1;
}

static hush_status_t hush_keyfile_fields(char *text, hush_keyfile_fields_t *out)
{
    char *cursor = NULL;

    assert(text != NULL && out != NULL);
    if (strncmp(text, HUSH_KEYFILE_MAGIC, strlen(HUSH_KEYFILE_MAGIC)) != 0)
        return HUSH_ERR_CRYPTO;
    cursor = text + strlen(HUSH_KEYFILE_MAGIC);
    if (!hush_keyfile_cut_line(&cursor, &out->salt))
        return HUSH_ERR_CRYPTO;
    if (!hush_keyfile_cut_line(&cursor, &out->iv))
        return HUSH_ERR_CRYPTO;
    if (!hush_keyfile_cut_line(&cursor, &out->tag))
        return HUSH_ERR_CRYPTO;
    out->ct = cursor;
    out->ct[strcspn(out->ct, "\r\n")] = '\0';
    return HUSH_OK;
}

static int hush_keyfile_take_hex(unsigned char *dst, size_t expect,
                                const char *hex)
{
    size_t n = 0;

    assert(dst != NULL && hex != NULL && expect > 0);
    if (!hush_keyfile_hex_get(dst, expect, &n, hex))
        return 0;
    return n == expect;
}

static hush_status_t hush_keyfile_decode(hush_keyfile_parts_t *out,
                                        const hush_keyfile_fields_t *fields)
{
    size_t n = 0;

    assert(out != NULL && fields != NULL);
    if (!hush_keyfile_take_hex(out->salt, HUSH_KEYFILE_SALT, fields->salt))
        return HUSH_ERR_CRYPTO;
    if (!hush_keyfile_take_hex(out->iv, HUSH_KEYFILE_IV, fields->iv))
        return HUSH_ERR_CRYPTO;
    if (!hush_keyfile_take_hex(out->tag, HUSH_KEYFILE_TAG, fields->tag))
        return HUSH_ERR_CRYPTO;
    if (!hush_keyfile_hex_get(out->blob, sizeof(out->blob), &n, fields->ct))
        return HUSH_ERR_CRYPTO;
    out->blob_n = n;
    return HUSH_OK;
}

static hush_status_t hush_keyfile_decrypt(unsigned char *plain, size_t *plain_n,
                                         const hush_keyfile_parts_t *parts,
                                         const char *phrase)
{
    unsigned char key[HUSH_KEYFILE_KEY] = {0};
    unsigned char tag[HUSH_KEYFILE_TAG] = {0};
    hush_keyfile_crypt_t box = {0};
    hush_status_t st = HUSH_OK;

    assert(plain != NULL && plain_n != NULL);
    assert(parts != NULL && phrase != NULL);
    memcpy(tag, parts->tag, sizeof(tag));
    st = hush_keyfile_derive(key, parts->salt, phrase);
    if (st == HUSH_OK) {
        box.out = plain;
        box.out_n = plain_n;
        box.in = parts->blob;
        box.in_n = parts->blob_n;
        box.key = key;
        box.iv = parts->iv;
        box.tag = tag;
        st = hush_keyfile_open(&box);
    }
    OPENSSL_cleanse(key, sizeof(key));
    OPENSSL_cleanse(tag, sizeof(tag));
    return st;
}

static hush_status_t hush_keyfile_read(unsigned char *plain, size_t *plain_n,
                                      const char *phrase)
{
    char text[HUSH_KEYFILE_FILE_MAX] = {0};
    hush_keyfile_fields_t fields = {0};
    hush_keyfile_parts_t parts = {0};
    hush_status_t st = HUSH_OK;

    assert(plain != NULL && plain_n != NULL && phrase != NULL);
    st = hush_keyfile_slurp(text, sizeof(text));
    if (st != HUSH_OK)
        return st;
    st = hush_keyfile_fields(text, &fields);
    if (st != HUSH_OK)
        return st;
    st = hush_keyfile_decode(&parts, &fields);
    if (st != HUSH_OK)
        return st;
    st = hush_keyfile_decrypt(plain, plain_n, &parts, phrase);
    OPENSSL_cleanse(&parts, sizeof(parts));
    if (st != HUSH_OK)
        return HUSH_ERR_CRYPTO;
    if (*plain_n < (size_t)HUSH_KEYFILE_PLAIN_MAX)
        plain[*plain_n] = '\0';
    return HUSH_OK;
}

static hush_status_t hush_keyfile_encrypt(hush_keyfile_parts_t *sealed,
                                         const unsigned char *plain,
                                         size_t plain_n, const char *phrase)
{
    unsigned char key[HUSH_KEYFILE_KEY] = {0};
    hush_keyfile_crypt_t box = {0};
    hush_status_t st = HUSH_OK;

    assert(sealed != NULL && plain != NULL && phrase != NULL);
    assert(plain_n <= (size_t)HUSH_KEYFILE_PLAIN_MAX);
    st = hush_keyfile_random(sealed->salt, sizeof(sealed->salt));
    if (st == HUSH_OK)
        st = hush_keyfile_random(sealed->iv, sizeof(sealed->iv));
    if (st == HUSH_OK)
        st = hush_keyfile_derive(key, sealed->salt, phrase);
    if (st == HUSH_OK) {
        box.out = sealed->blob;
        box.out_n = &sealed->blob_n;
        box.in = plain;
        box.in_n = plain_n;
        box.key = key;
        box.iv = sealed->iv;
        box.tag = sealed->tag;
        st = hush_keyfile_seal(&box);
    }
    OPENSSL_cleanse(key, sizeof(key));
    return st;
}

static hush_status_t hush_keyfile_encode(hush_keyfile_hexlines_t *lines,
                                        const hush_keyfile_parts_t *sealed)
{
    int salt_ok = 0;
    int iv_ok = 0;
    int tag_ok = 0;
    int ct_ok = 0;

    assert(lines != NULL && sealed != NULL);
    salt_ok = hush_keyfile_hex_put(lines->salt, sizeof(lines->salt),
                                  sealed->salt, sizeof(sealed->salt));
    iv_ok = hush_keyfile_hex_put(lines->iv, sizeof(lines->iv),
                                sealed->iv, sizeof(sealed->iv));
    tag_ok = hush_keyfile_hex_put(lines->tag, sizeof(lines->tag),
                                 sealed->tag, sizeof(sealed->tag));
    ct_ok = hush_keyfile_hex_put(lines->ct, sizeof(lines->ct),
                                sealed->blob, sealed->blob_n);
    if (!salt_ok || !iv_ok || !tag_ok || !ct_ok)
        return HUSH_ERR_FULL;
    return HUSH_OK;
}

static hush_status_t hush_keyfile_open_tmp(const char *tmp, FILE **out)
{
    int fd = -1;
    FILE *fp = NULL;

    assert(tmp != NULL && out != NULL);
    *out = NULL;
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, HUSH_KEYFILE_MODE);
    if (fd < 0)
        return HUSH_ERR_IO;
    if (fchmod(fd, HUSH_KEYFILE_MODE) != 0) {
        close(fd);
        unlink(tmp);
        return HUSH_ERR_IO;
    }
    fp = fdopen(fd, "w");
    if (fp == NULL) {
        close(fd);
        unlink(tmp);
        return HUSH_ERR_IO;
    }
    *out = fp;
    return HUSH_OK;
}

static hush_status_t hush_keyfile_close_tmp(FILE *fp, const char *tmp, int wrote)
{
    int bad = 0;

    assert(fp != NULL && tmp != NULL);
    if (!wrote)
        bad = 1;
    if (fclose(fp) != 0)
        bad = 1;
    if (!bad)
        return HUSH_OK;
    unlink(tmp);
    return HUSH_ERR_IO;
}

static hush_status_t hush_keyfile_commit(const char *path,
                                        const hush_keyfile_hexlines_t *lines)
{
    char tmp[HUSH_HOME_PATH_MAX] = {0};
    FILE *fp = NULL;
    hush_status_t st = HUSH_OK;
    int wrote = 0;

    assert(path != NULL && lines != NULL);
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp))
        return HUSH_ERR_FULL;
    st = hush_keyfile_open_tmp(tmp, &fp);
    if (st != HUSH_OK)
        return st;
    wrote = fprintf(fp, "%s%s\n%s\n%s\n%s\n", HUSH_KEYFILE_MAGIC,
                    lines->salt, lines->iv, lines->tag, lines->ct) >= 0;
    st = hush_keyfile_close_tmp(fp, tmp, wrote);
    if (st != HUSH_OK)
        return st;
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return HUSH_ERR_IO;
    }
    return HUSH_OK;
}

static hush_status_t hush_keyfile_write(const unsigned char *plain,
                                       size_t plain_n, const char *phrase)
{
    char path[HUSH_HOME_PATH_MAX] = {0};
    hush_keyfile_parts_t sealed = {0};
    hush_keyfile_hexlines_t lines = {0};
    hush_status_t st = HUSH_OK;

    assert(plain != NULL && phrase != NULL);
    if (!hush_keyfile_vault_path(path, sizeof(path)))
        return HUSH_ERR_IO;
    st = hush_keyfile_encrypt(&sealed, plain, plain_n, phrase);
    if (st != HUSH_OK)
        return st;
    st = hush_keyfile_encode(&lines, &sealed);
    OPENSSL_cleanse(&sealed, sizeof(sealed));
    if (st != HUSH_OK)
        return st;
    return hush_keyfile_commit(path, &lines);
}

static int hush_keyfile_next_rec(const char *p, const char *end,
                                hush_keyfile_pair_t *pair)
{
    const char *nl = NULL;
    const char *nl2 = NULL;

    assert(p != NULL && end != NULL && pair != NULL);
    if (p >= end)
        return 0;
    nl = memchr(p, '\n', (size_t)(end - p));
    if (nl == NULL)
        return 0;
    nl2 = memchr(nl + 1, '\n', (size_t)(end - (nl + 1)));
    if (nl2 == NULL)
        return 0;
    pair->path = p;
    pair->path_n = (size_t)(nl - p);
    pair->secret = nl + 1;
    pair->secret_n = (size_t)(nl2 - (nl + 1));
    pair->next = nl2 + 1;
    return 1;
}

static hush_status_t hush_keyfile_copy_secret(char *out, size_t outsz,
                                             const char *secret, size_t n)
{
    assert(out != NULL && secret != NULL);
    if (n + 1 > outsz)
        return HUSH_ERR_FULL;
    memcpy(out, secret, n);
    out[n] = '\0';
    return HUSH_OK;
}

static hush_status_t hush_keyfile_find(char *out, size_t outsz,
                                      const hush_keyfile_body_t *body,
                                      const char *path)
{
    const char *p = NULL;
    const char *end = NULL;
    size_t path_n = 0;

    assert(out != NULL && body != NULL && body->bytes != NULL && path != NULL);
    out[0] = '\0';
    path_n = strlen(path);
    p = (const char *)body->bytes;
    end = p + body->n;
    while (p < end) {
        hush_keyfile_pair_t pair = {0};

        if (!hush_keyfile_next_rec(p, end, &pair))
            break;
        if (pair.path_n == path_n && memcmp(pair.path, path, path_n) == 0)
            return hush_keyfile_copy_secret(out, outsz, pair.secret,
                                            pair.secret_n);
        p = pair.next;
    }
    return HUSH_ERR_NOT_FOUND;
}

static hush_status_t hush_keyfile_append(hush_keyfile_edit_t *edit,
                                        const char *path, const char *secret)
{
    int n = 0;

    assert(edit != NULL && edit->dst != NULL);
    assert(path != NULL && secret != NULL);
    if (edit->used >= edit->dstsz)
        return HUSH_ERR_FULL;
    n = snprintf((char *)edit->dst + edit->used, edit->dstsz - edit->used,
                 "%s\n%s\n", path, secret);
    if (n < 0 || edit->used + (size_t)n >= edit->dstsz)
        return HUSH_ERR_FULL;
    edit->used += (size_t)n;
    edit->replaced = 1;
    return HUSH_OK;
}

static hush_status_t hush_keyfile_keep(hush_keyfile_edit_t *edit,
                                      const char *rec, size_t rec_n)
{
    assert(edit != NULL && edit->dst != NULL && rec != NULL);
    if (edit->used + rec_n >= edit->dstsz)
        return HUSH_ERR_FULL;
    memcpy(edit->dst + edit->used, rec, rec_n);
    edit->used += rec_n;
    return HUSH_OK;
}

static void hush_keyfile_apply(unsigned char *plain, size_t *plain_n,
                              hush_keyfile_edit_t *edit)
{
    assert(plain != NULL && plain_n != NULL && edit != NULL);
    assert(edit->dst != NULL);
    assert(edit->used < (size_t)HUSH_KEYFILE_PLAIN_MAX);
    memcpy(plain, edit->dst, edit->used);
    plain[edit->used] = '\0';
    *plain_n = edit->used;
    OPENSSL_cleanse(edit->dst, edit->dstsz);
}

static hush_status_t hush_keyfile_upsert(unsigned char *plain, size_t *plain_n,
                                        const char *path, const char *secret)
{
    unsigned char next[HUSH_KEYFILE_PLAIN_MAX] = {0};
    hush_keyfile_edit_t edit = {0};
    const char *p = NULL;
    const char *end = NULL;
    size_t path_n = 0;
    hush_status_t st = HUSH_OK;

    assert(plain != NULL && plain_n != NULL && path != NULL && secret != NULL);
    edit.dst = next;
    edit.dstsz = sizeof(next);
    path_n = strlen(path);
    p = (const char *)plain;
    end = p + *plain_n;
    while (p < end) {
        hush_keyfile_pair_t pair = {0};

        if (!hush_keyfile_next_rec(p, end, &pair))
            break;
        if (pair.path_n == path_n && memcmp(pair.path, path, path_n) == 0)
            st = hush_keyfile_append(&edit, path, secret);
        else
            st = hush_keyfile_keep(&edit, p, (size_t)(pair.next - p));
        if (st != HUSH_OK)
            break;
        p = pair.next;
    }
    if (st == HUSH_OK && !edit.replaced)
        st = hush_keyfile_append(&edit, path, secret);
    if (st != HUSH_OK) {
        OPENSSL_cleanse(next, sizeof(next));
        return st;
    }
    hush_keyfile_apply(plain, plain_n, &edit);
    return HUSH_OK;
}
