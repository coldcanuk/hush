/* hush_wake.c: owns the bounded durable claim ledger and install device id. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <openssl/rand.h>

#include "hush_event.h"
#include "hush_home.h"
#include "hush_presence.h"
#include "hush_wake.h"

enum {
    HUSH_WAKE_FILE_MODE = 0600
};

#define HUSH_WAKE_CHAN_FALLBACK "general"

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t nslot;
    uint32_t reserved;
} hush_wake_hdr_t;

typedef struct {
    unsigned char key[HUSH_WAKE_KEY_LEN];
    unsigned char trigger[HUSH_WAKE_TRIGGER_LEN];
    unsigned char device[HUSH_WAKE_DEVICE_LEN];
    uint8_t state;
    uint8_t pad[7];
    int64_t lease_unix;
    char robot[HUSH_EVENT_PUBKEY_HEX_LEN + 1];
    char root[HUSH_EVENT_ID_HEX_LEN + 1];
    char channel[HUSH_EVENT_MAX_TAG_LEN + 1];
} hush_wake_slot_t;

typedef struct {
    unsigned char key[HUSH_WAKE_KEY_LEN];
    uint8_t used;
    uint8_t pad[7];
} hush_wake_deliv_t;

static hush_wake_slot_t g_slots[HUSH_WAKE_SLOT_MAX];
static hush_wake_deliv_t g_deliv[HUSH_WAKE_SLOT_MAX];
static unsigned char g_device[HUSH_WAKE_DEVICE_LEN];
static int g_ready;
static int g_persist;

static const char hush_wake_hex[] = "0123456789abcdef";

static void hush_wake_copy(char *dst, size_t dstsz, const char *src);
static hush_status_t hush_wake_fill_key(unsigned char key[HUSH_WAKE_KEY_LEN],
                                        const char *robot, const char *root);
static hush_status_t hush_wake_parse_hex(unsigned char *out, size_t out_len,
                                         const char *hex);
static int hush_wake_key_eq(const unsigned char *a, const unsigned char *b);
static int hush_wake_is_zero(const unsigned char *p, size_t n);
static hush_wake_slot_t *hush_wake_find_key(hush_wake_slot_t *slots,
                                            const unsigned char *key);
static hush_wake_slot_t *hush_wake_find_empty(hush_wake_slot_t *slots);
static int hush_wake_is_same_device(const hush_wake_slot_t *slot);
static int hush_wake_is_lease_live(const hush_wake_slot_t *slot, time_t now);
static int hush_wake_is_same_trigger(const hush_wake_slot_t *slot,
                                     const unsigned char *trigger);
static void hush_wake_take_slot(hush_wake_slot_t *slot, const hush_wake_in_t *in,
                                const unsigned char *key,
                                const unsigned char *trigger);
static void hush_wake_mark_slot_intro(hush_wake_slot_t *slot,
                                      const hush_wake_in_t *in,
                                      const unsigned char *key);
static void hush_wake_mark_slot_done(hush_wake_slot_t *slot,
                                     const unsigned char *trigger);
static hush_status_t hush_wake_claim_existing(hush_wake_slot_t *slot,
                                              const hush_wake_in_t *in,
                                              const unsigned char *key,
                                              const unsigned char *trigger);
static hush_status_t hush_wake_drop_lease(hush_store_t *store,
                                          const hush_wake_slot_t *slot,
                                          time_t now);
static void hush_wake_agents_join(char *out, size_t outsz, const char *name);
static hush_status_t hush_wake_fsync_fd(int fd);
static hush_status_t hush_wake_fsync_dir(const char *file);
static hush_wake_deliv_t *hush_wake_find_deliv(hush_wake_deliv_t *deliv,
                                              const unsigned char *key);
static hush_wake_deliv_t *hush_wake_find_empty_deliv(hush_wake_deliv_t *deliv);
static hush_status_t hush_wake_note_delivery(hush_wake_deliv_t *deliv,
                                             const unsigned char *key);
static int hush_wake_delivery_blocks(const hush_wake_slot_t *slot,
                                     const hush_wake_deliv_t *hit,
                                     time_t now);
static hush_status_t hush_wake_write_file(const char *path,
                                          const hush_wake_slot_t *slots,
                                          const hush_wake_deliv_t *deliv);
static hush_status_t hush_wake_commit(const hush_wake_slot_t *slots,
                                      const hush_wake_deliv_t *deliv);
static hush_status_t hush_wake_load_file(void);
static hush_status_t hush_wake_mint_device(void);
static hush_status_t hush_wake_load_device(void);
static hush_status_t hush_wake_need_ready(void);
static int hush_wake_has_intro(hush_wake_state_t state);
static int hush_wake_may_persist(void);

void hush_wake_init(void)
{
    memset(g_slots, 0, sizeof(g_slots));
    memset(g_deliv, 0, sizeof(g_deliv));
    memset(g_device, 0, sizeof(g_device));
    g_ready = 0;
    g_persist = 0;
    if (hush_wake_may_persist() && hush_home_ensure() == HUSH_OK &&
        hush_wake_load_device() == HUSH_OK) {
        if (hush_wake_load_file() != HUSH_OK) {
            memset(g_slots, 0, sizeof(g_slots));
            memset(g_deliv, 0, sizeof(g_deliv));
        }
        g_persist = 1;
    } else if (RAND_bytes(g_device, HUSH_WAKE_DEVICE_LEN) != 1) {
        return;
    }
    g_ready = 1;
}

int hush_wake_intro_seen(const char *robot_hex, const char *root_hex)
{
    unsigned char key[HUSH_WAKE_KEY_LEN];
    const hush_wake_slot_t *slot;

    if (hush_wake_fill_key(key, robot_hex, root_hex) != HUSH_OK)
        return 0;
    slot = hush_wake_find_key(g_slots, key);
    if (slot == NULL)
        return 0;
    return hush_wake_has_intro((hush_wake_state_t)slot->state);
}

hush_status_t hush_wake_mark_intro(const hush_wake_in_t *in)
{
    unsigned char key[HUSH_WAKE_KEY_LEN];
    hush_wake_slot_t slots[HUSH_WAKE_SLOT_MAX];
    hush_wake_slot_t *slot;

    HUSH_TRY(hush_wake_need_ready());
    if (in == NULL)
        return HUSH_ERR_ARG;
    HUSH_TRY(hush_wake_fill_key(key, in->robot_hex, in->root_hex));
    memcpy(slots, g_slots, sizeof(slots));
    slot = hush_wake_find_key(slots, key);
    if (slot != NULL && hush_wake_has_intro((hush_wake_state_t)slot->state))
        return HUSH_OK;
    if (slot == NULL) {
        slot = hush_wake_find_empty(slots);
        if (slot == NULL)
            return HUSH_ERR_FULL;
    }
    hush_wake_mark_slot_intro(slot, in, key);
    return hush_wake_commit(slots, g_deliv);
}

hush_status_t hush_wake_claim(const hush_wake_in_t *in)
{
    unsigned char key[HUSH_WAKE_KEY_LEN];
    unsigned char dkey[HUSH_WAKE_KEY_LEN];
    unsigned char trigger[HUSH_WAKE_TRIGGER_LEN];
    hush_wake_slot_t slots[HUSH_WAKE_SLOT_MAX];
    hush_wake_deliv_t deliv[HUSH_WAKE_SLOT_MAX];
    hush_wake_slot_t *slot;
    hush_wake_deliv_t *hit;
    time_t now;
    hush_status_t st;

    HUSH_TRY(hush_wake_need_ready());
    if (in == NULL)
        return HUSH_ERR_ARG;
    HUSH_TRY(hush_wake_fill_key(key, in->robot_hex, in->root_hex));
    HUSH_TRY(hush_wake_fill_key(dkey, in->robot_hex, in->trigger_id));
    HUSH_TRY(hush_wake_parse_hex(trigger, (size_t)HUSH_WAKE_TRIGGER_LEN,
                                 in->trigger_id));
    memcpy(slots, g_slots, sizeof(slots));
    memcpy(deliv, g_deliv, sizeof(deliv));
    now = in->now != 0 ? in->now : time(NULL);
    slot = hush_wake_find_key(slots, key);
    hit = hush_wake_find_deliv(deliv, dkey);
    if (hush_wake_delivery_blocks(slot, hit, now))
        return HUSH_ERR_DENIED;
    if (slot == NULL) {
        slot = hush_wake_find_empty(slots);
        if (slot == NULL)
            return HUSH_ERR_FULL;
        hush_wake_take_slot(slot, in, key, trigger);
        HUSH_TRY(hush_wake_note_delivery(deliv, dkey));
        return hush_wake_commit(slots, deliv);
    }
    st = hush_wake_claim_existing(slot, in, key, trigger);
    if (st == HUSH_OK)
        HUSH_TRY(hush_wake_note_delivery(deliv, dkey));
    if (memcmp(slots, g_slots, sizeof(slots)) == 0 &&
        memcmp(deliv, g_deliv, sizeof(deliv)) == 0)
        return st;
    {
        hush_status_t cs = hush_wake_commit(slots, deliv);

        if (cs != HUSH_OK)
            return cs;
    }
    return st;
}

hush_status_t hush_wake_done(const hush_wake_in_t *in)
{
    unsigned char key[HUSH_WAKE_KEY_LEN];
    unsigned char dkey[HUSH_WAKE_KEY_LEN];
    unsigned char trigger[HUSH_WAKE_TRIGGER_LEN];
    hush_wake_slot_t slots[HUSH_WAKE_SLOT_MAX];
    hush_wake_deliv_t deliv[HUSH_WAKE_SLOT_MAX];
    hush_wake_slot_t *slot;

    HUSH_TRY(hush_wake_need_ready());
    if (in == NULL)
        return HUSH_ERR_ARG;
    HUSH_TRY(hush_wake_fill_key(key, in->robot_hex, in->root_hex));
    HUSH_TRY(hush_wake_fill_key(dkey, in->robot_hex, in->trigger_id));
    HUSH_TRY(hush_wake_parse_hex(trigger, (size_t)HUSH_WAKE_TRIGGER_LEN,
                                 in->trigger_id));
    memcpy(slots, g_slots, sizeof(slots));
    memcpy(deliv, g_deliv, sizeof(deliv));
    slot = hush_wake_find_key(slots, key);
    if (slot == NULL) {
        slot = hush_wake_find_empty(slots);
        if (slot == NULL)
            return HUSH_ERR_FULL;
        hush_wake_take_slot(slot, in, key, trigger);
    }
    hush_wake_mark_slot_done(slot, trigger);
    HUSH_TRY(hush_wake_note_delivery(deliv, dkey));
    return hush_wake_commit(slots, deliv);
}

void hush_wake_expire(hush_store_t *store, time_t now)
{
    hush_wake_slot_t slots[HUSH_WAKE_SLOT_MAX];
    time_t t;
    size_t i;
    int dirty = 0;

    if (!g_ready)
        return;
    t = now != 0 ? now : time(NULL);
    memcpy(slots, g_slots, sizeof(slots));
    for (i = 0; i < (size_t)HUSH_WAKE_SLOT_MAX; i++) {
        if (slots[i].state != (uint8_t)HUSH_WAKE_ST_CLAIMED)
            continue;
        if (hush_wake_is_lease_live(&slots[i], t))
            continue;
        (void)hush_wake_drop_lease(store, &slots[i], t);
        hush_wake_mark_slot_done(&slots[i], slots[i].trigger);
        dirty = 1;
    }
    if (dirty)
        (void)hush_wake_commit(slots, g_deliv);
}

hush_wake_state_t hush_wake_state(const char *robot_hex, const char *root_hex)
{
    unsigned char key[HUSH_WAKE_KEY_LEN];
    const hush_wake_slot_t *slot;

    if (hush_wake_fill_key(key, robot_hex, root_hex) != HUSH_OK)
        return HUSH_WAKE_ST_EMPTY;
    slot = hush_wake_find_key(g_slots, key);
    if (slot == NULL)
        return HUSH_WAKE_ST_EMPTY;
    return (hush_wake_state_t)slot->state;
}

void hush_wake_test_set_device(const unsigned char *id)
{
    assert(id != NULL);
    memcpy(g_device, id, (size_t)HUSH_WAKE_DEVICE_LEN);
}

static void hush_wake_copy(char *dst, size_t dstsz, const char *src)
{
    size_t n;

    assert(dst != NULL);
    assert(dstsz > 0);
    dst[0] = '\0';
    if (src == NULL)
        return;
    n = strlen(src);
    if (n >= dstsz)
        n = dstsz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static hush_status_t hush_wake_fill_key(unsigned char key[HUSH_WAKE_KEY_LEN],
                                        const char *robot, const char *root)
{
    if (key == NULL)
        return HUSH_ERR_ARG;
    return hush_presence_work_digest(key, robot, root);
}

static hush_status_t hush_wake_parse_hex(unsigned char *out, size_t out_len,
                                         const char *hex)
{
    size_t i;

    if (out == NULL || hex == NULL || out_len == 0)
        return HUSH_ERR_ARG;
    if (strlen(hex) != out_len * 2)
        return HUSH_ERR_ARG;
    for (i = 0; i < out_len; i++) {
        unsigned char hi = (unsigned char)hex[i * 2];
        unsigned char lo = (unsigned char)hex[i * 2 + 1];
        unsigned int v;

        if (!isxdigit(hi) || !isxdigit(lo))
            return HUSH_ERR_ARG;
        if (hi >= '0' && hi <= '9')
            v = (unsigned int)(hi - '0');
        else
            v = 10u + (unsigned int)(tolower((int)hi) - 'a');
        v <<= 4;
        if (lo >= '0' && lo <= '9')
            v |= (unsigned int)(lo - '0');
        else
            v |= 10u + (unsigned int)(tolower((int)lo) - 'a');
        out[i] = (unsigned char)v;
    }
    return HUSH_OK;
}

static int hush_wake_key_eq(const unsigned char *a, const unsigned char *b)
{
    assert(a != NULL);
    assert(b != NULL);
    return memcmp(a, b, (size_t)HUSH_WAKE_KEY_LEN) == 0;
}

static int hush_wake_is_zero(const unsigned char *p, size_t n)
{
    size_t i;

    assert(p != NULL);
    for (i = 0; i < n; i++) {
        if (p[i] != 0)
            return 0;
    }
    return 1;
}

static hush_wake_slot_t *hush_wake_find_key(hush_wake_slot_t *slots,
                                            const unsigned char *key)
{
    size_t i;

    assert(slots != NULL);
    assert(key != NULL);
    for (i = 0; i < (size_t)HUSH_WAKE_SLOT_MAX; i++) {
        if (slots[i].state == (uint8_t)HUSH_WAKE_ST_EMPTY)
            continue;
        if (hush_wake_key_eq(slots[i].key, key))
            return &slots[i];
    }
    return NULL;
}

static hush_wake_slot_t *hush_wake_find_empty(hush_wake_slot_t *slots)
{
    size_t i;

    assert(slots != NULL);
    for (i = 0; i < (size_t)HUSH_WAKE_SLOT_MAX; i++) {
        if (slots[i].state == (uint8_t)HUSH_WAKE_ST_EMPTY &&
            hush_wake_is_zero(slots[i].key, (size_t)HUSH_WAKE_KEY_LEN))
            return &slots[i];
    }
    return NULL;
}

static int hush_wake_is_same_device(const hush_wake_slot_t *slot)
{
    assert(slot != NULL);
    return memcmp(slot->device, g_device, (size_t)HUSH_WAKE_DEVICE_LEN) == 0;
}

static int hush_wake_is_lease_live(const hush_wake_slot_t *slot, time_t now)
{
    assert(slot != NULL);
    if (slot->lease_unix <= 0)
        return 0;
    return now < slot->lease_unix;
}

static int hush_wake_is_same_trigger(const hush_wake_slot_t *slot,
                                     const unsigned char *trigger)
{
    assert(slot != NULL);
    assert(trigger != NULL);
    if (hush_wake_is_zero(slot->trigger, (size_t)HUSH_WAKE_TRIGGER_LEN))
        return 0;
    return memcmp(slot->trigger, trigger, (size_t)HUSH_WAKE_TRIGGER_LEN) == 0;
}

static void hush_wake_take_slot(hush_wake_slot_t *slot, const hush_wake_in_t *in,
                                const unsigned char *key,
                                const unsigned char *trigger)
{
    time_t now;

    assert(slot != NULL);
    assert(in != NULL);
    assert(key != NULL);
    assert(trigger != NULL);
    now = in->now != 0 ? in->now : time(NULL);
    memset(slot, 0, sizeof(*slot));
    memcpy(slot->key, key, (size_t)HUSH_WAKE_KEY_LEN);
    memcpy(slot->trigger, trigger, (size_t)HUSH_WAKE_TRIGGER_LEN);
    memcpy(slot->device, g_device, (size_t)HUSH_WAKE_DEVICE_LEN);
    slot->state = (uint8_t)HUSH_WAKE_ST_CLAIMED;
    slot->lease_unix = (int64_t)now + (int64_t)HUSH_WAKE_LEASE_S;
    hush_wake_copy(slot->robot, sizeof(slot->robot), in->robot_hex);
    hush_wake_copy(slot->root, sizeof(slot->root), in->root_hex);
    hush_wake_copy(slot->channel, sizeof(slot->channel),
                   in->channel != NULL && in->channel[0] != '\0'
                       ? in->channel : HUSH_WAKE_CHAN_FALLBACK);
}

static void hush_wake_mark_slot_intro(hush_wake_slot_t *slot,
                                      const hush_wake_in_t *in,
                                      const unsigned char *key)
{
    assert(slot != NULL);
    assert(in != NULL);
    assert(key != NULL);
    memset(slot, 0, sizeof(*slot));
    memcpy(slot->key, key, (size_t)HUSH_WAKE_KEY_LEN);
    memcpy(slot->device, g_device, (size_t)HUSH_WAKE_DEVICE_LEN);
    slot->state = (uint8_t)HUSH_WAKE_ST_INTRO;
    slot->lease_unix = 0;
    hush_wake_copy(slot->robot, sizeof(slot->robot), in->robot_hex);
    hush_wake_copy(slot->root, sizeof(slot->root), in->root_hex);
    hush_wake_copy(slot->channel, sizeof(slot->channel),
                   in->channel != NULL && in->channel[0] != '\0'
                       ? in->channel : HUSH_WAKE_CHAN_FALLBACK);
}

static void hush_wake_mark_slot_done(hush_wake_slot_t *slot,
                                     const unsigned char *trigger)
{
    assert(slot != NULL);
    assert(trigger != NULL);
    slot->state = (uint8_t)HUSH_WAKE_ST_DONE;
    slot->lease_unix = 0;
    memcpy(slot->trigger, trigger, (size_t)HUSH_WAKE_TRIGGER_LEN);
}

static hush_status_t hush_wake_claim_existing(hush_wake_slot_t *slot,
                                              const hush_wake_in_t *in,
                                              const unsigned char *key,
                                              const unsigned char *trigger)
{
    time_t now;

    assert(slot != NULL);
    assert(in != NULL);
    assert(key != NULL);
    assert(trigger != NULL);
    now = in->now != 0 ? in->now : time(NULL);
    if (slot->state == (uint8_t)HUSH_WAKE_ST_DONE) {
        if (hush_wake_is_same_trigger(slot, trigger))
            return HUSH_ERR_DENIED;
        hush_wake_take_slot(slot, in, key, trigger);
        return HUSH_OK;
    }
    if (slot->state == (uint8_t)HUSH_WAKE_ST_CLAIMED) {
        if (hush_wake_is_lease_live(slot, now) &&
            !hush_wake_is_same_device(slot))
            return HUSH_ERR_DENIED;
        if (hush_wake_is_lease_live(slot, now)) {
            hush_wake_take_slot(slot, in, key, trigger);
            return HUSH_OK;
        }
        HUSH_TRY(hush_wake_drop_lease(in->store, slot, now));
        if (hush_wake_is_same_trigger(slot, trigger)) {
            hush_wake_mark_slot_done(slot, trigger);
            return HUSH_ERR_DENIED;
        }
        hush_wake_take_slot(slot, in, key, trigger);
        return HUSH_OK;
    }
    hush_wake_take_slot(slot, in, key, trigger);
    return HUSH_OK;
}

static hush_status_t hush_wake_drop_lease(hush_store_t *store,
                                          const hush_wake_slot_t *slot,
                                          time_t now)
{
    hush_presence_in_t in;

    assert(slot != NULL);
    if (store == NULL)
        return HUSH_OK;
    memset(&in, 0, sizeof(in));
    in.pubkey = slot->robot;
    in.root = slot->root;
    in.channel = slot->channel[0] != '\0' ? slot->channel
                                          : HUSH_WAKE_CHAN_FALLBACK;
    in.now = now;
    return hush_presence_lease_drop(store, &in);
}

static void hush_wake_agents_join(char *out, size_t outsz, const char *name)
{
    char agents[HUSH_HOME_PATH_MAX];
    int n;

    assert(out != NULL);
    assert(outsz > 0);
    assert(name != NULL);
    out[0] = '\0';
    hush_home_agents_dir(agents, sizeof(agents));
    if (agents[0] == '\0')
        return;
    n = snprintf(out, outsz, "%s/%s", agents, name);
    if (n <= 0 || (size_t)n >= outsz)
        out[0] = '\0';
}

static hush_status_t hush_wake_fsync_fd(int fd)
{
    if (fd < 0)
        return HUSH_ERR_IO;
    if (fsync(fd) != 0)
        return HUSH_ERR_IO;
    return HUSH_OK;
}

static hush_status_t hush_wake_fsync_dir(const char *file)
{
    char dir[HUSH_HOME_PATH_MAX];
    char *slash;
    int fd;

    assert(file != NULL);
    hush_wake_copy(dir, sizeof(dir), file);
    slash = strrchr(dir, '/');
    if (slash == NULL)
        hush_wake_copy(dir, sizeof(dir), ".");
    else if (slash == dir)
        slash[1] = '\0';
    else
        *slash = '\0';
    fd = open(dir, O_RDONLY | O_DIRECTORY);
    if (fd < 0)
        return HUSH_ERR_IO;
    if (hush_wake_fsync_fd(fd) != HUSH_OK) {
        close(fd);
        return HUSH_ERR_IO;
    }
    if (close(fd) != 0)
        return HUSH_ERR_IO;
    return HUSH_OK;
}

static hush_wake_deliv_t *hush_wake_find_deliv(hush_wake_deliv_t *deliv,
                                              const unsigned char *key)
{
    size_t i;

    assert(deliv != NULL);
    assert(key != NULL);
    for (i = 0; i < (size_t)HUSH_WAKE_SLOT_MAX; i++) {
        if (!deliv[i].used)
            continue;
        if (hush_wake_key_eq(deliv[i].key, key))
            return &deliv[i];
    }
    return NULL;
}

static hush_wake_deliv_t *hush_wake_find_empty_deliv(hush_wake_deliv_t *deliv)
{
    size_t i;

    assert(deliv != NULL);
    for (i = 0; i < (size_t)HUSH_WAKE_SLOT_MAX; i++) {
        if (!deliv[i].used)
            return &deliv[i];
    }
    return NULL;
}

static hush_status_t hush_wake_note_delivery(hush_wake_deliv_t *deliv,
                                             const unsigned char *key)
{
    hush_wake_deliv_t *slot;

    assert(deliv != NULL);
    assert(key != NULL);
    slot = hush_wake_find_deliv(deliv, key);
    if (slot != NULL)
        return HUSH_OK;
    slot = hush_wake_find_empty_deliv(deliv);
    if (slot == NULL)
        return HUSH_ERR_FULL;
    memset(slot, 0, sizeof(*slot));
    memcpy(slot->key, key, (size_t)HUSH_WAKE_KEY_LEN);
    slot->used = 1;
    return HUSH_OK;
}

static int hush_wake_delivery_blocks(const hush_wake_slot_t *slot,
                                     const hush_wake_deliv_t *hit,
                                     time_t now)
{
    if (hit == NULL)
        return 0;
    if (slot != NULL && slot->state == (uint8_t)HUSH_WAKE_ST_CLAIMED &&
        !hush_wake_is_lease_live(slot, now))
        return 0;
    if (slot != NULL && slot->state == (uint8_t)HUSH_WAKE_ST_CLAIMED &&
        hush_wake_is_lease_live(slot, now) && hush_wake_is_same_device(slot))
        return 0;
    return 1;
}

static hush_status_t hush_wake_write_file(const char *path,
                                          const hush_wake_slot_t *slots,
                                          const hush_wake_deliv_t *deliv)
{
    char tmp[HUSH_HOME_PATH_MAX];
    hush_wake_hdr_t hdr;
    int fd;
    int n;
    ssize_t wr;

    assert(path != NULL);
    assert(slots != NULL);
    assert(deliv != NULL);
    n = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (n <= 0 || (size_t)n >= sizeof(tmp))
        return HUSH_ERR_IO;
    fd = open(tmp, O_CREAT | O_TRUNC | O_WRONLY, HUSH_WAKE_FILE_MODE);
    if (fd < 0)
        return HUSH_ERR_IO;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = HUSH_WAKE_MAGIC;
    hdr.version = (uint32_t)HUSH_WAKE_VERSION;
    hdr.nslot = (uint32_t)HUSH_WAKE_SLOT_MAX;
    wr = write(fd, &hdr, sizeof(hdr));
    if (wr != (ssize_t)sizeof(hdr)) {
        close(fd);
        unlink(tmp);
        return HUSH_ERR_IO;
    }
    wr = write(fd, slots, sizeof(g_slots));
    if (wr != (ssize_t)sizeof(g_slots)) {
        close(fd);
        unlink(tmp);
        return HUSH_ERR_IO;
    }
    wr = write(fd, deliv, sizeof(g_deliv));
    if (wr != (ssize_t)sizeof(g_deliv)) {
        close(fd);
        unlink(tmp);
        return HUSH_ERR_IO;
    }
    if (hush_wake_fsync_fd(fd) != HUSH_OK) {
        close(fd);
        unlink(tmp);
        return HUSH_ERR_IO;
    }
    if (close(fd) != 0) {
        unlink(tmp);
        return HUSH_ERR_IO;
    }
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return HUSH_ERR_IO;
    }
    return hush_wake_fsync_dir(path);
}

static hush_status_t hush_wake_commit(const hush_wake_slot_t *slots,
                                      const hush_wake_deliv_t *deliv)
{
    char path[HUSH_HOME_PATH_MAX];

    assert(slots != NULL);
    assert(deliv != NULL);
    memcpy(g_slots, slots, sizeof(g_slots));
    memcpy(g_deliv, deliv, sizeof(g_deliv));
    if (!g_persist)
        return HUSH_OK;
    hush_wake_agents_join(path, sizeof(path), HUSH_WAKE_FILE);
    if (path[0] == '\0')
        return HUSH_ERR_IO;
    return hush_wake_write_file(path, slots, deliv);
}

static hush_status_t hush_wake_load_file(void)
{
    char path[HUSH_HOME_PATH_MAX];
    hush_wake_hdr_t hdr;
    int fd;
    ssize_t n;

    hush_wake_agents_join(path, sizeof(path), HUSH_WAKE_FILE);
    if (path[0] == '\0')
        return HUSH_ERR_IO;
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT)
            return HUSH_OK;
        return HUSH_ERR_IO;
    }
    n = read(fd, &hdr, sizeof(hdr));
    if (n != (ssize_t)sizeof(hdr) || hdr.magic != HUSH_WAKE_MAGIC ||
        hdr.version != (uint32_t)HUSH_WAKE_VERSION ||
        hdr.nslot != (uint32_t)HUSH_WAKE_SLOT_MAX) {
        close(fd);
        return HUSH_ERR_PARSE;
    }
    n = read(fd, g_slots, sizeof(g_slots));
    if (n != (ssize_t)sizeof(g_slots)) {
        close(fd);
        return HUSH_ERR_IO;
    }
    n = read(fd, g_deliv, sizeof(g_deliv));
    close(fd);
    if (n != (ssize_t)sizeof(g_deliv))
        return HUSH_ERR_IO;
    return HUSH_OK;
}

static hush_status_t hush_wake_mint_device(void)
{
    char path[HUSH_HOME_PATH_MAX];
    char hex[HUSH_WAKE_DEVICE_HEX_LEN + 2];
    size_t i;
    int fd;
    ssize_t wr;

    if (RAND_bytes(g_device, HUSH_WAKE_DEVICE_LEN) != 1)
        return HUSH_ERR_CRYPTO;
    hush_wake_agents_join(path, sizeof(path), HUSH_WAKE_DEVICE_FILE);
    if (path[0] == '\0')
        return HUSH_ERR_IO;
    for (i = 0; i < (size_t)HUSH_WAKE_DEVICE_LEN; i++) {
        hex[i * 2] = hush_wake_hex[g_device[i] >> 4];
        hex[i * 2 + 1] = hush_wake_hex[g_device[i] & 0x0Fu];
    }
    hex[HUSH_WAKE_DEVICE_HEX_LEN] = '\n';
    hex[HUSH_WAKE_DEVICE_HEX_LEN + 1] = '\0';
    fd = open(path, O_CREAT | O_EXCL | O_WRONLY, HUSH_WAKE_FILE_MODE);
    if (fd < 0)
        return HUSH_ERR_IO;
    wr = write(fd, hex, (size_t)HUSH_WAKE_DEVICE_HEX_LEN + 1);
    if (wr != (ssize_t)HUSH_WAKE_DEVICE_HEX_LEN + 1) {
        close(fd);
        unlink(path);
        return HUSH_ERR_IO;
    }
    if (hush_wake_fsync_fd(fd) != HUSH_OK) {
        close(fd);
        unlink(path);
        return HUSH_ERR_IO;
    }
    if (close(fd) != 0) {
        unlink(path);
        return HUSH_ERR_IO;
    }
    return hush_wake_fsync_dir(path);
}

static hush_status_t hush_wake_load_device(void)
{
    char path[HUSH_HOME_PATH_MAX];
    char hex[HUSH_WAKE_DEVICE_HEX_LEN + 4];
    int fd;
    ssize_t n;

    hush_wake_agents_join(path, sizeof(path), HUSH_WAKE_DEVICE_FILE);
    if (path[0] == '\0')
        return HUSH_ERR_IO;
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT)
            return hush_wake_mint_device();
        return HUSH_ERR_IO;
    }
    n = read(fd, hex, sizeof(hex) - 1);
    close(fd);
    if (n < (ssize_t)HUSH_WAKE_DEVICE_HEX_LEN)
        return HUSH_ERR_PARSE;
    hex[HUSH_WAKE_DEVICE_HEX_LEN] = '\0';
    return hush_wake_parse_hex(g_device, (size_t)HUSH_WAKE_DEVICE_LEN, hex);
}

static hush_status_t hush_wake_need_ready(void)
{
    if (!g_ready)
        return HUSH_ERR_IO;
    return HUSH_OK;
}

static int hush_wake_has_intro(hush_wake_state_t state)
{
    if (state == HUSH_WAKE_ST_INTRO)
        return 1;
    if (state == HUSH_WAKE_ST_CLAIMED)
        return 1;
    if (state == HUSH_WAKE_ST_DONE)
        return 1;
    return 0;
}

static int hush_wake_may_persist(void)
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
