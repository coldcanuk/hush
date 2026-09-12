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
#define HUSH_AGENT_PROMPT_FALLBACK \
    "You are a robot in the Hush hive. Fulfill the last human ask (only your part) in one note."
#define HUSH_AGENT_ONE_JOKE \
    "If the last human ask is a joke, reply with exactly one joke."
#define HUSH_AGENT_PEER_STANDARD \
    " Inter-robot standard: do not copy the human's mention list and " \
    "do not repeat the original ask. Write only your assignment. " \
    "A non-last robot may add \"your turn, @Name\" after the work. " \
    "Never write npub keys or nostr: tokens. Do not mention yourself."
#define HUSH_AGENT_HOFF_TURN "your turn"
#define HUSH_AGENT_HOFF_NEXT "take the next turn"
#define HUSH_AGENT_HOFF_HERE "take it from here"
#define HUSH_AGENT_HOFF_CONT "continue the thread"
#define HUSH_AGENT_HOFF_DONE "riddle answered"
#define HUSH_AGENT_HOFF_GEN "generate a new riddle"
#define HUSH_AGENT_LAST_RULE \
    "You are last. Stop after your assignment. Do not hand off. "
#define HUSH_AGENT_PEER_LINE " Peers: "
#define HUSH_AGENT_AT_NPUB "@npub1"
#define HUSH_AGENT_NOSTR_HEAD "nostr:"
#define HUSH_AGENT_NOSTR_NPUB "nostr:npub1"
#define HUSH_AGENT_NPUB_HEAD "npub1"
#define HUSH_AGENT_HYGIENE \
    " Fulfill YOUR assignment in this note, not a peer's. " \
    "STOP immediately after your part is done. Do not answer questions or perform actions assigned to a peer. " \
    "If peers are mentioned, they take their turn automatically after you stop. " \
    "Do not mention yourself. Do not hand off by appending a bare mention. " \
    "Never end your note with a bare mention. Include any asked code. " \
    "No preamble-only replies. " \
    HUSH_AGENT_ONE_JOKE HUSH_AGENT_PEER_STANDARD
#define HUSH_AGENT_STRICT_SCOPE \
    " Do ONLY the assignment given to you. Do not perform, answer, or " \
    "complete any part assigned to another robot. Ignore other robots' jobs."
#define HUSH_AGENT_COOPERATE \
    " You are a pair. Divide the labor between you two: each does a distinct, " \
    "non-overlapping part of the ask. Do not duplicate your partner's part. " \
    "Do only your own part and stop."
#define HUSH_AGENT_LEADER_PROMPT \
    " You are the leader. Organize the other robots: assign each other robot " \
    "exactly one sub-task, then choose how to run them. Reply with ONE fenced " \
    "block and nothing else:\n" \
    "```plan\n" \
    "order: fifo\n" \
    "1 Happy: generate a riddle\n" \
    "2 Major: answer it\n" \
    "2 Scout: verify it\n" \
    "3 Builder: write a summary\n" \
    "```\n" \
    "order is fifo or lifo. Prefix each task with an integer wave number. " \
    "Tasks sharing a wave number run in parallel; waves run in order. A task " \
    "with no other task in its wave runs alone. Use parallel only when tasks " \
    "are truly independent. Name each robot by its exact display name."
#define HUSH_AGENT_RULES \
    "Fulfill YOUR assignment as the named robot. Do not mention yourself. " \
    "Include asked code. Address the human by first name. No tools. " \
    HUSH_AGENT_ONE_JOKE HUSH_AGENT_PEER_STANDARD
#define HUSH_AGENT_HUMAN_FALLBACK "you"
#define HUSH_AGENT_FIXUP_PROMPT \
    "Rewrite only the given text per the instruction. " \
    "Return only the rewritten text. No fences. No preamble."
#define HUSH_AGENT_FIXUP_RULES \
    "Return only the rewritten selection. No markdown fences. No chatter."
#define HUSH_AGENT_FIXUP_HEAD "Instruction:\n"
#define HUSH_AGENT_FIXUP_MID "\n\nText:\n"


static hush_agent_job_t g_jobs[HUSH_AGENT_JOBS_MAX];

static unsigned g_id_seq;

hush_agent_job_t *hush_agent_jobs(void)
{
    return g_jobs;
}

void hush_agent_copy(char *dst, size_t dstsz, const char *src);
void hush_agent_trim(char *text);
static void hush_agent_fill_fixup(hush_agent_job_t *job, const char *instruction,
                                            const char *text);
static hush_agent_job_t *hush_agent_find_slot(void);
static int hush_agent_key_matches(const char *mention, const char *npub,
                                  const char *hex);
#define HUSH_AGENT_ENV_CONFIG "HUSH_CONFIG_DIR"
#define HUSH_AGENT_CWD_LEAF "agent-cwd"
#define HUSH_AGENT_CWD_TMP "hush-agent-cwd"
#define HUSH_AGENT_TMP_FALLBACK "/tmp"

/* Binds required runtime view to Major's launch-owned fields. */
static void hush_agent_bind_payne(hush_agent_robot_t *out, const hush_launch_t *launch);
/* Binds required runtime view to roster-owned fields. */
static void hush_agent_bind_roster(hush_agent_robot_t *out, const hush_roster_agent_t *robot);
/* Initializes required job metadata from the initiating event. */
static void hush_agent_init_job(hush_agent_job_t *job, const hush_agent_job_in_t *in);
/* Binds required job identity to the selected robot and original human creator. */
static void hush_agent_bind_job(hush_agent_job_t *job, const hush_agent_job_in_t *in);
/* Collects required job's mentioned peers in tag order, excluding self/humans. */
static void hush_agent_collect_job_peers(hush_agent_job_t *job, const hush_agent_job_in_t *in);
/* Appends one eligible peer to required bounded job storage. */
static void hush_agent_add_job_peer(hush_agent_job_t *job, const hush_agent_job_in_t *in,
                                    const char *key);
/* Fills the required planner directive using bounded peer names. */
static hush_status_t hush_agent_fill_leader(hush_agent_job_t *job);
/* Fills the required worker's scoped instructions using existing bounded renderers. */
static void hush_agent_fill_worker(hush_agent_job_t *job, const hush_agent_job_in_t *in);
/* Selects required job's election, planning, or work directive. */
static hush_status_t hush_agent_fill_directive(hush_agent_job_t *job,
                                               const hush_agent_job_in_t *in);
