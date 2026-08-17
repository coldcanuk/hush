/* test_store.c: tests for hush_store. */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "hush_store.h"

int main(void)
{
    hush_store_t *s = NULL;
    assert(hush_store_create(&s) == HUSH_OK);

    hush_event_t ev = {0};
    strcpy(ev.id, "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef");
    strcpy(ev.pubkey, "0000000000000000000000000000000000000000000000000000000000000000");
    ev.kind = 1;
    ev.created_at = 1720000000;

    assert(hush_store_insert(s, &ev) == HUSH_OK);

    hush_filter_t f = {0};
    hush_event_t out[4];
    size_t n = hush_store_query(s, &f, 1, out, 4);
    assert(n == 1);

    hush_store_destroy(s);
    puts("test_store: PASSED");
    return 0;
}
