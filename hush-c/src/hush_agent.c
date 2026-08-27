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
#include "hush_cevent.h"
#include "hush_presence.h"
#include "hush_provider.h"
#include "hush_relay.h"
#include "hush_roster.h"

enum {
    HUSH_AGENT_INTRO_MAX = 32,
    HUSH_AGENT_JOBS_MAX = 4,
    HUSH_AGENT_TIMEOUT_S = 90,
    HUSH_AGENT_KIND_NOTE = 1,
    HUSH_AGENT_ID_WIDTH = 16,
    HUSH_AGENT_ARGV_MAX = 28,
    HUSH_AGENT_PATH_MAX = 256,
    HUSH_AGENT_FD_NONE = -1,
    HUSH_AGENT_CWD_MODE = 0700,
    HUSH_AGENT_THREAD_MAX = 6,
    HUSH_AGENT_SNIP_MAX = 160,
    HUSH_AGENT_SCAN_MAX = 64,
    HUSH_AGENT_KIND_NOTE_JOB = 0,
    HUSH_AGENT_KIND_FIXUP = 1,
    HUSH_AGENT_TOKEN_MAX = 16,
    HUSH_AGENT_FOLLOW_MAX = 8,
    HUSH_AGENT_FOLLOW_ROBOTS = 8
};

#define HUSH_AGENT_GROK_BIN "grok"
#define HUSH_AGENT_DEVNULL "/dev/null"
#define HUSH_AGENT_CHAN_FALLBACK "general"
#define HUSH_AGENT_ENV_CONFIG "HUSH_CONFIG_DIR"
#define HUSH_AGENT_CWD_LEAF "agent-cwd"
#define HUSH_AGENT_CWD_TMP "hush-agent-cwd"
#define HUSH_AGENT_TMP_FALLBACK "/tmp"
#define HUSH_AGENT_PROMPT_FALLBACK \
    "You are a robot in the Hush hive. Fulfill the last human ask in one note."
#define HUSH_AGENT_ONE_JOKE \
    "If the last human ask is a joke, reply with exactly one joke."
#define HUSH_AGENT_PEER_STANDARD \
    " Inter-robot standard: keep nostr:npub mentions in the same sentence " \
    "order they were given. One short intro per thread, then work. " \
    "To hand off, emit the peer nostr:npub in the note. Do not reorder mentions."
#define HUSH_AGENT_HYGIENE \
    " Fulfill YOUR assignment in this note, not a peer's. " \
    "Do not mention yourself. After your work you may emit nostr:<peer-npub> " \
    "to hand off. Include any asked code. No preamble-only replies. " \
    HUSH_AGENT_ONE_JOKE HUSH_AGENT_PEER_STANDARD
#define HUSH_AGENT_DISALLOWED \
    "run_terminal_cmd,web_search,web_fetch,read_file,search_replace,list_dir,grep,todo_write,task,Agent"
#define HUSH_AGENT_RULES \
    "Fulfill YOUR assignment as the named robot. Do not mention yourself. " \
    "Include asked code. Address the human by first name. No tools. " \
    HUSH_AGENT_ONE_JOKE HUSH_AGENT_PEER_STANDARD
#define HUSH_AGENT_HUMAN_FALLBACK "you"
#define HUSH_AGENT_GROK_EFFORT "low"
#define HUSH_AGENT_GROK_TURNS "2"
#define HUSH_AGENT_FIXUP_TURNS "1"
#define HUSH_AGENT_GROK_NOMEM "--no-memory"
#define HUSH_AGENT_FIXUP_PROMPT \
    "Rewrite only the given text per the instruction. " \
    "Return only the rewritten text. No fences. No preamble."
#define HUSH_AGENT_FIXUP_RULES \
    "Return only the rewritten selection. No markdown fences. No chatter."
#define HUSH_AGENT_FIXUP_HEAD "Instruction:\n"
#define HUSH_AGENT_FIXUP_MID "\n\nText:\n"
#define HUSH_AGENT_THREAD_HEAD \
    "Thread so far. Do not repeat a prior joke. " \
    "Fulfill the last human line in this note.\n"
#define HUSH_AGENT_ASSIGN " YOUR assignment: "
#define HUSH_AGENT_INTRO_PREFIX "At ease."
#define HUSH_AGENT_ACK_LINE "Mention received."
#define HUSH_AGENT_CHAPERON_LINE \
    "That's enough robot talk. Standing by for the human."

typedef struct {
    int busy;
    int kind;
    int done;
    int ok;
    pid_t pid;
    int fd;
    time_t started;
    char token[HUSH_AGENT_TOKEN_MAX];
    char parent_id[HUSH_EVENT_ID_HEX_LEN + 1];
    char channel[HUSH_EVENT_MAX_TAG_LEN + 1];
    char human_pub[HUSH_EVENT_PUBKEY_HEX_LEN + 1];
    char robot_pub[HUSH_EVENT_PUBKEY_HEX_LEN + 1];
    char robot_name[HUSH_ROSTER_NAME_MAX];
    char robot_role[HUSH_ROSTER_NAME_MAX];
    char presence_slug[HUSH_PRESENCE_SLUG_MAX];
    char human_name[HUSH_ROSTER_NAME_MAX];
    char prompt[HUSH_ROSTER_PROMPT_MAX];
    char rules[HUSH_ROSTER_PROMPT_MAX];
    char cwd[HUSH_AGENT_PATH_MAX];
    char note[HUSH_EVENT_MAX_CONTENT + 1];
    char out[HUSH_EVENT_MAX_CONTENT + 1];
    size_t out_n;
    /* Co-robots mentioned together with this one on the triggering note.
     * Enables group negotiation: robots can see peers and p-mention back. */
    char co_npubs[4][HUSH_IDENTITY_NPUB_MAX];
    int n_co_robots;
    const hush_launch_t *launch;
    char ask[HUSH_EVENT_MAX_CONTENT + 1];
} hush_agent_job_t;

typedef struct {
    int live;
    size_t nnext;
    size_t at;
    char root[HUSH_EVENT_ID_HEX_LEN + 1];
    char channel[HUSH_EVENT_MAX_TAG_LEN + 1];
    char human_pub[HUSH_EVENT_PUBKEY_HEX_LEN + 1];
    char ask[HUSH_EVENT_MAX_CONTENT + 1];
    char next[HUSH_AGENT_FOLLOW_ROBOTS][HUSH_EVENT_PUBKEY_HEX_LEN + 1];
} hush_agent_follow_t;

typedef struct {
    const char *name;
    const char *npub;
    const char *hex;
    const char *provider;
    const char *prompt;
    const char *slug;
    const char *role;
    const char *intro;
    int intro_enabled;
} hush_agent_robot_t;

typedef struct {
    const char *pubkey;
    const char *content;
    const char *channel;
    const char *parent_id;
    const char *human_pub;
    /* Optional additional p-npubs (for group mention seam: robots addressing peers).
     * Null-terminated list or up to 4. Values are npub strings. */
    const char *extra_p[4];
} hush_agent_note_in_t;

typedef struct {
    const char *root;
    const char *human_pub;
    const char *human;
    const char *robot;
} hush_agent_thread_walk_t;

typedef struct {
    hush_store_t *store;
    const hush_launch_t *launch;
    const hush_agent_robot_t *bot;
    const hush_event_t *parent;
    const char *ask;
} hush_agent_job_in_t;

static hush_agent_job_t g_jobs[HUSH_AGENT_JOBS_MAX];
static hush_agent_follow_t g_follow[HUSH_AGENT_FOLLOW_MAX];
static unsigned g_id_seq;
static char g_intro_hex[HUSH_AGENT_INTRO_MAX][HUSH_EVENT_PUBKEY_HEX_LEN + 1];
static char g_intro_root[HUSH_AGENT_INTRO_MAX][HUSH_EVENT_ID_HEX_LEN + 1];
static size_t g_nintro;

