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
    HUSH_AGENT_TIMEOUT_S = 90
};

/* Zeros the job table and reloads the wake ledger. Safe to call twice.
 * Does not wipe wake.ledger or device.id. */
void hush_agent_init(void);

/* Kills live jobs and closes pipes. Safe on an empty table. */
void hush_agent_shutdown(void);

/* Clears completed handoff state before a new human request in the thread.
 * Ignores non-human authors, NULL launch, and NULL event. */
void hush_agent_reset_follow(const hush_launch_t *launch,
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

/* Cancels the live job for robot on root. root is the thread's root event id;
 * robot matches the robot's hex pubkey or roster name. Sends SIGTERM to the
 * job's process group and SIGKILL once the grace period passes without an
 * exit. Returns HUSH_ERR_NOT_FOUND when no live job matches. */
hush_status_t hush_agent_cancel(const char *root, const char *robot);

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
