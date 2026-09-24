/* api_status.c: owns status, event, session, and presence routes. */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hush_agent.h"
#include "hush_build.h"
#include "hush_cevent.h"
#include "hush_http_internal.h"
#include "hush_presence.h"
#include "hush_store.h"
#include "hush_thread.h"
#include "hush_wake.h"

enum {
    HUSH_HTTP_STATUS_MAX = 2048,
    HUSH_HTTP_THINKING_MAX = 1024,
    HUSH_HTTP_CONTENT_ESC_MAX = (HUSH_EVENT_MAX_CONTENT + 1) * HUSH_JSON_U_LEN,
    HUSH_HTTP_MENTIONS_JSON_MAX = HUSH_EVENT_MAX_TAGS *
        ((HUSH_EVENT_MAX_TAG_LEN + 1) * HUSH_JSON_U_LEN + 3),
    HUSH_HTTP_EVENT_JSON_MAX = HUSH_HTTP_CONTENT_ESC_MAX +
        HUSH_HTTP_MENTIONS_JSON_MAX + 4096
};

/* True for required events exposed in conversation history. */
static int hush_http_is_visible_event(const hush_event_t *event);

/* Serializes p-tags from required event into bounded caller storage. */
static hush_status_t hush_http_format_mentions(char *out, size_t outsz,
                                               const hush_event_t *event);

/* Writes one event frame; first controls the leading comma. */
static hush_status_t hush_http_send_event(int fd, const hush_event_t *event,
                                          int first);

/* Copies ?root= from the request line. 0 when absent or not a thread root. */
static int hush_http_take_thread_root(const char *req, char *out,
                                      size_t outsz);

void hush_http_serve_status(int fd, const hush_store_t *store)
{
    char body[HUSH_HTTP_STATUS_MAX];
    char thinking[HUSH_HTTP_THINKING_MAX];
    size_t n;
    int w;
    int whisper;
    int turn_on;
    int vibe_pub;

    n = hush_store_count(store);
    whisper = hush_turn_whisper_available();
    turn_on = (hush_http_turn() != NULL && hush_http_turn()->running);
    vibe_pub = (hush_http_launch() == NULL || !hush_http_launch()->has_vibe || hush_http_launch()->vibe_public);
    hush_agent_status(thinking, sizeof(thinking));
    w = snprintf(body, sizeof(body),
                 "{\"ok\":true,\"version\":\"%s\",\"build\":\"%s\",\"events\":%zu,"
                 "\"clients\":%d,\"port\":%u,\"whisper\":%s,"
                 "\"turn_running\":%s,\"vibe_public\":%s,\"thinking\":%s}\n",
                 HUSH_BUILD_VERSION, HUSH_BUILD_SHA,
                 n, hush_http_client_count(), (unsigned)hush_http_listen_port(),
                 whisper ? "true" : "false",
                 turn_on ? "true" : "false",
                 vibe_pub ? "true" : "false",
                 thinking[0] ? thinking : "[]");
    if (w < 0)
        w = 0;
    hush_http_reply(fd, "200 OK", "application/json", body, (size_t)w);
}

