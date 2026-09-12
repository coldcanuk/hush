/* tests/test_limiter.c: token-bucket refill, burst, and deny semantics. */

#include <stdio.h>

#include "hush_limiter.h"

static int g_fail;

static void expect(int cond, const char *msg);

int main(void)
{
    hush_limiter_t limiter;

    hush_limiter_init(&limiter, 2.0, 3.0);
    expect(hush_limiter_take(&limiter, 1000) == 1, "first token");
    expect(hush_limiter_take(&limiter, 1000) == 1, "second token");
    expect(hush_limiter_take(&limiter, 1000) == 1, "third token");
    expect(hush_limiter_take(&limiter, 1000) == 0, "burst exhausted");

    hush_limiter_init(&limiter, 2.0, 3.0);
    expect(hush_limiter_take(&limiter, 1000) == 1, "prime");
    expect(hush_limiter_take(&limiter, 1000) == 1, "prime");
    expect(hush_limiter_take(&limiter, 1000) == 1, "prime");
    expect(hush_limiter_take(&limiter, 1500) == 1, "half second refills one");
    expect(hush_limiter_take(&limiter, 1500) == 0, "no further refill");
    expect(hush_limiter_take(&limiter, 2000) == 1, "full second refills one");

    hush_limiter_init(&limiter, 10.0, 2.0);
    expect(hush_limiter_take(&limiter, 0) == 1, "clamp prime");
    expect(hush_limiter_take(&limiter, 0) == 1, "clamp prime");
    expect(hush_limiter_take(&limiter, 0) == 0, "clamp empty");
    expect(hush_limiter_take(&limiter, 60000) == 1, "refill clamped at burst");
    expect(hush_limiter_take(&limiter, 60000) == 1, "burst holds two");
    expect(hush_limiter_take(&limiter, 60000) == 0, "clamped burst consumed");

    hush_limiter_init(&limiter, 2.0, 3.0);
    expect(hush_limiter_take(&limiter, 5000) == 1, "backwards prime");
    expect(hush_limiter_take(&limiter, 4000) == 1, "backwards mints nothing");
    expect(hush_limiter_take(&limiter, 4000) == 1, "backwards mints nothing");

    hush_limiter_init(&limiter, 0.0, 5.0);
    expect(hush_limiter_take(&limiter, 0) == 0, "zero rate denies");
    expect(hush_limiter_take(&limiter, 60000) == 0, "zero rate stays denied");

    {
        hush_limiter_t zeroed = {0};

        expect(hush_limiter_take(&zeroed, 0) == 0, "zeroed limiter denies");
        expect(hush_limiter_take(&zeroed, 60000) == 0,
               "zeroed limiter stays denied");
    }

    if (g_fail)
        return 1;
    printf("test_limiter ok\n");
    return 0;
}

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}
