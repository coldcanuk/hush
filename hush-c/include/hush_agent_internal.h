/* hush_agent_internal.h: shared types, constants, and helpers between the
 * agent core and its per-cluster modules. Internal to the relay; not
 * installed. */

#ifndef HUSH_AGENT_INTERNAL_H
#define HUSH_AGENT_INTERNAL_H

#include <stddef.h>
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

/* Writes the human display name into out. */
void hush_agent_human_name(char *out, size_t outsz, const hush_launch_t *launch);

#endif /* HUSH_AGENT_INTERNAL_H */
