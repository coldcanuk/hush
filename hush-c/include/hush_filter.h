/* hush_filter.h: NIP-01 filter representation and matching. */

#ifndef HUSH_FILTER_H
#define HUSH_FILTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "hush_event.h"
#include "hush_status.h"

enum {
    HUSH_FILTER_MAX_KINDS = 8,
    HUSH_FILTER_MAX_IDS = 8,
    HUSH_FILTER_MAX_AUTHORS = 8,
    HUSH_FILTER_MAX_TAGS = 4
};

typedef struct {
    size_t kinds_len;
    uint32_t kinds[HUSH_FILTER_MAX_KINDS];
    size_t ids_len;
    char ids[HUSH_FILTER_MAX_IDS][HUSH_EVENT_ID_HEX_LEN + 1];
    size_t authors_len;
    char authors[HUSH_FILTER_MAX_AUTHORS][HUSH_EVENT_PUBKEY_HEX_LEN + 1];
    int64_t since;
    int64_t until;
    /* simple tag filters: key like "h", values */
    size_t tag_count;
    char tag_keys[HUSH_FILTER_MAX_TAGS][3];
    size_t tag_vals_len[HUSH_FILTER_MAX_TAGS];
    char tag_vals[HUSH_FILTER_MAX_TAGS][4][HUSH_EVENT_MAX_TAG_LEN + 1];
} hush_filter_t;

/* Returns true if event matches this single filter (AND semantics inside). */
bool hush_filter_match(const hush_filter_t *f, const hush_event_t *ev);

#endif /* HUSH_FILTER_H */
