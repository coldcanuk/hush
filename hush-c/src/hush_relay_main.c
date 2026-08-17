/* hush_relay_main.c: entry point for hush-relay binary. */

#include <stdio.h>
#include <stdlib.h>
#include "hush_relay.h"

int main(int argc, char **argv)
{
    uint16_t port = 10555;
    if (argc > 1)
        port = (uint16_t)atoi(argv[1]);
    printf("hush-relay starting on :%u (MVP poll)\n", port);
    hush_status_t st = hush_relay_run(port);
    return (st == HUSH_OK) ? 0 : 1;
}
