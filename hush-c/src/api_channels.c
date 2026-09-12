/* api_channels.c: owns note, channel, group, project, and signal routes. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hush_http_internal.h"
#include "hush_intel.h"
#include "hush_presence.h"
#include "hush_store.h"
#include "hush_thread.h"

enum {
    HUSH_HTTP_KIND_BYTES = 16,
    HUSH_HTTP_CHAN_LIST_MAX = 8,
    HUSH_HTTP_MENTIONS_MAX = 8,
    HUSH_HTTP_KIND_SIGNAL = 25000
};

static hush_status_t hush_http_take_room_prompt(char *out, size_t outsz, const char *body);
static hush_status_t hush_http_channel_manage(int fd, const char *body,
                                              const char *slug);
static hush_status_t hush_http_channel_policy(const char *body,
                                              const char *slug);
static const hush_launch_channel_t *hush_http_find_channel(const char *slug);
static int hush_http_read_int(const char *body, const char *key, int fallback);
static void hush_http_fill_policy_text(hush_launch_policy_t *policy,
                                       const hush_launch_channel_t *ch,
                                       const char *body);
static void hush_http_fill_policy_nums(hush_launch_policy_t *policy,
                                       const hush_launch_channel_t *ch,
                                       const char *body);
static void hush_http_collect_indexed(const char *body, const char *stem,
                                      char (*out)[HUSH_IDENTITY_NPUB_MAX],
                                      size_t *out_n, size_t maxn);
static void hush_http_add_mentions(hush_event_t *out, const char *body);
static hush_status_t hush_http_resolve_conversation(hush_event_t *event, const hush_store_t *store);
static hush_status_t hush_http_parse_note(hush_event_t *out, const char *body);
static void hush_http_note_presence(hush_store_t *store, const hush_event_t *event);
static hush_status_t hush_http_channel_create(const char *body);
static hush_status_t hush_http_channel_group(const char *body, const char *slug);
static hush_status_t hush_http_channel_roster(const char *body, const hush_launch_channel_t *channel);
static hush_status_t hush_http_channel_about(const char *body, const char *slug);
static int hush_http_is_robot_key(const char *key);
static void hush_http_add_reply_to(hush_event_t *out, const char *body);

hush_status_t hush_http_serve_post(int fd, const char *req, size_t len,
                                          hush_store_t *store, hush_event_t *out)
{
    if (hush_http_launch() == NULL || !hush_http_launch()->logged_in) {
        const char *error = "Sign in before sending a message.\n";
        hush_http_reply(fd, "401 Unauthorized", "text/plain", error, strlen(error));
        return HUSH_ERR_DENIED;
    }
    hush_status_t status = hush_http_parse_note(out, hush_http_body(req, len));
    if (status == HUSH_OK) status = hush_http_resolve_conversation(out, store);
    if (status != HUSH_OK) {
        const char *error = "Invalid message or conversation. Reopen the thread and retry.\n";
        hush_http_reply(fd, "400 Bad Request", "text/plain", error, strlen(error));
        return status;
    }
    status = hush_event_compute_id(out, out->id);
    if (status == HUSH_OK) status = hush_store_insert(store, out);
    if (status != HUSH_OK) {
        const char *error = "Message could not be stored.\n";
        hush_http_reply(fd, "507 Insufficient Storage", "text/plain", error, strlen(error));
        return status;
    }
    hush_thread_record(out);
    hush_intel_consider(store, hush_http_launch(), out);
    hush_http_note_presence(store, out);
    const char *success = "{\"ok\":true}\n";
    hush_http_reply(fd, "200 OK", "application/json", success, strlen(success));
    return HUSH_OK;
}

static hush_status_t hush_http_parse_note(hush_event_t *out, const char *body)
{
    assert(out != NULL && body != NULL);
    assert(hush_http_launch() != NULL && hush_http_launch()->logged_in);
    memset(out, 0, sizeof(*out));
    hush_json_value_t value = {0};
    HUSH_TRY(hush_json_lookup(&value, body, "/content"));
    HUSH_TRY(hush_json_decode(out->content, sizeof(out->content), &value));
    if (out->content[0] == '\0') return HUSH_ERR_PARSE;
    char channel[HUSH_LAUNCH_NAME_MAX] = "general";
    hush_status_t status = hush_json_lookup(&value, body, "/channel");
    if (status == HUSH_OK) status = hush_json_decode(channel, sizeof(channel), &value);
    if (status != HUSH_OK && status != HUSH_ERR_NOT_FOUND) return status;
    memcpy(out->pubkey, hush_http_launch()->human.pubkey_hex, sizeof(out->pubkey));
    out->kind = 1;
    char kind[HUSH_HTTP_KIND_BYTES] = {0};
    if (hush_http_json_field(body, "kind", kind, sizeof(kind))) out->kind = (uint32_t)atoi(kind);
    out->created_at = (int64_t)time(NULL);
    out->tag_count = 1;
    memcpy(out->tags[0][0], "h", sizeof("h"));
    memcpy(out->tags[0][1], channel, strlen(channel) + 1);
    hush_http_add_reply_to(out, body);
    hush_http_add_mentions(out, body);
    return HUSH_OK;
}

static void hush_http_note_presence(hush_store_t *store, const hush_event_t *event)
{
    assert(store != NULL && event != NULL && hush_http_launch() != NULL);
    if (event->kind != 1)
        return;
    hush_presence_in_t presence = {.pubkey = hush_http_launch()->human.pubkey_hex,
        .slug = HUSH_PRESENCE_SLUG_CONVERSING, .channel = event->tags[0][1],
        .root = hush_http_launch()->human.pubkey_hex, .now = time(NULL)};
    /* The stored note remains successful even if optional presence is unavailable. */
    if (hush_presence_publish(store, &presence) != HUSH_OK)
        return;
}

