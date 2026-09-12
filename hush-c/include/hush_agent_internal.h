/* hush_agent_internal.h: shared types, constants, and helpers between the
 * agent core and its per-cluster modules. Internal to the relay; not
 * installed. */

#ifndef HUSH_AGENT_INTERNAL_H
#define HUSH_AGENT_INTERNAL_H

#include <stddef.h>
#include <sys/types.h>
#include <time.h>

#include "hush_event.h"
#include "hush_launch.h"
#include "hush_presence.h"
#include "hush_roster.h"
#include "hush_skill.h"
#include "hush_status.h"
#include "hush_store.h"

enum {
    HUSH_AGENT_INTRO_MAX = 32,
    HUSH_AGENT_KIND_NOTE = 1,
    HUSH_AGENT_ARGV_MAX = 28,
    HUSH_AGENT_PATH_MAX = 256,
    HUSH_AGENT_FD_NONE = -1,
    HUSH_AGENT_THREAD_MAX = 6,
    /* Seconds a cancelled job gets to exit on SIGTERM before SIGKILL. */
    HUSH_AGENT_CANCEL_GRACE_S = 3,
    HUSH_AGENT_PAIR_COUNT = 2,
    /* Soft cap for flattened thread/assignment lines. Two nostr:npub
     * tokens are 138 bytes; 160 cut the second token and the LLM
     * copied the stump as @npub1t337pnf. Tokens themselves are copied
     * atomically even when they overrun this cap, up to outsz. */
    HUSH_AGENT_SNIP_MAX = 384,
    HUSH_AGENT_NPUB_MIN = 12,
    HUSH_AGENT_ECHO_MIN = 20,
    HUSH_AGENT_NOSTR_NPUB_LEN = 11,
    HUSH_AGENT_NPUB_HEAD_LEN = 5,
    HUSH_AGENT_NOSTR_HEAD_LEN = 6,
    HUSH_AGENT_AT_NPUB_LEN = 6,
    HUSH_AGENT_SCAN_MAX = 64,
    HUSH_AGENT_TASK_MAX = 512,
    HUSH_AGENT_KIND_NOTE_JOB = 0,
    HUSH_AGENT_KIND_FIXUP = 1,
    HUSH_AGENT_KIND_PLAN = 2,
    HUSH_AGENT_KIND_ELECT = 3,
    HUSH_AGENT_TOKEN_MAX = 16,
    HUSH_AGENT_FOLLOW_MAX = 8,
    /* Two prompt strings contribute their own NUL allowance; the note's
     * extra byte covers the two joining newlines plus the final NUL. */
    HUSH_AGENT_NOTE_MAX = HUSH_EVENT_MAX_CONTENT * 3 + 1,
    HUSH_AGENT_SYSTEM_MAX = HUSH_ROSTER_PROMPT_MAX * 2 + HUSH_SKILL_CHAR_HIGH +
                            HUSH_LAUNCH_PROMPT_BYTES +
                            HUSH_SKILL_EQUIP_MAX * (HUSH_SKILL_ID_MAX + 1),
    HUSH_AGENT_COMBINED_PROMPT_MAX = HUSH_AGENT_SYSTEM_MAX + HUSH_ROSTER_PROMPT_MAX +
                                     HUSH_AGENT_NOTE_MAX + 1,
    HUSH_AGENT_EXEC_FAILURE = 127,
    HUSH_AGENT_CAPTURE_MAX = 131072,
    HUSH_AGENT_WAIT_MAX = 8,
    HUSH_AGENT_FOLLOW_ROBOTS = 8
};

