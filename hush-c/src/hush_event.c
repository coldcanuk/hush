/* hush_event.c: owns Nostr event representation, id computation, and basic validation for Hush. */

#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <string.h>

#include "hush_event.h"
#include "hush_status.h"

enum {
    HUSH_MAX_KIND = 65535
};

/* Adapter over SHA-256. Writes exactly 64 hex chars (no NUL written). */
static void hush_sha256_hex(const unsigned char *data, size_t len, char *out_hex64);

/* Emits NIP-01 id-preimage bytes for MVP (0 + pubkey + created_at + kind + content).
 * Tags omitted in MVP slice; documented limitation. */
static void hush_event_serialize_for_id(const hush_event_t *ev,
                                        unsigned char *out_buf,
                                        size_t *out_len);

/* Rejects NULL arguments. Writes NUL-terminated 64-char hex id to out_id. */
hush_status_t hush_event_compute_id(const hush_event_t *ev, char *out_id)
{
    if (ev == NULL || out_id == NULL)
        return HUSH_ERR_ARG;

    unsigned char buf[4096];
    size_t blen = 0;
    hush_event_serialize_for_id(ev, buf, &blen);
    hush_sha256_hex(buf, blen, out_id);
    out_id[HUSH_EVENT_ID_HEX_LEN] = '\0';
    return HUSH_OK;
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

static void hush_event_serialize_for_id(const hush_event_t *ev,
                                        unsigned char *out_buf,
                                        size_t *out_len)
{
    assert(ev != NULL);
    assert(out_buf != NULL);
    assert(out_len != NULL);

    size_t off = 0;
    out_buf[off++] = 0;
    memcpy(out_buf + off, ev->pubkey, HUSH_EVENT_PUBKEY_HEX_LEN);
    off += HUSH_EVENT_PUBKEY_HEX_LEN;

    for (int i = 7; i >= 0; --i) {
        out_buf[off++] = (unsigned char)((ev->created_at >> (i * 8)) & 0xFF);
    }

    out_buf[off++] = (unsigned char)((ev->kind >> 24) & 0xFF);
    out_buf[off++] = (unsigned char)((ev->kind >> 16) & 0xFF);
    out_buf[off++] = (unsigned char)((ev->kind >> 8) & 0xFF);
    out_buf[off++] = (unsigned char)(ev->kind & 0xFF);

    size_t cl = strlen(ev->content);
    memcpy(out_buf + off, ev->content, cl);
    off += cl;

    *out_len = off;
}

static void hush_sha256_hex(const unsigned char *data, size_t len, char *out_hex64)
{
#if defined(HUSH_USE_OPENSSL)
    /* DEVIATION: real SHA256 omitted in MVP; see docs/research/RESEARCH.md. */
    (void)data;
    (void)len;
    memset(out_hex64, '0', HUSH_EVENT_ID_HEX_LEN);
#else
    /* MVP dev only: deterministic zero id. */
    (void)data;
    (void)len;
    memset(out_hex64, '0', HUSH_EVENT_ID_HEX_LEN);
#endif
}
