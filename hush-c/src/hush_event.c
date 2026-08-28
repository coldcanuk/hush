/* hush_event.c: owns Nostr event representation, id computation, and basic validation for Hush. */

#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <string.h>

#include <openssl/evp.h>

#include "hush_event.h"
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
        int emitted = 0;

        if (i > 0)
            (void)EVP_DigestUpdate(ctx, ",", 1);
        (void)EVP_DigestUpdate(ctx, "[", 1);
        for (j = 0; j < (size_t)HUSH_EVENT_MAX_TAG_ELEMS; j++) {
            if (ev->tags[i][j][0] == '\0')
                continue;
            if (emitted)
                (void)EVP_DigestUpdate(ctx, ",", 1);
            (void)EVP_DigestUpdate(ctx, "\"", 1);
            hush_event_digest_esc(ctx, ev->tags[i][j]);
            (void)EVP_DigestUpdate(ctx, "\"", 1);
            emitted = 1;
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

/* Rejects NULL or structurally invalid (lengths, kind). */
hush_status_t hush_event_validate(const hush_event_t *ev)
{
    if (ev == NULL)
        return HUSH_ERR_ARG;
    if (strlen(ev->id) != HUSH_EVENT_ID_HEX_LEN)
        return HUSH_ERR_ARG;
    if (ev->kind > (uint32_t)HUSH_MAX_KIND)
        return HUSH_ERR_ARG;
    if (strlen(ev->content) > HUSH_EVENT_MAX_CONTENT)
        return HUSH_ERR_ARG;
    return HUSH_OK;
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
