/* hush_auth.c: owns the per-hive session token and its constant-time check. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hush_auth.h"
#include "hush_home.h"

/* Propagates any non-OK status to the caller. Permitted only in
 * functions that acquire nothing. Sole macro allowed to return. */
#define HUSH_AUTH_TRY(expr)                          \
    do {                                             \
        hush_status_t hush_auth_try_s_ = (expr);     \
        if (hush_auth_try_s_ != HUSH_OK)             \
            return hush_auth_try_s_;                 \
    } while (0)

static char g_token[HUSH_AUTH_TOKEN_BUF];
static int g_ready = 0;

/* Writes raw bytes as lowercase hex plus a terminator. */
static void hush_auth_hex_encode(const unsigned char *raw, size_t raw_len,
                                 char *out);

/* True when text is exactly HUSH_AUTH_TOKEN_HEX hex characters. */
static int hush_auth_token_is_hex(const char *text);

/* Strips trailing CR/LF in place. */
static void hush_auth_trim(char *text);

/* Reads path into out. NOT_FOUND when absent, DENIED on an insecure file,
 * PARSE on malformed content. */
static hush_status_t hush_auth_read_file(const char *path, char *out,
                                         size_t outsz);

/* Replaces g_token with fresh /dev/urandom bytes. Fails HUSH_ERR_IO. */
static hush_status_t hush_auth_mint(void);

/* Writes g_token to path through a 0600 temp file and rename. */
static hush_status_t hush_auth_write_file(const char *path);

/* Loads path or mints a replacement when absent or malformed. */
static hush_status_t hush_auth_load_or_mint(const char *path);

hush_status_t hush_auth_init(void)
{
    char root[HUSH_HOME_PATH_MAX];
    char path[HUSH_HOME_PATH_MAX];
    int n;

    if (g_ready)
        return HUSH_OK;
    if (hush_home_ensure() != HUSH_OK)
        return HUSH_ERR_IO;
    hush_home_root(root, sizeof(root));
    if (root[0] == '\0')
        return HUSH_ERR_IO;
    n = snprintf(path, sizeof(path), "%s/%s", root, HUSH_AUTH_FILE);
    if (n <= 0 || (size_t)n >= sizeof(path))
        return HUSH_ERR_IO;
    HUSH_AUTH_TRY(hush_auth_load_or_mint(path));
    g_ready = 1;
    assert(g_token[0] != '\0');
    return HUSH_OK;
}

hush_status_t hush_auth_token_copy(char *out, size_t outsz)
{
    if (out == NULL || outsz < sizeof(g_token))
        return HUSH_ERR_ARG;
    if (!g_ready)
        return HUSH_ERR_IO;
    memcpy(out, g_token, sizeof(g_token));
    return HUSH_OK;
}

int hush_auth_tokens_equal(const char *a, const char *b)
{
    size_t i;
    unsigned char diff = 0;

    if (a == NULL || b == NULL)
        return 0;
    if (strlen(a) != strlen(b))
        return 0;
    for (i = 0; a[i] != '\0'; ++i)
        diff |= (unsigned char)((unsigned char)a[i] ^ (unsigned char)b[i]);
    return diff == 0;
}

int hush_auth_token_matches(const char *presented)
{
    if (!g_ready || presented == NULL || presented[0] == '\0')
        return 0;
    return hush_auth_tokens_equal(presented, g_token);
}

hush_status_t hush_auth_challenge_mint(char *out, size_t outsz)
{
    unsigned char raw[HUSH_AUTH_CHALLENGE_HEX / 2];

    if (out == NULL || outsz < (size_t)HUSH_AUTH_CHALLENGE_BUF)
        return HUSH_ERR_ARG;
    if (RAND_bytes(raw, (int)sizeof(raw)) != 1)
        return HUSH_ERR_IO;
    hush_auth_hex_encode(raw, sizeof(raw), out);
    return HUSH_OK;
}

hush_status_t hush_auth_sha256_hex(const char *text, char *out, size_t outsz)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    EVP_MD_CTX *ctx;

    if (text == NULL || out == NULL || outsz < (size_t)HUSH_AUTH_SHA256_BUF)
        return HUSH_ERR_ARG;
    ctx = EVP_MD_CTX_new();
    if (ctx == NULL)
        return HUSH_ERR_CRYPTO;
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1 ||
        EVP_DigestUpdate(ctx, text, strlen(text)) != 1 ||
        EVP_DigestFinal_ex(ctx, digest, &digest_len) != 1) {
        EVP_MD_CTX_free(ctx);
        return HUSH_ERR_CRYPTO;
    }
    EVP_MD_CTX_free(ctx);
    assert(digest_len == (unsigned int)(HUSH_AUTH_SHA256_HEX / 2));
    hush_auth_hex_encode(digest, digest_len, out);
    return HUSH_OK;
}

