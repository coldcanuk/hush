/* hush_cevent.h: in-hive channel events, ordered and timed. */

#ifndef HUSH_CEVENT_H
#define HUSH_CEVENT_H

#include <stddef.h>
#include <stdint.h>
#include "hush_status.h"

enum {
    HUSH_CEVENT_MAX = 64,
    HUSH_CEVENT_TYPE_MAX = 24,
    HUSH_CEVENT_NOTE_MAX = 160,
    HUSH_CEVENT_JSON_MAX = 16384
};

#define HUSH_CEVENT_MENTION "mention"
#define HUSH_CEVENT_INTRO "intro"
#define HUSH_CEVENT_JOB_START "job_start"
#define HUSH_CEVENT_JOB_DONE "job_done"
#define HUSH_CEVENT_FOLLOW "follow"
#define HUSH_CEVENT_HOP_DENIED "hop_denied"
#define HUSH_CEVENT_JOBS_HELD "jobs_held"
#define HUSH_CEVENT_CHAPERON "chaperon"
#define HUSH_CEVENT_PRESENCE "presence"
#define HUSH_CEVENT_STUCK "stuck"

typedef struct {
    uint32_t seq;
    int64_t due;
    char type[HUSH_CEVENT_TYPE_MAX];
    char channel[256];
    char root[65];
    char actor[65];
    char note[HUSH_CEVENT_NOTE_MAX];
} hush_cevent_t;

/* Zeros the ring. Safe twice. */
void hush_cevent_init(void);

/* Appends one event. due 0 means now. */
hush_status_t hush_cevent_emit(const hush_cevent_t *in);

/* Writes {"ok":true,"drops":N,"events":[...]} in seq order. */
hush_status_t hush_cevent_format_json(char *out, size_t outsz, size_t *out_len);

/* Total events overwritten when the ring wrapped (oldest dropped). Lets a
 * consumer detect that it may have missed signals and force a full refresh. */
uint32_t hush_cevent_drops(void);

#endif /* HUSH_CEVENT_H */