static void hush_agent_copy(char *dst, size_t dstsz, const char *src);
static void hush_agent_trim(char *text);
static void hush_agent_make_id(char *out65);
static hush_agent_job_t *hush_agent_find_slot(void);
static int hush_agent_key_matches(const char *mention, const char *npub,
                                  const char *hex);
static int hush_agent_is_human(const hush_launch_t *launch,
                               const char *mention);
static int hush_agent_lookup_robot(hush_agent_robot_t *out,
                                   const hush_launch_t *launch,
                                   const char *mention);
static void hush_agent_event_channel(char *out, size_t outsz,
                                     const hush_event_t *ev);
static void hush_agent_event_root(char *out, size_t outsz,
                                  const hush_event_t *ev);
static void hush_agent_human_name(char *out, size_t outsz,
                                  const hush_launch_t *launch);
static void hush_agent_fill_prompt(char *out, size_t outsz,
                                   const hush_agent_robot_t *bot,
                                   const char *human);
static void hush_agent_fill_rules(char *out, size_t outsz, const char *human);
static void hush_agent_prepare_cwd(char *out, size_t outsz);
static int hush_agent_status_append(char *out, size_t outsz, size_t *off,
                                    const hush_agent_job_t *job);
static void hush_agent_fill_note(hush_event_t *ev, const hush_agent_note_in_t *in);
static hush_status_t hush_agent_insert_note(hush_store_t *store,
                                            const hush_agent_note_in_t *in);
static void hush_agent_on_deck(hush_store_t *store, const hush_agent_robot_t *bot,
                               const hush_event_t *parent, const char *why);
static int hush_agent_grok_ready(void);
/* True when this robot can start a grok job (own id or Payne ranked grok). */
static int hush_agent_can_start_grok(const hush_launch_t *launch,
                                     const hush_agent_robot_t *bot);
static hush_status_t hush_agent_start_grok(const hush_agent_job_in_t *in);
static void hush_agent_fill_job(hush_agent_job_t *job,
                                const hush_agent_job_in_t *in);
static int hush_agent_event_is_root(const hush_event_t *ev, const char *root);
/* True when ch is space, tab, CR, or LF. Pure. */
static int hush_agent_is_space(char ch);
/* Writes one space at o when room remains. Returns the next index. */
static size_t hush_agent_put_gap(char *out, size_t o, size_t cap);
/* Copies src into out, collapsing whitespace to one space, up to SNIP_MAX. */
static void hush_agent_snip_line(char *out, size_t outsz, const char *src);
static void hush_agent_append_turn(char *out, size_t outsz,
                                   const hush_event_t *ev, const char *who);
static size_t hush_agent_thread_skip(const hush_event_t *evs, size_t n,
                                    const char *root);
static void hush_agent_walk_thread(char *out, size_t outsz,
                                   const hush_event_t *evs, size_t n,
                                   const hush_agent_thread_walk_t *walk);
static void hush_agent_fill_thread(char *out, size_t outsz,
                                   hush_store_t *store,
                                   const hush_event_t *parent,
                                   const hush_agent_thread_walk_t *names);
static void hush_agent_exec_grok(int write_fd, const hush_agent_job_t *job);
static hush_status_t hush_agent_spawn_grok(hush_agent_job_t *job);
static void hush_agent_fill_fixup(hush_agent_job_t *job,
                                  const char *instruction,
                                  const char *text);
static void hush_agent_make_token(char *out, size_t outsz);
static hush_agent_job_t *hush_agent_find_token(const char *token);
static void hush_agent_close_job(hush_agent_job_t *job);
static void hush_agent_kill_job(hush_agent_job_t *job);
static void hush_agent_finish_job(hush_store_t *store, hush_agent_job_t *job,
                                  int ok);
static void hush_agent_read_job(hush_agent_job_t *job);
static void hush_agent_handle_mention(hush_store_t *store,
                                      const hush_launch_t *launch,
                                      const hush_event_t *ev,
                                      const char *mention);
static int hush_agent_job_timed_out(const hush_agent_job_t *job, time_t now);
static int hush_agent_robot_busy(const hush_agent_robot_t *bot,
                                 const hush_event_t *parent);
static int hush_agent_intro_seen(const char *hex, const char *root);
static void hush_agent_intro_remember(const char *hex, const char *root);
static int hush_agent_is_work_ok(const hush_launch_t *launch,
                                 const hush_agent_robot_t *bot);
static size_t hush_agent_collect_hexes(const hush_launch_t *launch,
                                       const hush_event_t *ev,
                                       char hexes[][HUSH_EVENT_PUBKEY_HEX_LEN + 1],
                                       size_t maxn);
static hush_agent_follow_t *hush_agent_follow_find(const char *root);
static hush_agent_follow_t *hush_agent_follow_take(const char *root);
static void hush_agent_follow_push(const hush_event_t *ev,
                                   const hush_launch_t *launch,
                                   char hexes[][HUSH_EVENT_PUBKEY_HEX_LEN + 1],
                                   size_t nhex, size_t start);
static void hush_agent_follow_kick(hush_store_t *store,
                                   const hush_launch_t *launch,
                                   const hush_event_t *ev);
static void hush_agent_emit(const char *type, const char *channel,
                            const char *root, const char *actor,
                            const char *note);
static void hush_agent_begin_work(const hush_agent_job_in_t *in);
static void hush_agent_presence_put(hush_store_t *store, hush_agent_job_t *job,
                                    const char *slug);
static void hush_agent_nudge_stuck(hush_store_t *store, hush_agent_job_t *job);
static int hush_agent_job_enabled(const hush_agent_job_t *job);
static void hush_agent_append_assign(char *prompt, size_t promptsz,
                                     const char *ask);
static void hush_agent_push_hex(char hexes[][HUSH_EVENT_PUBKEY_HEX_LEN + 1],
                                size_t *n, size_t maxn, const char *hex);
static const hush_launch_channel_t *hush_agent_channel(
    const hush_launch_t *launch, const char *slug);
static size_t hush_agent_count_turns(hush_store_t *store,
                                     const hush_launch_t *launch,
                                     const char *root);
/* True when content is a robot work note, not an ack, intro, or deny. */
static int hush_agent_is_work_note(const char *content);
static int hush_agent_turns_full(hush_store_t *store,
                                 const hush_launch_t *launch,
                                 const hush_event_t *ev);
static int hush_agent_lookup_slug(hush_agent_robot_t *out,
                                  const hush_launch_t *launch,
                                  const char *slug);
static void hush_agent_nudge_chaperon(hush_store_t *store,
                                      const hush_launch_t *launch,
                                      const hush_event_t *ev);

