/* hush_intel.c: owns the channel leash and chatty-human burst hold. */

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "hush_agent.h"
#include "hush_intel.h"

enum {
    HUSH_INTEL_KIND_NOTE = 1,
    HUSH_INTEL_ID_WIDTH = 16,
    HUSH_INTEL_DUP_S = 1,
    HUSH_INTEL_MS_PER_S = 1000,
    HUSH_INTEL_STATUS_MAX = 256
};

#define HUSH_INTEL_TAG_H "h"
#define HUSH_INTEL_TAG_E "e"
#define HUSH_INTEL_TAG_P "p"
#define HUSH_INTEL_TAG_T "t"
#define HUSH_INTEL_CHAN_FALLBACK "general"
#define HUSH_INTEL_RECAP_ONE "I heard: %s Confirm or correct."
#define HUSH_INTEL_RECAP_MANY \
    "I heard several asks. Reply 1, 2, or say which to do first."
#define HUSH_INTEL_DENY_OFF \
    "This channel is humans talking. Change policy in Manage Channel."
#define HUSH_INTEL_DENY_ROSTER "Not on this channel."
#define HUSH_INTEL_DENY_EMPTY "Say the ask."
#define HUSH_INTEL_DENY_HOP "Robots do not chain here."
#define HUSH_INTEL_DENY_JOBS "Holding. This channel is at its job cap."

typedef struct {
    int live;
    int awaiting;
    size_t nnotes;
    time_t last;
    char channel[HUSH_EVENT_MAX_TAG_LEN + 1];
    char root[HUSH_EVENT_ID_HEX_LEN + 1];
    char robot[HUSH_EVENT_PUBKEY_HEX_LEN + 1];
    char human[HUSH_EVENT_PUBKEY_HEX_LEN + 1];
    char last_body[HUSH_EVENT_MAX_CONTENT + 1];
    char notes[HUSH_INTEL_BURST_MAX][HUSH_EVENT_MAX_CONTENT + 1];
} hush_intel_hold_t;

static hush_intel_hold_t g_holds[HUSH_INTEL_HOLD_MAX];
static unsigned g_id_seq;

static void hush_intel_copy(char *dst, size_t dstsz, const char *src);
static void hush_intel_lower(char *text);
static void hush_intel_make_id(char *out65);
static void hush_intel_event_channel(char *out, size_t outsz,
                                     const hush_event_t *ev);
static void hush_intel_event_root(char *out, size_t outsz,
                                  const hush_event_t *ev);
static const hush_launch_channel_t *hush_intel_find_slug(
    const hush_launch_t *launch, const char *slug);
static const hush_launch_channel_t *hush_intel_channel(
    const hush_launch_t *launch, const hush_event_t *ev);
static int hush_intel_has_tag(const hush_event_t *ev, const char *name,
                              const char *value);
static int hush_intel_key_match(const char *mention, const char *npub,
                                const char *hex);
static int hush_intel_is_human(const hush_launch_t *launch,
                               const char *pubkey);
static int hush_intel_lookup_robot(const hush_launch_t *launch,
                                   const char *mention, char *hex,
                                   size_t hexsz);
static int hush_intel_robot_on_channel(const hush_launch_channel_t *ch,
                                       const hush_launch_t *launch,
                                       const char *hex);
static int hush_intel_is_cue(const char *text);
static int hush_intel_is_mention_only(const char *text);
static int hush_intel_looks_many(const hush_intel_hold_t *hold);
static hush_intel_hold_t *hush_intel_find_hold(const char *channel,
                                               const char *root,
                                               const char *robot);
static hush_intel_hold_t *hush_intel_take_hold(const char *channel,
                                               const char *root,
                                               const char *robot);
static void hush_intel_clear_hold(hush_intel_hold_t *hold);
static int hush_intel_jobs_busy(void);
static void hush_intel_post_line(hush_store_t *store, const hush_event_t *ev,
                                 const char *robot, const char *line);
static void hush_intel_post_recap(hush_store_t *store, const hush_event_t *ev,
                                  hush_intel_hold_t *hold);
static void hush_intel_fill_synth(hush_event_t *out,
                                  const hush_intel_hold_t *hold);
static void hush_intel_release(hush_store_t *store, hush_launch_t *launch,
                               hush_intel_hold_t *hold,
                               const hush_event_t *ev);