int hush_auth_join_matches(const char *presented, const char *stored_hash)
{
    char computed[HUSH_AUTH_SHA256_BUF];

    if (presented == NULL || stored_hash == NULL)
        return 0;
    if (presented[0] == '\0' || stored_hash[0] == '\0')
        return 0;
    if (hush_auth_sha256_hex(presented, computed, sizeof(computed)) != HUSH_OK)
        return 0;
    return hush_auth_tokens_equal(computed, stored_hash);
}

static void hush_auth_hex_encode(const unsigned char *raw, size_t raw_len,
                                 char *out)
{
    static const char digits[] = "0123456789abcdef";
    size_t i;

    assert(raw != NULL);
    assert(out != NULL);
    for (i = 0; i < raw_len; ++i) {
        out[i * 2] = digits[(raw[i] >> 4) & 0x0fu];
        out[i * 2 + 1] = digits[raw[i] & 0x0fu];
    }
    out[raw_len * 2] = '\0';
}

static int hush_auth_token_is_hex(const char *text)
{
    size_t i;

    if (text == NULL || strlen(text) != (size_t)HUSH_AUTH_TOKEN_HEX)
        return 0;
    for (i = 0; i < (size_t)HUSH_AUTH_TOKEN_HEX; ++i) {
        if (!isxdigit((unsigned char)text[i]))
            return 0;
    }
    return 1;
}

static void hush_auth_trim(char *text)
{
    size_t len;

    assert(text != NULL);
    len = strlen(text);
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r'))
        text[--len] = '\0';
}

static hush_status_t hush_auth_read_file(const char *path, char *out,
                                         size_t outsz)
{
    char buf[HUSH_AUTH_TOKEN_BUF + 4];
    struct stat st;
    ssize_t n;
    int fd;

    assert(path != NULL);
    assert(out != NULL);
    fd = open(path, O_RDONLY | O_NOFOLLOW);
    if (fd < 0)
        return errno == ENOENT ? HUSH_ERR_NOT_FOUND : HUSH_ERR_IO;
    if (fstat(fd, &st) != 0) {
        close(fd);
        return HUSH_ERR_IO;
    }
    if (!S_ISREG(st.st_mode) || st.st_uid != getuid()) {
        close(fd);
        return HUSH_ERR_DENIED;
    }
    if ((st.st_mode & (S_IRWXG | S_IRWXO)) != 0 &&
        fchmod(fd, S_IRUSR | S_IWUSR) != 0) {
        close(fd);
        return HUSH_ERR_DENIED;
    }
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0)
        return HUSH_ERR_PARSE;
    buf[n] = '\0';
    hush_auth_trim(buf);
    if (!hush_auth_token_is_hex(buf) || strlen(buf) + 1 > outsz)
        return HUSH_ERR_PARSE;
    memcpy(out, buf, strlen(buf) + 1);
    return HUSH_OK;
}

static hush_status_t hush_auth_mint(void)
{
    unsigned char raw[HUSH_AUTH_TOKEN_HEX / 2];
    ssize_t n;
    int fd;

    assert(g_ready == 0);
    fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0)
        return HUSH_ERR_IO;
    n = read(fd, raw, sizeof(raw));
    close(fd);
    if (n != (ssize_t)sizeof(raw))
        return HUSH_ERR_IO;
    hush_auth_hex_encode(raw, sizeof(raw), g_token);
    return HUSH_OK;
}

static hush_status_t hush_auth_write_file(const char *path)
{
    char tmp[HUSH_HOME_PATH_MAX + 8];
    size_t len = strlen(g_token);
    struct stat st;
    ssize_t w;
    int n;
    int fd;

    assert(path != NULL);
    n = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (n <= 0 || (size_t)n >= sizeof(tmp))
        return HUSH_ERR_IO;
    if (lstat(tmp, &st) == 0 && unlink(tmp) != 0)
        return HUSH_ERR_IO;
    fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW,
              S_IRUSR | S_IWUSR);
    if (fd < 0)
        return HUSH_ERR_IO;
    w = write(fd, g_token, len);
    if (w != (ssize_t)len || fsync(fd) != 0 || close(fd) != 0) {
        (void)unlink(tmp);
        return HUSH_ERR_IO;
    }
    if (rename(tmp, path) != 0) {
        (void)unlink(tmp);
        return HUSH_ERR_IO;
    }
    return HUSH_OK;
}

static hush_status_t hush_auth_load_or_mint(const char *path)
{
    hush_status_t st;

    assert(path != NULL);
    st = hush_auth_read_file(path, g_token, sizeof(g_token));
    if (st == HUSH_OK)
        return HUSH_OK;
    if (st != HUSH_ERR_NOT_FOUND && st != HUSH_ERR_PARSE)
        return st;
    HUSH_AUTH_TRY(hush_auth_mint());
    return hush_auth_write_file(path);
}
