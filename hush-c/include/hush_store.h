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

#define HUSH_STORE_FILE "store.ring"
#define HUSH_STORE_MAGIC 0x31545348u

typedef struct hush_store hush_store_t; /* opaque for MVP; or expose for tests */

hush_status_t hush_store_create(hush_store_t **out_store);
void hush_store_destroy(hush_store_t *store);

/* Loads $HUSH_HOME/store.ring by inserting each record so addressable
 * replace re-applies. Enables fsync-on-insert. Missing file is OK.
 * Separate from wake.ledger. */
hush_status_t hush_store_persist_open(hush_store_t *store);

/* Insert. Evicts oldest when full. Addressable kinds replace on
 * (pubkey, kind, d). When persist is enabled, snapshots and fsyncs. */
hush_status_t hush_store_insert(hush_store_t *store, const hush_event_t *ev);

/* Collect up to max_events matching any filter. Returns count written. */
size_t hush_store_query(const hush_store_t *store, const hush_filter_t *filters,
                        size_t nfilters, hush_event_t *out_events, size_t max_events);

/* Live ring occupancy, 0..HUSH_STORE_CAPACITY. */
size_t hush_store_count(const hush_store_t *store);

/* Copies the oldest-first event at idx. Fails HUSH_ERR_NOT_FOUND. */
hush_status_t hush_store_get(const hush_store_t *store, size_t idx,
                             hush_event_t *out);

#endif /* HUSH_STORE_H */
