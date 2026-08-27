/* hush_cevent.c: owns the bounded in-hive channel event ring. */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "hush_cevent.h"
#include "hush_json.h"

enum {
    HUSH_CEVENT_ESC_CHANNEL = 512
};

static hush_cevent_t g_events[HUSH_CEVENT_MAX];
static size_t g_head;
static size_t g_n;
static uint32_t g_seq;

static void hush_cevent_copy(char *dst, size_t dstsz, const char *src);
static hush_status_t hush_cevent_put_one(char *out, size_t outsz, size_t *off,
                                         const hush_cevent_t *ev, int first);

void hush_cevent_init(void)
{
    memset(g_events, 0, sizeof(g_events));
    g_head = 0;
    g_n = 0;
    g_seq = 0;
}

hush_status_t hush_cevent_emit(const hush_cevent_t *in)
{
    hush_cevent_t *slot;
    size_t idx;

    if (in == NULL || in->type[0] == '\0')
        return HUSH_ERR_ARG;
    if (g_n < (size_t)HUSH_CEVENT_MAX) {
        idx = (g_head + g_n) % (size_t)HUSH_CEVENT_MAX;
        g_n++;
    } else {
        idx = g_head;
        g_head = (g_head + 1) % (size_t)HUSH_CEVENT_MAX;
    }
    slot = &g_events[idx];
    memset(slot, 0, sizeof(*slot));
    g_seq++;
    slot->seq = g_seq;
    slot->due = in->due != 0 ? in->due : (int64_t)time(NULL);
    hush_cevent_copy(slot->type, sizeof(slot->type), in->type);
    hush_cevent_copy(slot->channel, sizeof(slot->channel), in->channel);
    hush_cevent_copy(slot->root, sizeof(slot->root), in->root);
    hush_cevent_copy(slot->actor, sizeof(slot->actor), in->actor);
    hush_cevent_copy(slot->note, sizeof(slot->note), in->note);
    return HUSH_OK;
}

hush_status_t hush_cevent_format_json(char *out, size_t outsz, size_t *out_len)
{
    size_t off = 0;
    size_t i;
    size_t idx;
    int n;

    if (out == NULL || outsz == 0)
        return HUSH_ERR_ARG;
    n = snprintf(out, outsz, "{\"ok\":true,\"events\":[");
    if (n < 0 || (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    off = (size_t)n;
    for (i = 0; i < g_n; i++) {
        idx = (g_head + i) % (size_t)HUSH_CEVENT_MAX;
        if (hush_cevent_put_one(out, outsz, &off, &g_events[idx], i == 0)
            != HUSH_OK)
            return HUSH_ERR_FULL;
    }
    n = snprintf(out + off, outsz - off, "]}\n");
    if (n < 0 || off + (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    off += (size_t)n;
    if (out_len != NULL)
        *out_len = off;
    return HUSH_OK;
}

static void hush_cevent_copy(char *dst, size_t dstsz, const char *src)
{
    size_t n;

    assert(dst != NULL);
    assert(dstsz > 0);
    dst[0] = '\0';
    if (src == NULL)
        return;
    n = strlen(src);
    if (n + 1 > dstsz)
        n = dstsz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static hush_status_t hush_cevent_put_one(char *out, size_t outsz, size_t *off,
                                         const hush_cevent_t *ev, int first)
{
    char type[HUSH_CEVENT_TYPE_MAX * 2];
    char channel[HUSH_CEVENT_ESC_CHANNEL];
    char root[HUSH_CEVENT_NOTE_MAX * 2];
    char actor[HUSH_CEVENT_NOTE_MAX * 2];
    char note[HUSH_CEVENT_NOTE_MAX * 2];
    int n;

    assert(out != NULL);
    assert(off != NULL);
    assert(ev != NULL);
    hush_json_escape(ev->type, type, sizeof(type));
    hush_json_escape(ev->channel, channel, sizeof(channel));
    hush_json_escape(ev->root, root, sizeof(root));
    hush_json_escape(ev->actor, actor, sizeof(actor));
    hush_json_escape(ev->note, note, sizeof(note));
    n = snprintf(out + *off, outsz - *off,
                 "%s{\"seq\":%u,\"due\":%lld,\"type\":\"%s\","
                 "\"channel\":\"%s\",\"root\":\"%s\",\"actor\":\"%s\","
                 "\"note\":\"%s\"}",
                 first ? "" : ",",
                 ev->seq, (long long)ev->due, type, channel, root, actor, note);
    if (n < 0 || *off + (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    *off += (size_t)n;
    return HUSH_OK;
}
