/* hush_agent.c: owns mention replies for raised robots. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "hush_agent.h"
#include "hush_agent_internal.h"
#include "hush_codex.h"
#include "hush_cevent.h"
#include "hush_dir.h"
#include "hush_inference.h"
#include "hush_presence.h"
#include "hush_provider.h"
#include "hush_relay.h"
#include "hush_roster.h"
#include "hush_seg.h"
#include "hush_thread.h"
#include "hush_wake.h"


#define HUSH_AGENT_CHAN_FALLBACK "general"


static hush_agent_job_t g_jobs[HUSH_AGENT_JOBS_MAX];


hush_agent_job_t *hush_agent_jobs(void)
{
    return g_jobs;
}

void hush_agent_copy(char *dst, size_t dstsz, const char *src);
void hush_agent_trim(char *text);
#define HUSH_AGENT_ENV_CONFIG "HUSH_CONFIG_DIR"
#define HUSH_AGENT_CWD_LEAF "agent-cwd"
#define HUSH_AGENT_CWD_TMP "hush-agent-cwd"
#define HUSH_AGENT_TMP_FALLBACK "/tmp"

/* Binds required runtime view to Major's launch-owned fields. */
static void hush_agent_bind_payne(hush_agent_robot_t *out, const hush_launch_t *launch);
/* Binds required runtime view to roster-owned fields. */
static void hush_agent_bind_roster(hush_agent_robot_t *out, const hush_roster_agent_t *robot);
int hush_agent_lookup_robot(hush_agent_robot_t *out,
                                   const hush_launch_t *launch,
                                   const char *mention);
void hush_agent_event_channel(char *out, size_t outsz,
                                     const hush_event_t *ev);
void hush_agent_event_root(char *out, size_t outsz,
                                  const hush_event_t *ev);
void hush_agent_human_name(char *out, size_t outsz,
                                  const hush_launch_t *launch);
void hush_agent_prepare_cwd(char *out, size_t outsz);
static int hush_agent_status_append(char *out, size_t outsz, size_t *off,
                                    const hush_agent_job_t *job);
static int hush_agent_runtime_ready(const char *provider);
int hush_agent_event_is_root(const hush_event_t *ev, const char *root);
void hush_agent_append_turn(char *out, size_t outsz,
                                   const hush_event_t *ev, const char *who);
static void hush_agent_kill_job(hush_agent_job_t *job);
/* Claims the required job's wake slot before spawning a harness. */
static hush_status_t hush_agent_claim_job(hush_store_t *store, hush_agent_job_t *job);
/* Publishes an instruction-loading failure for the required job. */
static hush_status_t hush_agent_report_guidance(hush_store_t *store, const hush_agent_job_t *job);

static int hush_agent_intro_seen(const char *hex, const char *root);
static void hush_agent_intro_remember(const char *hex, const char *root);

void hush_agent_init(void)
{
    size_t i;

    for (i = 0; i < (size_t)HUSH_AGENT_JOBS_MAX; i++) {
        memset(&g_jobs[i], 0, sizeof(g_jobs[i]));
        g_jobs[i].fd = HUSH_AGENT_FD_NONE;
    }
    hush_agent_follow_init();
    hush_cevent_init();
    hush_presence_init();
    hush_wake_init();
}

void hush_agent_shutdown(void)
{
    size_t i;

    for (i = 0; i < (size_t)HUSH_AGENT_JOBS_MAX; i++) {
        if (!g_jobs[i].busy)
            continue;
        hush_agent_kill_job(&g_jobs[i]);
        hush_agent_close_job(&g_jobs[i]);
    }
}

void hush_agent_mention(hush_store_t *store, hush_launch_t *launch,
                        const hush_event_t *ev, const char *mention)
{
    hush_agent_handle_mention(store, launch, ev, mention);
}

void hush_agent_on_posted(hush_store_t *store, const hush_launch_t *launch,
                          const hush_event_t *ev)
{
    if (store == NULL || launch == NULL || ev == NULL)
        return;
    if (ev->kind != (uint32_t)HUSH_AGENT_KIND_NOTE)
        return;
    if (hush_agent_is_human(launch, ev->pubkey))
        return;
    hush_agent_follow_kick(store, launch, ev);
}

