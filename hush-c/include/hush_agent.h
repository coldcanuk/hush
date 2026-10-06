/* hush_agent.h: mention dispatch for raised robots. */

#ifndef HUSH_AGENT_H
#define HUSH_AGENT_H

#include "hush_event.h"
#include "hush_launch.h"
#include "hush_status.h"
#include "hush_store.h"

enum {
    /* Wall clock for a live grok job. hush_wake lease uses the same
     * number (HUSH_WAKE_LEASE_S). Not HUSH_PRESENCE_STALL_S. */
    HUSH_AGENT_TIMEOUT_S = 90,
    /* Global concurrent job cap; the overload gate refuses new dispatches
     * when every slot is busy. */
    HUSH_AGENT_JOBS_MAX = 4,
    /* Robots one ```team fence may name. */
    HUSH_AGENT_TEAM_MAX = 8
};

typedef struct {
    char name[HUSH_ROSTER_NAME_MAX];
    char provider[HUSH_ROSTER_PROVIDER_MAX];
    char prompt[HUSH_ROSTER_PROMPT_MAX];
} hush_agent_team_member_t;

typedef struct {
    hush_agent_team_member_t member[HUSH_AGENT_TEAM_MAX];
    size_t count;
} hush_agent_team_t;

/* Zeros the job table and reloads the wake ledger. Safe to call twice.
 * Does not wipe wake.ledger or device.id. */
void hush_agent_init(void);

/* Kills live jobs and closes pipes. Safe on an empty table. */
void hush_agent_shutdown(void);

/* Clears completed handoff state before a new human request in the thread.
 * Ignores non-human authors, NULL launch, and NULL event. */
void hush_agent_reset_follow(const hush_launch_t *launch,
                             const hush_event_t *ev);

/* #279: the owner's Yes runs the oldest turn waiting for approval in the
 * thread; No declines it. Returns 1 when ev answered one (it is consumed);
 * 0 for anyone else, no waiting turn, or any other text. */
int hush_agent_approval_answer(hush_store_t *store, const hush_launch_t *launch,
                               const hush_event_t *ev);

/* Handles a human "Yes"/"No" reply to a waiting "Continue this loop?"
 * prompt in ev's thread. Returns 1 when ev was consumed as the answer;
 * 0 leaves ev to the normal path (which ends any loop in that thread). */
int hush_agent_loop_answer(hush_store_t *store, const hush_launch_t *launch,
                           const hush_event_t *ev);

/* Dispatches one mention. Later co-mentions wait for the previous robot. */
void hush_agent_mention(hush_store_t *store, hush_launch_t *launch,
                        const hush_event_t *ev, const char *mention);

/* After a robot note is stored, starts the next queued assignee. */
void hush_agent_on_posted(hush_store_t *store, const hush_launch_t *launch,
                          const hush_event_t *ev);

/* Reaps finished Grok jobs and inserts their notes. store may be NULL. */
void hush_agent_poll(hush_store_t *store);

/* Writes a JSON array of busy jobs into out. No-op if out is NULL. */
void hush_agent_status(char *out, size_t outsz);

/* Number of live jobs dispatched from this channel. 0 for NULL or empty. */
int hush_agent_channel_busy(const char *channel);

/* Number of busy jobs across all channels. */
int hush_agent_jobs_active(void);

/* Parses one ```team fence. NOT_FOUND when the fence is absent. PARSE when
 * a line is unfit. A parsed team is not raised until the owner answers Yes. */
hush_status_t hush_agent_team_parse(hush_agent_team_t *out, const char *text);

/* FULL when the roster cannot hold the team. PARSE on a name clash or a
 * provider the roster refuses. */
hush_status_t hush_agent_team_check(const hush_launch_t *launch,
                                    const hush_agent_team_t *team);

/* Remembers a team for root. A later offer for the same root replaces it. */
int hush_agent_team_offer(const char *root, const hush_agent_team_t *team);

/* Owner Yes raises the offered team. Owner No drops it. Returns 1 when ev
 * was that answer. A robot Yes is left for the normal path. */
int hush_agent_team_answer(hush_store_t *store, hush_launch_t *launch,
                           const hush_event_t *ev);

/* Grok tool denylist. project_tools 1 drops the file tools only. */
const char *hush_agent_tool_denylist(int project_tools);

/* Cancels the live job for robot on root. root is the thread's root event id;
 * robot matches the robot's hex pubkey or roster name. Sends SIGTERM to the
 * job's process group and SIGKILL once the grace period passes without an
 * exit. Returns HUSH_ERR_NOT_FOUND when no live job matches. */
hush_status_t hush_agent_cancel(const char *root, const char *robot);

/* Copies the live job's partial answer for root and robot into out. Empty
 * while nothing has arrived. Returns HUSH_ERR_NOT_FOUND when no job matches. */
hush_status_t hush_agent_partial(char *out, size_t outsz, const char *root,
                                 const char *robot);

/* Starts a one-shot grok rewrite. Does not insert a hive note.
 * instruction and text may be empty; both are copied. Fails with
 * HUSH_ERR_ARG, HUSH_ERR_FULL, or HUSH_ERR_IO. Writes a token. */
hush_status_t hush_agent_start_fixup(char *token, size_t tokensz,
                                     const char *instruction,
                                     const char *text);

/* Copies a finished fixup into out. HUSH_OK when ready,
 * HUSH_ERR_NOT_FOUND while busy or unknown, HUSH_ERR_IO on fail. */
hush_status_t hush_agent_take_fixup(const char *token, char *out,
                                    size_t outsz);

#endif /* HUSH_AGENT_H */
