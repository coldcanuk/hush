/* tests/test_secure_zero.c: hush_secure_zero overwrites a buffer. */

#include <stdio.h>
#include <string.h>

#include "hush_mem.h"

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

static int hush_test_is_zero(const unsigned char *buf, size_t n)
{
    size_t i;

    for (i = 0; i < n; ++i) {
        if (buf[i] != 0)
            return 0;
    }
    return 1;
}

int main(void)
{
    unsigned char buf[32];
    size_t i;

    for (i = 0; i < sizeof(buf); ++i)
        buf[i] = 0xa5;
    hush_secure_zero(buf, sizeof(buf));
    expect(hush_test_is_zero(buf, sizeof(buf)), "full buffer zeroed");

    for (i = 0; i < sizeof(buf); ++i)
        buf[i] = 0xa5;
    hush_secure_zero(buf, 4);
    expect(hush_test_is_zero(buf, 4), "prefix zeroed");
    expect(buf[4] == 0xa5, "tail kept");
    hush_secure_zero(NULL, 0);

    if (g_fail)
        return 1;
    printf("test_secure_zero ok\n");
    return 0;
}
