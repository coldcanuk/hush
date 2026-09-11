/* hush_event.c: owns Nostr event representation, id computation, and basic validation for Hush. */

#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <string.h>

#include <openssl/evp.h>

#include "hush_event.h"
#include "hush_schnorr.h"
#include "hush_status.h"

enum {
    HUSH_MAX_KIND = 65535,
    HUSH_DEC_MAX = 24
};

static const char hush_event_hex[16] = "0123456789abcdef";

/* Writes lower-hex of digest[0..dlen) plus a NUL into out (2*dlen + 1 bytes). */
static void hush_event_hex_encode(char *out, const unsigned char *digest,
                                  size_t dlen);

/* Feeds the canonical decimal form of v into an in-progress digest. */
static void hush_event_digest_dec(EVP_MD_CTX *ctx, int64_t v);

/* Feeds a JSON-string-escaped copy of s into an in-progress digest. */
static void hush_event_digest_esc(EVP_MD_CTX *ctx, const char *s);

/* True when text is exactly len lowercase hex characters. */
static int hush_event_is_hex(const char *text, size_t len);

/* Decodes out_bytes of hex. 0 on malformed or wrong-length input. */
static int hush_event_hex_decode(const char *hex, unsigned char *out,
                                 size_t out_bytes);

/* Copies a denial reason and returns HUSH_ERR_DENIED. */
static hush_status_t hush_event_deny(char *reason, size_t reason_len,
                                     const char *text);

/* Computes the NIP-01 id = hex(sha256([0, pubkey, created_at, kind, tags, content])).
 * The canonical preimage is streamed straight into OpenSSL so no oversized
 * intermediate buffer is ever materialized. */
hush_status_t hush_event_compute_id(const hush_event_t *ev, char *out_id)
{
    EVP_MD_CTX *ctx = NULL;
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int dlen = 0;
    size_t i;
    size_t j;

    if (ev == NULL || out_id == NULL)
        return HUSH_ERR_ARG;
    if (strlen(ev->pubkey) != (size_t)HUSH_EVENT_PUBKEY_HEX_LEN)
        return HUSH_ERR_ARG;

    ctx = EVP_MD_CTX_new();
    if (ctx == NULL)
        return HUSH_ERR_CRYPTO;
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1)
        goto fail;

    (void)EVP_DigestUpdate(ctx, "[0,\"", 4);
    (void)EVP_DigestUpdate(ctx, ev->pubkey, (size_t)HUSH_EVENT_PUBKEY_HEX_LEN);
    (void)EVP_DigestUpdate(ctx, "\",", 2);
    hush_event_digest_dec(ctx, ev->created_at);
    (void)EVP_DigestUpdate(ctx, ",", 1);
    hush_event_digest_dec(ctx, (int64_t)ev->kind);
    (void)EVP_DigestUpdate(ctx, ",[", 2);

    for (i = 0; i < ev->tag_count && i < (size_t)HUSH_EVENT_MAX_TAGS; i++) {
        size_t elems = 0;

        /* hush_event_t carries no per-tag element count: elements after the
         * last non-empty one are absent, while interior empties stay canonical. */
        for (j = 0; j < (size_t)HUSH_EVENT_MAX_TAG_ELEMS; j++) {
            if (ev->tags[i][j][0] != '\0')
                elems = j + 1;
        }
        if (i > 0)
            (void)EVP_DigestUpdate(ctx, ",", 1);
        (void)EVP_DigestUpdate(ctx, "[", 1);
        for (j = 0; j < elems; j++) {
            if (j > 0)
                (void)EVP_DigestUpdate(ctx, ",", 1);
            (void)EVP_DigestUpdate(ctx, "\"", 1);
            hush_event_digest_esc(ctx, ev->tags[i][j]);
            (void)EVP_DigestUpdate(ctx, "\"", 1);
        }
        (void)EVP_DigestUpdate(ctx, "]", 1);
    }

    (void)EVP_DigestUpdate(ctx, "],\"", 3);
    hush_event_digest_esc(ctx, ev->content);
    (void)EVP_DigestUpdate(ctx, "\"]", 2);

    if (EVP_DigestFinal_ex(ctx, digest, &dlen) != 1)
        goto fail;
    EVP_MD_CTX_free(ctx);
    hush_event_hex_encode(out_id, digest, dlen);
    return HUSH_OK;

