/* hush_presence.c: owns NIP-38 lines, trail events, Idle expiry, Stuck clocks. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "hush_cevent.h"
#include "hush_json.h"
#include "hush_presence.h"
#include "hush_roster.h"

enum {
    HUSH_PRESENCE_ID_WIDTH = 16
};

#define HUSH_PRESENCE_DEBUG_PREFIX "Debugging "

typedef struct {
    int live;
    char pubkey[HUSH_EVENT_PUBKEY_HEX_LEN + 1];
    char d[HUSH_PRESENCE_D_MAX];
    char slug[HUSH_PRESENCE_SLUG_MAX];
    char channel[HUSH_EVENT_MAX_TAG_LEN + 1];
    char root[HUSH_EVENT_ID_HEX_LEN + 1];
    time_t started;
    time_t beat;
    time_t expire;
    time_t last_nudge;
} hush_presence_line_t;

static hush_presence_line_t g_lines[HUSH_PRESENCE_LINES_MAX];
static unsigned g_id_seq;

static void hush_presence_copy(char *dst, size_t dstsz, const char *src);
static int hush_presence_is_exact_slug(const char *slug);
static int hush_presence_is_debug_slug(const char *slug);
static int hush_presence_slug_expires(const char *slug);
static hush_presence_line_t *hush_presence_find_d(const char *d);
static hush_presence_line_t *hush_presence_take_slot(void);
static void hush_presence_make_id(char *out65);
static void hush_presence_fill_line_event(hush_event_t *ev,
                                          const hush_presence_in_t *in,
                                          const char *d, int trail);
static void hush_presence_emit(const char *type, const hush_presence_in_t *in,
                               const char *d);
static hush_status_t hush_presence_put_one(char *out, size_t outsz, size_t *off,
                                           const hush_presence_line_t *line,
                                           int first);

void hush_presence_init(void)
{
    memset(g_lines, 0, sizeof(g_lines));
    g_id_seq = 0;
}

int hush_presence_slug_ok(const char *slug)
{
    if (slug == NULL || slug[0] == '\0')
        return 0;
    if (strlen(slug) >= (size_t)HUSH_PRESENCE_SLUG_MAX)
        return 0;
    if (hush_presence_is_exact_slug(slug))
        return 1;
    return hush_presence_is_debug_slug(slug);
}

int hush_presence_role_ok(const char *role)
{
    if (role == NULL || role[0] == '\0')
        return 1;
    return strcmp(role, HUSH_ROSTER_ROLE_CHAPERON) != 0;
}

int hush_presence_req_ok(uint32_t kind, int vibe_public)
{
    if (kind != (uint32_t)HUSH_PRESENCE_KIND_LINE &&
        kind != (uint32_t)HUSH_PRESENCE_KIND_TRAIL)
        return 1;
    return vibe_public ? 1 : 0;
}

hush_status_t hush_presence_make_d(char *out, size_t outsz, const char *token)
{
    int n;

    if (out == NULL || outsz < 8 || token == NULL || token[0] == '\0')
        return HUSH_ERR_ARG;
    n = snprintf(out, outsz, "%s%s", HUSH_PRESENCE_D_PREFIX, token);
    if (n < 0 || (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    return HUSH_OK;
}

hush_status_t hush_presence_publish(hush_store_t *store,
                                    const hush_presence_in_t *in)
{
    hush_event_t line_ev;
    hush_event_t trail_ev;
    hush_presence_line_t *slot;
    char d[HUSH_PRESENCE_D_MAX];
    time_t now;

    if (store == NULL || in == NULL)
        return HUSH_ERR_ARG;
    if (in->pubkey == NULL || in->pubkey[0] == '\0')
        return HUSH_ERR_ARG;
    if (in->token == NULL || in->token[0] == '\0')
        return HUSH_ERR_ARG;
    if (!hush_presence_role_ok(in->role))
        return HUSH_ERR_DENIED;
    if (!hush_presence_slug_ok(in->slug))
        return HUSH_ERR_PARSE;
    if (hush_presence_make_d(d, sizeof(d), in->token) != HUSH_OK)
        return HUSH_ERR_FULL;
    now = in->now != 0 ? in->now : time(NULL);
    slot = hush_presence_find_d(d);
    if (slot == NULL)
        slot = hush_presence_take_slot();
    if (slot == NULL)
        return HUSH_ERR_FULL;
    memset(slot, 0, sizeof(*slot));
    slot->live = 1;
    hush_presence_copy(slot->pubkey, sizeof(slot->pubkey), in->pubkey);
    hush_presence_copy(slot->d, sizeof(slot->d), d);
    hush_presence_copy(slot->slug, sizeof(slot->slug), in->slug);
    hush_presence_copy(slot->channel, sizeof(slot->channel),
                       in->channel != NULL ? in->channel : "general");
    hush_presence_copy(slot->root, sizeof(slot->root),
                       in->root != NULL ? in->root : "");
    slot->started = now;
    slot->beat = now;
    slot->expire = 0;
    if (hush_presence_slug_expires(in->slug))
        slot->expire = now + (time_t)HUSH_PRESENCE_IDLE_S;
    hush_presence_fill_line_event(&line_ev, in, d, 0);
    if (hush_store_insert(store, &line_ev) != HUSH_OK)
        return HUSH_ERR_FULL;
    hush_presence_fill_line_event(&trail_ev, in, d, 1);
    if (hush_store_insert(store, &trail_ev) != HUSH_OK)
        return HUSH_ERR_FULL;
    hush_presence_emit(HUSH_CEVENT_PRESENCE, in, d);
    if (strcmp(in->slug, HUSH_PRESENCE_SLUG_STUCK) == 0) {
        slot->last_nudge = now;
        hush_presence_emit(HUSH_CEVENT_STUCK, in, d);
    }
    return HUSH_OK;
}

hush_status_t hush_presence_clear(hush_store_t *store, const char *pubkey,
                                  const char *token, const char *channel,
                                  time_t now)
{
    hush_presence_in_t in;
    hush_event_t ev;
    hush_presence_line_t *slot;
    char d[HUSH_PRESENCE_D_MAX];

    if (store == NULL || pubkey == NULL || token == NULL)
        return HUSH_ERR_ARG;
    if (hush_presence_make_d(d, sizeof(d), token) != HUSH_OK)
        return HUSH_ERR_FULL;
    slot = hush_presence_find_d(d);
    if (slot != NULL)
        slot->live = 0;
    memset(&in, 0, sizeof(in));
    in.pubkey = pubkey;
    in.token = token;
    in.slug = "";
    in.channel = channel != NULL ? channel : "general";
    in.now = now != 0 ? now : time(NULL);
    hush_presence_fill_line_event(&ev, &in, d, 0);
    ev.content[0] = '\0';
    return hush_store_insert(store, &ev);
}

hush_status_t hush_presence_beat(const char *token, time_t now)
{
    hush_presence_line_t *slot;
    char d[HUSH_PRESENCE_D_MAX];

    if (token == NULL || token[0] == '\0')
        return HUSH_ERR_ARG;
    if (hush_presence_make_d(d, sizeof(d), token) != HUSH_OK)
        return HUSH_ERR_FULL;
    slot = hush_presence_find_d(d);
    if (slot == NULL || !slot->live)
        return HUSH_ERR_NOT_FOUND;
    slot->beat = now != 0 ? now : time(NULL);
    if (hush_presence_slug_expires(slot->slug))
        slot->expire = slot->beat + (time_t)HUSH_PRESENCE_IDLE_S;
    return HUSH_OK;
}

int hush_presence_stuck_due(const char *token, time_t now)
{
    hush_presence_line_t *slot;
    char d[HUSH_PRESENCE_D_MAX];
    time_t t;

    if (token == NULL || hush_presence_make_d(d, sizeof(d), token) != HUSH_OK)
        return 0;
    slot = hush_presence_find_d(d);
    if (slot == NULL || !slot->live)
        return 0;
    if (strcmp(slot->slug, HUSH_PRESENCE_SLUG_STUCK) != 0)
        return 0;
    t = now != 0 ? now : time(NULL);
    if (slot->last_nudge == 0)
        return 1;
    return t >= slot->last_nudge + (time_t)HUSH_PRESENCE_HEARTBEAT_S;
}

int hush_presence_stall_s(const char *token, time_t now)
{
    hush_presence_line_t *slot;
    char d[HUSH_PRESENCE_D_MAX];
    time_t t;

    if (token == NULL || hush_presence_make_d(d, sizeof(d), token) != HUSH_OK)
        return -1;
    slot = hush_presence_find_d(d);
    if (slot == NULL || !slot->live)
        return -1;
    t = now != 0 ? now : time(NULL);
    if (t < slot->beat)
        return 0;
    return (int)(t - slot->beat);
}

void hush_presence_expire(hush_store_t *store, time_t now)
{
    size_t i;
    time_t t;

    if (store == NULL)
        return;
    t = now != 0 ? now : time(NULL);
    for (i = 0; i < (size_t)HUSH_PRESENCE_LINES_MAX; i++) {
        if (!g_lines[i].live)
            continue;
        if (g_lines[i].expire == 0)
            continue;
        if (t < g_lines[i].expire)
            continue;
        (void)hush_presence_clear(store, g_lines[i].pubkey,
                                  g_lines[i].d + strlen(HUSH_PRESENCE_D_PREFIX),
                                  g_lines[i].channel, t);
    }
}

hush_status_t hush_presence_format_json(char *out, size_t outsz, size_t *out_len)
{
    size_t off = 0;
    size_t i;
    int n;
    int first = 1;

    if (out == NULL || outsz == 0)
        return HUSH_ERR_ARG;
    n = snprintf(out, outsz, "{\"ok\":true,\"lines\":[");
    if (n < 0 || (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    off = (size_t)n;
    for (i = 0; i < (size_t)HUSH_PRESENCE_LINES_MAX; i++) {
        if (!g_lines[i].live)
            continue;
        if (hush_presence_put_one(out, outsz, &off, &g_lines[i], first)
            != HUSH_OK)
            return HUSH_ERR_FULL;
        first = 0;
    }
    n = snprintf(out + off, outsz - off, "]}\n");
    if (n < 0 || off + (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    off += (size_t)n;
    if (out_len != NULL)
        *out_len = off;
    return HUSH_OK;
}

static void hush_presence_copy(char *dst, size_t dstsz, const char *src)
{
    size_t n;

    assert(dst != NULL);
    assert(dstsz > 0);
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    n = strlen(src);
    if (n >= dstsz)
        n = dstsz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static int hush_presence_is_exact_slug(const char *slug)
{
    static const char *const table[] = {
        HUSH_PRESENCE_SLUG_BUILDING,
        HUSH_PRESENCE_SLUG_RESEARCHING,
        HUSH_PRESENCE_SLUG_PLANNING,
        HUSH_PRESENCE_SLUG_DEBUG_CODE,
        HUSH_PRESENCE_SLUG_CONVERSING,
        HUSH_PRESENCE_SLUG_BREEZE,
        HUSH_PRESENCE_SLUG_WASTING,
        HUSH_PRESENCE_SLUG_STUCK,
        HUSH_PRESENCE_SLUG_WORKING,
        HUSH_PRESENCE_SLUG_WAITING,
        HUSH_PRESENCE_SLUG_IDLE
    };
    size_t i;

    assert(slug != NULL);
    for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (strcmp(slug, table[i]) == 0)
            return 1;
    }
    return 0;
}

static int hush_presence_is_debug_slug(const char *slug)
{
    size_t n;
    size_t i;

    assert(slug != NULL);
    n = strlen(HUSH_PRESENCE_DEBUG_PREFIX);
    if (strncmp(slug, HUSH_PRESENCE_DEBUG_PREFIX, n) != 0)
        return 0;
    if (slug[n] == '\0')
        return 0;
    for (i = n; slug[i] != '\0'; i++) {
        if ((unsigned char)slug[i] < 32u)
            return 0;
    }
    return 1;
}

static int hush_presence_slug_expires(const char *slug)
{
    assert(slug != NULL);
    if (strcmp(slug, HUSH_PRESENCE_SLUG_IDLE) == 0)
        return 1;
    if (strcmp(slug, HUSH_PRESENCE_SLUG_WAITING) == 0)
        return 1;
    return 0;
}

static hush_presence_line_t *hush_presence_find_d(const char *d)
{
    size_t i;

    assert(d != NULL);
    for (i = 0; i < (size_t)HUSH_PRESENCE_LINES_MAX; i++) {
        if (!g_lines[i].live)
            continue;
        if (strcmp(g_lines[i].d, d) == 0)
            return &g_lines[i];
    }
    return NULL;
}

static hush_presence_line_t *hush_presence_take_slot(void)
{
    size_t i;

    for (i = 0; i < (size_t)HUSH_PRESENCE_LINES_MAX; i++) {
        if (!g_lines[i].live)
            return &g_lines[i];
    }
    return NULL;
}

static void hush_presence_make_id(char *out65)
{
    time_t now;

    assert(out65 != NULL);
    now = time(NULL);
    g_id_seq++;
    (void)snprintf(out65, HUSH_EVENT_ID_HEX_LEN + 1,
                   "%0*llx%0*x%0*x%0*x",
                   HUSH_PRESENCE_ID_WIDTH, (unsigned long long)now,
                   HUSH_PRESENCE_ID_WIDTH, g_id_seq,
                   HUSH_PRESENCE_ID_WIDTH, g_id_seq ^ 0x30315u,
                   HUSH_PRESENCE_ID_WIDTH, g_id_seq * 11u);
}

static void hush_presence_fill_line_event(hush_event_t *ev,
                                          const hush_presence_in_t *in,
                                          const char *d, int trail)
{
    char exp[32];
    time_t now;

    assert(ev != NULL);
    assert(in != NULL);
    assert(d != NULL);
    memset(ev, 0, sizeof(*ev));
    hush_presence_make_id(ev->id);
    hush_presence_copy(ev->pubkey, sizeof(ev->pubkey), in->pubkey);
    ev->kind = trail ? (uint32_t)HUSH_PRESENCE_KIND_TRAIL
                     : (uint32_t)HUSH_PRESENCE_KIND_LINE;
    now = in->now != 0 ? in->now : time(NULL);
    ev->created_at = (int64_t)now;
    if (in->slug != NULL)
        hush_presence_copy(ev->content, sizeof(ev->content), in->slug);
    ev->tag_count = 0;
    memcpy(ev->tags[ev->tag_count][0], "d", 2);
    hush_presence_copy(ev->tags[ev->tag_count][1],
                       sizeof(ev->tags[0][1]), d);
    ev->tag_count++;
    memcpy(ev->tags[ev->tag_count][0], "h", 2);
    hush_presence_copy(ev->tags[ev->tag_count][1], sizeof(ev->tags[0][1]),
                       in->channel != NULL && in->channel[0] != '\0'
                           ? in->channel : "general");
    ev->tag_count++;
    if (!trail && in->slug != NULL && hush_presence_slug_expires(in->slug)) {
        (void)snprintf(exp, sizeof(exp), "%lld",
                       (long long)(now + (time_t)HUSH_PRESENCE_IDLE_S));
        memcpy(ev->tags[ev->tag_count][0], "expiration", 11);
        hush_presence_copy(ev->tags[ev->tag_count][1],
                           sizeof(ev->tags[0][1]), exp);
        ev->tag_count++;
    }
}

static void hush_presence_emit(const char *type, const hush_presence_in_t *in,
                               const char *d)
{
    hush_cevent_t ev;

    assert(type != NULL);
    assert(in != NULL);
    memset(&ev, 0, sizeof(ev));
    hush_presence_copy(ev.type, sizeof(ev.type), type);
    hush_presence_copy(ev.channel, sizeof(ev.channel),
                       in->channel != NULL ? in->channel : "general");
    hush_presence_copy(ev.root, sizeof(ev.root),
                       in->root != NULL ? in->root : "");
    hush_presence_copy(ev.actor, sizeof(ev.actor), in->pubkey);
    hush_presence_copy(ev.note, sizeof(ev.note),
                       in->slug != NULL && in->slug[0] != '\0' ? in->slug : d);
    (void)hush_cevent_emit(&ev);
}

static hush_status_t hush_presence_put_one(char *out, size_t outsz, size_t *off,
                                           const hush_presence_line_t *line,
                                           int first)
{
    char slug[HUSH_PRESENCE_SLUG_MAX * 2];
    char d[HUSH_PRESENCE_D_MAX * 2];
    int n;

    assert(out != NULL);
    assert(off != NULL);
    assert(line != NULL);
    hush_json_escape(line->slug, slug, sizeof(slug));
    hush_json_escape(line->d, d, sizeof(d));
    n = snprintf(out + *off, outsz - *off,
                 "%s{\"pubkey\":\"%s\",\"d\":\"%s\",\"slug\":\"%s\","
                 "\"channel\":\"%s\",\"root\":\"%s\",\"started\":%lld,"
                 "\"beat\":%lld}",
                 first ? "" : ",",
                 line->pubkey, d, slug, line->channel, line->root,
                 (long long)line->started, (long long)line->beat);
    if (n < 0 || (size_t)n >= outsz - *off)
        return HUSH_ERR_FULL;
    *off += (size_t)n;
    return HUSH_OK;
}
