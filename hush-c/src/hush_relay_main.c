/* hush_relay_main.c: entry point for hush-relay binary. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hush_relay.h"

/* Canonical product version (keep in sync with top-level VERSION). */
#define HUSH_VERSION "0.0.1"

struct hush_cli {
    uint16_t port;
    int open_ui;
    int want_quit;
    int want_close;
    int want_help;
};

static int hush_display_available(void);
static void hush_print_help(void);
static void hush_print_close_hint(uint16_t port);
static int hush_parse_args(struct hush_cli *cli, int argc, char **argv);
static int hush_cli_run(const struct hush_cli *cli);

int main(int argc, char **argv)
{
    struct hush_cli cli;

    cli.port = (uint16_t)HUSH_DEFAULT_PORT;
    cli.open_ui = hush_display_available();
    cli.want_quit = 0;
    cli.want_close = 0;
    cli.want_help = 0;
    if (hush_parse_args(&cli, argc, argv) != 0)
        return 1;
    return hush_cli_run(&cli);
}

static int hush_cli_run(const struct hush_cli *cli)
{
    hush_status_t st;

    if (cli->want_help) {
        hush_print_help();
        return 0;
    }
    if (cli->want_close) {
        hush_print_close_hint(cli->port);
        return 0;
    }
    if (cli->want_quit)
        return (hush_relay_quit(cli->port) == HUSH_OK) ? 0 : 1;
    printf("hush-relay %s\n", HUSH_VERSION);
    fflush(stdout);
    st = hush_relay_run(cli->port, cli->open_ui);
    return (st == HUSH_OK) ? 0 : 1;
}

static int hush_parse_args(struct hush_cli *cli, int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--open") == 0)
            cli->open_ui = 1;
        else if (strcmp(argv[i], "--no-open") == 0)
            cli->open_ui = 0;
        else if (strcmp(argv[i], "--quit") == 0)
            cli->want_quit = 1;
        else if (strcmp(argv[i], "--close") == 0)
            cli->want_close = 1;
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
            cli->want_help = 1;
        else if (argv[i][0] != '-')
            cli->port = (uint16_t)atoi(argv[i]);
        else {
            fprintf(stderr, "hush-relay: unknown option %s\n", argv[i]);
            hush_print_help();
            return 1;
        }
    }
    return 0;
}

static int hush_display_available(void)
{
    const char *d = getenv("DISPLAY");
    const char *w = getenv("WAYLAND_DISPLAY");

    return (d != NULL && d[0] != '\0') || (w != NULL && w[0] != '\0');
}

static void hush_print_close_hint(uint16_t port)
{
    printf("GUI closed. Relay still running on http://127.0.0.1:%u/.\n",
           (unsigned)port);
    printf("Click the launcher to re-attach. Use --quit or Exit to stop.\n");
}

static void hush_print_help(void)
{
    printf("hush-relay %s — local Nostr relay + chat UI\n", HUSH_VERSION);
    printf("usage: hush-relay [port] [--open|--no-open|--close|--quit]\n");
    printf("  port       listen port (default 10555)\n");
    printf("  --open     open the chat as a standalone app window\n");
    printf("  --no-open  do not open a window (even on a graphical session)\n");
    printf("  --close    detach the GUI; the relay stays up (exit 0)\n");
    printf("  --quit     stop the running relay (exit 0)\n");
    printf("Close vs Exit:\n");
    printf("  Close dismisses the window. The hive keeps listening.\n");
    printf("  Exit / --quit stops every process. Clean quit is exit code 0.\n");
    printf("STUN/TURN: Settings → Enable STUN/TURN (needs coturn).\n");
    printf("Daemon:    sudo make install PREFIX=/usr, then Settings → Daemon mode\n");
    printf("           or: sudo systemctl enable --now hush-turn\n");
}