fail:
    EVP_MD_CTX_free(ctx);
    return HUSH_ERR_CRYPTO;
}

hush_status_t hush_event_validate(const hush_event_t *ev)
{
    if (ev == NULL)
        return HUSH_ERR_ARG;
    if (!hush_event_is_hex(ev->id, (size_t)HUSH_EVENT_ID_HEX_LEN))
        return HUSH_ERR_ARG;
    if (!hush_event_is_hex(ev->pubkey, (size_t)HUSH_EVENT_PUBKEY_HEX_LEN))
        return HUSH_ERR_ARG;
    if (ev->sig[0] != '\0' &&
        !hush_event_is_hex(ev->sig, (size_t)HUSH_EVENT_SIG_HEX_LEN))
        return HUSH_ERR_ARG;
    if (ev->kind > (uint32_t)HUSH_MAX_KIND)
        return HUSH_ERR_ARG;
    if (strlen(ev->content) > HUSH_EVENT_MAX_CONTENT)
        return HUSH_ERR_ARG;
    return HUSH_OK;
}

hush_status_t hush_event_verify(const hush_event_t *ev, char *reason,
                                size_t reason_len)
{
    char computed[HUSH_EVENT_ID_HEX_LEN + 1];
    unsigned char pubkey[HUSH_SCHNORR_PUBKEY_BYTES];
    unsigned char message[HUSH_SCHNORR_PUBKEY_BYTES];
    unsigned char signature[HUSH_SCHNORR_SIGNATURE_BYTES];
    hush_schnorr_request_t request;

    if (ev == NULL)
        return HUSH_ERR_ARG;
    if (reason != NULL && reason_len > 0)
        reason[0] = '\0';
    if (hush_event_validate(ev) != HUSH_OK)
        return hush_event_deny(reason, reason_len, "invalid: malformed event");
    if (ev->sig[0] == '\0')
        return hush_event_deny(reason, reason_len, "invalid: missing signature");
    if (hush_event_compute_id(ev, computed) != HUSH_OK)
        return HUSH_ERR_CRYPTO;
    if (strcmp(computed, ev->id) != 0)
        return hush_event_deny(reason, reason_len, "invalid: id mismatch");
    if (!hush_event_hex_decode(ev->pubkey, pubkey, sizeof(pubkey)) ||
        !hush_event_hex_decode(ev->id, message, sizeof(message)) ||
        !hush_event_hex_decode(ev->sig, signature, sizeof(signature)))
        return HUSH_ERR_CRYPTO;
    request = (hush_schnorr_request_t){.pubkey = pubkey, .message = message,
                                       .message_len = sizeof(message),
                                       .signature = signature};
    if (hush_schnorr_verify(&request) != HUSH_OK)
        return hush_event_deny(reason, reason_len, "invalid: bad signature");
    return HUSH_OK;
}

static int hush_event_is_hex(const char *text, size_t len)
{
    size_t i;

    assert(text != NULL);
    if (strlen(text) != len)
        return 0;
    for (i = 0; i < len; ++i) {
        unsigned char ch = (unsigned char)text[i];

        if (!isxdigit(ch))
            return 0;
    }
    return 1;
}