static int hush_intel_policy_blocks(hush_store_t *store,
                                    const hush_launch_channel_t *ch,
                                    const hush_launch_t *launch,
                                    const hush_event_t *ev,
                                    const char *hex);
static void hush_intel_fold_note(hush_intel_hold_t *hold,
                                 const hush_event_t *ev);
static void hush_intel_handle_robot(hush_store_t *store, hush_launch_t *launch,
                                    const hush_launch_channel_t *ch,
                                    const hush_event_t *ev,
                                    const char *mention);
static int hush_intel_burst_ready(const hush_intel_hold_t *hold,
                                  const hush_launch_channel_t *ch, time_t now);

void hush_intel_init(void)
{
    memset(g_holds, 0, sizeof(g_holds));
}

void hush_intel_consider(hush_store_t *store, hush_launch_t *launch,
                         const hush_event_t *ev)
{
    const hush_launch_channel_t *ch;
    size_t i;

    if (store == NULL || launch == NULL || ev == NULL)
        return;
    if (ev->kind != (uint32_t)HUSH_INTEL_KIND_NOTE)
        return;
    if (hush_intel_has_tag(ev, HUSH_INTEL_TAG_T, HUSH_INTEL_CONFIRM_TAG))
        return;
    ch = hush_intel_channel(launch, ev);
    if (ch == NULL)
        return;
    for (i = 0; i < ev->tag_count && i < (size_t)HUSH_EVENT_MAX_TAGS; i++) {
        if (strcmp(ev->tags[i][0], HUSH_INTEL_TAG_P) != 0)
            continue;
        hush_intel_handle_robot(store, launch, ch, ev, ev->tags[i][1]);
    }
}

void hush_intel_poll(hush_store_t *store, hush_launch_t *launch)
{
    const hush_launch_channel_t *ch;
    time_t now;
    size_t i;

    if (store == NULL || launch == NULL)
        return;
    now = time(NULL);
    for (i = 0; i < (size_t)HUSH_INTEL_HOLD_MAX; i++) {
        if (!g_holds[i].live || g_holds[i].awaiting)
            continue;
        ch = hush_intel_find_slug(launch, g_holds[i].channel);
        if (ch == NULL)
            continue;
        if (!hush_intel_burst_ready(&g_holds[i], ch, now))
            continue;
        hush_intel_release(store, launch, &g_holds[i], NULL);
    }
}

