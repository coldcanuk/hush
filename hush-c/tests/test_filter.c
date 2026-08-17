/* test_filter.c: tests for hush_filter (legible C). */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "hush_filter.h"

int main(void)
{
    hush_filter_t f = {0};
    f.kinds_len = 1;
    f.kinds[0] = 1;

    hush_event_t ev = {0};
    ev.kind = 1;
    strcpy(ev.pubkey, "1111111111111111111111111111111111111111111111111111111111111111");

    assert(hush_filter_match(&f, &ev) == true);

    f.kinds[0] = 9;
    assert(hush_filter_match(&f, &ev) == false);

    puts("test_filter: PASSED");
    return 0;
}