void hush_agent_status(char *out, size_t outsz)
{
    size_t i;
    size_t off;

    if (out == NULL || outsz < 3)
        return;
    out[0] = '[';
    out[1] = '\0';
    off = 1;
    for (i = 0; i < (size_t)HUSH_AGENT_JOBS_MAX && off + 8 < outsz; i++) {
        if (!g_jobs[i].busy)
            continue;
        if (!hush_agent_status_append(out, outsz, &off, &g_jobs[i]))
            break;
    }
    if (off + 1 < outsz) {
        out[off] = ']';
        out[off + 1] = '\0';
        return;
    }
    out[0] = '[';
    out[1] = ']';
    out[2] = '\0';
}

int hush_agent_channel_busy(const char *channel)
{
    int busy = 0;
    size_t i;

    if (channel == NULL || channel[0] == '\0')
        return 0;
    for (i = 0; i < (size_t)HUSH_AGENT_JOBS_MAX; ++i) {
        if (g_jobs[i].busy && strcmp(g_jobs[i].channel, channel) == 0)
            busy++;
    }
    return busy;
}

int hush_agent_jobs_active(void)
{
    int active = 0;
    size_t i;

    for (i = 0; i < (size_t)HUSH_AGENT_JOBS_MAX; ++i) {
        if (g_jobs[i].busy)
            active++;
    }
    return active;
}

void hush_agent_poll(hush_store_t *store)
{
    size_t i;
    time_t now;
    int status;

    now = time(NULL);
    if (store != NULL) {
        hush_presence_expire(store, now);
        hush_wake_expire(store, now);
    }
    for (i = 0; i < (size_t)HUSH_AGENT_JOBS_MAX; i++) {
        if (!g_jobs[i].busy)
            continue;
        if (g_jobs[i].kind == HUSH_AGENT_KIND_FIXUP) {
            hush_agent_read_job(&g_jobs[i]);
            if (g_jobs[i].pid > 0)
                (void)waitpid(g_jobs[i].pid, &status, WNOHANG);
            if (hush_agent_job_timed_out(&g_jobs[i], now)) {
                hush_agent_kill_job(&g_jobs[i]);
                hush_agent_finish_job(store, &g_jobs[i], 0);
            } else if (g_jobs[i].fd == HUSH_AGENT_FD_NONE)
                hush_agent_finish_job(store, &g_jobs[i], 1);
            continue;
        }
        if (!hush_agent_job_enabled(&g_jobs[i])) {
            hush_agent_kill_job(&g_jobs[i]);
            hush_agent_finish_job(store, &g_jobs[i], 0);
            continue;
        }
        hush_agent_read_job(&g_jobs[i]);
        if (g_jobs[i].out_n > 0)
            (void)hush_presence_beat(g_jobs[i].robot_pub, g_jobs[i].parent_id,
                                     now);
        if (g_jobs[i].pid > 0 &&
            waitpid(g_jobs[i].pid, &status, WNOHANG) == g_jobs[i].pid)
            g_jobs[i].pid = 0;
        if (g_jobs[i].cancelled && g_jobs[i].pid > 0 &&
            now >= g_jobs[i].kill_deadline) {
            (void)kill(-g_jobs[i].pid, SIGKILL);
            hush_agent_finish_job(store, &g_jobs[i], 0);
            continue;
        }
        if (store != NULL &&
            hush_presence_stall_s(g_jobs[i].robot_pub, g_jobs[i].parent_id, now)
                >= HUSH_PRESENCE_STALL_S &&
            strcmp(g_jobs[i].presence_slug, HUSH_PRESENCE_SLUG_STUCK) != 0)
            hush_agent_presence_put(store, &g_jobs[i],
                                    HUSH_PRESENCE_SLUG_STUCK);
        if (store != NULL &&
            hush_presence_stuck_due(g_jobs[i].robot_pub, g_jobs[i].parent_id,
                                    now)) {
            hush_agent_presence_put(store, &g_jobs[i],
                                    HUSH_PRESENCE_SLUG_STUCK);
            hush_agent_nudge_stuck(store, &g_jobs[i]);
        }
        if (hush_agent_job_timed_out(&g_jobs[i], now)) {
            hush_agent_kill_job(&g_jobs[i]);
            hush_agent_finish_job(store, &g_jobs[i], 0);
            continue;
        }
        if (g_jobs[i].fd == HUSH_AGENT_FD_NONE)
            hush_agent_finish_job(store, &g_jobs[i], 1);
    }
}

