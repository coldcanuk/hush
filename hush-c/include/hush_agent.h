/* hush_agent.h: mention dispatch for raised robots. */

#ifndef HUSH_AGENT_H
#define HUSH_AGENT_H

#include "hush_event.h"
#include "hush_launch.h"
#include "hush_status.h"
#include "hush_store.h"

/* Zeros the job table. Safe to call twice. */
void hush_agent_init(void);

/* Kills live jobs and closes pipes. Safe on an empty table. */
void hush_agent_shutdown(void);

/* Starts a reply for each robot p-tag on a kind-1 note. Ignores NULL. */
void hush_agent_consider(hush_store_t *store, hush_launch_t *launch,
                         const hush_event_t *ev);

/* Reaps finished Grok jobs and inserts their notes. store may be NULL. */
void hush_agent_poll(hush_store_t *store);

/* Writes a JSON array of busy jobs into out. No-op if out is NULL. */
void hush_agent_status(char *out, size_t outsz);

#endif /* HUSH_AGENT_H */
