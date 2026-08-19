/* hush_intel.h: channel leash + burst hold in front of hush_agent. */

#ifndef HUSH_INTEL_H
#define HUSH_INTEL_H

#include "hush_event.h"
#include "hush_launch.h"
#include "hush_status.h"
#include "hush_store.h"

enum {
    HUSH_INTEL_HOLD_MAX = 8,
    HUSH_INTEL_BURST_MAX = 8,
    HUSH_INTEL_SNIP_MAX = 160
};

#define HUSH_INTEL_CONFIRM_TAG "hush-confirm"

/* Zeros the hold table. Safe to call twice. */
void hush_intel_init(void);

/* Classifies ev, holds a burst, or forwards a job. Ignores NULL. */
void hush_intel_consider(hush_store_t *store, hush_launch_t *launch,
                         const hush_event_t *ev);

/* Flushes expired holds. store may be NULL. */
void hush_intel_poll(hush_store_t *store, hush_launch_t *launch);

#endif /* HUSH_INTEL_H */