typedef struct {
    int busy;
    int kind;
    int done;
    int ok;
    int cancelled;
    pid_t pid;
    time_t kill_deadline;
    int fd;
    time_t started;
    char token[HUSH_AGENT_TOKEN_MAX];
    char parent_id[HUSH_EVENT_ID_HEX_LEN + 1];
    char trigger_id[HUSH_EVENT_ID_HEX_LEN + 1];
    char channel[HUSH_EVENT_MAX_TAG_LEN + 1];
    char human_pub[HUSH_EVENT_PUBKEY_HEX_LEN + 1];
    char robot_pub[HUSH_EVENT_PUBKEY_HEX_LEN + 1];
    char robot_name[HUSH_ROSTER_NAME_MAX];
    char robot_role[HUSH_ROSTER_NAME_MAX];
    char provider[HUSH_ROSTER_PROVIDER_MAX];
    char presence_slug[HUSH_PRESENCE_SLUG_MAX];
    char human_name[HUSH_ROSTER_NAME_MAX];
    char prompt[HUSH_AGENT_SYSTEM_MAX];
    char rules[HUSH_ROSTER_PROMPT_MAX];
    char cwd[HUSH_AGENT_PATH_MAX];
    char note[HUSH_AGENT_NOTE_MAX];
    char out[HUSH_EVENT_MAX_CONTENT + 1];
    size_t out_n;
    /* Co-robots mentioned together with this one on the triggering note.
     * Enables group negotiation: robots can see peers and p-mention back. */
    char co_npubs[4][HUSH_IDENTITY_NPUB_MAX];
    char co_names[4][HUSH_ROSTER_NAME_MAX];
    int n_co_robots;
    /* True when this robot is in the last follow wave for the note. */
    int last;
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
    /* Per-robot sub-task (explicit clause or leader plan task). Empty means
     * fall back to slot->ask. Indexed in parallel with next[]. */
    char next_ask[HUSH_AGENT_FOLLOW_ROBOTS][HUSH_AGENT_TASK_MAX];
    /* Parallel wave (group) per task. Tasks sharing a group run in parallel;
     * groups run in order. 0 = unassigned (serial). */
    int group[HUSH_AGENT_FOLLOW_ROBOTS];
    int scoped;
    int mode;
    int order;    /* 0 = fifo, 1 = lifo */
    int parallel; /* 0 = serial, 1 = all-tasks-parallel (legacy plan-level) */
    int inflight; /* tasks dispatched in the current group not yet finished */
    int electing; /* 1 while waiting for the leader election to finish */
    char convener[HUSH_EVENT_PUBKEY_HEX_LEN + 1]; /* runs election + fallback */
} hush_agent_follow_t;

/* How a human note is interpreted for the tagged robot group. */
typedef enum {
    HUSH_AGENT_MODE_SOLO = 0,
    HUSH_AGENT_MODE_EXPLICIT,
    HUSH_AGENT_MODE_BROADCAST,
    HUSH_AGENT_MODE_AMBIGUOUS
} hush_agent_mode_t;

/* One robot's extracted assignment (clause) from an explicit delegation. */
typedef struct {
    char hex[HUSH_EVENT_PUBKEY_HEX_LEN + 1];
    char ask[HUSH_AGENT_TASK_MAX];
    int has_ask;
} hush_agent_assign_t;

typedef struct {
    const char *name;
    const char *npub;
    const char *hex;
    const char *provider;
    /* Ranked provider list (index 0 = primary). Populated from the roster
     * agent; may be empty for Payne (which has its own payne_providers). */
    const char *providers[HUSH_ROSTER_PROVIDERS_MAX];
    size_t nproviders;
    const char *prompt;
    const char *slug;
    const char *role;
    const char *intro;
    int intro_enabled;
    /* Attached file context (points into the roster agent). Empty when the
     * robot carries no files. Consumed by hush_agent_append_context(). */
    const hush_roster_context_t *context;
    size_t ncontext;
    const char (*skills)[HUSH_SKILL_ID_MAX];
    size_t nskills;
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
    const hush_launch_t *launch;
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
    /* True when this robot's ask is a strict per-robot sub-task (explicit
     * delegation or a leader plan task), not the shared human ask. */
    int scoped;
    /* How the human note was classified (solo / explicit / broadcast /
     * ambiguous). Drives cooperation vs leader vs strict-scope prompting. */
    int mode;
    /* True when this job is the leader's division-of-labor planning pass. */
    int leader;
    /* True when this job is the leader-election pass. */
    int elect;
    /* Prebuilt system-prompt override (election prompt). May be NULL. */
    const char *prompt_override;
    /* True when this robot is in the last follow wave (or is solo). */
    int last;
} hush_agent_job_in_t;

typedef struct {
    const char *name;
    const char *npub;
} hush_agent_alias_t;

