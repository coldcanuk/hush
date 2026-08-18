/* hush_relay_main.c: entry point for hush-relay binary. */

#include <stdio.h>
#include <stdlib.h>
#include "hush_relay.h"

/* Canonical product version (keep in sync with top-level VERSION). */
#define HUSH_VERSION "0.0.1"

int main(int argc, char **argv)
{
    uint16_t port = 10555;
    if (argc > 1)
        port = (uint16_t)atoi(argv[1]);
    printf("hush-relay %s starting on :%u (MVP poll)\n", HUSH_VERSION, port);
    hush_status_t st = hush_relay_run(port);
    return (st == HUSH_OK) ? 0 : 1;
}