void hush_agent_copy(char *dst, size_t dstsz, const char *src)
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

void hush_agent_trim(char *text)
{
    size_t n;

    assert(text != NULL);
    n = strlen(text);
    while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r' ||
                     text[n - 1] == ' ' || text[n - 1] == '\t')) {
        text[n - 1] = '\0';
        n--;
    }
}

hush_agent_job_t *hush_agent_find_slot(void)
{
    size_t i;

    for (i = 0; i < (size_t)HUSH_AGENT_JOBS_MAX; i++) {
        if (g_jobs[i].busy)
            continue;
        if (g_jobs[i].kind == HUSH_AGENT_KIND_FIXUP &&
            g_jobs[i].token[0] != '\0')
            continue;
        return &g_jobs[i];
    }
    return NULL;
}

int hush_agent_key_matches(const char *mention, const char *npub,
                           const char *hex)
{
    if (mention == NULL || mention[0] == '\0')
        return 0;
    if (npub != NULL && npub[0] != '\0' && strcmp(mention, npub) == 0)
        return 1;
    if (hex != NULL && hex[0] != '\0' && strcmp(mention, hex) == 0)
        return 1;
    return 0;
}

int hush_agent_is_human(const hush_launch_t *launch,
                               const char *mention)
{
    assert(launch != NULL);
    if (!launch->logged_in)
        return 0;
    return hush_agent_key_matches(mention, launch->human.npub,
                                  launch->human.pubkey_hex);
}

int hush_agent_lookup_robot(hush_agent_robot_t *out,
                                   const hush_launch_t *launch, const char *mention)
{
    assert(out != NULL);
    assert(launch != NULL);
    memset(out, 0, sizeof(*out));
    if (launch->has_vibe && hush_agent_key_matches(mention, launch->payne.npub,
                                                   launch->payne.pubkey_hex)) {
        hush_agent_bind_payne(out, launch);
        return launch->payne_enabled;
    }
    for (size_t i = 0; i < launch->roster.nagents && i < (size_t)HUSH_ROSTER_AGENTS_MAX; ++i) {
        const hush_roster_agent_t *robot = &launch->roster.agents[i];
        if (!hush_agent_key_matches(mention, robot->id.npub, robot->id.pubkey_hex))
            continue;
        hush_agent_bind_roster(out, robot);
        return robot->enabled;
    }
    return 0;
}

static void hush_agent_bind_payne(hush_agent_robot_t *out, const hush_launch_t *launch)
{
    assert(out != NULL && launch != NULL);
    out->name = hush_launch_payne_name(launch);
    out->npub = launch->payne.npub;
    out->hex = launch->payne.pubkey_hex;
    out->provider = launch->npayne_providers > 0
        ? launch->payne_providers[0] : HUSH_ROSTER_PROVIDER_GOOSE;
    out->nproviders = launch->npayne_providers;
    for (size_t i = 0; i < out->nproviders && i < (size_t)HUSH_ROSTER_PROVIDERS_MAX; ++i)
        out->providers[i] = launch->payne_providers[i];
    out->prompt = hush_launch_payne_prompt(launch);
    out->skills = launch->payne_skills;
    out->nskills = launch->npayne_skills;
    out->slug = HUSH_LAUNCH_PAYNE_SLUG;
    out->role = HUSH_ROSTER_ROLE_WORKER;
    out->intro = HUSH_ROSTER_INTRO_DEFAULT;
    out->intro_enabled = 1;
}

