/* hush_limiter.c: owns monotonic token-bucket rate limiting. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <time.h>

#include "hush_limiter.h"

enum {
    HUSH_LIMITER_MS_PER_S = 1000,
    HUSH_LIMITER_NS_PER_MS = 1000000
};

#define HUSH_LIMITER_ONE 1.0

void hush_limiter_init(hush_limiter_t *limiter, double rate_per_s,
                       double burst)
{
    assert(limiter != NULL);
    assert(burst > 0.0);
    limiter->rate_per_s = rate_per_s;
    limiter->burst = burst;
    limiter->tokens = rate_per_s > 0.0 ? burst : 0.0;
    limiter->last_ms = -1;
}

int hush_limiter_take(hush_limiter_t *limiter, int64_t now_ms)
{
    double elapsed_s;
    double refill;

    assert(limiter != NULL);
    assert(now_ms >= 0);
    if (limiter->last_ms < 0) {
        limiter->last_ms = now_ms;
    } else if (now_ms > limiter->last_ms) {
        elapsed_s = (double)(now_ms - limiter->last_ms) /
                    (double)HUSH_LIMITER_MS_PER_S;
        refill = limiter->rate_per_s * elapsed_s;
        limiter->tokens += refill;
        if (limiter->tokens > limiter->burst)
            limiter->tokens = limiter->burst;
        limiter->last_ms = now_ms;
    }
    if (limiter->tokens < HUSH_LIMITER_ONE)
        return 0;
    limiter->tokens -= HUSH_LIMITER_ONE;
    return 1;
}

int64_t hush_limiter_now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (int64_t)ts.tv_sec * (int64_t)HUSH_LIMITER_MS_PER_S +
           (int64_t)(ts.tv_nsec / (long)HUSH_LIMITER_NS_PER_MS);
}