void hush_http_serve_events(int fd, const hush_store_t *store)
{
    assert(store != NULL);
    const char *header = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n\r\n{\"events\":[";
    if (hush_http_write_all(fd, header, strlen(header)) != HUSH_OK)
        return;
    size_t count = hush_store_count(store);
    int first = 1;
    for (size_t i = 0; i < count && i < (size_t)HUSH_STORE_CAPACITY; ++i) {
        hush_event_t event = {0};
        if (hush_store_get(store, i, &event) != HUSH_OK)
            break;
        if (!hush_http_is_visible_event(&event))
            continue;
        if (hush_http_send_event(fd, &event, first) != HUSH_OK)
            return;
        first = 0;
    }
    if (hush_http_write_all(fd, "]}\n", strlen("]}\n")) != HUSH_OK)
        return;
}

void hush_http_serve_thread(int fd, const char *req)
{
    static char body[HUSH_THREAD_JSON_MAX];
    char root[HUSH_EVENT_ID_HEX_LEN + 1] = {0};
    size_t n = 0;

    if (req == NULL ||
        !hush_http_take_thread_root(req, root, sizeof(root))) {
        const char *error = "{\"ok\":false,\"error\":\"root is required\"}\n";

        hush_http_reply(fd, "400 Bad Request", "application/json", error,
                        strlen(error));
        return;
    }
    if (hush_thread_format_json(root, body, sizeof(body), &n) != HUSH_OK) {
        const char *failed = "{\"ok\":false,\"error\":\"thread too large\"}\n";

        hush_http_reply(fd, "507 Insufficient Storage", "application/json",
                        failed, strlen(failed));
        return;
    }
    hush_http_reply(fd, "200 OK", "application/json", body, n);
}

static int hush_http_is_visible_event(const hush_event_t *event)
{
    assert(event != NULL);
    return event->kind != (uint32_t)HUSH_PRESENCE_KIND_LINE &&
        event->kind != (uint32_t)HUSH_PRESENCE_KIND_TRAIL &&
        event->kind != (uint32_t)HUSH_WAKE_KIND_CLAIM;
}

static hush_status_t hush_http_format_mentions(char *out, size_t outsz,
                                               const hush_event_t *event)
{
    assert(out != NULL && outsz > 0);
    assert(event != NULL);
    size_t used = 0;
    out[0] = '\0';
    for (size_t i = 0; i < event->tag_count && i < (size_t)HUSH_EVENT_MAX_TAGS; ++i) {
        if (strcmp(event->tags[i][0], "p") != 0 || event->tags[i][1][0] == '\0')
            continue;
        char escaped[(HUSH_EVENT_MAX_TAG_LEN + 1) * HUSH_JSON_U_LEN] = {0};
        (void)hush_json_escape(event->tags[i][1], escaped, sizeof(escaped));
        int written = snprintf(out + used, outsz - used, "%s\"%s\"", used ? "," : "", escaped);
        if (written < 0 || (size_t)written >= outsz - used)
            return HUSH_ERR_FULL;
        used += (size_t)written;
    }
    return HUSH_OK;
}

static hush_status_t hush_http_send_event(int fd, const hush_event_t *event, int first)
{
    assert(event != NULL);
    char content[HUSH_HTTP_CONTENT_ESC_MAX] = {0};
    char mentions[HUSH_HTTP_MENTIONS_JSON_MAX] = {0};
    char root[HUSH_EVENT_ID_HEX_LEN + 1] = {0};
    char channel[(HUSH_EVENT_MAX_TAG_LEN + 1) * HUSH_JSON_U_LEN] = {0};
    char body[HUSH_HTTP_EVENT_JSON_MAX] = {0};
    (void)hush_json_escape(event->content, content, sizeof(content));
    (void)hush_json_escape(hush_http_event_channel(event),
                          channel, sizeof(channel));
    hush_http_event_reply_to(root, sizeof(root), event);
    HUSH_TRY(hush_http_format_mentions(mentions, sizeof(mentions), event));
    int written = snprintf(body, sizeof(body),
        "%s{\"id\":\"%s\",\"pubkey\":\"%s\",\"kind\":%u,"
        "\"created_at\":%lld,\"content\":\"%s\",\"channel\":\"%s\","
        "\"reply_to\":\"%s\",\"mentions\":[%s]}", first ? "" : ",",
        event->id, event->pubkey, event->kind, (long long)event->created_at,
        content, channel, root, mentions);
    if (written < 0 || (size_t)written >= sizeof(body))
        return HUSH_ERR_FULL;
    return hush_http_write_all(fd, body, (size_t)written);
}

static int hush_http_take_thread_root(const char *req, char *out,
                                      size_t outsz)
{
    const char *hit;
    size_t i;

    assert(req != NULL);
    assert(out != NULL);
    out[0] = '\0';
    if (outsz < (size_t)HUSH_EVENT_ID_HEX_LEN + 1)
        return 0;
    hit = strstr(req, "root=");
    if (hit == NULL)
        return 0;
    hit += sizeof("root=") - 1;
    for (i = 0; i < (size_t)HUSH_EVENT_ID_HEX_LEN; ++i) {
        char ch = hit[i];

        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')))
            return 0;
    }
    if (hit[HUSH_EVENT_ID_HEX_LEN] != ' ' &&
        hit[HUSH_EVENT_ID_HEX_LEN] != '&' &&
        hit[HUSH_EVENT_ID_HEX_LEN] != '\0')
        return 0;
    memcpy(out, hit, HUSH_EVENT_ID_HEX_LEN);
    out[HUSH_EVENT_ID_HEX_LEN] = '\0';
    return 1;
}

/* The relay's existing HTTP callback signature includes both transport and event outputs. */

void hush_http_serve_session(int fd)
{
    static const char k_empty[] =
        "{\"ok\":true,\"logged_in\":false,\"ready\":false}\n";
    static char body[HUSH_LAUNCH_JSON_MAX];
    size_t n = 0;

    if (hush_http_launch() == NULL) {
        hush_http_reply(fd, "200 OK", "application/json",
                        k_empty, sizeof(k_empty) - 1);
        return;
    }
    if (hush_launch_format_session(hush_http_launch(), hush_http_listen_port(), body,
                                   sizeof(body), &n) != HUSH_OK)
        n = 0;
    hush_http_reply(fd, "200 OK", "application/json", body, n);
}

void hush_http_serve_chan_events(int fd, const char *req)
{
    static const char k_empty[] = "{\"ok\":true,\"events\":[]}\n";
    char body[HUSH_CEVENT_JSON_MAX];
    size_t n = 0;
    uint32_t since = 0;
    const char *q;

    /* Optional ?since=N cursor: return only signals newer than N, and treat N
     * as an acknowledgement that the consumer has processed up to N (so a
     * later ring wrap that evicts only those events is not counted as loss). */
    if (req != NULL) {
        q = strstr(req, "since=");
        if (q != NULL) {
            since = (uint32_t)atoi(q + 6);
            hush_cevent_ack(since);
        }
    }
    if (hush_cevent_format_json_since(body, sizeof(body), &n, since) != HUSH_OK) {
        hush_http_reply(fd, "200 OK", "application/json",
                        k_empty, sizeof(k_empty) - 1);
        return;
    }
    hush_http_reply(fd, "200 OK", "application/json", body, n);
}

void hush_http_serve_presence_get(int fd)
{
    static const char k_empty[] = "{\"ok\":true,\"lines\":[]}\n";
    char body[HUSH_PRESENCE_JSON_MAX];
    size_t n = 0;

    if (hush_presence_format_json(body, sizeof(body), &n) != HUSH_OK) {
        hush_http_reply(fd, "200 OK", "application/json",
                        k_empty, sizeof(k_empty) - 1);
        return;
    }
    hush_http_reply(fd, "200 OK", "application/json", body, n);
}

hush_status_t hush_http_serve_presence_post(int fd, const char *body,
                                                   hush_store_t *store)
{
    char slug[HUSH_PRESENCE_SLUG_MAX];
    char root[HUSH_EVENT_ID_HEX_LEN + 1];
    char channel[64];
    hush_presence_in_t in;
    hush_status_t st;

    if (body == NULL || store == NULL)
        return HUSH_ERR_ARG;
    if (hush_http_launch() == NULL || !hush_http_launch()->logged_in) {
        hush_http_reply(fd, "401 Unauthorized", "text/plain", "login\n", 6);
        return HUSH_ERR_DENIED;
    }
    if (!hush_http_json_field(body, "slug", slug, sizeof(slug))) {
        hush_http_reply(fd, "400 Bad Request", "text/plain", "need slug\n", 10);
        return HUSH_ERR_PARSE;
    }
    if (!hush_http_json_field(body, "root", root, sizeof(root)) ||
        strlen(root) != (size_t)HUSH_EVENT_ID_HEX_LEN)
        memcpy(root, hush_http_launch()->human.pubkey_hex,
               (size_t)HUSH_EVENT_PUBKEY_HEX_LEN + 1);
    if (!hush_http_json_field(body, "channel", channel, sizeof(channel)))
        memcpy(channel, "general", 8);
    memset(&in, 0, sizeof(in));
    in.pubkey = hush_http_launch()->human.pubkey_hex;
    in.role = NULL;
    in.slug = slug;
    in.channel = channel;
    in.root = root;
    in.now = time(NULL);
    st = hush_presence_publish(store, &in);
    if (st == HUSH_ERR_PARSE) {
        hush_http_reply(fd, "400 Bad Request", "text/plain", "bad slug\n", 9);
        return st;
    }
    if (st != HUSH_OK) {
        hush_http_reply(fd, "400 Bad Request", "text/plain", "presence\n", 9);
        return st;
    }
    hush_http_reply(fd, "200 OK", "application/json", "{\"ok\":true}\n", 12);
    return HUSH_OK;
}