/* Builds required job's current request with recent conversation/file context. */
static void hush_agent_fill_job_note(hush_agent_job_t *job, const hush_agent_job_in_t *in);
int hush_agent_lookup_robot(hush_agent_robot_t *out,
                                   const hush_launch_t *launch,
                                   const char *mention);
void hush_agent_event_channel(char *out, size_t outsz,
                                     const hush_event_t *ev);
void hush_agent_event_root(char *out, size_t outsz,
                                  const hush_event_t *ev);
void hush_agent_human_name(char *out, size_t outsz,
                                  const hush_launch_t *launch);
static void hush_agent_fill_prompt(char *out, size_t outsz,
                                   const hush_agent_robot_t *bot,
                                   const char *human);
static void hush_agent_fill_rules(char *out, size_t outsz, const char *human);
void hush_agent_prepare_cwd(char *out, size_t outsz);
static int hush_agent_status_append(char *out, size_t outsz, size_t *off,
                                    const hush_agent_job_t *job);
static int hush_agent_grok_ready(void);
static int hush_agent_runtime_ready(const char *provider);
/* Adds required room/skill guidance to a prepared job. Borrowed pointers;
 * propagates missing skill, malformed content, or prompt capacity errors. */
static hush_status_t hush_agent_add_guidance(hush_agent_job_t *job,
                                             const hush_agent_robot_t *robot);
/* Appends a named instruction to required job storage; FULL on truncation. */
static hush_status_t hush_agent_add_instruction(hush_agent_job_t *job, const char *label,
                                                const char *text);
static hush_status_t hush_agent_fill_job(hush_agent_job_t *job,
                                const hush_agent_job_in_t *in);
int hush_agent_event_is_root(const hush_event_t *ev, const char *root);
/* True when ch is a bech32/npub body character [0-9a-z]. Pure. */
static int hush_agent_is_npub_char(char ch);
/* Length of a nostr:npub1… or npub1… token starting at src[i], else 0. */
static size_t hush_agent_npub_span(const char *src, size_t i);
/* Writes one space at o when room remains. Returns the next index. */
static size_t hush_agent_put_gap(char *out, size_t o, size_t cap);
/* Copies src into out, collapsing whitespace to one space. Soft-capped
 * at SNIP_MAX; npub tokens are copied whole up to outsz. */
void hush_agent_snip_line(char *out, size_t outsz, const char *src);
/* Strips self mentions, echoed ask, handoff phrases; last drops peers. */
static void hush_agent_scrub_reply(hush_agent_job_t *job);
/* Removes nostr:<own npub> and @OwnName from job->out. */
static void hush_agent_drop_self(hush_agent_job_t *job);
/* Removes sentences that contain a long substring of job->ask. */
static void hush_agent_drop_echo(hush_agent_job_t *job);
/* Removes known handoff phrases from job->out. */
static void hush_agent_drop_handoff(char *text);
/* Removes remaining nostr:npub tokens. Used when the robot is last. */
static void hush_agent_drop_npubs(char *text, size_t textsz);
/* Collapses whitespace and trailing junk punctuation. */
static void hush_agent_tidy_reply(char *text);
/* Cuts needle (case-insensitive) plus following junk from text. */
static void hush_agent_cut_ci(char *text, const char *needle);
/* Replaces @npub1 with nostr:npub1 in place. text is a writable C string. */
static void hush_agent_rewrite_at_npub(char *text, size_t textsz);
/* Expands truncated nostr:npub1 tokens to the unique roster npub. */
static void hush_agent_expand_npubs(char *text, size_t textsz,
                                    const hush_launch_t *launch);
/* Replaces @Name with nostr:<npub> for roster display names, longest first. */
static void hush_agent_rewrite_at_names(char *text, size_t textsz,
                                        const hush_launch_t *launch);
/* Fills out with Payne then enabled agents. Returns the count. */
static size_t hush_agent_list_aliases(const hush_launch_t *launch,
                                      hush_agent_alias_t *out, size_t maxn);
/* Sorts aliases longest-name-first. n is the live count. */
static void hush_agent_sort_aliases(hush_agent_alias_t *aliases, size_t n);
/* Writes the unique full npub for tok (exact or prefix). Returns 0 if none. */
static int hush_agent_unique_npub(const hush_launch_t *launch,
                                  const char *tok, char *out, size_t outsz);
/* Copies src into dst, mapping @Name from set to nostr:<npub>. */
static void hush_agent_emit_at_names(char *dst, size_t dstsz, const char *src,
                                     const hush_agent_alias_set_t *set);
/* Appends " Peers: @Name …" onto job->prompt. No-op when none. */
static void hush_agent_append_peers(hush_agent_job_t *job);
/* Prepends LAST_RULE to prompt (and appends to rules) when job->last. */
static void hush_agent_append_last(hush_agent_job_t *job);
/* True when a[0..n) equals b[0..n) ignoring ASCII case. Pure. */
static int hush_agent_is_same_ascii(const char *a, const char *b, size_t n);
/* True when ch cannot continue a display name. Pure. */
static int hush_agent_is_name_end(char ch);
/* True when tok is npub or a unique prefix of npub (min NPUB_MIN). Pure. */
static int hush_agent_npub_prefix_hit(const char *tok, const char *npub);
/* Writes nostr:<npub> at dst[o]. Returns the next index, or cap on overflow. */
static size_t hush_agent_put_full_npub(char *dst, size_t o, size_t cap,
                                       const char *npub);
/* Index of the longest @Name match at src, or set->naliases when none. */
static size_t hush_agent_alias_at(const char *src,
                                  const hush_agent_alias_set_t *set);
/* Removes only an incomplete trailing UTF-8 scalar from required bounded snippet. */
static hush_status_t hush_agent_complete_snippet(char *text, size_t capacity);
void hush_agent_append_turn(char *out, size_t outsz,
                                   const hush_event_t *ev, const char *who);
/* Collects the latest bounded conversation notes, excluding the current trigger.
 * Required borrowed store, root, trigger and fixed output. */
/* Appends to the required six-event window, dropping its oldest note when full. */
static void hush_agent_make_token(char *out, size_t outsz);
static hush_agent_job_t *hush_agent_find_token(const char *token);
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

