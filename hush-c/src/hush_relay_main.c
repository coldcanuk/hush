/* hush_relay_main.c: entry point for hush-relay binary. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hush_relay.h"

/* Canonical product version (keep in sync with top-level VERSION). */
#define HUSH_VERSION "0.0.1"

static int hush_display_available(void);
static void hush_print_help(void);

int main(int argc, char **argv)
{
    uint16_t port = 10555;
    int open_ui = hush_display_available();
    int i;
    hush_status_t st;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--open") == 0) {
            open_ui = 1;
        } else if (strcmp(argv[i], "--no-open") == 0) {
            open_ui = 0;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            hush_print_help();
            return 0;
        } else if (argv[i][0] != '-') {
            port = (uint16_t)atoi(argv[i]);
        } else {
            fprintf(stderr, "hush-relay: unknown option %s\n", argv[i]);
            hush_print_help();
            return 1;
        }
    }

    printf("hush-relay %s\n", HUSH_VERSION);
    fflush(stdout);
    st = hush_relay_run(port, open_ui);
    return (st == HUSH_OK) ? 0 : 1;
}

static int hush_display_available(void)
{
    const char *d = getenv("DISPLAY");
    const char *w = getenv("WAYLAND_DISPLAY");

    return (d != NULL && d[0] != '\0') || (w != NULL && w[0] != '\0');
}

static void hush_print_help(void)
{
    printf("hush-relay %s — local Nostr relay + chat UI\n", HUSH_VERSION);
    printf("usage: hush-relay [port] [--open|--no-open]\n");
    printf("  port       listen port (default 10555)\n");
    printf("  --open     open the chat as a standalone app window\n");
    printf("  --no-open  do not open a window (even on a graphical session)\n");
    printf("STUN/TURN: Settings → Enable STUN/TURN (needs coturn).\n");
    printf("Daemon:    sudo make install PREFIX=/usr, then Settings → Daemon mode\n");
    printf("           or: sudo systemctl enable --now hush-turn\n");
}
