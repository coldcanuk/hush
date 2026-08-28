/* hush_store.c: owns the bounded in-memory event store, query, and ring file. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hush_home.h"
#include "hush_store.h"

enum {
    HUSH_STORE_FILE_MODE = 0600,
    HUSH_STORE_VERSION = 1,
    HUSH_STORE_U16_MAX = 65535
};

/* Opaque ring buffer of events. persist_on selects fsync snapshots. */
struct hush_store {
    hush_event_t events[HUSH_STORE_CAPACITY];
    size_t head;
    size_t count;
    int persist_on;
    char persist_path[HUSH_HOME_PATH_MAX];
};

static hush_status_t hush_store_alloc(hush_store_t **out_store);
static void hush_store_write(hush_store_t *store, const hush_event_t *ev);
static const hush_event_t *hush_store_at(const hush_store_t *store, size_t i);
static int hush_store_is_addressable(uint32_t kind);
static const char *hush_store_d_tag(const hush_event_t *ev);
static int hush_store_replace_addressable(hush_store_t *store,
                                          const hush_event_t *ev);
static int hush_store_may_persist(void);
static hush_status_t hush_store_write_u32(int fd, uint32_t v);
static hush_status_t hush_store_write_u16(int fd, uint16_t v);
static hush_status_t hush_store_write_i64(int fd, int64_t v);
static hush_status_t hush_store_write_bytes(int fd, const void *p, size_t n);
static hush_status_t hush_store_read_u32(int fd, uint32_t *out);
static hush_status_t hush_store_read_u16(int fd, uint16_t *out);
static hush_status_t hush_store_read_i64(int fd, int64_t *out);
static hush_status_t hush_store_read_bytes(int fd, void *p, size_t n);
static hush_status_t hush_store_write_event(int fd, const hush_event_t *ev);
static hush_status_t hush_store_read_event(int fd, hush_event_t *ev);
static hush_status_t hush_store_fsync_fd(int fd);
static hush_status_t hush_store_fsync_dir(const char *file);
static hush_status_t hush_store_save(const hush_store_t *store);

hush_status_t hush_store_create(hush_store_t **out_store)
{
    return hush_store_alloc(out_store);
}

void hush_store_destroy(hush_store_t *store)
{
    if (store != NULL && store->persist_on)
        (void)hush_store_save(store);
    free(store);
}

hush_status_t hush_store_persist_open(hush_store_t *store)
{
    char root[HUSH_HOME_PATH_MAX];
    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t count = 0;
    uint32_t cap = 0;
    uint32_t i;
    int fd;
    int n;

    if (store == NULL)
        return HUSH_ERR_ARG;
    store->persist_on = 0;
    store->persist_path[0] = '\0';
    if (!hush_store_may_persist())
        return HUSH_OK;
    if (hush_home_ensure() != HUSH_OK)
        return HUSH_ERR_IO;
    hush_home_root(root, sizeof(root));
    if (root[0] == '\0')
        return HUSH_ERR_IO;
    n = snprintf(store->persist_path, sizeof(store->persist_path), "%s/%s",
                 root, HUSH_STORE_FILE);
    if (n <= 0 || (size_t)n >= sizeof(store->persist_path)) {
        store->persist_path[0] = '\0';
        return HUSH_ERR_IO;
    }
    fd = open(store->persist_path, O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT) {
            store->persist_on = 1;
            return HUSH_OK;
        }
        return HUSH_ERR_IO;
    }
    if (hush_store_read_u32(fd, &magic) != HUSH_OK ||
        hush_store_read_u32(fd, &version) != HUSH_OK ||
        hush_store_read_u32(fd, &count) != HUSH_OK ||
        hush_store_read_u32(fd, &cap) != HUSH_OK) {
        close(fd);
        store->persist_on = 1;
        return HUSH_OK;
    }
    if (magic != HUSH_STORE_MAGIC || version != (uint32_t)HUSH_STORE_VERSION ||
        cap != (uint32_t)HUSH_STORE_CAPACITY ||
        count > (uint32_t)HUSH_STORE_CAPACITY) {
        close(fd);
        store->persist_on = 1;
        return HUSH_OK;
    }
    for (i = 0; i < count; i++) {
        hush_event_t ev;

        memset(&ev, 0, sizeof(ev));
        if (hush_store_read_event(fd, &ev) != HUSH_OK) {
            close(fd);
            return HUSH_ERR_PARSE;
        }
        if (hush_store_insert(store, &ev) != HUSH_OK) {
            close(fd);
            return HUSH_ERR_FULL;
        }
    }
    close(fd);
    store->persist_on = 1;
    return HUSH_OK;
}

