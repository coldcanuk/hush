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

/* Starts a reply for each robot p-tag on a kind-1 note. Ignores NULL. */
void hush_agent_consider(hush_store_t *store, hush_launch_t *launch,
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