static void hush_agent_bind_roster(hush_agent_robot_t *out, const hush_roster_agent_t *robot)
{
    assert(out != NULL && robot != NULL);
    out->name = robot->name;
    out->npub = robot->id.npub;
    out->hex = robot->id.pubkey_hex;
    out->provider = robot->provider;
    out->nproviders = robot->nproviders;
    for (size_t i = 0; i < out->nproviders && i < (size_t)HUSH_ROSTER_PROVIDERS_MAX; ++i)
        out->providers[i] = robot->providers[i];
    out->prompt = robot->prompt;
    out->skills = robot->skills;
    out->nskills = robot->nskills;
    out->slug = robot->slug;
    out->role = robot->role[0] ? robot->role : HUSH_ROSTER_ROLE_WORKER;
    out->intro = robot->intro[0] ? robot->intro : HUSH_ROSTER_INTRO_DEFAULT;
    out->intro_enabled = robot->intro_enabled;
    out->context = robot->context;
    out->ncontext = robot->ncontext;
}

void hush_agent_event_channel(char *out, size_t outsz,
                                     const hush_event_t *ev)
{
    size_t i;

    assert(out != NULL);
    assert(ev != NULL);
    hush_agent_copy(out, outsz, HUSH_AGENT_CHAN_FALLBACK);
    for (i = 0; i < ev->tag_count; i++) {
        if (strcmp(ev->tags[i][0], "h") == 0 && ev->tags[i][1][0] != '\0') {
            hush_agent_copy(out, outsz, ev->tags[i][1]);
            return;
        }
    }
}

void hush_agent_event_root(char *out, size_t outsz,
                                 const hush_event_t *ev)
{
    size_t i;

    assert(out != NULL);
    assert(ev != NULL);
    hush_agent_copy(out, outsz, ev->id);
    for (i = 0; i < ev->tag_count; i++) {
        if (strcmp(ev->tags[i][0], "e") == 0 && ev->tags[i][1][0] != '\0') {
            hush_agent_copy(out, outsz, ev->tags[i][1]);
            return;
        }
    }
}

void hush_agent_human_name(char *out, size_t outsz,
                                 const hush_launch_t *launch)
{
    const char *name;

    assert(out != NULL);
    name = HUSH_AGENT_HUMAN_FALLBACK;
    if (launch != NULL && launch->roster.profile.first_name[0] != '\0')
        name = launch->roster.profile.first_name;
    hush_agent_copy(out, outsz, name);
}

void hush_agent_prepare_cwd(char *out, size_t outsz)
{
    const char *cfg;
    const char *base;
    const char *leaf;
    int n;

    assert(out != NULL);
    assert(outsz > 0);
    out[0] = '\0';
    cfg = getenv(HUSH_AGENT_ENV_CONFIG);
    base = getenv("TMPDIR");
    leaf = HUSH_AGENT_CWD_TMP;
    if (base == NULL || base[0] == '\0')
        base = HUSH_AGENT_TMP_FALLBACK;
    if (cfg != NULL && cfg[0] != '\0') {
        base = cfg;
        leaf = HUSH_AGENT_CWD_LEAF;
    }
    n = snprintf(out, outsz, "%s/%s", base, leaf);
    if (n < 0 || (size_t)n >= outsz ||
        hush_dir_ensure_private(out) != HUSH_OK) {
        /* A pre-created or foreign path is not a safe cwd; use a per-process
         * private directory instead of trusting it. */
        n = snprintf(out, outsz, "%s/%s-%d", base, leaf, (int)getpid());
        if (n < 0 || (size_t)n >= outsz ||
            hush_dir_ensure_private(out) != HUSH_OK)
            out[0] = '\0';
    }
}