static int hush_http_is_robot_key(const char *key)
{
    assert(key != NULL);
    if (hush_http_launch() == NULL)
        return 0;
    if (hush_http_launch()->payne_enabled && (strcmp(key, hush_http_launch()->payne.npub) == 0 ||
        strcmp(key, hush_http_launch()->payne.pubkey_hex) == 0))
        return 1;
    const hush_roster_t *roster = &hush_http_launch()->roster;
    for (size_t i = 0; i < roster->nagents && i < (size_t)HUSH_ROSTER_AGENTS_MAX; ++i) {
        const hush_roster_agent_t *robot = &roster->agents[i];
        const hush_identity_t *identity = &robot->id;
        if (robot->enabled && (strcmp(key, identity->npub) == 0 ||
            strcmp(key, identity->pubkey_hex) == 0))
            return 1;
    }
    return 0;
}

static hush_status_t hush_http_resolve_conversation(hush_event_t *event, const hush_store_t *store)
{
    assert(event != NULL);
    assert(store != NULL);
    char root_id[HUSH_EVENT_ID_HEX_LEN + 1] = {0};
    hush_http_event_reply_to(root_id, sizeof(root_id), event);
    if (root_id[0] == '\0')
        return HUSH_OK;
    hush_event_t root = {0};
    HUSH_TRY(hush_store_find(store, &root, root_id));
    if (strcmp(hush_http_event_channel(event), hush_http_event_channel(&root)) != 0)
        return HUSH_ERR_DENIED;
    for (size_t i = 0; i < event->tag_count && i < (size_t)HUSH_EVENT_MAX_TAGS; ++i) {
        if (strcmp(event->tags[i][0], "p") == 0)
            return HUSH_OK;
    }
    for (size_t i = 0; i < root.tag_count && i < (size_t)HUSH_EVENT_MAX_TAGS; ++i) {
        if (strcmp(root.tags[i][0], "p") != 0 || !hush_http_is_robot_key(root.tags[i][1]))
            continue;
        if (event->tag_count == (size_t)HUSH_EVENT_MAX_TAGS)
            return HUSH_ERR_FULL;
        memcpy(event->tags[event->tag_count], root.tags[i], sizeof(root.tags[i]));
        ++event->tag_count;
        return HUSH_OK;
    }
    return HUSH_OK;
}