void hush_agent_init(void)
{
    size_t i;

    for (i = 0; i < (size_t)HUSH_AGENT_JOBS_MAX; i++) {
        memset(&g_jobs[i], 0, sizeof(g_jobs[i]));
        g_jobs[i].fd = HUSH_AGENT_FD_NONE;
    }
    memset(g_intro_hex, 0, sizeof(g_intro_hex));
    memset(g_intro_root, 0, sizeof(g_intro_root));
    g_nintro = 0;
    memset(g_follow, 0, sizeof(g_follow));
    hush_cevent_init();
    hush_presence_init();
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

void hush_agent_consider(hush_store_t *store, hush_launch_t *launch,
                         const hush_event_t *ev)
{
    size_t i;

    if (store == NULL || launch == NULL || ev == NULL)
        return;
    if (ev->kind != (uint32_t)HUSH_AGENT_KIND_NOTE)
        return;
    for (i = 0; i < ev->tag_count && i < (size_t)HUSH_EVENT_MAX_TAGS; i++) {
        if (strcmp(ev->tags[i][0], "p") != 0)
            continue;
        hush_agent_mention(store, launch, ev, ev->tags[i][1]);
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

void hush_agent_poll(hush_store_t *store)
{
    size_t i;
    time_t now;
    int status;

    now = time(NULL);
    if (store != NULL)
        hush_presence_expire(store, now);
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
            if (store != NULL)
                (void)hush_presence_clear(store, g_jobs[i].robot_pub,
                                          g_jobs[i].token, g_jobs[i].channel,
                                          now);
            hush_agent_kill_job(&g_jobs[i]);
            hush_agent_finish_job(store, &g_jobs[i], 0);
            continue;
        }
        hush_agent_read_job(&g_jobs[i]);
        if (g_jobs[i].out_n > 0)
            (void)hush_presence_beat(g_jobs[i].token, now);
        if (g_jobs[i].pid > 0)
            (void)waitpid(g_jobs[i].pid, &status, WNOHANG);
        if (store != NULL &&
            hush_presence_stall_s(g_jobs[i].token, now)
                >= HUSH_PRESENCE_STALL_S &&
            strcmp(g_jobs[i].presence_slug, HUSH_PRESENCE_SLUG_STUCK) != 0)
            hush_agent_presence_put(store, &g_jobs[i],
                                    HUSH_PRESENCE_SLUG_STUCK);
        if (store != NULL && hush_presence_stuck_due(g_jobs[i].token, now)) {
            hush_agent_presence_put(store, &g_jobs[i],
                                    HUSH_PRESENCE_SLUG_STUCK);
            hush_agent_nudge_stuck(store, &g_jobs[i]);
        }
        if (hush_agent_job_timed_out(&g_jobs[i], now)) {
            if (store != NULL)
                (void)hush_presence_clear(store, g_jobs[i].robot_pub,
                                          g_jobs[i].token, g_jobs[i].channel,
                                          now);
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

static void hush_agent_copy(char *dst, size_t dstsz, const char *src)
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

static void hush_agent_trim(char *text)
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

static void hush_agent_make_id(char *out65)
{
    time_t now;

    assert(out65 != NULL);
    now = time(NULL);
    g_id_seq++;
    (void)snprintf(out65, HUSH_EVENT_ID_HEX_LEN + 1,
                   "%0*llx%0*x%0*x%0*x",
                   HUSH_AGENT_ID_WIDTH, (unsigned long long)now,
                   HUSH_AGENT_ID_WIDTH, g_id_seq,
                   HUSH_AGENT_ID_WIDTH, g_id_seq ^ 0x51ed270bu,
                   HUSH_AGENT_ID_WIDTH, g_id_seq * 7u);
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

static int hush_agent_is_human(const hush_launch_t *launch,
                               const char *mention)
{
    assert(launch != NULL);
    if (!launch->logged_in)
        return 0;
    return hush_agent_key_matches(mention, launch->human.npub,
                                  launch->human.pubkey_hex);
}

static int hush_agent_lookup_robot(hush_agent_robot_t *out,
                                   const hush_launch_t *launch,
                                   const char *mention)
{
    size_t i;
    const hush_roster_agent_t *agent;

    assert(out != NULL);
    assert(launch != NULL);
    memset(out, 0, sizeof(*out));
    if (launch->has_vibe &&
        hush_agent_key_matches(mention, launch->payne.npub,
                               launch->payne.pubkey_hex)) {
        out->name = hush_launch_payne_name(launch);
        out->npub = launch->payne.npub;
        out->hex = launch->payne.pubkey_hex;
        out->provider = (launch->npayne_providers > 0)
            ? launch->payne_providers[0] : HUSH_ROSTER_PROVIDER_GOOSE;
        out->prompt = hush_launch_payne_prompt(launch);
        out->slug = HUSH_LAUNCH_PAYNE_SLUG;
        out->role = HUSH_ROSTER_ROLE_WORKER;
        out->intro = HUSH_ROSTER_INTRO_DEFAULT;
        out->intro_enabled = 1;
        if (!launch->payne_enabled)
            return 0;
        return 1;
    }
    for (i = 0; i < launch->roster.nagents; i++) {
        agent = &launch->roster.agents[i];
        if (!hush_agent_key_matches(mention, agent->id.npub,
                                    agent->id.pubkey_hex))
            continue;
        if (!agent->enabled)
            return 0;
        out->name = agent->name;
        out->npub = agent->id.npub;
        out->hex = agent->id.pubkey_hex;
        out->provider = agent->provider;
        out->prompt = agent->prompt;
        out->slug = agent->slug;
        out->role = agent->role[0] ? agent->role : HUSH_ROSTER_ROLE_WORKER;
        out->intro = agent->intro[0] ? agent->intro : HUSH_ROSTER_INTRO_DEFAULT;
        out->intro_enabled = agent->intro_enabled;
        return 1;
    }
    return 0;
}

static void hush_agent_event_channel(char *out, size_t outsz,
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

static void hush_agent_event_root(char *out, size_t outsz,
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

static void hush_agent_human_name(char *out, size_t outsz,
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
    int n;

    assert(out != NULL);
    assert(outsz > 0);
    assert(bot != NULL);
    base = (bot->prompt != NULL && bot->prompt[0] != '\0')
        ? bot->prompt : HUSH_AGENT_PROMPT_FALLBACK;
    who = (human != NULL && human[0] != '\0') ? human : HUSH_AGENT_HUMAN_FALLBACK;
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

static void hush_agent_prepare_cwd(char *out, size_t outsz)
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
    if (n < 0 || (size_t)n >= outsz) {
        hush_agent_copy(out, outsz, HUSH_AGENT_TMP_FALLBACK);
        return;
    }
    (void)mkdir(out, (mode_t)HUSH_AGENT_CWD_MODE);
}

static int hush_agent_status_append(char *out, size_t outsz, size_t *off,
                                   const hush_agent_job_t *job)
{
    const char *name;
    const char *sep;
    int n;

    assert(out != NULL);
    assert(off != NULL);
    assert(job != NULL);
    if (job->kind == HUSH_AGENT_KIND_FIXUP)
        return 1;
    name = job->robot_name[0] ? job->robot_name : "robot";
    sep = (*off > 1) ? "," : "";
    n = snprintf(out + *off, outsz - *off,
                 "%s{\"name\":\"%s\",\"parent\":\"%s\",\"slug\":\"%s\"}",
                 sep, name, job->parent_id,
                 job->presence_slug[0] ? job->presence_slug : "");
    if (n < 0 || (size_t)n >= outsz - *off)
        return 0;
    *off += (size_t)n;
    return 1;
}

static void hush_agent_fill_note(hush_event_t *ev, const hush_agent_note_in_t *in)
{
    assert(ev != NULL);
    assert(in != NULL);
    assert(in->pubkey != NULL);
    assert(in->content != NULL);
    assert(in->channel != NULL);
    memset(ev, 0, sizeof(*ev));
    hush_agent_make_id(ev->id);
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
}

static hush_status_t hush_agent_insert_note(hush_store_t *store,
                                            const hush_agent_note_in_t *in)
{
    hush_event_t ev;

    assert(store != NULL);
    hush_agent_fill_note(&ev, in);
    return hush_store_insert(store, &ev);
}

static int hush_agent_intro_seen(const char *hex, const char *root)
{
    size_t i;

    if (hex == NULL || root == NULL)
        return 0;
    for (i = 0; i < g_nintro && i < (size_t)HUSH_AGENT_INTRO_MAX; i++) {
        if (strcmp(g_intro_hex[i], hex) == 0 &&
            strcmp(g_intro_root[i], root) == 0)
            return 1;
    }
    return 0;
}

static void hush_agent_intro_remember(const char *hex, const char *root)
{
    if (hex == NULL || root == NULL)
        return;
    if (g_nintro >= (size_t)HUSH_AGENT_INTRO_MAX)
        return;
    hush_agent_copy(g_intro_hex[g_nintro], sizeof(g_intro_hex[0]), hex);
    hush_agent_copy(g_intro_root[g_nintro], sizeof(g_intro_root[0]), root);
    g_nintro++;
}

static void hush_agent_on_deck(hush_store_t *store, const hush_agent_robot_t *bot,
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

static int hush_agent_grok_ready(void)
{
    hush_provider_status_t st;

    if (hush_provider_status(&st, HUSH_ROSTER_PROVIDER_GROK_BUILD) != HUSH_OK)
        return 0;
    return st.has_home && st.has_binary;
}

static int hush_agent_can_start_grok(const hush_launch_t *launch,
                                     const hush_agent_robot_t *bot)
{
    size_t i;

    assert(bot != NULL);
    if (!hush_agent_grok_ready())
        return 0;
    if (bot->provider != NULL &&
        strcmp(bot->provider, HUSH_ROSTER_PROVIDER_GROK_BUILD) == 0)
        return 1;
    if (launch == NULL || bot->slug == NULL)
        return 0;
    if (strcmp(bot->slug, HUSH_LAUNCH_PAYNE_SLUG) != 0)
        return 0;
    for (i = 0; i < launch->npayne_providers; i++) {
        if (strcmp(launch->payne_providers[i], HUSH_ROSTER_PROVIDER_GROK_BUILD)
            == 0)
            return 1;
    }
    return 0;
}

static int hush_agent_event_is_root(const hush_event_t *ev, const char *root)
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

static int hush_agent_is_space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

static size_t hush_agent_put_gap(char *out, size_t o, size_t cap)
{
    assert(out != NULL);
    if (o >= cap)
        return o;
    out[o] = ' ';
    return o + 1;
}

static void hush_agent_snip_line(char *out, size_t outsz, const char *src)
{
    size_t i;
    size_t o;
    size_t cap;
    int gap;

    assert(out != NULL);
    assert(outsz > 0);
    if (src == NULL)
        src = "";
    cap = outsz - 1;
    if (cap > (size_t)HUSH_AGENT_SNIP_MAX)
        cap = (size_t)HUSH_AGENT_SNIP_MAX;
    o = 0;
    gap = 0;
    for (i = 0; src[i] != '\0' && o < cap &&
         i < (size_t)HUSH_EVENT_MAX_CONTENT; i++) {
        if (hush_agent_is_space(src[i])) {
            gap = 1;
            continue;
        }
        if (gap && o > 0)
            o = hush_agent_put_gap(out, o, cap);
        if (o >= cap)
            break;
        gap = 0;
        out[o] = src[i];
        o++;
    }
    out[o] = '\0';
}

static void hush_agent_append_turn(char *out, size_t outsz,
                                  const hush_event_t *ev, const char *who)
{
    char line[HUSH_AGENT_SNIP_MAX + 1];
    size_t used;

    assert(out != NULL);
    assert(ev != NULL);
    assert(who != NULL);
    hush_agent_snip_line(line, sizeof(line), ev->content);
    used = strlen(out);
    if (used + 8 >= outsz)
        return;
    (void)snprintf(out + used, outsz - used, "%s: %s\n", who, line);
}

static size_t hush_agent_thread_skip(const hush_event_t *evs, size_t n,
                                    const char *root)
{
    size_t i;
    size_t kept;
    size_t start;

    assert(evs != NULL);
    assert(root != NULL);
    kept = 0;
    start = 0;
    for (i = 0; i < n; i++) {
        if (!hush_agent_event_is_root(&evs[i], root))
            continue;
        kept++;
        if (kept > (size_t)HUSH_AGENT_THREAD_MAX)
            start++;
    }
    return start;
}

static void hush_agent_walk_thread(char *out, size_t outsz,
                                  const hush_event_t *evs, size_t n,
                                  const hush_agent_thread_walk_t *walk)
{
    size_t i;
    size_t start;
    size_t seen;
    const char *who;

    assert(out != NULL);
    assert(evs != NULL);
    assert(walk != NULL);
    assert(walk->root != NULL);
    assert(walk->human_pub != NULL);
    assert(walk->human != NULL);
    assert(walk->robot != NULL);
    start = hush_agent_thread_skip(evs, n, walk->root);
    seen = 0;
    for (i = 0; i < n; i++) {
        if (!hush_agent_event_is_root(&evs[i], walk->root))
            continue;
        if (seen < start) {
            seen++;
            continue;
        }
        who = walk->human;
        if (strcmp(evs[i].pubkey, walk->human_pub) != 0)
            who = walk->robot;
        hush_agent_append_turn(out, outsz, &evs[i], who);
        seen++;
    }
}

static void hush_agent_fill_thread(char *out, size_t outsz,
                                  hush_store_t *store,
                                  const hush_event_t *parent,
                                  const hush_agent_thread_walk_t *names)
{
    hush_event_t evs[HUSH_AGENT_SCAN_MAX];
    char root[HUSH_EVENT_ID_HEX_LEN + 1];
    hush_agent_thread_walk_t walk;
    size_t n;

    assert(out != NULL);
    assert(outsz > 0);
    assert(parent != NULL);
    assert(names != NULL);
    out[0] = '\0';
    if (store == NULL)
        return;
    hush_agent_event_root(root, sizeof(root), parent);
    n = hush_store_query(store, NULL, 0, evs, HUSH_AGENT_SCAN_MAX);
    hush_agent_copy(out, outsz, HUSH_AGENT_THREAD_HEAD);
    walk = *names;
    walk.root = root;
    walk.human_pub = parent->pubkey;
    hush_agent_walk_thread(out, outsz, evs, n, &walk);
}

static void hush_agent_fill_job(hush_agent_job_t *job,
                                const hush_agent_job_in_t *in)
{
    const hush_agent_robot_t *bot;
    const hush_event_t *parent;
    hush_agent_thread_walk_t names;

    assert(job != NULL);
    assert(in != NULL);
    bot = in->bot;
    parent = in->parent;
    assert(bot != NULL);
    assert(parent != NULL);
    memset(job, 0, sizeof(*job));
    job->fd = HUSH_AGENT_FD_NONE;
    job->busy = 1;
    job->kind = HUSH_AGENT_KIND_NOTE_JOB;
    job->started = time(NULL);
    hush_agent_event_root(job->parent_id, sizeof(job->parent_id), parent);
    hush_agent_event_channel(job->channel, sizeof(job->channel), parent);
    hush_agent_copy(job->human_pub, sizeof(job->human_pub), parent->pubkey);
    hush_agent_copy(job->robot_pub, sizeof(job->robot_pub),
                    bot->hex != NULL ? bot->hex : "");
    hush_agent_copy(job->robot_name, sizeof(job->robot_name),
                    bot->name != NULL ? bot->name : "robot");
    hush_agent_copy(job->robot_role, sizeof(job->robot_role),
                    bot->role != NULL && bot->role[0] != '\0'
                        ? bot->role : HUSH_ROSTER_ROLE_WORKER);
    hush_agent_make_token(job->token, sizeof(job->token));
    job->launch = in->launch;
    if (in->ask != NULL && in->ask[0] != '\0')
        hush_agent_copy(job->ask, sizeof(job->ask), in->ask);
    else if (in->parent->content[0] != '\0')
        hush_agent_copy(job->ask, sizeof(job->ask), in->parent->content);
    hush_agent_human_name(job->human_name, sizeof(job->human_name), in->launch);

    /* Collect co-robots from parent p-tags (for group mention negotiation seam).
     * Skip self and humans. These are passed into prompt and may become p-tags
     * on our reply if we address them with nostr:npub in content. */
    job->n_co_robots = 0;
    for (size_t t = 0;
         t < parent->tag_count && t < (size_t)HUSH_EVENT_MAX_TAGS &&
         job->n_co_robots < 4;
         ++t) {
        if (strcmp(parent->tags[t][0], "p") != 0)
            continue;
        const char *np = parent->tags[t][1];
        if (np == NULL || np[0] == '\0')
            continue;
        if (bot->hex && strcmp(np, bot->hex) == 0)
            continue; /* self */
        if (hush_agent_is_human(in->launch, np))
            continue;
        hush_agent_robot_t tmp;
        if (!hush_agent_lookup_robot(&tmp, in->launch, np))
            continue;
        hush_agent_copy(job->co_npubs[job->n_co_robots],
                        sizeof(job->co_npubs[0]), np);
        job->n_co_robots++;
    }

    hush_agent_fill_prompt(job->prompt, sizeof(job->prompt), bot, job->human_name);
    hush_agent_append_assign(job->prompt, sizeof(job->prompt), job->ask);

    /* Group mention seam + deliberation (M3.4):
     * Co-mentioned robots must decide: own reply? cooperate? split? full convo?
     * They can mention peers via nostr:npub (turned into p-tags by finish).
     * Prompt hygiene + existing hop limits prevent runaway loops. */
    if (job->n_co_robots > 0 && strlen(job->prompt) + 160 < sizeof(job->prompt)) {
        size_t off = strlen(job->prompt);
        const char *g = " Other robots mentioned: ";
        size_t glen = strlen(g);
        if (off + glen < sizeof(job->prompt)) {
            memcpy(job->prompt + off, g, glen);
            off += glen;
            for (int c = 0; c < job->n_co_robots && off + 70 < sizeof(job->prompt); ++c) {
                if (c > 0 && off + 1 < sizeof(job->prompt)) job->prompt[off++] = ' ';
                const char *np = job->co_npubs[c];
                size_t nlen = strlen(np);
                if (off + 6 + nlen + 1 >= sizeof(job->prompt)) break;
                memcpy(job->prompt + off, "nostr:", 6);
                off += 6;
                memcpy(job->prompt + off, np, nlen);
                off += nlen;
            }
        }
        const char *delib = " You + these peers were mentioned together. Decide strategy (own reply / cooperate on one / split / full convo among us). Call peers by emitting their nostr:npub in content with at least 1 word of context. Never leave a mention hanging at the end of a message.";
        if (off + strlen(delib) < sizeof(job->prompt)) {
            memcpy(job->prompt + off, delib, strlen(delib));
        }
    }

    hush_agent_fill_rules(job->rules, sizeof(job->rules), job->human_name);
    hush_agent_prepare_cwd(job->cwd, sizeof(job->cwd));
    memset(&names, 0, sizeof(names));
    names.human = job->human_name;
    names.robot = job->robot_name;
    hush_agent_fill_thread(job->note, sizeof(job->note), in->store, parent,
                           &names);
    if (job->note[0] == '\0')
        hush_agent_copy(job->note, sizeof(job->note), parent->content);

    /* Pills / channel topic -> system prompt injection.
     * If the channel has an "about" (topic), append a short pointer so the
     * robot's behavior is channel-aware without changing its base prompt. */
    if (in->launch != NULL && job->channel[0] != '\0') {
        const char *ab = hush_launch_channel_about(in->launch, job->channel);
        if (ab && ab[0] != '\0' &&
            strlen(job->prompt) + 24 + strlen(ab) < sizeof(job->prompt)) {
            size_t off = strlen(job->prompt);
            const char *pre = " Channel topic: ";
            size_t plen = strlen(pre);
            if (off + plen + strlen(ab) < sizeof(job->prompt)) {
                memcpy(job->prompt + off, pre, plen);
                off += plen;
                memcpy(job->prompt + off, ab, strlen(ab));
                off += strlen(ab);
                job->prompt[off] = '\0';
            }
        }
    }
}

static void hush_agent_exec_grok(int write_fd, const hush_agent_job_t *job)
{
    char *argv[HUSH_AGENT_ARGV_MAX];
    int dn;

    assert(job != NULL);
    if (write_fd >= 0)
        (void)dup2(write_fd, STDOUT_FILENO);
    if (write_fd >= 0)
        close(write_fd);
    dn = open(HUSH_AGENT_DEVNULL, O_WRONLY);
    if (dn >= 0) {
        (void)dup2(dn, STDERR_FILENO);
        close(dn);
    }
    argv[0] = (char *)HUSH_AGENT_GROK_BIN;
    argv[1] = (char *)"-p";
    argv[2] = (char *)job->note;
    argv[3] = (char *)"--system-prompt-override";
    argv[4] = (char *)job->prompt;
    argv[5] = (char *)"--output-format";
    argv[6] = (char *)"plain";
    argv[7] = (char *)"--always-approve";
    argv[8] = (char *)"--no-plan";
    argv[9] = (char *)"--no-subagents";
    argv[10] = (char *)"--disable-web-search";
    argv[11] = (char *)"--max-turns";
    argv[12] = (char *)(job->kind == HUSH_AGENT_KIND_FIXUP
                       ? HUSH_AGENT_FIXUP_TURNS : HUSH_AGENT_GROK_TURNS);
    argv[13] = (char *)"--reasoning-effort";
    argv[14] = (char *)HUSH_AGENT_GROK_EFFORT;
    argv[15] = (char *)"--cwd";
    argv[16] = (char *)job->cwd;
    argv[17] = (char *)"--disallowed-tools";
    argv[18] = (char *)HUSH_AGENT_DISALLOWED;
    argv[19] = (char *)"--rules";
    argv[20] = (char *)job->rules;
    argv[21] = (char *)HUSH_AGENT_GROK_NOMEM;
    argv[22] = NULL;
    execvp(HUSH_AGENT_GROK_BIN, argv);
    _exit(127);
}

static hush_status_t hush_agent_spawn_grok(hush_agent_job_t *job)
{
    int fds[2];
    pid_t pid;
    int flags;

    assert(job != NULL);
    if (pipe(fds) != 0)
        return HUSH_ERR_IO;
    pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return HUSH_ERR_IO;
    }
    if (pid == 0) {
        close(fds[0]);
        hush_agent_exec_grok(fds[1], job);
    }
    close(fds[1]);
    flags = fcntl(fds[0], F_GETFL, 0);
    if (flags >= 0)
        (void)fcntl(fds[0], F_SETFL, flags | O_NONBLOCK);
    job->pid = pid;
    job->fd = fds[0];
    hush_relay_track_child(pid);
    return HUSH_OK;
}

static void hush_agent_make_token(char *out, size_t outsz)
{
    unsigned n;

    assert(out != NULL);
    assert(outsz > 0);
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

static hush_status_t hush_agent_start_grok(const hush_agent_job_in_t *in)
{
    hush_agent_job_t *job;

    assert(in != NULL);
    assert(in->bot != NULL);
    assert(in->parent != NULL);
    job = hush_agent_find_slot();
    if (job == NULL)
        return HUSH_ERR_FULL;
    hush_agent_fill_job(job, in);
    if (hush_agent_spawn_grok(job) != HUSH_OK) {
        job->busy = 0;
        return HUSH_ERR_IO;
    }
    hush_agent_presence_put(in->store, job, HUSH_PRESENCE_SLUG_WORKING);
    return HUSH_OK;
}

static void hush_agent_close_job(hush_agent_job_t *job)
{
    assert(job != NULL);
    if (job->fd >= 0)
        close(job->fd);
    job->fd = HUSH_AGENT_FD_NONE;
    job->pid = 0;
    job->busy = 0;
}

static void hush_agent_kill_job(hush_agent_job_t *job)
{
    int status;

    assert(job != NULL);
    if (job->pid > 1) {
        (void)kill(job->pid, SIGTERM);
        (void)waitpid(job->pid, &status, WNOHANG);
    }
}

static void hush_agent_finish_job(hush_store_t *store, hush_agent_job_t *job,
                                  int ok)
{
    hush_agent_robot_t bot;
    hush_event_t parent;

    assert(job != NULL);
    hush_agent_trim(job->out);
    if (job->kind == HUSH_AGENT_KIND_FIXUP) {
        job->ok = ok && job->out[0] != '\0';
        job->busy = 0;
        if (job->fd >= 0)
            close(job->fd);
        job->fd = HUSH_AGENT_FD_NONE;
        job->pid = 0;
        return;
    }
    if (store != NULL && ok && job->out[0] != '\0') {
        hush_agent_note_in_t in = {
            .pubkey = job->robot_pub,
            .content = job->out,
            .channel = job->channel,
            .parent_id = job->parent_id,
            .human_pub = job->human_pub
        };
        /* Group mention seam: if this robot addressed a co-mentioned peer by
         * writing the peer's nostr:npub (or the npub appears) in its reply,
         * emit a real p-tag for it. This makes the peer receive dispatch + ack.
         * Co-npubs live in this job until after insert. */
        int ei = 0;
        for (int c = 0; c < job->n_co_robots && ei < 3; ++c) {
            if (strstr(job->out, job->co_npubs[c])) {
                in.extra_p[ei++] = job->co_npubs[c];
            }
        }

        (void)hush_agent_insert_note(store, &in);
        if (job->launch != NULL) {
            hush_event_t posted;

            memset(&posted, 0, sizeof(posted));
            hush_agent_copy(posted.pubkey, sizeof(posted.pubkey),
                            job->robot_pub);
            posted.kind = (uint32_t)HUSH_AGENT_KIND_NOTE;
            hush_agent_copy(posted.content, sizeof(posted.content), job->out);
            posted.tag_count = 2;
            memcpy(posted.tags[0][0], "h", 2);
            hush_agent_copy(posted.tags[0][1], sizeof(posted.tags[0][1]),
                            job->channel);
            memcpy(posted.tags[1][0], "e", 2);
            hush_agent_copy(posted.tags[1][1], sizeof(posted.tags[1][1]),
                            job->parent_id);
            hush_agent_emit(HUSH_CEVENT_JOB_DONE, job->channel, job->parent_id,
                            job->robot_pub, "job_done");
            hush_agent_on_posted(store, job->launch, &posted);
        }
        hush_agent_presence_put(store, job, HUSH_PRESENCE_SLUG_IDLE);
        hush_agent_close_job(job);
        return;
    }
    if (store != NULL) {
        memset(&bot, 0, sizeof(bot));
        memset(&parent, 0, sizeof(parent));
        bot.name = job->robot_name;
        bot.hex = job->robot_pub;
        hush_agent_copy(parent.id, sizeof(parent.id), job->parent_id);
        hush_agent_copy(parent.pubkey, sizeof(parent.pubkey), job->human_pub);
        parent.tag_count = 1;
        memcpy(parent.tags[0][0], "h", 2);
        hush_agent_copy(parent.tags[0][1], sizeof(parent.tags[0][1]),
                        job->channel);
        /* M3.1 dev log gate: this error on_deck path is internal.
         * Suppress to keep main chat clean (main intros gated at dispatch).
         * When dev logging is later wired to a panel we can surface here. */
        (void)store; (void)&bot; (void)&parent; (void)ok; /* no-op for now */
    }
    hush_agent_close_job(job);
}

static void hush_agent_read_job(hush_agent_job_t *job)
{
    char buf[256];
    ssize_t n;
    size_t room;

    assert(job != NULL);
    if (job->fd < 0)
        return;
    for (;;) {
        room = sizeof(job->out) - 1 - job->out_n;
        if (room == 0)
            break;
        n = read(job->fd, buf, sizeof(buf) < room ? sizeof(buf) : room);
        if (n > 0) {
            memcpy(job->out + job->out_n, buf, (size_t)n);
            job->out_n += (size_t)n;
            job->out[job->out_n] = '\0';
            continue;
        }
        if (n == 0) {
            close(job->fd);
            job->fd = HUSH_AGENT_FD_NONE;
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;
        close(job->fd);
        job->fd = HUSH_AGENT_FD_NONE;
        return;
    }
}

static int hush_agent_job_timed_out(const hush_agent_job_t *job, time_t now)
{
    assert(job != NULL);
    if (job->started <= 0)
        return 0;
    return now >= job->started + (time_t)HUSH_AGENT_TIMEOUT_S;
}

static int hush_agent_robot_busy(const hush_agent_robot_t *bot,
                                 const hush_event_t *parent)
{
    char root[HUSH_EVENT_ID_HEX_LEN + 1];
    const char *hex;
    size_t i;

    assert(bot != NULL);
    assert(parent != NULL);
    hex = bot->hex != NULL ? bot->hex : "";
    if (hex[0] == '\0')
        return 0;
    hush_agent_event_root(root, sizeof(root), parent);
    for (i = 0; i < (size_t)HUSH_AGENT_JOBS_MAX; i++) {
        if (!g_jobs[i].busy)
            continue;
        if (strcmp(g_jobs[i].robot_pub, hex) != 0)
            continue;
        if (strcmp(g_jobs[i].parent_id, root) == 0)
            return 1;
    }
    return 0;
}

static void hush_agent_handle_mention(hush_store_t *store,
                                      const hush_launch_t *launch,
                                      const hush_event_t *ev,
                                      const char *mention)
{
    hush_agent_robot_t bot;
    char hexes[HUSH_AGENT_FOLLOW_ROBOTS][HUSH_EVENT_PUBKEY_HEX_LEN + 1];
    size_t nhex = 0;
    size_t idx;

    assert(store != NULL);
    assert(launch != NULL);
    assert(ev != NULL);
    if (mention == NULL || mention[0] == '\0')
        return;
    if (hush_agent_is_human(launch, mention))
        return;
    if (!hush_agent_lookup_robot(&bot, launch, mention))
        return;
    if (!hush_agent_is_work_ok(launch, &bot))
        return;
    if (hush_agent_robot_busy(&bot, ev))
        return;
    hush_agent_emit(HUSH_CEVENT_MENTION, NULL, NULL, bot.hex, mention);
    if (hush_agent_is_human(launch, ev->pubkey)) {
        nhex = hush_agent_collect_hexes(launch, ev, hexes,
                                        (size_t)HUSH_AGENT_FOLLOW_ROBOTS);
        for (idx = 0; idx < nhex; idx++) {
            hush_agent_robot_t peer;

            if (!hush_agent_lookup_robot(&peer, launch, hexes[idx]))
                continue;
            hush_agent_on_deck(store, &peer, ev,
                               "I am on deck. Standing orders are noted.");
        }
        for (idx = 0; idx < nhex; idx++) {
            if (bot.hex != NULL && strcmp(hexes[idx], bot.hex) == 0)
                break;
        }
        if (idx > 0 && idx < nhex) {
            hush_agent_follow_push(ev, launch, hexes, nhex, idx);
            return;
        }
        if (idx == 0 && nhex > 1)
            hush_agent_follow_push(ev, launch, hexes, nhex, 1);
    }
    {
        hush_agent_job_in_t in;

        memset(&in, 0, sizeof(in));
        in.store = store;
        in.launch = launch;
        in.bot = &bot;
        in.parent = ev;
        in.ask = ev->content;
        hush_agent_begin_work(&in);
    }
}

static int hush_agent_is_work_ok(const hush_launch_t *launch,
                                 const hush_agent_robot_t *bot)
{
    assert(launch != NULL);
    assert(bot != NULL);
    if (bot->slug != NULL && strcmp(bot->slug, HUSH_LAUNCH_PAYNE_SLUG) == 0)
        return 1;
    if (bot->role != NULL && strcmp(bot->role, HUSH_ROSTER_ROLE_CHAPERON) == 0)
        return 0;
    return 1;
}

static void hush_agent_push_hex(char hexes[][HUSH_EVENT_PUBKEY_HEX_LEN + 1],
                                size_t *n, size_t maxn, const char *hex)
{
    size_t i;

    assert(n != NULL);
    if (hex == NULL || hex[0] == '\0' || *n >= maxn)
        return;
    for (i = 0; i < *n; i++) {
        if (strcmp(hexes[i], hex) == 0)
            return;
    }
    hush_agent_copy(hexes[*n], sizeof(hexes[0]), hex);
    (*n)++;
}

static size_t hush_agent_collect_hexes(const hush_launch_t *launch,
                                       const hush_event_t *ev,
                                       char hexes[][HUSH_EVENT_PUBKEY_HEX_LEN + 1],
                                       size_t maxn)
{
    hush_agent_robot_t bot;
    const char *p;
    size_t n = 0;
    size_t i;

    assert(launch != NULL);
    assert(ev != NULL);
    p = ev->content;
    while (p != NULL && *p != '\0' && n < maxn) {
        char tok[HUSH_IDENTITY_NPUB_MAX];
        size_t k = 0;

        p = strstr(p, "nostr:");
        if (p == NULL)
            break;
        p += 6;
        while (p[k] != '\0' && !hush_agent_is_space(p[k])
               && k + 1 < sizeof(tok)) {
            tok[k] = p[k];
            k++;
        }
        tok[k] = '\0';
        if (hush_agent_lookup_robot(&bot, launch, tok) &&
            hush_agent_is_work_ok(launch, &bot))
            hush_agent_push_hex(hexes, &n, maxn, bot.hex);
        p += k > 0 ? k : 1;
    }
    for (i = 0; i < ev->tag_count && i < (size_t)HUSH_EVENT_MAX_TAGS; i++) {
        if (strcmp(ev->tags[i][0], "p") != 0)
            continue;
        if (!hush_agent_lookup_robot(&bot, launch, ev->tags[i][1]))
            continue;
        if (!hush_agent_is_work_ok(launch, &bot))
            continue;
        hush_agent_push_hex(hexes, &n, maxn, bot.hex);
    }
    return n;
}

static hush_agent_follow_t *hush_agent_follow_find(const char *root)
{
    size_t i;

    if (root == NULL || root[0] == '\0')
        return NULL;
    for (i = 0; i < (size_t)HUSH_AGENT_FOLLOW_MAX; i++) {
        if (g_follow[i].live && strcmp(g_follow[i].root, root) == 0)
            return &g_follow[i];
    }
    return NULL;
}

static hush_agent_follow_t *hush_agent_follow_take(const char *root)
{
    hush_agent_follow_t *slot;
    size_t i;

    slot = hush_agent_follow_find(root);
    if (slot != NULL)
        return slot;
    for (i = 0; i < (size_t)HUSH_AGENT_FOLLOW_MAX; i++) {
        if (!g_follow[i].live) {
            memset(&g_follow[i], 0, sizeof(g_follow[i]));
            g_follow[i].live = 1;
            hush_agent_copy(g_follow[i].root, sizeof(g_follow[i].root), root);
            return &g_follow[i];
        }
    }
    memset(&g_follow[0], 0, sizeof(g_follow[0]));
    g_follow[0].live = 1;
    hush_agent_copy(g_follow[0].root, sizeof(g_follow[0].root), root);
    return &g_follow[0];
}

static void hush_agent_follow_push(const hush_event_t *ev,
                                   const hush_launch_t *launch,
                                   char hexes[][HUSH_EVENT_PUBKEY_HEX_LEN + 1],
                                   size_t nhex, size_t start)
{
    hush_agent_follow_t *slot;
    char root[HUSH_EVENT_ID_HEX_LEN + 1];
    size_t i;

    assert(ev != NULL);
    assert(launch != NULL);
    hush_agent_event_root(root, sizeof(root), ev);
    slot = hush_agent_follow_take(root);
    hush_agent_event_channel(slot->channel, sizeof(slot->channel), ev);
    hush_agent_copy(slot->human_pub, sizeof(slot->human_pub), ev->pubkey);
    hush_agent_copy(slot->ask, sizeof(slot->ask), ev->content);
    for (i = start; i < nhex && slot->nnext < (size_t)HUSH_AGENT_FOLLOW_ROBOTS;
         i++)
        hush_agent_push_hex(slot->next, &slot->nnext,
                            (size_t)HUSH_AGENT_FOLLOW_ROBOTS, hexes[i]);
}

static void hush_agent_presence_put(hush_store_t *store, hush_agent_job_t *job,
                                    const char *slug)
{
    hush_presence_in_t in;

    assert(job != NULL);
    assert(slug != NULL);
    if (store == NULL || job->token[0] == '\0')
        return;
    if (job->kind == HUSH_AGENT_KIND_FIXUP)
        return;
    memset(&in, 0, sizeof(in));
    in.pubkey = job->robot_pub;
    in.role = job->robot_role;
    in.token = job->token;
    in.slug = slug;
    in.channel = job->channel;
    in.root = job->parent_id;
    in.now = time(NULL);
    if (hush_presence_publish(store, &in) == HUSH_OK)
        hush_agent_copy(job->presence_slug, sizeof(job->presence_slug), slug);
}

static void hush_agent_nudge_stuck(hush_store_t *store, hush_agent_job_t *job)
{
    hush_event_t ev;
    char line[HUSH_EVENT_MAX_CONTENT];

    assert(job != NULL);
    if (store == NULL || job->launch == NULL)
        return;
    if (!job->launch->payne_enabled)
        return;
    if (strcmp(job->robot_pub, job->launch->payne.pubkey_hex) == 0)
        return;
    if (snprintf(line, sizeof(line),
                 "Stuck: %s needs unstuck or disable. %s",
                 job->robot_name[0] ? job->robot_name : "robot",
                 job->ask[0] ? job->ask : "") >= (int)sizeof(line))
        hush_agent_copy(line, sizeof(line), "Stuck. Unstuck or disable.");
    memset(&ev, 0, sizeof(ev));
    hush_agent_copy(ev.pubkey, sizeof(ev.pubkey), job->robot_pub);
    ev.kind = (uint32_t)HUSH_AGENT_KIND_NOTE;
    ev.created_at = (int64_t)time(NULL);
    hush_agent_copy(ev.content, sizeof(ev.content), line);
    ev.tag_count = 3;
    memcpy(ev.tags[0][0], "h", 2);
    hush_agent_copy(ev.tags[0][1], sizeof(ev.tags[0][1]), job->channel);
    memcpy(ev.tags[1][0], "e", 2);
    hush_agent_copy(ev.tags[1][1], sizeof(ev.tags[1][1]), job->parent_id);
    memcpy(ev.tags[2][0], "p", 2);
    hush_agent_copy(ev.tags[2][1], sizeof(ev.tags[2][1]),
                    job->launch->payne.pubkey_hex);
    hush_agent_mention(store, (hush_launch_t *)job->launch, &ev,
                       job->launch->payne.npub);
}

static int hush_agent_job_enabled(const hush_agent_job_t *job)
{
    hush_agent_robot_t bot;

    assert(job != NULL);
    if (job->launch == NULL)
        return 1;
    return hush_agent_lookup_robot(&bot, job->launch, job->robot_pub);
}

static void hush_agent_begin_work(const hush_agent_job_in_t *in)
{
    char root[HUSH_EVENT_ID_HEX_LEN + 1];
    char channel[HUSH_EVENT_MAX_TAG_LEN + 1];
    hush_agent_job_in_t job;

    assert(in != NULL);
    assert(in->store != NULL);
    assert(in->bot != NULL);
    assert(in->parent != NULL);
    if (hush_agent_turns_full(in->store, in->launch, in->parent)) {
        hush_agent_nudge_chaperon(in->store, in->launch, in->parent);
        return;
    }
    hush_agent_on_deck(in->store, in->bot, in->parent,
                       "I am on deck. Standing orders are noted.");
    hush_agent_event_root(root, sizeof(root), in->parent);
    hush_agent_event_channel(channel, sizeof(channel), in->parent);
    hush_agent_emit(HUSH_CEVENT_INTRO, channel, root, in->bot->hex,
                    in->bot->name);
    if (!hush_agent_can_start_grok(in->launch, in->bot))
        return;
    job = *in;
    if (hush_agent_start_grok(&job) == HUSH_OK)
        hush_agent_emit(HUSH_CEVENT_JOB_START, channel, root, in->bot->hex,
                        in->ask != NULL ? in->ask : "");
}

static void hush_agent_follow_kick(hush_store_t *store,
                                   const hush_launch_t *launch,
                                   const hush_event_t *ev)
{
    hush_agent_follow_t *slot;
    hush_agent_robot_t bot;
    hush_agent_job_in_t in;
    char root[HUSH_EVENT_ID_HEX_LEN + 1];
    const char *hex;
    size_t n;

    assert(store != NULL);
    assert(launch != NULL);
    assert(ev != NULL);
    hush_agent_event_root(root, sizeof(root), ev);
    slot = hush_agent_follow_find(root);
    if (slot == NULL || slot->at >= slot->nnext)
        return;
    for (n = 0; n < (size_t)HUSH_AGENT_FOLLOW_ROBOTS && slot->at < slot->nnext;
         n++) {
        hex = slot->next[slot->at];
        slot->at++;
        if (!hush_agent_lookup_robot(&bot, launch, hex))
            continue;
        if (!hush_agent_is_work_ok(launch, &bot))
            continue;
        hush_agent_emit(HUSH_CEVENT_FOLLOW, slot->channel, root, bot.hex,
                        "follow");
        memset(&in, 0, sizeof(in));
        in.store = store;
        in.launch = launch;
        in.bot = &bot;
        in.parent = ev;
        in.ask = slot->ask[0] ? slot->ask : ev->content;
        hush_agent_begin_work(&in);
        return;
    }
}

static void hush_agent_emit(const char *type, const char *channel,
                            const char *root, const char *actor,
                            const char *note)
{
    hush_cevent_t ev;

    memset(&ev, 0, sizeof(ev));
    hush_agent_copy(ev.type, sizeof(ev.type), type);
    hush_agent_copy(ev.channel, sizeof(ev.channel), channel);
    hush_agent_copy(ev.root, sizeof(ev.root), root);
    hush_agent_copy(ev.actor, sizeof(ev.actor), actor);
    hush_agent_copy(ev.note, sizeof(ev.note), note);
    (void)hush_cevent_emit(&ev);
}

static void hush_agent_append_assign(char *prompt, size_t promptsz,
                                     const char *ask)
{
    char snip[HUSH_AGENT_SNIP_MAX + 1];
    size_t used;
    int n;

    assert(prompt != NULL);
    assert(promptsz > 0);
    if (ask == NULL || ask[0] == '\0')
        return;
    hush_agent_snip_line(snip, sizeof(snip), ask);
    used = strlen(prompt);
    if (used + 8 >= promptsz)
        return;
    n = snprintf(prompt + used, promptsz - used, "%s%s",
                 HUSH_AGENT_ASSIGN, snip);
    if (n < 0)
        prompt[used] = '\0';
}

static const hush_launch_channel_t *hush_agent_channel(
    const hush_launch_t *launch, const char *slug)
{
    size_t i;

    if (launch == NULL || slug == NULL || slug[0] == '\0')
        return NULL;
    for (i = 0; i < launch->nchannels && i < (size_t)HUSH_LAUNCH_CHANNELS_MAX;
         i++) {
        if (strcmp(launch->channels[i].slug, slug) == 0)
            return &launch->channels[i];
    }
    return NULL;
}

static size_t hush_agent_count_turns(hush_store_t *store,
                                     const hush_launch_t *launch,
                                     const char *root)
{
    hush_event_t evs[HUSH_AGENT_SCAN_MAX];
    hush_agent_robot_t bot;
    size_t n;
    size_t i;
    size_t turns = 0;

    assert(store != NULL);
    assert(launch != NULL);
    assert(root != NULL);
    n = hush_store_query(store, NULL, 0, evs, HUSH_AGENT_SCAN_MAX);
    for (i = 0; i < n; i++) {
        if (!hush_agent_event_is_root(&evs[i], root))
            continue;
        if (hush_agent_is_human(launch, evs[i].pubkey))
            continue;
        if (!hush_agent_lookup_robot(&bot, launch, evs[i].pubkey))
            continue;
        if (!hush_agent_is_work_note(evs[i].content))
            continue;
        turns++;
    }
    return turns;
}

static int hush_agent_is_work_note(const char *content)
{
    static const char *const skip[] = {
        HUSH_AGENT_INTRO_PREFIX,
        HUSH_AGENT_ACK_LINE,
        HUSH_AGENT_CHAPERON_LINE,
        "Holding.",
        "Robots do not chain",
        "Not on this channel.",
        "Say the ask.",
        "I heard:",
        "This channel is humans talking"
    };
    size_t i;
    size_t n;

    if (content == NULL || content[0] == '\0')
        return 0;
    for (i = 0; i < sizeof(skip) / sizeof(skip[0]); i++) {
        n = strlen(skip[i]);
        if (strncmp(content, skip[i], n) == 0)
            return 0;
    }
    return 1;
}

static int hush_agent_turns_full(hush_store_t *store,
                                 const hush_launch_t *launch,
                                 const hush_event_t *ev)
{
    const hush_launch_channel_t *ch;
    char channel[HUSH_EVENT_MAX_TAG_LEN + 1];
    char root[HUSH_EVENT_ID_HEX_LEN + 1];
    int cap;

    assert(store != NULL);
    assert(ev != NULL);
    if (launch == NULL)
        return 0;
    hush_agent_event_channel(channel, sizeof(channel), ev);
    ch = hush_agent_channel(launch, channel);
    cap = HUSH_LAUNCH_TURNS_DEFAULT;
    if (ch != NULL && ch->max_robot_turns > 0)
        cap = ch->max_robot_turns;
    hush_agent_event_root(root, sizeof(root), ev);
    return hush_agent_count_turns(store, launch, root) >= (size_t)cap;
}

static int hush_agent_lookup_slug(hush_agent_robot_t *out,
                                  const hush_launch_t *launch,
                                  const char *slug)
{
    size_t i;
    const hush_roster_agent_t *agent;

    assert(out != NULL);
    assert(launch != NULL);
    if (slug == NULL || slug[0] == '\0' ||
        strcmp(slug, HUSH_LAUNCH_PAYNE_SLUG) == 0)
        return hush_agent_lookup_robot(out, launch, launch->payne.pubkey_hex);
    for (i = 0; i < launch->roster.nagents; i++) {
        agent = &launch->roster.agents[i];
        if (strcmp(agent->slug, slug) != 0)
            continue;
        return hush_agent_lookup_robot(out, launch, agent->id.pubkey_hex);
    }
    return 0;
}

static void hush_agent_nudge_chaperon(hush_store_t *store,
                                      const hush_launch_t *launch,
                                      const hush_event_t *ev)
{
    const hush_launch_channel_t *ch;
    hush_agent_robot_t bot;
    hush_agent_note_in_t in;
    char channel[HUSH_EVENT_MAX_TAG_LEN + 1];
    char root[HUSH_EVENT_ID_HEX_LEN + 1];
    const char *slug;

    assert(store != NULL);
    assert(ev != NULL);
    hush_agent_event_channel(channel, sizeof(channel), ev);
    hush_agent_event_root(root, sizeof(root), ev);
    ch = hush_agent_channel(launch, channel);
    slug = HUSH_LAUNCH_PAYNE_SLUG;
    if (ch != NULL && ch->chaperon[0] != '\0')
        slug = ch->chaperon;
    hush_agent_emit(HUSH_CEVENT_CHAPERON, channel, root, slug,
                    HUSH_AGENT_CHAPERON_LINE);
    if (launch == NULL || !hush_agent_lookup_slug(&bot, launch, slug))
        return;
    memset(&in, 0, sizeof(in));
    in.pubkey = bot.hex != NULL ? bot.hex : "";
    in.content = HUSH_AGENT_CHAPERON_LINE;
    in.channel = channel;
    in.parent_id = root;
    in.human_pub = ev->pubkey;
    (void)hush_agent_insert_note(store, &in);
}