static int hush_agent_status_append(char *out, size_t outsz, size_t *off,
                                   const hush_agent_job_t *job)
{
    assert(out != NULL && off != NULL && *off < outsz);
    assert(job != NULL);
    if (job->kind == HUSH_AGENT_KIND_FIXUP)
        return 1;
    char name[HUSH_ROSTER_NAME_MAX * HUSH_JSON_U_LEN] = {0};
    (void)hush_json_escape(job->robot_name, name, sizeof(name));
    const char *stage = "replying";
    if (job->kind == HUSH_AGENT_KIND_PLAN) stage = "planning";
    if (job->kind == HUSH_AGENT_KIND_ELECT) stage = "choosing a lead";
    int written = snprintf(out + *off, outsz - *off,
        "%s{\"name\":\"%s\",\"parent\":\"%s\",\"slug\":\"%s\","
        "\"provider\":\"%s\",\"stage\":\"%s\"}", *off > 1 ? "," : "", name,
        job->parent_id, job->presence_slug, job->provider, stage);
    if (written < 0 || (size_t)written >= outsz - *off)
        return 0;
    *off += (size_t)written;
    return 1;
}

void hush_agent_fill_note(hush_event_t *ev, const hush_agent_note_in_t *in)
{
    assert(ev != NULL);
    assert(in != NULL);
    assert(in->pubkey != NULL);
    assert(in->content != NULL);
    assert(in->channel != NULL);
    memset(ev, 0, sizeof(*ev));
    hush_agent_copy(ev->pubkey, sizeof(ev->pubkey), in->pubkey);
    ev->kind = (uint32_t)HUSH_AGENT_KIND_NOTE;
    ev->created_at = (int64_t)time(NULL);
    hush_agent_copy(ev->content, sizeof(ev->content), in->content);
    ev->tag_count = 1;
    memcpy(ev->tags[0][0], "h", 2);
    hush_agent_copy(ev->tags[0][1], sizeof(ev->tags[0][1]), in->channel);
    if (in->parent_id != NULL && in->parent_id[0] != '\0' &&
        ev->tag_count < (size_t)HUSH_EVENT_MAX_TAGS) {
        memcpy(ev->tags[ev->tag_count][0], "e", 2);
        hush_agent_copy(ev->tags[ev->tag_count][1],
                        sizeof(ev->tags[ev->tag_count][1]), in->parent_id);
        ev->tag_count++;
    }
    if (in->human_pub != NULL && in->human_pub[0] != '\0' &&
        ev->tag_count < (size_t)HUSH_EVENT_MAX_TAGS) {
        memcpy(ev->tags[ev->tag_count][0], "p", 2);
        hush_agent_copy(ev->tags[ev->tag_count][1],
                        sizeof(ev->tags[ev->tag_count][1]), in->human_pub);
        ev->tag_count++;
    }
    /* Group seam: extra p-npubs (peer robots addressed by nostr: in reply).
     * These become real p-tags so the peer gets dispatch + acks. */
    for (int i = 0; i < 4 && in->extra_p[i] != NULL && ev->tag_count < (size_t)HUSH_EVENT_MAX_TAGS; ++i) {
        const char *np = in->extra_p[i];
        if (np[0] == '\0')
            continue;
        /* avoid dups with human */
        if (in->human_pub && strcmp(np, in->human_pub) == 0)
            continue;
        memcpy(ev->tags[ev->tag_count][0], "p", 2);
        hush_agent_copy(ev->tags[ev->tag_count][1],
                        sizeof(ev->tags[ev->tag_count][1]), np);
        ev->tag_count++;
    }
    (void)hush_event_compute_id(ev, ev->id);
}

hush_status_t hush_agent_insert_note(hush_store_t *store,
                                            const hush_agent_note_in_t *in)
{
    hush_event_t ev;
    hush_status_t status;

    assert(store != NULL);
    hush_agent_fill_note(&ev, in);
    status = hush_store_insert(store, &ev);
    if (status == HUSH_OK)
        hush_thread_record(&ev);
    return status;
}

static int hush_agent_intro_seen(const char *hex, const char *root)
{
    return hush_wake_intro_seen(hex, root);
}

static void hush_agent_intro_remember(const char *hex, const char *root)
{
    hush_wake_in_t in;

    if (hex == NULL || root == NULL)
        return;
    memset(&in, 0, sizeof(in));
    in.robot_hex = hex;
    in.root_hex = root;
    (void)hush_wake_mark_intro(&in);
}

