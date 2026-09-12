/* hush_nip42.c: owns NIP-42 AUTH-event validation for wire connections. */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "hush_nip42.h"

enum {
    HUSH_NIP42_RELAY_HOST_LEN = HUSH_NIP42_RELAY_HOST_MAX + 1
};

/* Writes the denial text and returns HUSH_ERR_DENIED. */
static hush_status_t hush_nip42_deny(char *reason, size_t reason_len,
                                     const char *text);

/* Returns the first tag index whose key element equals key, else -1. */
static int hush_nip42_find_tag(const hush_event_t *ev, const char *key);

/* True when |now - created_at| is within the AUTH freshness window. */
static int hush_nip42_created_fresh(int64_t created_at, time_t now);

hush_status_t hush_nip42_validate(const hush_nip42_request_t *request,
                                  char *reason, size_t reason_len)
{
    const hush_event_t *ev;
    int challenge_tag;
    int relay_tag;

    if (request == NULL || request->event == NULL ||
        request->challenge == NULL || request->bind_addr == NULL)
        return HUSH_ERR_ARG;
    ev = request->event;
    if (reason != NULL && reason_len > 0)
        reason[0] = '\0';
    if (ev->kind != (uint32_t)HUSH_NIP42_KIND_AUTH)
        return hush_nip42_deny(reason, reason_len, "invalid: wrong kind");
    if (!hush_nip42_created_fresh(ev->created_at, request->now))
        return hush_nip42_deny(reason, reason_len, "invalid: stale created_at");
    challenge_tag = hush_nip42_find_tag(ev, "challenge");
    if (challenge_tag < 0 || ev->tags[challenge_tag][1][0] == '\0' ||
        strcmp(ev->tags[challenge_tag][1], request->challenge) != 0)
        return hush_nip42_deny(reason, reason_len, "invalid: challenge mismatch");
    relay_tag = hush_nip42_find_tag(ev, "relay");
    /* Deviation: NIP-42 says the relay URL SHOULD be included; Hush has no
     * canonical public URL, so the tag is optional and, when present, only
     * its host is matched against the listener. */
    if (relay_tag >= 0 && ev->tags[relay_tag][1][0] != '\0' &&
        !hush_nip42_relay_host_ok(ev->tags[relay_tag][1], request->bind_addr))
        return hush_nip42_deny(reason, reason_len, "invalid: relay mismatch");
    return hush_event_verify(ev, reason, reason_len);
}

int hush_nip42_relay_host_ok(const char *relay_url, const char *bind_addr)
{
    char host[HUSH_NIP42_RELAY_HOST_LEN];
    const char *start;
    const char *end;
    size_t len;

    if (relay_url == NULL || bind_addr == NULL)
        return 0;
    start = strstr(relay_url, "://");
    start = start != NULL ? start + 3 : relay_url;
    end = start + strlen(start);
    if (start[0] == '[') {
        const char *close = strchr(start, ']');

        if (close == NULL)
            return 0;
        end = close + 1;
    } else {
        const char *colon = strchr(start, ':');
        const char *slash = strchr(start, '/');

        if (colon != NULL && colon > start && (slash == NULL || colon < slash))
            end = colon;
        else if (slash != NULL)
            end = slash;
    }
    len = (size_t)(end - start);
    if (len == 0 || len > (size_t)HUSH_NIP42_RELAY_HOST_MAX)
        return 0;
    memcpy(host, start, len);
    host[len] = '\0';
    if (strcasecmp(host, "localhost") == 0 ||
        strcasecmp(host, "127.0.0.1") == 0 || strcasecmp(host, "::1") == 0 ||
        strcasecmp(host, "[::1]") == 0)
        return 1;
    if (bind_addr[0] == '\0')
        return 0;
    return strcasecmp(host, bind_addr) == 0;
}

static hush_status_t hush_nip42_deny(char *reason, size_t reason_len,
                                     const char *text)
{
    assert(text != NULL);
    if (reason != NULL && reason_len > 0)
        snprintf(reason, reason_len, "%s", text);
    return HUSH_ERR_DENIED;
}

static int hush_nip42_find_tag(const hush_event_t *ev, const char *key)
{
    size_t tag;

    assert(ev != NULL);
    assert(key != NULL);
    for (tag = 0; tag < ev->tag_count && tag < (size_t)HUSH_EVENT_MAX_TAGS;
         ++tag) {
        if (strcmp(ev->tags[tag][0], key) == 0)
            return (int)tag;
    }
    return -1;
}

static int hush_nip42_created_fresh(int64_t created_at, time_t now)
{
    int64_t delta = created_at - (int64_t)now;

    if (delta < 0)
        delta = -delta;
    return delta <= (int64_t)HUSH_NIP42_CREATED_WINDOW_S;
}
