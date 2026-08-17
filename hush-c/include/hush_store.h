/* hush_store.h: bounded in-memory event store (MVP ring). */

#ifndef HUSH_STORE_H
#define HUSH_STORE_H

#include <stddef.h>
#include "hush_event.h"
#include "hush_filter.h"
#include "hush_status.h"

enum {
    HUSH_STORE_CAPACITY = 1024
};

typedef struct hush_store hush_store_t; /* opaque for MVP; or expose for tests */

hush_status_t hush_store_create(hush_store_t **out_store);
void hush_store_destroy(hush_store_t *store);

/* Insert if not duplicate id. Fails HUSH_ERR_FULL when at cap (MVP policy: drop oldest). */
hush_status_t hush_store_insert(hush_store_t *store, const hush_event_t *ev);

/* Collect up to max_events matching any filter. Returns count written. */
size_t hush_store_query(const hush_store_t *store, const hush_filter_t *filters,
                        size_t nfilters, hush_event_t *out_events, size_t max_events);

#endif /* HUSH_STORE_H */