static void hush_intel_copy(char *dst, size_t dstsz, const char *src)
{
    size_t n;

    assert(dst != NULL);
    assert(dstsz > 0);
    if (src == NULL)
        src = "";
    n = strlen(src);
    if (n >= dstsz)
        n = dstsz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void hush_intel_lower(char *text)
{
    size_t i;

    assert(text != NULL);
    for (i = 0; text[i] != '\0'; i++)
        text[i] = (char)tolower((unsigned char)text[i]);
}

static void hush_intel_make_id(char *out65)
{
    time_t now;

    assert(out65 != NULL);
    now = time(NULL);
    g_id_seq++;
    (void)snprintf(out65, HUSH_EVENT_ID_HEX_LEN + 1,
                   "%0*llx%0*x%0*x%0*x",
                   HUSH_INTEL_ID_WIDTH, (unsigned long long)now,
                   HUSH_INTEL_ID_WIDTH, g_id_seq,
                   HUSH_INTEL_ID_WIDTH, g_id_seq ^ 0x51ed270bu,
                   HUSH_INTEL_ID_WIDTH, g_id_seq * 7u);
}

static void hush_intel_event_channel(char *out, size_t outsz,
                                     const hush_event_t *ev)
{
    size_t i;

    assert(out != NULL);
    hush_intel_copy(out, outsz, HUSH_INTEL_CHAN_FALLBACK);
    if (ev == NULL)
        return;
    for (i = 0; i < ev->tag_count; i++) {
        if (strcmp(ev->tags[i][0], HUSH_INTEL_TAG_H) == 0 &&
            ev->tags[i][1][0] != '\0') {
            hush_intel_copy(out, outsz, ev->tags[i][1]);
            return;
        }
    }
}

static void hush_intel_event_root(char *out, size_t outsz,
                                  const hush_event_t *ev)
{
    size_t i;

    assert(out != NULL);
    assert(ev != NULL);
    hush_intel_copy(out, outsz, ev->id);
    for (i = 0; i < ev->tag_count; i++) {
        if (strcmp(ev->tags[i][0], HUSH_INTEL_TAG_E) == 0 &&
            ev->tags[i][1][0] != '\0') {
            hush_intel_copy(out, outsz, ev->tags[i][1]);
            return;
        }
    }
}

static const hush_launch_channel_t *hush_intel_find_slug(
    const hush_launch_t *launch, const char *slug)
{
    size_t i;

    assert(launch != NULL);
    if (slug == NULL)
        return NULL;
    for (i = 0; i < launch->nchannels; i++) {
        if (strcmp(launch->channels[i].slug, slug) == 0)
            return &launch->channels[i];
    }
    return NULL;
}

static const hush_launch_channel_t *hush_intel_channel(
    const hush_launch_t *launch, const hush_event_t *ev)
{
    char slug[HUSH_EVENT_MAX_TAG_LEN + 1];
    const hush_launch_channel_t *ch;

    assert(launch != NULL);
    hush_intel_event_channel(slug, sizeof(slug), ev);
    ch = hush_intel_find_slug(launch, slug);
    if (ch != NULL)
        return ch;
    if (launch->nchannels == 0)
        return NULL;
    return &launch->channels[0];
}

static int hush_intel_has_tag(const hush_event_t *ev, const char *name,
                              const char *value)
{
    size_t i;

    assert(ev != NULL);
    assert(name != NULL);
    for (i = 0; i < ev->tag_count; i++) {
        if (strcmp(ev->tags[i][0], name) != 0)
            continue;
        if (value == NULL || strcmp(ev->tags[i][1], value) == 0)
            return 1;
    }
    return 0;
}

static int hush_intel_key_match(const char *mention, const char *npub,
                                const char *hex)
{
    if (mention == NULL || mention[0] == '\0')
        return 0;
    if (npub != NULL && strcmp(mention, npub) == 0)
        return 1;
    if (hex != NULL && strcmp(mention, hex) == 0)
        return 1;
    return 0;
}

static int hush_intel_is_human(const hush_launch_t *launch,
                               const char *pubkey)
{
    size_t i;

    assert(launch != NULL);
    if (pubkey == NULL || pubkey[0] == '\0')
        return 0;
    if (launch->logged_in &&
        strcmp(launch->human.pubkey_hex, pubkey) == 0)
        return 1;
    for (i = 0; i < launch->roster.nmembers; i++) {
        if (strcmp(launch->roster.members[i].pubkey_hex, pubkey) == 0)
            return 1;
    }
    return 0;
}

static int hush_intel_lookup_robot(const hush_launch_t *launch,
                                   const char *mention, char *hex,
                                   size_t hexsz)
{
    size_t i;

    assert(launch != NULL);
    assert(hex != NULL);
    hex[0] = '\0';
    if (launch->has_vibe &&
        hush_intel_key_match(mention, launch->payne.npub,
                             launch->payne.pubkey_hex)) {
        hush_intel_copy(hex, hexsz, launch->payne.pubkey_hex);
        return 1;
    }
    for (i = 0; i < launch->roster.nagents; i++) {
        const hush_roster_agent_t *agent = &launch->roster.agents[i];

        if (!hush_intel_key_match(mention, agent->id.npub,
                                  agent->id.pubkey_hex))
            continue;
        hush_intel_copy(hex, hexsz, agent->id.pubkey_hex);
        return 1;
    }
    return 0;
}

static int hush_intel_robot_on_channel(const hush_launch_channel_t *ch,
                                       const hush_launch_t *launch,
                                       const char *hex)
{
    size_t i;
    size_t a;

    assert(ch != NULL);
    assert(launch != NULL);
    if (strcmp(ch->kind, HUSH_LAUNCH_KIND_OPEN) == 0 && ch->nrobots == 0)
        return 1;
    for (i = 0; i < ch->nrobots; i++) {
        if (strcmp(ch->robots[i], HUSH_LAUNCH_PAYNE_SLUG) == 0 &&
            launch->has_vibe &&
            strcmp(hex, launch->payne.pubkey_hex) == 0)
            return 1;
        for (a = 0; a < launch->roster.nagents; a++) {
            if (strcmp(launch->roster.agents[a].slug, ch->robots[i]) != 0)
                continue;
            if (strcmp(launch->roster.agents[a].id.pubkey_hex, hex) == 0)
                return 1;
        }
    }
    return 0;
}

static int hush_intel_is_cue(const char *text)
{
    char buf[16];

    hush_intel_copy(buf, sizeof(buf), text);
    hush_intel_lower(buf);
    if (strcmp(buf, "yes") == 0 || strcmp(buf, "y") == 0)
        return 1;
    if (strcmp(buf, "confirm") == 0 || strcmp(buf, "go") == 0)
        return 1;
    if (strcmp(buf, "do it") == 0)
        return 1;
    if (buf[0] >= '1' && buf[0] <= '8' && buf[1] == '\0')
        return 1;
    return 0;
}

static int hush_intel_is_mention_only(const char *text)
{
    const char *p;

    if (text == NULL)
        return 1;
    p = text;
    while (*p == ' ' || *p == '\t')
        p++;
    if (strncmp(p, "nostr:", 6) == 0) {
        while (*p != '\0' && *p != ' ' && *p != '\t')
            p++;
        while (*p == ' ' || *p == '\t')
            p++;
    }
    return *p == '\0';
}

static int hush_intel_looks_many(const hush_intel_hold_t *hold)
{
    size_t i;
    size_t marks;

    assert(hold != NULL);
    if (hold->nnotes < 2)
        return 0;
    marks = 0;
    for (i = 0; i < hold->nnotes; i++) {
        if (strchr(hold->notes[i], '?') != NULL)
            marks++;
        if (strstr(hold->notes[i], "also") != NULL)
            return 1;
    }
    return marks >= 2;
}

static hush_intel_hold_t *hush_intel_find_hold(const char *channel,
                                               const char *root,
                                               const char *robot)
{
    size_t i;

    for (i = 0; i < (size_t)HUSH_INTEL_HOLD_MAX; i++) {
        if (!g_holds[i].live)
            continue;
        if (strcmp(g_holds[i].channel, channel) == 0 &&
            strcmp(g_holds[i].root, root) == 0 &&
            strcmp(g_holds[i].robot, robot) == 0)
            return &g_holds[i];
    }
    return NULL;
}

static hush_intel_hold_t *hush_intel_take_hold(const char *channel,
                                               const char *root,
                                               const char *robot)
{
    hush_intel_hold_t *hold;
    size_t i;

    hold = hush_intel_find_hold(channel, root, robot);
    if (hold != NULL)
        return hold;
    for (i = 0; i < (size_t)HUSH_INTEL_HOLD_MAX; i++) {
        if (g_holds[i].live)
            continue;
        memset(&g_holds[i], 0, sizeof(g_holds[i]));
        g_holds[i].live = 1;
        hush_intel_copy(g_holds[i].channel, sizeof(g_holds[i].channel),
                        channel);
        hush_intel_copy(g_holds[i].root, sizeof(g_holds[i].root), root);
        hush_intel_copy(g_holds[i].robot, sizeof(g_holds[i].robot), robot);
        return &g_holds[i];
    }
    return &g_holds[0];
}

static void hush_intel_clear_hold(hush_intel_hold_t *hold)
{
    assert(hold != NULL);
    memset(hold, 0, sizeof(*hold));
}

static int hush_intel_jobs_busy(void)
{
    char thinking[HUSH_INTEL_STATUS_MAX];
    const char *p;
    int n;

    hush_agent_status(thinking, sizeof(thinking));
    n = 0;
    for (p = thinking; *p != '\0'; p++) {
        if (*p == '{')
            n++;
    }
    return n;
}

static void hush_intel_post_line(hush_store_t *store, const hush_event_t *ev,
                                 const char *robot, const char *line)
{
    hush_event_t note;
    char channel[HUSH_EVENT_MAX_TAG_LEN + 1];
    char root[HUSH_EVENT_ID_HEX_LEN + 1];

    assert(store != NULL);
    assert(ev != NULL);
    assert(line != NULL);
    memset(&note, 0, sizeof(note));
    hush_intel_make_id(note.id);
    hush_intel_copy(note.pubkey, sizeof(note.pubkey),
                    robot != NULL ? robot : ev->pubkey);
    note.kind = (uint32_t)HUSH_INTEL_KIND_NOTE;
    note.created_at = (int64_t)time(NULL);
    hush_intel_copy(note.content, sizeof(note.content), line);
    hush_intel_event_channel(channel, sizeof(channel), ev);
    hush_intel_event_root(root, sizeof(root), ev);
    note.tag_count = 3;
    memcpy(note.tags[0][0], HUSH_INTEL_TAG_H, 2);
    hush_intel_copy(note.tags[0][1], sizeof(note.tags[0][1]), channel);
    memcpy(note.tags[1][0], HUSH_INTEL_TAG_E, 2);
    hush_intel_copy(note.tags[1][1], sizeof(note.tags[1][1]), root);
    memcpy(note.tags[2][0], HUSH_INTEL_TAG_T, 2);
    hush_intel_copy(note.tags[2][1], sizeof(note.tags[2][1]),
                    HUSH_INTEL_CONFIRM_TAG);
    (void)hush_store_insert(store, &note);
}

static void hush_intel_post_recap(hush_store_t *store, const hush_event_t *ev,
                                  hush_intel_hold_t *hold)
{
    char line[HUSH_EVENT_MAX_CONTENT];
    char snip[HUSH_INTEL_SNIP_MAX + 1];
    const char *src;

    assert(hold != NULL);
    if (hush_intel_looks_many(hold)) {
        hush_intel_copy(line, sizeof(line), HUSH_INTEL_RECAP_MANY);
    } else {
        src = hold->nnotes > 0 ? hold->notes[hold->nnotes - 1] : "";
        hush_intel_copy(snip, sizeof(snip), src);
        if (snprintf(line, sizeof(line), HUSH_INTEL_RECAP_ONE, snip)
            >= (int)sizeof(line))
            hush_intel_copy(line, sizeof(line), src);
    }
    hush_intel_post_line(store, ev, hold->robot, line);
    hold->awaiting = 1;
}

static void hush_intel_fill_synth(hush_event_t *out,
                                  const hush_intel_hold_t *hold)
{
    assert(out != NULL);
    assert(hold != NULL);
    memset(out, 0, sizeof(*out));
    hush_intel_copy(out->id, sizeof(out->id), hold->root);
    hush_intel_copy(out->pubkey, sizeof(out->pubkey), hold->human);
    out->kind = (uint32_t)HUSH_INTEL_KIND_NOTE;
    hush_intel_copy(out->content, sizeof(out->content),
                    hold->notes[hold->nnotes - 1]);
    out->tag_count = 3;
    memcpy(out->tags[0][0], HUSH_INTEL_TAG_H, 2);
    hush_intel_copy(out->tags[0][1], sizeof(out->tags[0][1]), hold->channel);
    memcpy(out->tags[1][0], HUSH_INTEL_TAG_E, 2);
    hush_intel_copy(out->tags[1][1], sizeof(out->tags[1][1]), hold->root);
    memcpy(out->tags[2][0], HUSH_INTEL_TAG_P, 2);
    hush_intel_copy(out->tags[2][1], sizeof(out->tags[2][1]), hold->robot);
}

static int hush_intel_should_recap(const hush_intel_hold_t *hold,
                                   const hush_launch_channel_t *ch)
{
    assert(hold != NULL);
    if (ch != NULL &&
        strcmp(ch->robot_reply, HUSH_LAUNCH_REPLY_CONFIRM) == 0)
        return 1;
    return hold->nnotes >= 2;
}

static void hush_intel_release(hush_store_t *store, hush_launch_t *launch,
                               hush_intel_hold_t *hold,
                               const hush_event_t *ev)
{
    const hush_launch_channel_t *ch;
    hush_event_t synth;

    assert(hold != NULL);
    if (store == NULL || launch == NULL || hold->nnotes == 0) {
        hush_intel_clear_hold(hold);
        return;
    }
    ch = hush_intel_find_slug(launch, hold->channel);
    if (ev != NULL && !hush_intel_should_recap(hold, ch)) {
        hush_agent_consider(store, launch, ev);
        hush_intel_clear_hold(hold);
        return;
    }
    if (ev != NULL) {
        hush_intel_post_recap(store, ev, hold);
        return;
    }
    hush_intel_fill_synth(&synth, hold);
    if (hush_intel_should_recap(hold, ch))
        hush_intel_post_recap(store, &synth, hold);
    else {
        hush_agent_consider(store, launch, &synth);
        hush_intel_clear_hold(hold);
    }
}

static int hush_intel_burst_ready(const hush_intel_hold_t *hold,
                                  const hush_launch_channel_t *ch, time_t now)
{
    int wait_s;

    assert(hold != NULL);
    assert(ch != NULL);
    wait_s = (ch->burst_ms + HUSH_INTEL_MS_PER_S - 1) / HUSH_INTEL_MS_PER_S;
    if (wait_s < 1)
        wait_s = 1;
    return now >= hold->last + (time_t)wait_s;
}

static int hush_intel_policy_blocks(hush_store_t *store,
                                    const hush_launch_channel_t *ch,
                                    const hush_launch_t *launch,
                                    const hush_event_t *ev,
                                    const char *hex)
{
    assert(ch != NULL);
    if (strcmp(ch->robot_reply, HUSH_LAUNCH_REPLY_OFF) == 0) {
        hush_intel_post_line(store, ev, hex, HUSH_INTEL_DENY_OFF);
        return 1;
    }
    if (!hush_intel_robot_on_channel(ch, launch, hex)) {
        hush_intel_post_line(store, ev, hex, HUSH_INTEL_DENY_ROSTER);
        return 1;
    }
    if (hush_intel_is_mention_only(ev->content)) {
        hush_intel_post_line(store, ev, hex, HUSH_INTEL_DENY_EMPTY);
        return 1;
    }
    if (!hush_intel_is_human(launch, ev->pubkey) && ch->robot_hops == 0) {
        hush_intel_post_line(store, ev, hex, HUSH_INTEL_DENY_HOP);
        return 1;
    }
    if (hush_intel_jobs_busy() >= ch->max_jobs) {
        hush_intel_post_line(store, ev, hex, HUSH_INTEL_DENY_JOBS);
        return 1;
    }
    return 0;
}

static void hush_intel_fold_note(hush_intel_hold_t *hold,
                                 const hush_event_t *ev)
{
    size_t slot;

    assert(hold != NULL);
    assert(ev != NULL);
    if (hold->nnotes > 0 &&
        strcmp(hold->last_body, ev->content) == 0 &&
        ev->created_at <= (int64_t)hold->last + (int64_t)HUSH_INTEL_DUP_S)
        return;
    slot = hold->nnotes;
    if (slot >= (size_t)HUSH_INTEL_BURST_MAX)
        slot = (size_t)HUSH_INTEL_BURST_MAX - 1;
    else
        hold->nnotes++;
    hush_intel_copy(hold->notes[slot], sizeof(hold->notes[0]), ev->content);
    hush_intel_copy(hold->last_body, sizeof(hold->last_body), ev->content);
    hush_intel_copy(hold->human, sizeof(hold->human), ev->pubkey);
    hold->last = time(NULL);
}

static void hush_intel_handle_robot(hush_store_t *store, hush_launch_t *launch,
                                    const hush_launch_channel_t *ch,
                                    const hush_event_t *ev,
                                    const char *mention)
{
    char hex[HUSH_EVENT_PUBKEY_HEX_LEN + 1];
    char channel[HUSH_EVENT_MAX_TAG_LEN + 1];
    char root[HUSH_EVENT_ID_HEX_LEN + 1];
    hush_intel_hold_t *hold;

    if (!hush_intel_lookup_robot(launch, mention, hex, sizeof(hex)))
        return;
    if (hush_intel_policy_blocks(store, ch, launch, ev, hex))
        return;

    /* Always emit the durable server ack note (protocol + existing checks expect it).
     * "Mention received." is treated as a log receipt.
     * UI (M4) will suppress it from main chat stream when dev_log_enabled==0.
     * Emoji reaction is the user-visible ack. When dev logging enabled,
     * it surfaces in the developer panel. */
    hush_intel_post_line(store, ev, hex, "Mention received.");

    hush_intel_event_channel(channel, sizeof(channel), ev);
    hush_intel_event_root(root, sizeof(root), ev);
    hold = hush_intel_find_hold(channel, root, hex);
    if (hold != NULL && hold->awaiting && hush_intel_is_cue(ev->content)) {
        hush_agent_consider(store, launch, ev);
        hush_intel_clear_hold(hold);
        return;
    }
    if (hold != NULL)
        hold->awaiting = 0;
    hold = hush_intel_take_hold(channel, root, hex);
    hush_intel_fold_note(hold, ev);
    if (ch != NULL &&
        strcmp(ch->robot_reply, HUSH_LAUNCH_REPLY_CONFIRM) == 0) {
        hush_intel_release(store, launch, hold, ev);
        return;
    }
    if (hold->nnotes == 1)
        hush_intel_release(store, launch, hold, ev);
}