void hush_agent_on_deck(hush_store_t *store, const hush_agent_robot_t *bot,
                               const hush_event_t *parent, const char *why)
{
    char content[HUSH_EVENT_MAX_CONTENT];
    char channel[HUSH_EVENT_MAX_TAG_LEN + 1];
    const char *name;
    const char *line;
    char root[HUSH_EVENT_ID_HEX_LEN + 1];

    assert(store != NULL);
    assert(bot != NULL);
    assert(parent != NULL);
    name = (bot->name != NULL && bot->name[0] != '\0') ? bot->name : "robot";
    if (!bot->intro_enabled)
        return;
    if (bot->intro != NULL && bot->intro[0] != '\0')
        line = bot->intro;
    else if (why != NULL && why[0] != '\0')
        line = why;
    else
        line = HUSH_ROSTER_INTRO_DEFAULT;

    /* One intro per (robot hex, thread root). Table, not a single last-pair. */
    hush_agent_event_root(root, sizeof(root), parent);
    if (hush_agent_intro_seen(bot->hex, root))
        return;
    hush_agent_intro_remember(bot->hex, root);

    if (snprintf(content, sizeof(content),
                 "%s %s — %s", HUSH_AGENT_INTRO_PREFIX, line, name)
        >= (int)sizeof(content))
        hush_agent_copy(content, sizeof(content), line);
    hush_agent_event_channel(channel, sizeof(channel), parent);
    {
        hush_agent_note_in_t in;

        memset(&in, 0, sizeof(in));
        in.pubkey = bot->hex != NULL ? bot->hex : "";
        in.content = content;
        in.channel = channel;
        in.parent_id = root;
        in.human_pub = parent->pubkey;
        (void)hush_agent_insert_note(store, &in);
    }
}

/* Posts one diagnostic note when a mentioned robot cannot run a turn because
 * its runtime has no execution path. Prevents the silent intro-only no-op. */
void hush_agent_note_no_runtime(hush_store_t *store,
                                       const hush_agent_robot_t *bot,
                                       const hush_event_t *parent)
{
    char content[HUSH_EVENT_MAX_CONTENT];
    char channel[HUSH_EVENT_MAX_TAG_LEN + 1];
    char root[HUSH_EVENT_ID_HEX_LEN + 1];
    const char *name;

    assert(store != NULL);
    assert(bot != NULL);
    assert(parent != NULL);
    name = (bot->name != NULL && bot->name[0] != '\0') ? bot->name : "robot";
    hush_agent_event_root(root, sizeof(root), parent);
    hush_agent_event_channel(channel, sizeof(channel), parent);
    if (snprintf(content, sizeof(content),
                 "No selected provider is ready for %s. Open Configure Providers to check its harness login or API model and credentials.",
                 name) >= (int)sizeof(content))
        hush_agent_copy(content, sizeof(content), "No runtime available.");
    {
        hush_agent_note_in_t in;

        memset(&in, 0, sizeof(in));
        in.pubkey = bot->hex != NULL ? bot->hex : "";
        in.content = content;
        in.channel = channel;
        in.parent_id = root;
        in.human_pub = parent->pubkey;
        (void)hush_agent_insert_note(store, &in);
    }
}

void hush_agent_note_start_failed(hush_store_t *store,
                                         const hush_agent_robot_t *bot,
                                         const hush_event_t *parent)
{
    char content[HUSH_EVENT_MAX_CONTENT];
    char channel[HUSH_EVENT_MAX_TAG_LEN + 1];
    char root[HUSH_EVENT_ID_HEX_LEN + 1];
    const char *name;

    assert(store != NULL);
    assert(bot != NULL);
    assert(parent != NULL);
    name = (bot->name != NULL && bot->name[0] != '\0') ? bot->name : "robot";
    hush_agent_event_root(root, sizeof(root), parent);
    hush_agent_event_channel(channel, sizeof(channel), parent);
    if (snprintf(content, sizeof(content),
                 "%s could not start a turn: every job slot is busy. "
                 "Wait for a reply and ask again.", name) >=
        (int)sizeof(content))
        hush_agent_copy(content, sizeof(content),
                        "No free job slot; try again shortly.");
    {
        hush_agent_note_in_t note;

        memset(&note, 0, sizeof(note));
        note.pubkey = bot->hex != NULL ? bot->hex : "";
        note.content = content;
        note.channel = channel;
        note.parent_id = root;
        note.human_pub = parent->pubkey;
        (void)hush_agent_insert_note(store, &note);
    }
}