static int hush_event_hex_decode(const char *hex, unsigned char *out,
                                 size_t out_bytes)
{
    size_t i;

    assert(hex != NULL);
    assert(out != NULL);
    if (strlen(hex) != out_bytes * 2)
        return 0;
    for (i = 0; i < out_bytes; ++i) {
        int hi = isxdigit((unsigned char)hex[i * 2])
            ? (int)(isdigit((unsigned char)hex[i * 2])
                        ? hex[i * 2] - '0'
                        : (tolower((unsigned char)hex[i * 2]) - 'a' + 10))
            : -1;
        int lo = isxdigit((unsigned char)hex[i * 2 + 1])
            ? (int)(isdigit((unsigned char)hex[i * 2 + 1])
                        ? hex[i * 2 + 1] - '0'
                        : (tolower((unsigned char)hex[i * 2 + 1]) - 'a' + 10))
            : -1;

        if (hi < 0 || lo < 0)
            return 0;
        out[i] = (unsigned char)((hi << 4) | lo);
    }
    return 1;
}

static hush_status_t hush_event_deny(char *reason, size_t reason_len,
                                     const char *text)
{
    size_t len;
    size_t copy;

    assert(text != NULL);
    if (reason != NULL && reason_len > 0) {
        len = strlen(text);
        copy = len < reason_len - 1 ? len : reason_len - 1;
        memcpy(reason, text, copy);
        reason[copy] = '\0';
    }
    return HUSH_ERR_DENIED;
}

static void hush_event_hex_encode(char *out, const unsigned char *digest,
                                  size_t dlen)
{
    size_t i;

    assert(out != NULL);
    assert(digest != NULL);
    for (i = 0; i < dlen; i++) {
        out[i * 2] = hush_event_hex[digest[i] >> 4];
        out[i * 2 + 1] = hush_event_hex[digest[i] & 0x0Fu];
    }
    out[dlen * 2] = '\0';
}

static void hush_event_digest_dec(EVP_MD_CTX *ctx, int64_t v)
{
    char buf[HUSH_DEC_MAX];
    char tmp[HUSH_DEC_MAX];
    size_t n = 0;
    size_t i;
    uint64_t u;

    assert(ctx != NULL);
    if (v < 0) {
        buf[0] = '-';
        i = 1;
        u = (uint64_t)(-(v + 1)) + 1u;
    } else {
        i = 0;
        u = (uint64_t)v;
    }
    do {
        tmp[n++] = (char)('0' + (unsigned)(u % 10u));
        u /= 10u;
    } while (u != 0);
    while (n > 0)
        buf[i++] = tmp[--n];
    (void)EVP_DigestUpdate(ctx, buf, i);
}

static void hush_event_digest_esc(EVP_MD_CTX *ctx, const char *s)
{
    const unsigned char *p = (const unsigned char *)(s != NULL ? s : "");
    char esc[6];

    assert(ctx != NULL);
    while (*p != '\0') {
        unsigned char c = *p++;

        switch (c) {
        case '"':
            (void)EVP_DigestUpdate(ctx, "\\\"", 2);
            break;
        case '\\':
            (void)EVP_DigestUpdate(ctx, "\\\\", 2);
            break;
        case '\n':
            (void)EVP_DigestUpdate(ctx, "\\n", 2);
            break;
        case '\r':
            (void)EVP_DigestUpdate(ctx, "\\r", 2);
            break;
        case '\t':
            (void)EVP_DigestUpdate(ctx, "\\t", 2);
            break;
        case '\b':
            (void)EVP_DigestUpdate(ctx, "\\b", 2);
            break;
        case '\f':
            (void)EVP_DigestUpdate(ctx, "\\f", 2);
            break;
        default:
            if (c < 0x20u) {
                esc[0] = '\\';
                esc[1] = 'u';
                esc[2] = '0';
                esc[3] = '0';
                esc[4] = hush_event_hex[c >> 4];
                esc[5] = hush_event_hex[c & 0x0Fu];
                (void)EVP_DigestUpdate(ctx, esc, 6);
            } else {
                (void)EVP_DigestUpdate(ctx, &c, 1);
            }
            break;
        }
    }
}
