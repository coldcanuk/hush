/* hush_event.h: Nostr event representation and id computation for Hush. */

#ifndef HUSH_EVENT_H
#define HUSH_EVENT_H

#include <stddef.h>
#include <stdint.h>
#include "hush_status.h"

enum {
    HUSH_EVENT_ID_HEX_LEN = 64,
    HUSH_EVENT_PUBKEY_HEX_LEN = 64,
    HUSH_EVENT_SIG_HEX_LEN = 128,
    HUSH_EVENT_MAX_CONTENT = 4096,
    HUSH_EVENT_MAX_TAGS = 32,
    HUSH_EVENT_MAX_TAG_ELEMS = 4,
    HUSH_EVENT_MAX_TAG_LEN = 256
};

typedef struct {
    char id[HUSH_EVENT_ID_HEX_LEN + 1];
    char pubkey[HUSH_EVENT_PUBKEY_HEX_LEN + 1];
    uint32_t kind;
    int64_t created_at;
    char content[HUSH_EVENT_MAX_CONTENT + 1];
    /* tags stored as flattened for MVP simplicity; parser populates */
    size_t tag_count;
    char tags[HUSH_EVENT_MAX_TAGS][HUSH_EVENT_MAX_TAG_ELEMS][HUSH_EVENT_MAX_TAG_LEN + 1];
} hush_event_t;

/* Computes id = hex(sha256( serialized 0 + pubkey + created + kind + tags + content )).
 * On success writes NUL-terminated hex to out_id (65 bytes). Fails HUSH_ERR_ARG on NULLs. */
hush_status_t hush_event_compute_id(const hush_event_t *ev, char *out_id);

/* Basic structural validation (lengths, kind bounds). Does not verify signature. */
hush_status_t hush_event_validate(const hush_event_t *ev);

#endif /* HUSH_EVENT_H */