int hush_agent_grok_ready(void)
{
    hush_provider_status_t st;
    unsigned int flags;

    if (hush_provider_status(&st, HUSH_ROSTER_PROVIDER_GROK_BUILD) != HUSH_OK)
        return 0;
    if (!st.has_binary)
        return 0;
    /* OAUTH providers must be logged in (home config) before dispatch. The
     * gate is driven by the policy-flag table, not a hardcoded name. */
    flags = hush_provider_flags(HUSH_ROSTER_PROVIDER_GROK_BUILD);
    if ((flags & HUSH_PROVIDER_FLAG_OAUTH) && !st.has_home)
        return 0;
    return 1;
}

static int hush_agent_runtime_ready(const char *provider)
{
    if (provider == NULL || provider[0] == '\0')
        return 0;
    hush_provider_status_t status = {0};
    if (hush_provider_status(&status, provider) != HUSH_OK)
        return 0;
    return hush_provider_ready(&status);
}

/* Returns the first ready provider in the robot's ranked list, else NULL. */
const char *hush_agent_pick_provider(const hush_agent_robot_t *bot)
{
    size_t i;

    if (bot == NULL)
        return NULL;
    for (i = 0; i < bot->nproviders &&
                i < (size_t)HUSH_ROSTER_PROVIDERS_MAX; i++) {
        if (bot->providers[i] != NULL &&
            hush_agent_runtime_ready(bot->providers[i]))
            return bot->providers[i];
    }
    if (bot->provider != NULL && hush_agent_runtime_ready(bot->provider))
        return bot->provider;
    return NULL;
}

int hush_agent_can_start(const hush_launch_t *launch,
                                const hush_agent_robot_t *bot)
{
    (void)launch;
    assert(bot != NULL);
    return hush_agent_pick_provider(bot) != NULL;
}

int hush_agent_event_is_root(const hush_event_t *ev, const char *root)
{
    size_t i;

    assert(ev != NULL);
    assert(root != NULL);
    if (strcmp(ev->id, root) == 0)
        return 1;
    for (i = 0; i < ev->tag_count; i++) {
        if (strcmp(ev->tags[i][0], "e") == 0 &&
            strcmp(ev->tags[i][1], root) == 0)
            return 1;
    }
    return 0;
}

void hush_agent_append_turn(char *out, size_t outsz,
                                  const hush_event_t *ev, const char *who)
{
    char line[HUSH_AGENT_SNIP_MAX + HUSH_IDENTITY_NPUB_MAX + 1];
    size_t used;

    assert(out != NULL);
    assert(ev != NULL);
    assert(who != NULL);
    hush_agent_snip_line(line, sizeof(line), ev->content);
    if (hush_agent_complete_snippet(line, sizeof(line)) != HUSH_OK)
        return;
    used = strlen(out);
    if (used + 8 >= outsz)
        return;
    (void)snprintf(out + used, outsz - used, "%s: %s\n", who, line);
}