typedef struct {
    const hush_agent_alias_t *aliases;
    size_t naliases;
} hush_agent_alias_set_t;
typedef struct {
    char hexes[HUSH_AGENT_FOLLOW_ROBOTS][HUSH_EVENT_PUBKEY_HEX_LEN + 1];
    hush_agent_assign_t assigns[HUSH_AGENT_FOLLOW_ROBOTS];
    size_t count;
    hush_agent_mode_t mode;
} hush_agent_mentions_t;


/* Forks the provider for job. Fails HUSH_ERR_IO/FULL. */
hush_status_t hush_agent_spawn_grok(hush_agent_job_t *job);

/* Borrowed live job table. */
hush_agent_job_t *hush_agent_jobs(void);

/* Copies text into dst, truncated at dstsz. */
void hush_agent_copy(char *dst, size_t dstsz, const char *src);

/* Trims trailing whitespace in place. */
void hush_agent_trim(char *text);

/* Writes the provider working directory into out. */
void hush_agent_prepare_cwd(char *out, size_t outsz);

/* Writes the event's h-tag channel into out, or general. */
void hush_agent_event_channel(char *out, size_t outsz, const hush_event_t *ev);

/* Writes the event's reply-to root into out. */
void hush_agent_event_root(char *out, size_t outsz, const hush_event_t *ev);

/* Looks up a robot by mention or hex. 0 when unknown. */
int hush_agent_lookup_robot(hush_agent_robot_t *out, const hush_launch_t *launch, const char *mention);

/* True when ev's root tag equals root. */
int hush_agent_event_is_root(const hush_event_t *ev, const char *root);

/* Snips a note line at the flattened cap. */
void hush_agent_snip_line(char *out, size_t outsz, const char *src);

/* Appends the turn marker line. */
void hush_agent_append_turn(char *out, size_t outsz, const hush_event_t *ev,
                            const char *who);

/* True when content is a work note. */
int hush_agent_is_work_note(const char *content);

/* Fills the thread transcript for parent into out. */
void hush_agent_fill_thread(char *out, size_t outsz, hush_store_t *store,
                             const hush_launch_t *launch,
                             const hush_event_t *parent,
                             const hush_agent_thread_walk_t *names);

/* Appends the robot's context notes to note. */
void hush_agent_append_context(char *note, size_t notesz,
                               const hush_agent_robot_t *bot);

/* Writes the human display name into out. */
void hush_agent_human_name(char *out, size_t outsz, const hush_launch_t *launch);

/* ---- shared prompt strings (core + agent_dispatch) ---- */

#define HUSH_AGENT_INTRO_PREFIX "At ease."
#define HUSH_AGENT_HUMAN_FALLBACK "you"
#define HUSH_AGENT_ELECT_PROMPT \
    " You are the election committee. Elect the single best leader for the " \
    "task below from these candidates. Consider their skills and fit. Reply " \
    "with exactly one candidate name and nothing else."

/* ---- agent_dispatch.c: job lifecycle and dispatch/follow flow ---- */

/* Clears the follow table. Called once from hush_agent_init(). */
void hush_agent_follow_init(void);

/* Dispatches one known eligible robot from required event context. */
void hush_agent_handle_mention(hush_store_t *store, const hush_launch_t *launch,
                               const hush_event_t *event, const char *mention);

/* Reads pending output; more means progress is immediately available. */
void hush_agent_read_job(hush_agent_job_t *job);

/* Completes a finished job, posting its reply and handoffs. */
void hush_agent_finish_job(hush_store_t *store, hush_agent_job_t *job, int ok);

/* True when the job outlived its provider turn budget. */
int hush_agent_job_timed_out(const hush_agent_job_t *job, time_t now);

/* True when the job's provider/roster turn is still enabled. */
int hush_agent_job_enabled(const hush_agent_job_t *job);

/* Publishes the job's current presence state. */
void hush_agent_presence_put(hush_store_t *store, hush_agent_job_t *job,
                             const char *slug);

/* Posts a diagnostic when a running job stalls past its progress window. */
void hush_agent_nudge_stuck(hush_store_t *store, hush_agent_job_t *job);

/* Releases the job's wake line on the stored conversation. */
void hush_agent_release_line(hush_store_t *store, hush_agent_job_t *job);

