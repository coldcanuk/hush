/* hush_store.c: owns the bounded in-memory event store and query for Hush. */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "hush_store.h"

/* Opaque ring buffer of events. */
struct hush_store {
    hush_event_t events[HUSH_STORE_CAPACITY];
    size_t head;   /* next write index */
    size_t count;  /* valid entries; <= CAPACITY */
};

/* Rejects NULL out pointer. Allocates and zeros the ring. Transfers ownership to caller. */
static hush_status_t hush_store_alloc(hush_store_t **out_store);

/* Copies ev into the ring at head. Evicts oldest when full (MVP policy). Asserts capacity. */
static void hush_store_write(hush_store_t *store, const hush_event_t *ev);

/* Returns pointer to the logical i-th event (oldest first). */
static const hush_event_t *hush_store_at(const hush_store_t *store, size_t i);

hush_status_t hush_store_create(hush_store_t **out_store)
{
    return hush_store_alloc(out_store);
}

void hush_store_destroy(hush_store_t *store)
{
    free(store);
}

static int hush_store_is_addressable(uint32_t kind)
{
    return kind >= 30000u && kind < 40000u;
}

static const char *hush_store_d_tag(const hush_event_t *ev)
{
    size_t i;

    assert(ev != NULL);
    for (i = 0; i < ev->tag_count && i < (size_t)HUSH_EVENT_MAX_TAGS; i++) {
        if (strcmp(ev->tags[i][0], "d") == 0)
            return ev->tags[i][1];
    }
    return "";
}

static int hush_store_replace_addressable(hush_store_t *store,
                                          const hush_event_t *ev)
{
    size_t i;
    size_t pos;
    const char *d;

    assert(store != NULL);
    assert(ev != NULL);
    if (!hush_store_is_addressable(ev->kind))
        return 0;
    d = hush_store_d_tag(ev);
    for (i = 0; i < store->count; i++) {
        pos = (store->head + HUSH_STORE_CAPACITY - store->count + i)
            % HUSH_STORE_CAPACITY;
        if (store->events[pos].kind != ev->kind)
            continue;
        if (strcmp(store->events[pos].pubkey, ev->pubkey) != 0)
            continue;
        if (strcmp(hush_store_d_tag(&store->events[pos]), d) != 0)
            continue;
        store->events[pos] = *ev;
        return 1;
    }
    return 0;
}

hush_status_t hush_store_insert(hush_store_t *store, const hush_event_t *ev)
{
    if (store == NULL || ev == NULL)
        return HUSH_ERR_ARG;
    if (hush_store_replace_addressable(store, ev))
        return HUSH_OK;
    hush_store_write(store, ev);
    return HUSH_OK;
}

size_t hush_store_query(const hush_store_t *store,
                        const hush_filter_t *filters,
                        size_t nfilters,
                        hush_event_t *out_events,
                        size_t max_events)
{
    if (store == NULL || out_events == NULL)
        return 0;

    size_t written = 0;
    size_t n = (nfilters == 0) ? 1 : nfilters;

    for (size_t i = 0; i < store->count && written < max_events; ++i) {
        const hush_event_t *ev = hush_store_at(store, i);
        bool any = false;
        for (size_t fi = 0; fi < n; ++fi) {
            const hush_filter_t *f = (nfilters == 0) ? NULL : &filters[fi];
            if (f == NULL || hush_filter_match(f, ev)) {
                any = true;
                break;
            }
        }
        if (any) {
            out_events[written++] = *ev;
        }
    }
    return written;
}

static hush_status_t hush_store_alloc(hush_store_t **out_store)
{
    if (out_store == NULL)
        return HUSH_ERR_ARG;
    hush_store_t *s = (hush_store_t *)calloc(1, sizeof(*s));
    if (s == NULL)
        return HUSH_ERR_FULL;
    *out_store = s;
    return HUSH_OK;
}

static void hush_store_write(hush_store_t *store, const hush_event_t *ev)
{
    assert(store != NULL);
    assert(ev != NULL);
    assert(store->count <= HUSH_STORE_CAPACITY);

    if (store->count >= HUSH_STORE_CAPACITY) {
        store->head = (store->head + 1) % HUSH_STORE_CAPACITY;
    } else {
        store->count++;
    }

    size_t idx = store->head;
    store->events[idx] = *ev;
    store->head = (store->head + 1) % HUSH_STORE_CAPACITY;
}

static const hush_event_t *hush_store_at(const hush_store_t *store, size_t i)
{
    assert(store != NULL);
    assert(i < store->count);
    size_t pos = (store->head + HUSH_STORE_CAPACITY - store->count + i) % HUSH_STORE_CAPACITY;
    return &store->events[pos];
}
