/* hush_identity.c: owns secp256k1 generate/import and nsec/npub display. */

#include <assert.h>
#include <ctype.h>
#include <string.h>

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <openssl/rand.h>

#include "hush_identity.h"

enum {
    HUSH_IDENTITY_TRIM_MAX = 160
};

/* Writes lowercase hex of n bytes into out (2n + NUL). */
static void hush_identity_hex_encode(char *out, const unsigned char *in, size_t n);

/* Parses 64 hex chars into 32 bytes. Returns 0 on a bad nibble. */
static int hush_identity_hex_decode(unsigned char *out32, const char *hex);

/* Copies text without leading/trailing space into out. */
static void hush_identity_trim(char *out, size_t outsz, const char *text);

/* Fills pubkey, nsec, npub, and hex from seckey. */
static hush_status_t hush_identity_derive(hush_identity_t *id);

/* Writes the 32-byte x-only pubkey for seckey. */
static hush_status_t hush_identity_pubkey_xonly(unsigned char *out32,
                                                const unsigned char *seckey);

hush_status_t hush_identity_generate(hush_identity_t *id)
{
    int i;

    if (id == NULL)
        return HUSH_ERR_ARG;
    memset(id, 0, sizeof(*id));
    for (i = 0; i < 8; ++i) {
        if (RAND_bytes(id->seckey, HUSH_BECH32_DATA_LEN) != 1)
            return HUSH_ERR_CRYPTO;
        if (hush_identity_derive(id) == HUSH_OK)
            return HUSH_OK;
    }
    hush_identity_clear(id);
    return HUSH_ERR_CRYPTO;
}

hush_status_t hush_identity_import(hush_identity_t *id, const char *secret)
{
    char trimmed[HUSH_IDENTITY_TRIM_MAX];
    char hrp[HUSH_BECH32_HRP_MAX + 1];
    hush_status_t st;

    if (id == NULL || secret == NULL)
        return HUSH_ERR_ARG;
    memset(id, 0, sizeof(*id));
    hush_identity_trim(trimmed, sizeof(trimmed), secret);
    if (trimmed[0] == '\0')
        return HUSH_ERR_PARSE;
    if (strncmp(trimmed, "nsec1", 5) == 0) {
        st = hush_bech32_decode(id->seckey, hrp, sizeof(hrp), trimmed);
        if (st != HUSH_OK)
            return HUSH_ERR_PARSE;
        if (strcmp(hrp, HUSH_BECH32_HRP_NSEC) != 0)
            return HUSH_ERR_PARSE;
    } else if (strlen(trimmed) == (size_t)HUSH_IDENTITY_HEX_LEN) {
        if (!hush_identity_hex_decode(id->seckey, trimmed))
            return HUSH_ERR_PARSE;
    } else {
        return HUSH_ERR_PARSE;
    }
    st = hush_identity_derive(id);
    if (st != HUSH_OK)
        hush_identity_clear(id);
    return st;
}

void hush_identity_clear(hush_identity_t *id)
{
    if (id == NULL)
        return;
    memset(id, 0, sizeof(*id));
}

static void hush_identity_hex_encode(char *out, const unsigned char *in, size_t n)
{
    static const char hex[] = "0123456789abcdef";
    size_t i;

    assert(out != NULL);
    assert(in != NULL);
    for (i = 0; i < n; ++i) {
        out[i * 2] = hex[(in[i] >> 4) & 0x0f];
        out[i * 2 + 1] = hex[in[i] & 0x0f];
    }
    out[n * 2] = '\0';
}

static int hush_identity_hex_decode(unsigned char *out32, const char *hex)
{
    size_t i;
    unsigned hi;
    unsigned lo;

    assert(out32 != NULL);
    assert(hex != NULL);
    for (i = 0; i < (size_t)HUSH_BECH32_DATA_LEN; ++i) {
        if (!isxdigit((unsigned char)hex[i * 2]) ||
            !isxdigit((unsigned char)hex[i * 2 + 1]))
            return 0;
        hi = (unsigned)hex[i * 2];
        lo = (unsigned)hex[i * 2 + 1];
        if (hi >= 'A' && hi <= 'F')
            hi = hi - 'A' + 10;
        else if (hi >= 'a' && hi <= 'f')
            hi = hi - 'a' + 10;
        else
            hi = hi - '0';
        if (lo >= 'A' && lo <= 'F')
            lo = lo - 'A' + 10;
        else if (lo >= 'a' && lo <= 'f')
            lo = lo - 'a' + 10;
        else
            lo = lo - '0';
        out32[i] = (unsigned char)((hi << 4) | lo);
    }
    return 1;
}

static void hush_identity_trim(char *out, size_t outsz, const char *text)
{
    const char *end;
    size_t n;

    assert(out != NULL);
    assert(text != NULL);
    assert(outsz > 0);
    while (*text != '\0' && isspace((unsigned char)*text))
        text++;
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1]))
        end--;
    n = (size_t)(end - text);
    if (n + 1 > outsz)
        n = outsz - 1;
    memcpy(out, text, n);
    out[n] = '\0';
}

static hush_status_t hush_identity_derive(hush_identity_t *id)
{
    hush_status_t st;

    assert(id != NULL);
    st = hush_identity_pubkey_xonly(id->pubkey, id->seckey);
    if (st != HUSH_OK)
        return st;
    if (hush_bech32_encode(id->nsec, sizeof(id->nsec),
                           HUSH_BECH32_HRP_NSEC, id->seckey) != HUSH_OK)
        return HUSH_ERR_CRYPTO;
    if (hush_bech32_encode(id->npub, sizeof(id->npub),
                           HUSH_BECH32_HRP_NPUB, id->pubkey) != HUSH_OK)
        return HUSH_ERR_CRYPTO;
    hush_identity_hex_encode(id->pubkey_hex, id->pubkey, HUSH_BECH32_DATA_LEN);
    return HUSH_OK;
}

static hush_status_t hush_identity_pubkey_xonly(unsigned char *out32,
                                                const unsigned char *seckey)
{
    EC_GROUP *group = NULL;
    EC_POINT *pub = NULL;
    BIGNUM *priv = NULL;
    BIGNUM *x = NULL;
    BIGNUM *y = NULL;
    int ok = 0;

    assert(out32 != NULL);
    assert(seckey != NULL);
    group = EC_GROUP_new_by_curve_name(NID_secp256k1);
    priv = BN_bin2bn(seckey, HUSH_BECH32_DATA_LEN, NULL);
    if (group == NULL || priv == NULL || BN_is_zero(priv))
        goto hush_identity_pubkey_cleanup;
    pub = EC_POINT_new(group);
    x = BN_new();
    y = BN_new();
    if (pub == NULL || x == NULL || y == NULL)
        goto hush_identity_pubkey_cleanup;
    if (EC_POINT_mul(group, pub, priv, NULL, NULL, NULL) != 1)
        goto hush_identity_pubkey_cleanup;
    if (EC_POINT_get_affine_coordinates(group, pub, x, y, NULL) != 1)
        goto hush_identity_pubkey_cleanup;
    if (BN_bn2binpad(x, out32, HUSH_BECH32_DATA_LEN) != HUSH_BECH32_DATA_LEN)
        goto hush_identity_pubkey_cleanup;
    ok = 1;
/* Four OpenSSL objects; one forward cleanup keeps half-init off the helpers. */
hush_identity_pubkey_cleanup:
    BN_free(y);
    BN_free(x);
    EC_POINT_free(pub);
    BN_free(priv);
    EC_GROUP_free(group);
    return ok ? HUSH_OK : HUSH_ERR_CRYPTO;
}
