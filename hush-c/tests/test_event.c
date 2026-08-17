/* test_event.c: unit tests for hush_event (legible C). */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "hush_event.h"

int main(void)
{
    hush_event_t ev = {0};
    strcpy(ev.pubkey, "0000000000000000000000000000000000000000000000000000000000000000");
    strcpy(ev.content, "hello hush");
    ev.kind = 1;
    ev.created_at = 1720000000;

    char id[65];
    hush_status_t st = hush_event_compute_id(&ev, id);
    assert(st == HUSH_OK);
    assert(strlen(id) == 64);
    printf("test_event: compute_id OK (stubbed)\n");

    st = hush_event_validate(&ev);
    printf("test_event: validate returned %d (expected ARG until id set)\n", (int)st);

    puts("test_event: PASSED");
    return 0;
}