hush_status_t hush_agent_start_fixup(char *token, size_t tokensz,
                                     const char *instruction,
                                     const char *text)
{
    hush_agent_job_t *job;

    if (token == NULL || tokensz < 2)
        return HUSH_ERR_ARG;
    if (!hush_agent_grok_ready())
        return HUSH_ERR_IO;
    job = hush_agent_find_slot();
    if (job == NULL)
        return HUSH_ERR_FULL;
    hush_agent_fill_fixup(job, instruction, text);
    if (hush_agent_spawn_grok(job) != HUSH_OK) {
        job->busy = 0;
        return HUSH_ERR_IO;
    }
    hush_agent_copy(token, tokensz, job->token);
    return HUSH_OK;
}

hush_status_t hush_agent_take_fixup(const char *token, char *out, size_t outsz)
{
    hush_agent_job_t *job;

    if (token == NULL || token[0] == '\0' || out == NULL || outsz == 0)
        return HUSH_ERR_ARG;
    out[0] = '\0';
    job = hush_agent_find_token(token);
    if (job == NULL)
        return HUSH_ERR_NOT_FOUND;
    if (job->busy)
        return HUSH_ERR_NOT_FOUND;
    if (!job->ok || job->out[0] == '\0') {
        hush_agent_close_job(job);
        return HUSH_ERR_IO;
    }
    hush_agent_copy(out, outsz, job->out);
    hush_agent_close_job(job);
    return HUSH_OK;
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

static hush_agent_job_t *hush_agent_find_slot(void)
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

static int hush_agent_key_matches(const char *mention, const char *npub,
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

static void hush_agent_fill_prompt(char *out, size_t outsz,
                                  const hush_agent_robot_t *bot,
                                  const char *human)
{
    const char *base;
    const char *who;
    const char *self;
    int n;

    assert(out != NULL);
    assert(outsz > 0);
    assert(bot != NULL);
    base = (bot->prompt != NULL && bot->prompt[0] != '\0')
        ? bot->prompt : HUSH_AGENT_PROMPT_FALLBACK;
    who = (human != NULL && human[0] != '\0') ? human : HUSH_AGENT_HUMAN_FALLBACK;
    self = (bot->name != NULL && bot->name[0] != '\0') ? bot->name : NULL;
    if (self != NULL)
        n = snprintf(out, outsz, "%s You are %s. You are speaking to %s.%s",
                     base, self, who, HUSH_AGENT_HYGIENE);
    else
        n = snprintf(out, outsz, "%s You are speaking to %s.%s",
                     base, who, HUSH_AGENT_HYGIENE);
    if (n < 0 || (size_t)n >= outsz)
        hush_agent_copy(out, outsz, HUSH_AGENT_PROMPT_FALLBACK);
}

static void hush_agent_fill_rules(char *out, size_t outsz, const char *human)
{
    const char *who;
    int n;

    assert(out != NULL);
    assert(outsz > 0);
    who = (human != NULL && human[0] != '\0') ? human : HUSH_AGENT_HUMAN_FALLBACK;
    n = snprintf(out, outsz, "%s Speak to %s.", HUSH_AGENT_RULES, who);
    if (n < 0 || (size_t)n >= outsz)
        hush_agent_copy(out, outsz, HUSH_AGENT_RULES);
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

static int hush_agent_grok_ready(void)
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
static const char *hush_agent_pick_provider(const hush_agent_robot_t *bot)
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

int hush_agent_is_space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

static int hush_agent_is_npub_char(char ch)
{
    if (ch >= '0' && ch <= '9')
        return 1;
    if (ch >= 'a' && ch <= 'z')
        return 1;
    return 0;
}

static size_t hush_agent_npub_span(const char *src, size_t i)
{
    size_t n;

    assert(src != NULL);
    if (strncmp(src + i, HUSH_AGENT_NOSTR_NPUB,
                (size_t)HUSH_AGENT_NOSTR_NPUB_LEN) == 0)
        n = (size_t)HUSH_AGENT_NOSTR_NPUB_LEN;
    else if (strncmp(src + i, HUSH_AGENT_NPUB_HEAD,
                     (size_t)HUSH_AGENT_NPUB_HEAD_LEN) == 0) {
        if (i > 0 && hush_agent_is_npub_char(src[i - 1]))
            return 0;
        n = (size_t)HUSH_AGENT_NPUB_HEAD_LEN;
    } else {
        return 0;
    }
    while (src[i + n] != '\0' && hush_agent_is_npub_char(src[i + n]) &&
           i + n < (size_t)HUSH_EVENT_MAX_CONTENT)
        n++;
    return n;
}

static size_t hush_agent_put_gap(char *out, size_t o, size_t cap)
{
    assert(out != NULL);
    if (o >= cap)
        return o;
    out[o] = ' ';
    return o + 1;
}

void hush_agent_snip_line(char *out, size_t outsz, const char *src)
{
    size_t i;
    size_t o;
    size_t hard;
    size_t soft;
    size_t span;
    int gap;

    assert(out != NULL);
    assert(outsz > 0);
    if (src == NULL)
        src = "";
    hard = outsz - 1;
    soft = hard;
    if (soft > (size_t)HUSH_AGENT_SNIP_MAX)
        soft = (size_t)HUSH_AGENT_SNIP_MAX;
    o = 0;
    gap = 0;
    for (i = 0; src[i] != '\0' && i < (size_t)HUSH_EVENT_MAX_CONTENT; i++) {
        if (hush_agent_is_space(src[i])) {
            gap = 1;
            continue;
        }
        span = hush_agent_npub_span(src, i);
        if (span == 0 && o >= soft)
            break;
        if (gap && o > 0)
            o = hush_agent_put_gap(out, o, hard);
        if (span > 0) {
            if (o + span > hard)
                break;
            memcpy(out + o, src + i, span);
            o += span;
            i += span - 1;
            gap = 0;
            continue;
        }
        if (o >= hard)
            break;
        out[o] = src[i];
        o++;
        gap = 0;
    }
    out[o] = '\0';
}

static hush_status_t hush_agent_complete_snippet(char *text, size_t capacity)
{
    assert(text != NULL && capacity > 0);
    size_t len = strlen(text);
    for (size_t i = 0; i < (size_t)HUSH_JSON_UTF8_MAX; ++i) {
        size_t characters = 0;
        hush_status_t status = hush_json_count_chars(&characters, text, capacity);
        if (status == HUSH_OK) return HUSH_OK;
        if (status != HUSH_ERR_PARSE || len == 0) return status;
        text[--len] = '\0';
    }
    return HUSH_ERR_PARSE;
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

static hush_status_t hush_agent_fill_job(hush_agent_job_t *job,
                                         const hush_agent_job_in_t *in)
{
    assert(job != NULL && in != NULL);
    assert(in->bot != NULL && in->parent != NULL);
    hush_agent_init_job(job, in);
    hush_agent_bind_job(job, in);
    hush_agent_collect_job_peers(job, in);
    HUSH_TRY(hush_agent_fill_directive(job, in));
    hush_agent_fill_rules(job->rules, sizeof(job->rules), job->human_name);
    hush_agent_append_last(job);
    hush_agent_prepare_cwd(job->cwd, sizeof(job->cwd));
    hush_agent_fill_job_note(job, in);
    return HUSH_OK;
}

static void hush_agent_init_job(hush_agent_job_t *job, const hush_agent_job_in_t *in)
{
    assert(job != NULL && in != NULL);
    const hush_event_t *parent = in->parent;
    memset(job, 0, sizeof(*job));
    job->fd = HUSH_AGENT_FD_NONE;
    job->busy = 1;
    job->kind = HUSH_AGENT_KIND_NOTE_JOB;
    job->started = time(NULL);
    job->launch = in->launch;
    job->last = in->last;
    hush_agent_event_root(job->parent_id, sizeof(job->parent_id), parent);
    hush_agent_copy(job->trigger_id, sizeof(job->trigger_id),
                    parent->id[0] ? parent->id : job->parent_id);
    hush_agent_event_channel(job->channel, sizeof(job->channel), parent);
    hush_agent_copy(job->ask, sizeof(job->ask),
                    in->ask != NULL && in->ask[0] ? in->ask : parent->content);
    hush_agent_make_token(job->token, sizeof(job->token));
}

static void hush_agent_bind_job(hush_agent_job_t *job, const hush_agent_job_in_t *in)
{
    assert(job != NULL && in != NULL);
    const hush_agent_robot_t *robot = in->bot;
    hush_event_t original = {0};
    const char *human = in->parent->pubkey;
    if (hush_store_find(in->store, &original, job->parent_id) == HUSH_OK)
        human = original.pubkey;
    hush_agent_copy(job->human_pub, sizeof(job->human_pub), human);
    hush_agent_copy(job->robot_pub, sizeof(job->robot_pub), robot->hex);
    hush_agent_copy(job->robot_name, sizeof(job->robot_name), robot->name);
    hush_agent_copy(job->robot_role, sizeof(job->robot_role), robot->role);
    hush_agent_copy(job->provider, sizeof(job->provider), hush_agent_pick_provider(robot));
    hush_agent_human_name(job->human_name, sizeof(job->human_name), in->launch);
}

static void hush_agent_collect_job_peers(hush_agent_job_t *job, const hush_agent_job_in_t *in)
{
    assert(job != NULL && in != NULL);
    const hush_event_t *parent = in->parent;
    for (size_t i = 0; i < parent->tag_count && i < (size_t)HUSH_EVENT_MAX_TAGS; ++i) {
        if (strcmp(parent->tags[i][0], "p") == 0)
            hush_agent_add_job_peer(job, in, parent->tags[i][1]);
    }
}

static void hush_agent_add_job_peer(hush_agent_job_t *job, const hush_agent_job_in_t *in,
                                    const char *key)
{
    assert(job != NULL && in != NULL && key != NULL);
    if ((size_t)job->n_co_robots >= sizeof(job->co_names) / sizeof(job->co_names[0]))
        return;
    if (hush_agent_key_matches(key, in->bot->npub, in->bot->hex) ||
        hush_agent_is_human(in->launch, key))
        return;
    hush_agent_robot_t peer = {0};
    if (!hush_agent_lookup_robot(&peer, in->launch, key))
        return;
    hush_agent_copy(job->co_npubs[job->n_co_robots], sizeof(job->co_npubs[0]), key);
    hush_agent_copy(job->co_names[job->n_co_robots], sizeof(job->co_names[0]), peer.name);
    ++job->n_co_robots;
}

static hush_status_t hush_agent_fill_leader(hush_agent_job_t *job)
{
    assert(job != NULL);
    job->kind = HUSH_AGENT_KIND_PLAN;
    hush_agent_copy(job->prompt, sizeof(job->prompt), HUSH_AGENT_LEADER_PROMPT);
    for (size_t i = 0; i < (size_t)job->n_co_robots && i < (size_t)HUSH_AGENT_FOLLOW_ROBOTS; ++i)
        HUSH_TRY(hush_agent_add_instruction(job, "Other robot", job->co_names[i]));
    return hush_agent_add_instruction(job, "The ask", job->ask);
}

static void hush_agent_fill_worker(hush_agent_job_t *job, const hush_agent_job_in_t *in)
{
    assert(job != NULL && in != NULL);
    hush_agent_fill_prompt(job->prompt, sizeof(job->prompt), in->bot, job->human_name);
    hush_agent_append_assign(job->prompt, sizeof(job->prompt), job->ask,
                             in->launch, in->bot->hex);
    hush_agent_name_dangling_peer(job, in->scoped);
    if (in->scoped && strlen(job->prompt) + strlen(HUSH_AGENT_STRICT_SCOPE) < sizeof(job->prompt))
        strcat(job->prompt, HUSH_AGENT_STRICT_SCOPE);
    if (!in->scoped && in->mode == HUSH_AGENT_MODE_BROADCAST && job->n_co_robots == 1 &&
        strlen(job->prompt) + strlen(HUSH_AGENT_COOPERATE) < sizeof(job->prompt))
        strcat(job->prompt, HUSH_AGENT_COOPERATE);
    hush_agent_append_peers(job);
}

static hush_status_t hush_agent_fill_directive(hush_agent_job_t *job,
                                               const hush_agent_job_in_t *in)
{
    assert(job != NULL && in != NULL);
    if (in->elect) {
        job->kind = HUSH_AGENT_KIND_ELECT;
        hush_agent_copy(job->prompt, sizeof(job->prompt),
                        in->prompt_override != NULL ? in->prompt_override : HUSH_AGENT_ELECT_PROMPT);
        return HUSH_OK;
    }
    if (in->leader)
        return hush_agent_fill_leader(job);
    hush_agent_fill_worker(job, in);
    return HUSH_OK;
}

static void hush_agent_fill_job_note(hush_agent_job_t *job, const hush_agent_job_in_t *in)
{
    assert(job != NULL && in != NULL);
    hush_agent_thread_walk_t names = {.human = job->human_name, .robot = job->robot_name};
    hush_agent_fill_thread(job->note, sizeof(job->note), in->store, in->launch, in->parent, &names);
    if (job->note[0] == '\0')
        hush_agent_copy(job->note, sizeof(job->note), in->parent->content);
    hush_agent_humanize_ask(job->note, sizeof(job->note), in->launch, in->bot->hex);
    hush_agent_append_context(job->note, sizeof(job->note), in->bot);
}

static hush_status_t hush_agent_add_instruction(hush_agent_job_t *job, const char *label,
                                                const char *text)
{
    assert(job != NULL);
    assert(label != NULL && text != NULL);
    size_t used = strlen(job->prompt);
    int written = snprintf(job->prompt + used, sizeof(job->prompt) - used,
                           "\n%s: %s\n", label, text);
    if (written < 0 || (size_t)written >= sizeof(job->prompt) - used)
        return HUSH_ERR_FULL;
    return HUSH_OK;
}

static hush_status_t hush_agent_add_guidance(hush_agent_job_t *job,
                                             const hush_agent_robot_t *robot)
{
    assert(job != NULL);
    assert(robot != NULL && robot->nskills <= (size_t)HUSH_SKILL_EQUIP_MAX);
    if (job->kind != HUSH_AGENT_KIND_NOTE_JOB && robot->prompt != NULL)
        HUSH_TRY(hush_agent_add_instruction(job, "Robot system prompt", robot->prompt));
    const hush_launch_channel_t *channel = hush_agent_channel(job->launch, job->channel);
    if (channel != NULL) {
        HUSH_TRY(hush_agent_add_instruction(job, "Room system prompt", channel->system_prompt));
        if (channel->about[0] != '\0')
            HUSH_TRY(hush_agent_add_instruction(job, "Channel topic", channel->about));
    }
    for (size_t i = 0; i < robot->nskills; ++i) {
        char instructions[HUSH_SKILL_BODY_MAX] = {0};
        HUSH_TRY(hush_skill_read_instructions(instructions, sizeof(instructions), robot->skills[i]));
        HUSH_TRY(hush_agent_add_instruction(job, robot->skills[i], instructions));
    }
    return HUSH_OK;
}

static void hush_agent_make_token(char *out, size_t outsz)
{
    unsigned n;

    assert(out != NULL);
    assert(outsz > 0);
    /* Local pipe id for fixup/HTTP only. Must not enter presence d. */
    g_id_seq++;
    n = g_id_seq;
    (void)snprintf(out, outsz, "f%u", n);
}

static hush_agent_job_t *hush_agent_find_token(const char *token)
{
    size_t i;

    assert(token != NULL);
    for (i = 0; i < (size_t)HUSH_AGENT_JOBS_MAX; i++) {
        if (g_jobs[i].kind != HUSH_AGENT_KIND_FIXUP)
            continue;
        if (strcmp(g_jobs[i].token, token) == 0)
            return &g_jobs[i];
    }
    return NULL;
}

static void hush_agent_fill_fixup(hush_agent_job_t *job,
                                  const char *instruction,
                                  const char *text)
{
    assert(job != NULL);
    memset(job, 0, sizeof(*job));
    job->fd = HUSH_AGENT_FD_NONE;
    job->busy = 1;
    job->kind = HUSH_AGENT_KIND_FIXUP;
    job->started = time(NULL);
    hush_agent_copy(job->provider, sizeof(job->provider),
                    HUSH_ROSTER_PROVIDER_GROK_BUILD);
    hush_agent_make_token(job->token, sizeof(job->token));
    hush_agent_copy(job->prompt, sizeof(job->prompt), HUSH_AGENT_FIXUP_PROMPT);
    hush_agent_copy(job->rules, sizeof(job->rules), HUSH_AGENT_FIXUP_RULES);
    hush_agent_prepare_cwd(job->cwd, sizeof(job->cwd));
    if (snprintf(job->note, sizeof(job->note), "%s%s%s%s",
                 HUSH_AGENT_FIXUP_HEAD,
                 instruction != NULL ? instruction : "",
                 HUSH_AGENT_FIXUP_MID,
                 text != NULL ? text : "") >= (int)sizeof(job->note))
        hush_agent_copy(job->note, sizeof(job->note),
                        text != NULL ? text : "");
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

static int hush_agent_npub_prefix_hit(const char *tok, const char *npub)
{
    size_t n;
    size_t m;

    if (tok == NULL || npub == NULL || tok[0] == '\0' || npub[0] == '\0')
        return 0;
    if (strcmp(tok, npub) == 0)
        return 1;
    n = strlen(tok);
    m = strlen(npub);
    if (n < (size_t)HUSH_AGENT_NPUB_MIN)
        return 0;
    if (n >= m)
        return 0;
    return strncmp(tok, npub, n) == 0;
}

static size_t hush_agent_list_aliases(const hush_launch_t *launch,
                                      hush_agent_alias_t *out, size_t maxn)
{
    size_t n = 0;
    size_t i;

    assert(out != NULL);
    if (launch == NULL || maxn == 0)
        return 0;
    if (launch->has_vibe && launch->payne.npub[0] != '\0' && n < maxn) {
        out[n].name = hush_launch_payne_name(launch);
        out[n].npub = launch->payne.npub;
        n++;
    }
    for (i = 0; i < launch->roster.nagents && n < maxn; i++) {
        const hush_roster_agent_t *agent = &launch->roster.agents[i];

        if (!agent->enabled || agent->id.npub[0] == '\0')
            continue;
        out[n].name = agent->name;
        out[n].npub = agent->id.npub;
        n++;
    }
    return n;
}

static void hush_agent_sort_aliases(hush_agent_alias_t *aliases, size_t n)
{
    size_t i;
    size_t j;

    assert(aliases != NULL || n == 0);
    for (i = 1; i < n; i++) {
        hush_agent_alias_t hold = aliases[i];

        j = i;
        while (j > 0 && strlen(aliases[j - 1].name) < strlen(hold.name)) {
            aliases[j] = aliases[j - 1];
            j--;
        }
        aliases[j] = hold;
    }
}

static int hush_agent_unique_npub(const hush_launch_t *launch,
                                  const char *tok, char *out, size_t outsz)
{
    hush_agent_alias_t aliases[HUSH_ROSTER_AGENTS_MAX + 1];
    size_t n;
    size_t i;
    size_t hits = 0;
    const char *hit = NULL;

    assert(out != NULL);
    assert(outsz > 0);
    out[0] = '\0';
    if (launch == NULL || tok == NULL || tok[0] == '\0')
        return 0;
    n = hush_agent_list_aliases(launch, aliases, HUSH_ROSTER_AGENTS_MAX + 1);
    for (i = 0; i < n; i++) {
        if (!hush_agent_npub_prefix_hit(tok, aliases[i].npub))
            continue;
        hits++;
        hit = aliases[i].npub;
        if (hits > 1)
            return 0;
    }
    if (hits != 1)
        return 0;
    hush_agent_copy(out, outsz, hit);
    return 1;
}

static size_t hush_agent_put_full_npub(char *dst, size_t o, size_t cap,
                                       const char *npub)
{
    size_t nlen;

    assert(dst != NULL);
    assert(npub != NULL);
    nlen = strlen(npub);
    if (o + (size_t)HUSH_AGENT_NOSTR_HEAD_LEN + nlen >= cap)
        return cap;
    memcpy(dst + o, HUSH_AGENT_NOSTR_HEAD, (size_t)HUSH_AGENT_NOSTR_HEAD_LEN);
    o += (size_t)HUSH_AGENT_NOSTR_HEAD_LEN;
    memcpy(dst + o, npub, nlen);
    return o + nlen;
}

static int hush_agent_is_same_ascii(const char *a, const char *b, size_t n)
{
    size_t i;

    assert(a != NULL);
    assert(b != NULL);
    for (i = 0; i < n; i++) {
        unsigned char ca;
        unsigned char cb;

        if (a[i] == '\0' || b[i] == '\0')
            return 0;
        ca = (unsigned char)a[i];
        cb = (unsigned char)b[i];
        if (ca >= 'A' && ca <= 'Z')
            ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb)
            return 0;
    }
    return 1;
}

static int hush_agent_is_name_end(char ch)
{
    if (ch == '\0')
        return 1;
    if (ch >= '0' && ch <= '9')
        return 0;
    if (ch >= 'A' && ch <= 'Z')
        return 0;
    if (ch >= 'a' && ch <= 'z')
        return 0;
    return 1;
}

static size_t hush_agent_alias_at(const char *src,
                                  const hush_agent_alias_set_t *set)
{
    size_t a;

    assert(src != NULL);
    assert(set != NULL);
    if (src[0] != '@')
        return set->naliases;
    for (a = 0; a < set->naliases; a++) {
        const char *nm = set->aliases[a].name;
        size_t nlen;

        if (nm == NULL || nm[0] == '\0')
            continue;
        nlen = strlen(nm);
        if (!hush_agent_is_same_ascii(src + 1, nm, nlen))
            continue;
        if (!hush_agent_is_name_end(src[1 + nlen]))
            continue;
        return a;
    }
    return set->naliases;
}

static void hush_agent_rewrite_at_npub(char *text, size_t textsz)
{
    char scratch[HUSH_EVENT_MAX_CONTENT + 1];
    size_t i = 0;
    size_t o = 0;

    assert(text != NULL);
    assert(textsz > 0);
    while (text[i] != '\0' && i < (size_t)HUSH_EVENT_MAX_CONTENT &&
           o + 1 < sizeof(scratch)) {
        if (strncmp(text + i, HUSH_AGENT_AT_NPUB,
                    (size_t)HUSH_AGENT_AT_NPUB_LEN) == 0) {
            if (o + (size_t)HUSH_AGENT_NOSTR_HEAD_LEN >= sizeof(scratch))
                break;
            memcpy(scratch + o, HUSH_AGENT_NOSTR_HEAD,
                   (size_t)HUSH_AGENT_NOSTR_HEAD_LEN);
            o += (size_t)HUSH_AGENT_NOSTR_HEAD_LEN;
            i += 1;
            continue;
        }
        scratch[o++] = text[i++];
    }
    scratch[o] = '\0';
    hush_agent_copy(text, textsz, scratch);
}

static void hush_agent_expand_npubs(char *text, size_t textsz,
                                    const hush_launch_t *launch)
{
    char scratch[HUSH_EVENT_MAX_CONTENT + 1];
    char full[HUSH_IDENTITY_NPUB_MAX];
    char tok[HUSH_IDENTITY_NPUB_MAX];
    size_t i = 0;
    size_t o = 0;

    assert(text != NULL);
    if (launch == NULL)
        return;
    while (text[i] != '\0' && i < (size_t)HUSH_EVENT_MAX_CONTENT &&
           o + 1 < sizeof(scratch)) {
        size_t span = hush_agent_npub_span(text, i);
        size_t tlen;
        size_t next;

        if (span < (size_t)HUSH_AGENT_NOSTR_NPUB_LEN ||
            strncmp(text + i, HUSH_AGENT_NOSTR_HEAD,
                    (size_t)HUSH_AGENT_NOSTR_HEAD_LEN) != 0) {
            scratch[o++] = text[i++];
            continue;
        }
        tlen = span - (size_t)HUSH_AGENT_NOSTR_HEAD_LEN;
        if (tlen >= sizeof(tok))
            tlen = sizeof(tok) - 1;
        memcpy(tok, text + i + (size_t)HUSH_AGENT_NOSTR_HEAD_LEN, tlen);
        tok[tlen] = '\0';
        if (!hush_agent_unique_npub(launch, tok, full, sizeof(full))) {
            hush_agent_robot_t bot;

            if (hush_agent_lookup_robot(&bot, launch, tok) &&
                o + span < sizeof(scratch)) {
                memcpy(scratch + o, text + i, span);
                o += span;
                i += span;
                continue;
            }
            i += span;
            continue;
        }
        next = hush_agent_put_full_npub(scratch, o, sizeof(scratch), full);
        if (next >= sizeof(scratch))
            break;
        o = next;
        i += span;
    }
    scratch[o] = '\0';
    hush_agent_copy(text, textsz, scratch);
}

static void hush_agent_emit_at_names(char *dst, size_t dstsz, const char *src,
                                     const hush_agent_alias_set_t *set)
{
    size_t i = 0;
    size_t o = 0;

    assert(dst != NULL);
    assert(dstsz > 0);
    assert(src != NULL);
    assert(set != NULL);
    while (src[i] != '\0' && i < (size_t)HUSH_EVENT_MAX_CONTENT &&
           o + 1 < dstsz) {
        size_t hit = hush_agent_alias_at(src + i, set);
        size_t next;

        if (hit >= set->naliases) {
            dst[o++] = src[i++];
            continue;
        }
        next = hush_agent_put_full_npub(dst, o, dstsz, set->aliases[hit].npub);
        if (next >= dstsz)
            break;
        o = next;
        i += 1 + strlen(set->aliases[hit].name);
    }
    dst[o] = '\0';
}

static void hush_agent_rewrite_at_names(char *text, size_t textsz,
                                        const hush_launch_t *launch)
{
    hush_agent_alias_t aliases[HUSH_ROSTER_AGENTS_MAX + 1];
    hush_agent_alias_set_t set;
    char scratch[HUSH_EVENT_MAX_CONTENT + 1];

    assert(text != NULL);
    if (launch == NULL)
        return;
    set.aliases = aliases;
    set.naliases = hush_agent_list_aliases(launch, aliases,
                                           HUSH_ROSTER_AGENTS_MAX + 1);
    hush_agent_sort_aliases(aliases, set.naliases);
    hush_agent_emit_at_names(scratch, sizeof(scratch), text, &set);
    hush_agent_copy(text, textsz, scratch);
}

void hush_agent_humanize_ask(char *text, size_t textsz,
                                    const hush_launch_t *launch,
                                    const char *self_hex)
{
    char scratch[HUSH_EVENT_MAX_CONTENT + 1];
    char tok[HUSH_IDENTITY_NPUB_MAX];
    size_t i = 0;
    size_t o = 0;

    assert(text != NULL);
    assert(textsz > 0);
    if (launch == NULL)
        return;
    while (text[i] != '\0' && i < (size_t)HUSH_EVENT_MAX_CONTENT &&
           o + 1 < sizeof(scratch)) {
        size_t span = hush_agent_npub_span(text, i);
        size_t tlen;
        const char *name = NULL;

        if (span < (size_t)HUSH_AGENT_NOSTR_NPUB_LEN ||
            strncmp(text + i, HUSH_AGENT_NOSTR_HEAD,
                    (size_t)HUSH_AGENT_NOSTR_HEAD_LEN) != 0) {
            scratch[o++] = text[i++];
            continue;
        }
        tlen = span - (size_t)HUSH_AGENT_NOSTR_HEAD_LEN;
        if (tlen >= sizeof(tok))
            tlen = sizeof(tok) - 1;
        memcpy(tok, text + i + (size_t)HUSH_AGENT_NOSTR_HEAD_LEN, tlen);
        tok[tlen] = '\0';
        i += span;
        {
            hush_agent_robot_t bot;

            if (hush_agent_lookup_robot(&bot, launch, tok)) {
                if (self_hex != NULL && self_hex[0] != '\0' &&
                    bot.hex != NULL && strcmp(bot.hex, self_hex) == 0)
                    continue; /* drop the acting robot's own mention */
                name = bot.name;
            } else if (launch->logged_in && hush_agent_is_human(launch, tok)) {
                name = launch->roster.profile.first_name[0] != '\0'
                    ? launch->roster.profile.first_name
                    : HUSH_AGENT_HUMAN_FALLBACK;
            }
        }
        if (name == NULL || name[0] == '\0')
            continue; /* drop unknown npub */
        if (o + 2 + strlen(name) >= sizeof(scratch))
            break;
        scratch[o++] = '@';
        memcpy(scratch + o, name, strlen(name));
        o += strlen(name);
    }
    scratch[o] = '\0';
    hush_agent_copy(text, textsz, scratch);
}

static void hush_agent_cut_ci(char *text, const char *needle)
{
    size_t nlen;
    size_t i;

    assert(text != NULL);
    assert(needle != NULL);
    nlen = strlen(needle);
    if (nlen == 0)
        return;
    for (i = 0; text[i] != '\0' && i < (size_t)HUSH_EVENT_MAX_CONTENT; i++) {
        size_t k;

        if (!hush_agent_is_same_ascii(text + i, needle, nlen))
            continue;
        k = i + nlen;
        while (text[k] != '\0' && text[k] != '.' && text[k] != '!' &&
               text[k] != '?' && text[k] != '\n')
            k++;
        if (text[k] != '\0')
            k++;
        memmove(text + i, text + k, strlen(text + k) + 1);
        return;
    }
}

static void hush_agent_drop_handoff(char *text)
{
    int n;

    assert(text != NULL);
    for (n = 0; n < 8; n++) {
        char before[HUSH_EVENT_MAX_CONTENT + 1];

        hush_agent_copy(before, sizeof(before), text);
        hush_agent_cut_ci(text, HUSH_AGENT_HOFF_TURN);
        hush_agent_cut_ci(text, HUSH_AGENT_HOFF_NEXT);
        hush_agent_cut_ci(text, HUSH_AGENT_HOFF_HERE);
        hush_agent_cut_ci(text, HUSH_AGENT_HOFF_CONT);
        hush_agent_cut_ci(text, HUSH_AGENT_HOFF_DONE);
        hush_agent_cut_ci(text, HUSH_AGENT_HOFF_GEN);
        if (strcmp(before, text) == 0)
            return;
    }
}

static void hush_agent_drop_npubs(char *text, size_t textsz)
{
    char scratch[HUSH_EVENT_MAX_CONTENT + 1];
    size_t i = 0;
    size_t o = 0;

    assert(text != NULL);
    assert(textsz > 0);
    while (text[i] != '\0' && i < (size_t)HUSH_EVENT_MAX_CONTENT &&
           o + 1 < sizeof(scratch)) {
        size_t span = hush_agent_npub_span(text, i);

        if (span > 0) {
            i += span;
            continue;
        }
        scratch[o++] = text[i++];
    }
    scratch[o] = '\0';
    hush_agent_copy(text, textsz, scratch);
}

static void hush_agent_drop_self(hush_agent_job_t *job)
{
    hush_agent_robot_t bot;
    char key[HUSH_IDENTITY_NPUB_MAX + 8];
    char *hit;

    assert(job != NULL);
    if (job->launch == NULL)
        return;
    if (!hush_agent_lookup_robot(&bot, job->launch, job->robot_pub))
        return;
    if (bot.npub != NULL && bot.npub[0] != '\0') {
        (void)snprintf(key, sizeof(key), "%s%s", HUSH_AGENT_NOSTR_HEAD,
                       bot.npub);
        while ((hit = strstr(job->out, key)) != NULL)
            memmove(hit, hit + strlen(key), strlen(hit + strlen(key)) + 1);
    }
    if (job->robot_name[0] != '\0') {
        char token[HUSH_ROSTER_NAME_MAX + 2];

        (void)snprintf(token, sizeof(token), "@%s", job->robot_name);
        hush_agent_cut_ci(job->out, token);
    }
}

static void hush_agent_drop_echo(hush_agent_job_t *job)
{
    char snip[HUSH_AGENT_SNIP_MAX + HUSH_IDENTITY_NPUB_MAX + 1];
    char needle[HUSH_AGENT_SNIP_MAX + 1];
    char *hit;
    char *a;
    char *b;
    size_t i;
    size_t o = 0;

    assert(job != NULL);
    hush_agent_snip_line(snip, sizeof(snip), job->ask);
    for (i = 0; snip[i] != '\0' && o + 1 < sizeof(needle); ) {
        size_t span = hush_agent_npub_span(snip, i);

        if (span > 0) {
            i += span;
            continue;
        }
        needle[o++] = snip[i++];
    }
    needle[o] = '\0';
    if (o > (size_t)HUSH_AGENT_ECHO_MIN)
        needle[HUSH_AGENT_ECHO_MIN] = '\0';
    if (strlen(needle) < 12)
        return;
    hit = strstr(job->out, needle);
    if (hit == NULL)
        return;
    a = hit;
    while (a > job->out && a[-1] != '.' && a[-1] != '!' && a[-1] != '?')
        a--;
    b = hit;
    while (*b != '\0' && *b != '.' && *b != '!' && *b != '?')
        b++;
    if (*b != '\0')
        b++;
    if (a == job->out && *b == '\0')
        return;
    memmove(a, b, strlen(b) + 1);
}

static void hush_agent_tidy_reply(char *text)
{
    size_t i;
    size_t o = 0;
    int gap = 0;

    assert(text != NULL);
    for (i = 0; text[i] != '\0' && i < (size_t)HUSH_EVENT_MAX_CONTENT; i++) {
        if (text[i] == ' ' || text[i] == '\n' || text[i] == '\r') {
            gap = 1;
            continue;
        }
        if (gap && o > 0 && text[i] != ',' && text[i] != ';' &&
            text[i] != '.')
            text[o++] = ' ';
        gap = 0;
        text[o++] = text[i];
    }
    text[o] = '\0';
    while (o > 0) {
        char c = text[o - 1];

        if (c == ';' || c == ',' || c == ':' || c == ' ') {
            text[--o] = '\0';
            continue;
        }
        break;
    }
}

static void hush_agent_scrub_reply(hush_agent_job_t *job)
{
    assert(job != NULL);
    hush_agent_drop_self(job);
    hush_agent_drop_echo(job);
    hush_agent_drop_handoff(job->out);
    if (job->last)
        hush_agent_drop_npubs(job->out, sizeof(job->out));
    hush_agent_tidy_reply(job->out);
}

void hush_agent_rewrite_mentions(hush_agent_job_t *job)
{
    assert(job != NULL);
    if (job->kind == HUSH_AGENT_KIND_FIXUP ||
        job->kind == HUSH_AGENT_KIND_ELECT)
        return;
    hush_agent_rewrite_at_npub(job->out, sizeof(job->out));
    if (job->launch == NULL)
        return;
    hush_agent_expand_npubs(job->out, sizeof(job->out), job->launch);
    hush_agent_rewrite_at_names(job->out, sizeof(job->out), job->launch);
    hush_agent_scrub_reply(job);
}

static void hush_agent_append_peers(hush_agent_job_t *job)
{
    size_t off;
    size_t glen;
    int c;

    assert(job != NULL);
    if (job->n_co_robots <= 0)
        return;
    off = strlen(job->prompt);
    glen = strlen(HUSH_AGENT_PEER_LINE);
    if (off + glen >= sizeof(job->prompt))
        return;
    memcpy(job->prompt + off, HUSH_AGENT_PEER_LINE, glen);
    off += glen;
    for (c = 0; c < job->n_co_robots; c++) {
        const char *nm = job->co_names[c][0] ? job->co_names[c] : "robot";
        int nwritten;

        nwritten = snprintf(job->prompt + off, sizeof(job->prompt) - off,
                            "%s@%s", c > 0 ? " " : "", nm);
        if (nwritten < 0 || (size_t)nwritten >= sizeof(job->prompt) - off)
            break;
        off += (size_t)nwritten;
    }
    if (off < sizeof(job->prompt))
        job->prompt[off] = '\0';
}

static void hush_agent_append_last(hush_agent_job_t *job)
{
    assert(job != NULL);
    if (!job->last)
        return;
    size_t rule_len = strlen(HUSH_AGENT_LAST_RULE);
    size_t prompt_len = strlen(job->prompt);
    if (rule_len + prompt_len >= sizeof(job->prompt))
        return;
    memmove(job->prompt + rule_len, job->prompt, prompt_len + 1);
    memcpy(job->prompt, HUSH_AGENT_LAST_RULE, rule_len);
    size_t rules_len = strlen(job->rules);
    if (rules_len + rule_len < sizeof(job->rules))
        memcpy(job->rules + rules_len, HUSH_AGENT_LAST_RULE, rule_len + 1);
}
