/* hush_limiter.h: monotonic token-bucket rate limiting for ingress paths. */

#ifndef HUSH_LIMITER_H
#define HUSH_LIMITER_H

#include <stdint.h>

typedef struct {
    double tokens;          /* available tokens */
    int64_t last_ms;        /* monotonic ms at the last refill; -1 = untouched */
    double rate_per_s;      /* refill rate */
    double burst;           /* capacity */
} hush_limiter_t;

/* Sets the refill rate and capacity. A non-positive rate denies every
 * take. A zeroed (never initialized) limiter also denies every take. */
void hush_limiter_init(hush_limiter_t *limiter, double rate_per_s,
                       double burst);

/* 1 when a token was available, else 0. now_ms is hush_limiter_now_ms().
 * Refills linearly and clamps at burst; backwards clocks mint nothing. */
int hush_limiter_take(hush_limiter_t *limiter, int64_t now_ms);

/* Monotonic milliseconds, 0 when the clock is unavailable (fail open). */
int64_t hush_limiter_now_ms(void);

#endif /* HUSH_LIMITER_H */