hush_status_t hush_agent_start_grok(const hush_agent_job_in_t *in)
{
    assert(in != NULL && in->bot != NULL && in->parent != NULL);
    hush_agent_job_t *job = hush_agent_find_slot();
    if (job == NULL)
        return HUSH_ERR_FULL;
    hush_status_t status = hush_agent_fill_job(job, in);
    if (status == HUSH_OK)
        status = hush_agent_add_guidance(job, in->bot);
    if (status != HUSH_OK) {
        job->busy = 0;
        HUSH_TRY(hush_agent_report_guidance(in->store, job));
        return status;
    }
    status = hush_agent_claim_job(in->store, job);
    if (status == HUSH_OK)
        status = hush_agent_spawn_grok(job);
    if (status != HUSH_OK) {
        hush_agent_release_line(in->store, job);
        hush_agent_close_job(job);
        return status;
    }
    hush_agent_presence_put(in->store, job, HUSH_PRESENCE_SLUG_WORKING);
    return HUSH_OK;
}

static hush_status_t hush_agent_claim_job(hush_store_t *store, hush_agent_job_t *job)
{
    assert(store != NULL && job != NULL);
    hush_wake_in_t wake = {.store = store, .robot_hex = job->robot_pub,
        .root_hex = job->parent_id, .trigger_id = job->trigger_id,
        .channel = job->channel, .now = job->started};
    return hush_wake_claim(&wake);
}

static hush_status_t hush_agent_report_guidance(hush_store_t *store, const hush_agent_job_t *job)
{
    assert(store != NULL && job != NULL);
    hush_agent_note_in_t notice = {.pubkey = job->robot_pub,
        .content = "Could not load the assigned skills or room guidance. Review this robot's skills before retrying.",
        .channel = job->channel, .parent_id = job->parent_id, .human_pub = job->human_pub};
    return hush_agent_insert_note(store, &notice);
}

void hush_agent_close_job(hush_agent_job_t *job)
{
    assert(job != NULL);
    if (job->fd >= 0)
        close(job->fd);
    job->fd = HUSH_AGENT_FD_NONE;
    job->pid = 0;
    job->busy = 0;
    job->cancelled = 0;
    job->kill_deadline = 0;
}

static void hush_agent_kill_job(hush_agent_job_t *job)
{
    int status;

    assert(job != NULL);
    if (job->pid > 1) {
        (void)kill(-job->pid, SIGTERM);
        (void)waitpid(job->pid, &status, WNOHANG);
    }
}

/* True when job is the live non-fixup job for root and the robot selector. */
static int hush_agent_job_matches(const hush_agent_job_t *job, const char *root,
                                  const char *robot)
{
    assert(job != NULL);
    assert(root != NULL);
    assert(robot != NULL);
    if (strcmp(job->parent_id, root) != 0)
        return 0;
    return strcmp(job->robot_pub, robot) == 0 ||
           strcmp(job->robot_name, robot) == 0;
}

hush_status_t hush_agent_cancel(const char *root, const char *robot)
{
    size_t i;

    if (root == NULL || robot == NULL || root[0] == '\0' || robot[0] == '\0')
        return HUSH_ERR_ARG;
    for (i = 0; i < (size_t)HUSH_AGENT_JOBS_MAX; ++i) {
        hush_agent_job_t *job = &g_jobs[i];

        if (!job->busy || job->kind == HUSH_AGENT_KIND_FIXUP)
            continue;
        if (!hush_agent_job_matches(job, root, robot))
            continue;
        job->cancelled = 1;
        job->kill_deadline = time(NULL) + HUSH_AGENT_CANCEL_GRACE_S;
        hush_agent_kill_job(job);
        return HUSH_OK;
    }
    return HUSH_ERR_NOT_FOUND;
}

hush_status_t hush_agent_partial(char *out, size_t outsz, const char *root,
                                 const char *robot)
{
    size_t i;

    if (out == NULL || outsz == 0 || root == NULL || robot == NULL)
        return HUSH_ERR_ARG;
    out[0] = '\0';
    for (i = 0; i < (size_t)HUSH_AGENT_JOBS_MAX; ++i) {
        const hush_agent_job_t *job = &g_jobs[i];

        if (!job->busy || job->kind == HUSH_AGENT_KIND_FIXUP)
            continue;
        if (!hush_agent_job_matches(job, root, robot))
            continue;
        hush_agent_copy(out, outsz, job->out);
        return HUSH_OK;
    }
    return HUSH_ERR_NOT_FOUND;
}