hush_status_t hush_store_insert(hush_store_t *store, const hush_event_t *ev)
{
    if (store == NULL || ev == NULL)
        return HUSH_ERR_ARG;
    if (hush_store_replace_addressable(store, ev)) {
        if (store->persist_on)
            (void)hush_store_save(store);
        return HUSH_OK;
    }
    hush_store_write(store, ev);
    if (store->persist_on)
        (void)hush_store_save(store);
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

size_t hush_store_count(const hush_store_t *store)
{
    if (store == NULL)
        return 0;
    return store->count;
}

hush_status_t hush_store_get(const hush_store_t *store, size_t idx,
                             hush_event_t *out)
{
    if (store == NULL || out == NULL)
        return HUSH_ERR_ARG;
    if (idx >= store->count)
        return HUSH_ERR_NOT_FOUND;
    *out = *hush_store_at(store, idx);
    return HUSH_OK;
}

static hush_status_t hush_store_alloc(hush_store_t **out_store)
{
    hush_store_t *s;

    if (out_store == NULL)
        return HUSH_ERR_ARG;
    s = (hush_store_t *)calloc(1, sizeof(*s));
    if (s == NULL)
        return HUSH_ERR_FULL;
    *out_store = s;
    return HUSH_OK;
}

static void hush_store_write(hush_store_t *store, const hush_event_t *ev)
{
    size_t idx;

    assert(store != NULL);
    assert(ev != NULL);
    assert(store->count <= (size_t)HUSH_STORE_CAPACITY);

    if (store->count >= (size_t)HUSH_STORE_CAPACITY)
        store->head = (store->head + 1) % (size_t)HUSH_STORE_CAPACITY;
    else
        store->count++;

    idx = store->head;
    store->events[idx] = *ev;
    store->head = (store->head + 1) % (size_t)HUSH_STORE_CAPACITY;
}

static const hush_event_t *hush_store_at(const hush_store_t *store, size_t i)
{
    size_t pos;

    assert(store != NULL);
    assert(i < store->count);
    pos = (store->head + (size_t)HUSH_STORE_CAPACITY - store->count + i)
        % (size_t)HUSH_STORE_CAPACITY;
    return &store->events[pos];
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
        pos = (store->head + (size_t)HUSH_STORE_CAPACITY - store->count + i)
            % (size_t)HUSH_STORE_CAPACITY;
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

static int hush_store_may_persist(void)
{
    const char *home;
    const char *cfg;

    home = getenv(HUSH_HOME_ENV);
    if (home != NULL && home[0] != '\0')
        return 1;
    cfg = getenv(HUSH_HOME_ENV_CONFIG);
    if (cfg != NULL && cfg[0] != '\0')
        return 0;
    return 1;
}

static hush_status_t hush_store_write_u32(int fd, uint32_t v)
{
    unsigned char b[4];

    b[0] = (unsigned char)(v & 0xffu);
    b[1] = (unsigned char)((v >> 8) & 0xffu);
    b[2] = (unsigned char)((v >> 16) & 0xffu);
    b[3] = (unsigned char)((v >> 24) & 0xffu);
    return hush_store_write_bytes(fd, b, sizeof(b));
}

static hush_status_t hush_store_write_u16(int fd, uint16_t v)
{
    unsigned char b[2];

    b[0] = (unsigned char)(v & 0xffu);
    b[1] = (unsigned char)((v >> 8) & 0xffu);
    return hush_store_write_bytes(fd, b, sizeof(b));
}

static hush_status_t hush_store_write_i64(int fd, int64_t v)
{
    uint64_t u = (uint64_t)v;
    unsigned char b[8];
    size_t i;

    for (i = 0; i < 8; i++)
        b[i] = (unsigned char)((u >> (8u * (unsigned)i)) & 0xffu);
    return hush_store_write_bytes(fd, b, sizeof(b));
}

static hush_status_t hush_store_write_bytes(int fd, const void *p, size_t n)
{
    const unsigned char *b = p;
    size_t off = 0;

    if (n == 0)
        return HUSH_OK;
    while (off < n) {
        ssize_t w = write(fd, b + off, n - off);

        if (w <= 0)
            return HUSH_ERR_IO;
        off += (size_t)w;
    }
    return HUSH_OK;
}

static hush_status_t hush_store_read_u32(int fd, uint32_t *out)
{
    unsigned char b[4];

    HUSH_TRY(hush_store_read_bytes(fd, b, sizeof(b)));
    *out = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16)
        | ((uint32_t)b[3] << 24);
    return HUSH_OK;
}

static hush_status_t hush_store_read_u16(int fd, uint16_t *out)
{
    unsigned char b[2];

    HUSH_TRY(hush_store_read_bytes(fd, b, sizeof(b)));
    *out = (uint16_t)((unsigned)b[0] | ((unsigned)b[1] << 8));
    return HUSH_OK;
}

static hush_status_t hush_store_read_i64(int fd, int64_t *out)
{
    unsigned char b[8];
    uint64_t u = 0;
    size_t i;

    HUSH_TRY(hush_store_read_bytes(fd, b, sizeof(b)));
    for (i = 0; i < 8; i++)
        u |= (uint64_t)b[i] << (8u * (unsigned)i);
    *out = (int64_t)u;
    return HUSH_OK;
}

static hush_status_t hush_store_read_bytes(int fd, void *p, size_t n)
{
    unsigned char *b = p;
    size_t off = 0;

    if (n == 0)
        return HUSH_OK;
    while (off < n) {
        ssize_t r = read(fd, b + off, n - off);

        if (r <= 0)
            return HUSH_ERR_IO;
        off += (size_t)r;
    }
    return HUSH_OK;
}

static hush_status_t hush_store_write_event(int fd, const hush_event_t *ev)
{
    size_t clen;
    size_t t;
    size_t e;
    uint32_t tags;

    assert(ev != NULL);
    HUSH_TRY(hush_store_write_bytes(fd, ev->id, (size_t)HUSH_EVENT_ID_HEX_LEN));
    HUSH_TRY(hush_store_write_bytes(fd, ev->pubkey,
                                    (size_t)HUSH_EVENT_PUBKEY_HEX_LEN));
    HUSH_TRY(hush_store_write_u32(fd, ev->kind));
    HUSH_TRY(hush_store_write_i64(fd, ev->created_at));
    clen = strlen(ev->content);
    if (clen > (size_t)HUSH_EVENT_MAX_CONTENT)
        clen = (size_t)HUSH_EVENT_MAX_CONTENT;
    HUSH_TRY(hush_store_write_u32(fd, (uint32_t)clen));
    HUSH_TRY(hush_store_write_bytes(fd, ev->content, clen));
    tags = (uint32_t)ev->tag_count;
    if (tags > (uint32_t)HUSH_EVENT_MAX_TAGS)
        tags = (uint32_t)HUSH_EVENT_MAX_TAGS;
    HUSH_TRY(hush_store_write_u32(fd, tags));
    for (t = 0; t < (size_t)tags; t++) {
        for (e = 0; e < (size_t)HUSH_EVENT_MAX_TAG_ELEMS; e++) {
            size_t len = strlen(ev->tags[t][e]);
            uint16_t n16;

            if (len > (size_t)HUSH_EVENT_MAX_TAG_LEN)
                len = (size_t)HUSH_EVENT_MAX_TAG_LEN;
            if (len > (size_t)HUSH_STORE_U16_MAX)
                return HUSH_ERR_FULL;
            n16 = (uint16_t)len;
            HUSH_TRY(hush_store_write_u16(fd, n16));
            HUSH_TRY(hush_store_write_bytes(fd, ev->tags[t][e], len));
        }
    }
    return HUSH_OK;
}

static hush_status_t hush_store_read_event(int fd, hush_event_t *ev)
{
    uint32_t clen = 0;
    uint32_t tags = 0;
    size_t t;
    size_t e;

    assert(ev != NULL);
    memset(ev, 0, sizeof(*ev));
    HUSH_TRY(hush_store_read_bytes(fd, ev->id, (size_t)HUSH_EVENT_ID_HEX_LEN));
    ev->id[HUSH_EVENT_ID_HEX_LEN] = '\0';
    HUSH_TRY(hush_store_read_bytes(fd, ev->pubkey,
                                   (size_t)HUSH_EVENT_PUBKEY_HEX_LEN));
    ev->pubkey[HUSH_EVENT_PUBKEY_HEX_LEN] = '\0';
    HUSH_TRY(hush_store_read_u32(fd, &ev->kind));
    HUSH_TRY(hush_store_read_i64(fd, &ev->created_at));
    HUSH_TRY(hush_store_read_u32(fd, &clen));
    if (clen > (uint32_t)HUSH_EVENT_MAX_CONTENT)
        return HUSH_ERR_PARSE;
    HUSH_TRY(hush_store_read_bytes(fd, ev->content, (size_t)clen));
    ev->content[clen] = '\0';
    HUSH_TRY(hush_store_read_u32(fd, &tags));
    if (tags > (uint32_t)HUSH_EVENT_MAX_TAGS)
        return HUSH_ERR_PARSE;
    ev->tag_count = (size_t)tags;
    for (t = 0; t < (size_t)tags; t++) {
        for (e = 0; e < (size_t)HUSH_EVENT_MAX_TAG_ELEMS; e++) {
            uint16_t n16 = 0;

            HUSH_TRY(hush_store_read_u16(fd, &n16));
            if ((size_t)n16 > (size_t)HUSH_EVENT_MAX_TAG_LEN)
                return HUSH_ERR_PARSE;
            HUSH_TRY(hush_store_read_bytes(fd, ev->tags[t][e], (size_t)n16));
            ev->tags[t][e][n16] = '\0';
        }
    }
    return HUSH_OK;
}

static hush_status_t hush_store_fsync_fd(int fd)
{
    if (fd < 0)
        return HUSH_ERR_IO;
    if (fsync(fd) != 0)
        return HUSH_ERR_IO;
    return HUSH_OK;
}

static hush_status_t hush_store_fsync_dir(const char *file)
{
    char dir[HUSH_HOME_PATH_MAX];
    char *slash;
    int fd;
    size_t n;

    assert(file != NULL);
    n = strlen(file);
    if (n >= sizeof(dir))
        return HUSH_ERR_IO;
    memcpy(dir, file, n + 1);
    slash = strrchr(dir, '/');
    if (slash == NULL)
        memcpy(dir, ".\0", 2);
    else if (slash == dir)
        slash[1] = '\0';
    else
        *slash = '\0';
    fd = open(dir, O_RDONLY | O_DIRECTORY);
    if (fd < 0)
        return HUSH_ERR_IO;
    if (hush_store_fsync_fd(fd) != HUSH_OK) {
        close(fd);
        return HUSH_ERR_IO;
    }
    if (close(fd) != 0)
        return HUSH_ERR_IO;
    return HUSH_OK;
}

static hush_status_t hush_store_save(const hush_store_t *store)
{
    char tmp[HUSH_HOME_PATH_MAX];
    int fd;
    int n;
    size_t i;

    assert(store != NULL);
    if (store->persist_path[0] == '\0')
        return HUSH_ERR_IO;
    n = snprintf(tmp, sizeof(tmp), "%s.tmp", store->persist_path);
    if (n <= 0 || (size_t)n >= sizeof(tmp))
        return HUSH_ERR_IO;
    fd = open(tmp, O_CREAT | O_TRUNC | O_WRONLY, HUSH_STORE_FILE_MODE);
    if (fd < 0)
        return HUSH_ERR_IO;
    if (hush_store_write_u32(fd, HUSH_STORE_MAGIC) != HUSH_OK ||
        hush_store_write_u32(fd, (uint32_t)HUSH_STORE_VERSION) != HUSH_OK ||
        hush_store_write_u32(fd, (uint32_t)store->count) != HUSH_OK ||
        hush_store_write_u32(fd, (uint32_t)HUSH_STORE_CAPACITY) != HUSH_OK) {
        close(fd);
        unlink(tmp);
        return HUSH_ERR_IO;
    }
    for (i = 0; i < store->count; i++) {
        if (hush_store_write_event(fd, hush_store_at(store, i)) != HUSH_OK) {
            close(fd);
            unlink(tmp);
            return HUSH_ERR_IO;
        }
    }
    if (hush_store_fsync_fd(fd) != HUSH_OK) {
        close(fd);
        unlink(tmp);
        return HUSH_ERR_IO;
    }
    if (close(fd) != 0) {
        unlink(tmp);
        return HUSH_ERR_IO;
    }
    if (rename(tmp, store->persist_path) != 0) {
        unlink(tmp);
        return HUSH_ERR_IO;
    }
    return hush_store_fsync_dir(store->persist_path);
}