/* Appends " @PeerName" when a scoped assignment is a dangling delegation. */
void hush_agent_name_dangling_peer(hush_agent_job_t *job, int scoped);

/* Appends one robot assignment line to the leader plan prompt. */
void hush_agent_append_assign(char *prompt, size_t promptsz, const char *ask,
                              const hush_launch_t *launch,
                              const char *self_hex);

/* Resolves a channel slug to its launch configuration. */
const hush_launch_channel_t *hush_agent_channel(
    const hush_launch_t *launch, const char *slug);

/* Kicks the next follow wave for a newly posted human event. */
void hush_agent_follow_kick(hush_store_t *store, const hush_launch_t *launch,
                            const hush_event_t *ev);

/* ---- hush_agent.c helpers shared with the per-cluster modules ---- */

/* True when mention names the human creator. */
int hush_agent_is_human(const hush_launch_t *launch, const char *mention);

/* True when ch is space, tab, CR, or LF. Pure. */
int hush_agent_is_space(char ch);

/* Fills borrowed event fields from a prepared note input. */
void hush_agent_fill_note(hush_event_t *ev, const hush_agent_note_in_t *in);

/* Stores a filled robot note in the conversation. */
hush_status_t hush_agent_insert_note(hush_store_t *store,
                                     const hush_agent_note_in_t *in);

/* Posts the robot's intro greeting for the parent event. */
void hush_agent_on_deck(hush_store_t *store, const hush_agent_robot_t *bot,
                        const hush_event_t *parent, const char *why);

/* Posts a note when the provider runtime is not ready. */
void hush_agent_note_no_runtime(hush_store_t *store,
                                const hush_agent_robot_t *bot,
                                const hush_event_t *parent);

/* Posts one diagnostic when no slot, lease, or claim was available. */
void hush_agent_note_start_failed(hush_store_t *store,
                                  const hush_agent_robot_t *bot,
                                  const hush_event_t *parent);

/* True when the robot's provider runtime is ready to execute a turn. */
int hush_agent_can_start(const hush_launch_t *launch,
                         const hush_agent_robot_t *bot);

/* Starts one grok/provider job from a prepared input. */
hush_status_t hush_agent_start_grok(const hush_agent_job_in_t *in);

/* Releases the job's captured resources. */
void hush_agent_close_job(hush_agent_job_t *job);

/* Rewrites @npub1 to nostr:npub1, expands truncated npubs, maps @Name,
 * then scrubs self-mentions, ask-echo, and last-robot handoff. */
void hush_agent_rewrite_mentions(hush_agent_job_t *job);

/* Rewrites nostr:npub1 tokens to @Name for prompt text. Drops the acting
 * robot's own token and unknown tokens; keeps peers and the human readable. */
void hush_agent_humanize_ask(char *text, size_t textsz,
                             const hush_launch_t *launch,
                             const char *self_hex);

/* Removes only an incomplete trailing UTF-8 scalar from a bounded snippet. */
hush_status_t hush_agent_complete_snippet(char *text, size_t capacity);

/* ---- agent_prompt.c: prompt and directive builders ---- */

/* Fills the job's prompt, rules, peers, and note from a prepared input. */
hush_status_t hush_agent_fill_job(hush_agent_job_t *job,
                                  const hush_agent_job_in_t *in);

/* Adds room/skill guidance to a prepared job. Borrowed pointers;
 * propagates missing skill, malformed content, or prompt capacity errors. */
hush_status_t hush_agent_add_guidance(hush_agent_job_t *job,
                                      const hush_agent_robot_t *robot);

/* True when the p-tag key names the given robot. */
int hush_agent_key_matches(const char *mention, const char *npub,
                           const char *hex);

/* Picks the first ready provider for the robot, or NULL. */
const char *hush_agent_pick_provider(const hush_agent_robot_t *bot);

/* Mints a fresh unique job token. */
void hush_agent_make_token(char *out, size_t outsz);

/* True when the grok provider binary is ready to run. */
int hush_agent_grok_ready(void);

/* Returns the next free job slot, or NULL when the table is full. */
hush_agent_job_t *hush_agent_find_slot(void);

#endif /* HUSH_AGENT_INTERNAL_H */