hush_status_t hush_http_reply_session(int fd, hush_status_t st)
{
    if (st == HUSH_OK) {
        hush_http_serve_session(fd);
        return HUSH_OK;
    }
    if (st == HUSH_ERR_IO) {
        hush_http_reply(fd, "500 Internal Server Error", "text/plain",
                        "io error\n", 9);
        return st;
    }
    hush_http_reply(fd, "400 Bad Request", "text/plain", "bad request\n", 12);
    return st;
}


hush_status_t hush_http_serve_channel(int fd, const char *body)
{
    if (hush_http_launch() == NULL || body == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_ARG);
    char action[HUSH_HTTP_PATH_MAX] = {0};
    (void)hush_http_json_field(body, "action", action, sizeof(action));
    if (action[0] == '\0' || strcmp(action, "create") == 0)
        return hush_http_reply_session(fd, hush_http_channel_create(body));
    char slug[HUSH_LAUNCH_NAME_MAX] = {0};
    if (!hush_http_json_field(body, "slug", slug, sizeof(slug)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    if (strcmp(action, "delete") == 0)
        return hush_http_reply_session(fd, hush_launch_remove_channel(hush_http_launch(), slug));
    if (strcmp(action, "ungroup") == 0)
        return hush_http_reply_session(fd, hush_launch_set_channel_group(hush_http_launch(), slug, ""));
    if (strcmp(action, "group") == 0)
        return hush_http_reply_session(fd, hush_http_channel_group(body, slug));
    if (strcmp(action, "manage") == 0)
        return hush_http_channel_manage(fd, body, slug);
    return hush_http_reply_session(fd, HUSH_ERR_PARSE);
}

static hush_status_t hush_http_channel_create(const char *body)
{
    assert(body != NULL && hush_http_launch() != NULL);
    char name[HUSH_LAUNCH_NAME_MAX] = {0};
    if (!hush_http_json_field(body, "name", name, sizeof(name)))
        return HUSH_ERR_PARSE;
    char prompt[HUSH_LAUNCH_PROMPT_BYTES] = HUSH_LAUNCH_ROOM_PROMPT;
    HUSH_TRY(hush_http_take_room_prompt(prompt, sizeof(prompt), body));
    return hush_launch_add_channel_prompt(hush_http_launch(), name, prompt);
}

static hush_status_t hush_http_channel_group(const char *body, const char *slug)
{
    assert(body != NULL && slug != NULL && hush_http_launch() != NULL);
    char group[HUSH_LAUNCH_NAME_MAX] = {0};
    if (hush_http_json_field(body, "group_id", group, sizeof(group)))
        return hush_launch_set_channel_group(hush_http_launch(), slug, group);
    if (!hush_http_json_field(body, "group", group, sizeof(group)))
        return HUSH_ERR_PARSE;
    HUSH_TRY(hush_launch_add_group(hush_http_launch(), group));
    if (hush_http_launch()->ngroups == 0)
        return HUSH_ERR_FULL;
    return hush_launch_set_channel_group(hush_http_launch(), slug, hush_http_launch()->groups[hush_http_launch()->ngroups - 1].id);
}

hush_status_t hush_http_serve_group(int fd, const char *body)
{
    char name[HUSH_LAUNCH_NAME_MAX];

    if (hush_http_launch() == NULL || body == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_ARG);
    if (!hush_http_json_field(body, "name", name, sizeof(name)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    return hush_http_reply_session(fd, hush_launch_add_group(hush_http_launch(), name));
}

static hush_status_t hush_http_channel_manage(int fd, const char *body, const char *slug)
{
    const hush_launch_channel_t *channel = hush_http_find_channel(slug);
    if (channel == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_NOT_FOUND);
    char prompt[HUSH_LAUNCH_PROMPT_BYTES] = {0};
    memcpy(prompt, channel->system_prompt, sizeof(prompt));
    hush_status_t status = hush_http_take_room_prompt(prompt, sizeof(prompt), body);
    if (status == HUSH_OK) status = hush_http_channel_roster(body, channel);
    if (status == HUSH_OK) status = hush_http_channel_policy(body, slug);
    if (status == HUSH_OK) status = hush_http_channel_about(body, slug);
    if (status == HUSH_OK) status = hush_launch_set_channel_prompt(hush_http_launch(), slug, prompt);
    return hush_http_reply_session(fd, status);
}

static hush_status_t hush_http_channel_roster(const char *body, const hush_launch_channel_t *channel)
{
    assert(body != NULL && channel != NULL);
    int has_humans = hush_http_json_has_key(body, "human_0");
    int has_robots = hush_http_json_has_key(body, "robot_0");
    if (!has_humans && !has_robots)
        return HUSH_OK;
    char humans[HUSH_HTTP_CHAN_LIST_MAX][HUSH_IDENTITY_NPUB_MAX] = {{0}};
    char robots[HUSH_HTTP_CHAN_LIST_MAX][HUSH_IDENTITY_NPUB_MAX] = {{0}};
    const char *human_ptrs[HUSH_HTTP_CHAN_LIST_MAX] = {0};
    const char *robot_ptrs[HUSH_HTTP_CHAN_LIST_MAX] = {0};
    size_t nhumans = channel->nhumans;
    size_t nrobots = channel->nrobots;
    if (has_humans) hush_http_collect_indexed(body, "human", humans, &nhumans, HUSH_HTTP_CHAN_LIST_MAX);
    if (has_robots) hush_http_collect_indexed(body, "robot", robots, &nrobots, HUSH_HTTP_CHAN_LIST_MAX);
    for (size_t i = 0; i < nhumans && i < (size_t)HUSH_HTTP_CHAN_LIST_MAX; ++i) {
        if (!has_humans) memcpy(humans[i], channel->humans[i], strlen(channel->humans[i]) + 1);
        human_ptrs[i] = humans[i];
    }
    for (size_t i = 0; i < nrobots && i < (size_t)HUSH_HTTP_CHAN_LIST_MAX; ++i) {
        if (!has_robots) memcpy(robots[i], channel->robots[i], strlen(channel->robots[i]) + 1);
        robot_ptrs[i] = robots[i];
    }
    return hush_launch_set_channel_roster(hush_http_launch(), channel->slug, human_ptrs, nhumans,
                                          robot_ptrs, nrobots);
}

static hush_status_t hush_http_channel_about(const char *body, const char *slug)
{
    assert(body != NULL && slug != NULL);
    if (!hush_http_json_has_key(body, "about"))
        return HUSH_OK;
    char about[HUSH_LAUNCH_ABOUT_MAX] = {0};
    (void)hush_http_json_field(body, "about", about, sizeof(about));
    return hush_launch_set_channel_about(hush_http_launch(), slug, about);
}

static hush_status_t hush_http_take_room_prompt(char *out, size_t outsz, const char *body)
{
    assert(out != NULL && outsz >= (size_t)HUSH_LAUNCH_PROMPT_BYTES);
    assert(body != NULL);
    hush_json_value_t value = {0};
    hush_status_t status = hush_json_lookup(&value, body, "/system_prompt");
    if (status == HUSH_ERR_NOT_FOUND)
        return HUSH_OK;
    if (status != HUSH_OK)
        return status;
    HUSH_TRY(hush_json_decode(out, outsz, &value));
    return hush_launch_validate_channel_prompt(out);
}

static const hush_launch_channel_t *hush_http_find_channel(const char *slug)
{
    size_t i;

    if (hush_http_launch() == NULL || slug == NULL)
        return NULL;
    for (i = 0; i < hush_http_launch()->nchannels; ++i) {
        if (strcmp(hush_http_launch()->channels[i].slug, slug) == 0)
            return &hush_http_launch()->channels[i];
    }
    return NULL;
}

static int hush_http_read_int(const char *body, const char *key, int fallback)
{
    char text[HUSH_LAUNCH_POLICY_MAX];

    if (!hush_http_json_field(body, key, text, sizeof(text)))
        return fallback;
    if (text[0] == '\0')
        return fallback;
    return atoi(text);
}

static void hush_http_fill_policy_text(hush_launch_policy_t *policy,
                                       const hush_launch_channel_t *ch,
                                       const char *body)
{
    assert(policy != NULL);
    assert(ch != NULL);
    if (!hush_http_json_field(body, "kind", policy->kind, sizeof(policy->kind))
        || policy->kind[0] == '\0')
        memcpy(policy->kind, ch->kind, sizeof(policy->kind));
    if (!hush_http_json_field(body, "robot_reply", policy->robot_reply,
                         sizeof(policy->robot_reply))
        || policy->robot_reply[0] == '\0')
        memcpy(policy->robot_reply, ch->robot_reply,
               sizeof(policy->robot_reply));
    if (!hush_http_json_field(body, "chaperon", policy->chaperon,
                         sizeof(policy->chaperon)))
        memcpy(policy->chaperon, ch->chaperon, sizeof(policy->chaperon));
}

static void hush_http_fill_policy_nums(hush_launch_policy_t *policy,
                                       const hush_launch_channel_t *ch,
                                       const char *body)
{
    assert(policy != NULL);
    assert(ch != NULL);
    policy->robot_talk = hush_http_read_int(body, "robot_talk", ch->robot_talk);
    policy->burst_ms = hush_http_read_int(body, "burst_ms", ch->burst_ms);
    policy->max_jobs = hush_http_read_int(body, "max_jobs", ch->max_jobs);
    policy->cooldown_s = hush_http_read_int(body, "cooldown_s", ch->cooldown_s);
    policy->robot_hops = hush_http_read_int(body, "robot_hops", ch->robot_hops);
    policy->max_robot_turns = hush_http_read_int(body, "max_robot_turns",
                                                 ch->max_robot_turns);
}

static hush_status_t hush_http_channel_policy(const char *body,
                                              const char *slug)
{
    const hush_launch_channel_t *ch;
    hush_launch_policy_t policy;

    ch = hush_http_find_channel(slug);
    if (ch == NULL)
        return HUSH_ERR_NOT_FOUND;
    memset(&policy, 0, sizeof(policy));
    hush_http_fill_policy_text(&policy, ch, body);
    hush_http_fill_policy_nums(&policy, ch, body);
    return hush_launch_set_channel_policy(hush_http_launch(), slug, &policy);
}

static void hush_http_collect_indexed(const char *body, const char *stem,
                                      char (*out)[HUSH_IDENTITY_NPUB_MAX],
                                      size_t *out_n, size_t maxn)
{
    char key[32];
    size_t i;

    assert(out != NULL);
    assert(out_n != NULL);
    *out_n = 0;
    if (body == NULL || stem == NULL)
        return;
    for (i = 0; i < maxn; ++i) {
        if (snprintf(key, sizeof(key), "%s_%zu", stem, i) >= (int)sizeof(key))
            break;
        if (!hush_http_json_field(body, key, out[*out_n], HUSH_IDENTITY_NPUB_MAX))
            continue;
        (*out_n)++;
    }
}

static void hush_http_add_reply_to(hush_event_t *out, const char *body)
{
    char reply_to[HUSH_EVENT_ID_HEX_LEN + 1];

    assert(out != NULL);
    if (body == NULL)
        return;
    if (out->tag_count >= (size_t)HUSH_EVENT_MAX_TAGS)
        return;
    if (!hush_http_json_field(body, "reply_to", reply_to, sizeof(reply_to)))
        return;
    if (reply_to[0] == '\0')
        return;
    memcpy(out->tags[out->tag_count][0], "e", 2);
    memcpy(out->tags[out->tag_count][1], reply_to, strlen(reply_to) + 1);
    out->tag_count++;
}

static void hush_http_add_mentions(hush_event_t *out, const char *body)
{
    char key[24];
    char mention[HUSH_IDENTITY_NPUB_MAX];
    size_t i;

    assert(out != NULL);
    if (body == NULL)
        return;
    for (i = 0; i < (size_t)HUSH_HTTP_MENTIONS_MAX; ++i) {
        if (out->tag_count >= (size_t)HUSH_EVENT_MAX_TAGS)
            return;
        if (snprintf(key, sizeof(key), "mention_%zu", i) >= (int)sizeof(key))
            return;
        if (!hush_http_json_field(body, key, mention, sizeof(mention)))
            continue;
        memcpy(out->tags[out->tag_count][0], "p", 2);
        memcpy(out->tags[out->tag_count][1], mention, strlen(mention) + 1);
        out->tag_count++;
    }
}

void hush_http_event_reply_to(char *out, size_t outsz,
                                     const hush_event_t *ev)
{
    size_t i;

    assert(out != NULL);
    assert(outsz > 0);
    out[0] = '\0';
    if (ev == NULL)
        return;
    for (i = 0; i < ev->tag_count; i++) {
        if (strcmp(ev->tags[i][0], "e") != 0)
            continue;
        if (ev->tags[i][1][0] == '\0')
            continue;
        if (strlen(ev->tags[i][1]) >= outsz)
            return;
        memcpy(out, ev->tags[i][1], strlen(ev->tags[i][1]) + 1);
        return;
    }
}

hush_status_t hush_http_serve_project(int fd, const char *body,
                                             hush_store_t *store)
{
    char name[HUSH_LAUNCH_NAME_MAX];
    char path[HUSH_LAUNCH_PATH_MAX];
    char gitbuf[8];
    int init_git = 0;

    if (hush_http_launch() == NULL || body == NULL || store == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_ARG);
    if (!hush_http_json_field(body, "name", name, sizeof(name)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    if (!hush_http_json_field(body, "path", path, sizeof(path)))
        path[0] = '\0';
    if (hush_http_json_field(body, "git", gitbuf, sizeof(gitbuf)) &&
        (strcmp(gitbuf, "1") == 0 || strcmp(gitbuf, "true") == 0))
        init_git = 1;
    return hush_http_reply_session(fd,
                                   hush_launch_add_project(hush_http_launch(), store,
                                                           name, path, init_git));
}


hush_status_t hush_http_serve_signal(int fd, const char *body,
                                            hush_store_t *store,
                                            hush_event_t *out)
{
    char channel[64];

    if (body == NULL || store == NULL || out == NULL)
        return HUSH_ERR_ARG;
    memset(out, 0, sizeof(*out));
    if (body[0] == '\0' || strlen(body) >= HUSH_EVENT_MAX_CONTENT) {
        hush_http_reply(fd, "400 Bad Request", "text/plain", "need body\n", 10);
        return HUSH_ERR_PARSE;
    }
    if (!hush_http_json_field(body, "channel", channel, sizeof(channel)))
        memcpy(channel, "general", 8);
    if (hush_http_launch() != NULL && hush_http_launch()->logged_in)
        memcpy(out->pubkey, hush_http_launch()->human.pubkey_hex, 65);
    else
        memcpy(out->pubkey,
               "0000000000000000000000000000000000000000000000000000000000000001",
               65);
    out->kind = (uint32_t)HUSH_HTTP_KIND_SIGNAL;
    out->created_at = (int64_t)time(NULL);
    memcpy(out->content, body, strlen(body) + 1);
    out->tag_count = 1;
    memcpy(out->tags[0][0], "h", 2);
    memcpy(out->tags[0][1], channel, strlen(channel) + 1);
    (void)hush_event_compute_id(out, out->id);
    if (hush_store_insert(store, out) != HUSH_OK) {
        hush_http_reply(fd, "507 Insufficient Storage", "text/plain", "full\n", 5);
        return HUSH_ERR_FULL;
    }
    hush_http_reply(fd, "200 OK", "application/json", "{\"ok\":true}\n", 12);
    return HUSH_OK;
}
