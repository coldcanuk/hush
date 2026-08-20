/* hush_relay.c: owns the poll-based relay server and connection dispatch for Hush. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "hush_agent.h"
#include "hush_canvas.h"
#include "hush_http.h"
#include "hush_intel.h"
#include "hush_launch.h"
#include "hush_proto.h"
#include "hush_relay.h"
#include "hush_status.h"
#include "hush_store.h"
#include "hush_turn.h"
#include "hush_win.h"

enum {
    HUSH_MAX_CLIENTS = 16,
    /* 32 KiB: HTTP JSON plus a downscaled avatar. Was 8192. */
    HUSH_BUF_SZ = 32768,
    HUSH_LISTEN_BACKLOG = 8,
    HUSH_POLL_TIMEOUT_MS = 1000,
    HUSH_FD_NONE = -1,
    HUSH_UI_URL_MAX = 64,
    HUSH_UI_APP_ARG_MAX = 80,
    HUSH_PIDFILE_PATH_MAX = 256,
    HUSH_PIDFILE_BODY_MAX = 32,
    HUSH_QUIT_WAIT_TRIES = 20,
    HUSH_QUIT_WAIT_MS = 100,
    HUSH_CHILD_MAX = 8,
    HUSH_CHILD_CMDLINE_MAX = 512,
    HUSH_PROC_PATH_MAX = 64,
    HUSH_LEAVE_OUT_MAX = 256,
    HUSH_LEAVE_MISSING = 127
};

#define HUSH_PIDFILE_NAME_FMT "relay-%u.pid"
#define HUSH_LEAVE_BIN        "zenity"
#define HUSH_LEAVE_TITLE      "Leave the hive?"
#define HUSH_LEAVE_TEXT       "The window closed. The hive is still standing."
#define HUSH_LEAVE_OK         "Exit the application"
#define HUSH_LEAVE_EXTRA      "Close the window"
#define HUSH_LEAVE_CANCEL     "Cancel"

struct client {
    int fd;
    char buf[HUSH_BUF_SZ];
    size_t len;
    int is_http;
    int has_sub;
    char sub_id[256 + 1];
    hush_filter_t filter;
};

static struct client clients[HUSH_MAX_CLIENTS];
static hush_store_t *g_store = NULL;
static hush_launch_t g_launch;
static hush_turn_t g_turn;
static volatile sig_atomic_t g_shutdown = 0;
static char g_pidfile_path[HUSH_PIDFILE_PATH_MAX];
static int g_pidfile_ready = 0;
static pid_t g_children[HUSH_CHILD_MAX];
static int g_nchildren = 0;
static uint16_t g_listen_port = 0;
static int g_leave_ack = 0;
static int g_saw_app = 0;
static pid_t g_leave_pid = 0;
static int g_leave_rd = HUSH_FD_NONE;

static void hush_clients_reset(void);
static int hush_listen_on(uint16_t port);
static void hush_set_nonblock(int fd);
static void hush_open_app_window(uint16_t port);
static void hush_exec_app_browser(const char *url, const char *app_arg);
static int hush_active_clients(void);
static hush_status_t hush_accept_new(int ls);
static void hush_drop_client(struct client *c);
static void hush_service_clients(struct pollfd *fds, int nf);
static void hush_on_bytes(struct client *c);
static void hush_on_nostr_line(struct client *c, const char *line);
static void hush_send_str(int fd, const char *s);
static void hush_handle_event_msg(struct client *c, const hush_client_msg_t *msg);
static void hush_handle_req_msg(struct client *c, const hush_client_msg_t *msg);
static void hush_fanout(const hush_event_t *ev);
static void hush_shutdown_handler(int sig);
static void hush_install_shutdown_handlers(void);
static void hush_pidfile_dir(char *out, size_t sz);
static void hush_pidfile_path(char *out, size_t sz, uint16_t port);
static void hush_write_pidfile(uint16_t port);
static int hush_write_pid_bytes(int fd, const char *body, size_t len);
static void hush_remove_pidfile(void);
static hush_status_t hush_read_pidfile(uint16_t port, pid_t *out_pid);
static int hush_pid_is_alive(pid_t pid);
static void hush_wait_pid_gone(pid_t pid);
static void hush_relay_prepare(uint16_t port);
static hush_status_t hush_relay_bind(uint16_t port, int open_ui, int *out_ls);
static void hush_relay_announce(uint16_t port, int open_ui);
static void hush_relay_pump(int ls);
static void hush_child_reset(void);
static void hush_child_stop_one(pid_t pid);
static int hush_child_is_app(pid_t pid);
static int hush_leave_app_alive(void);
static void hush_leave_forget_dead(void);
static void hush_leave_print_hint(void);
static void hush_leave_write_result(int wr, int status, const char *out);
static void hush_leave_exec_dialog(int wr);
static void hush_leave_run_zenity(int wr);
static void hush_leave_apply(int status, const char *out);
static void hush_leave_close_rd(void);
static void hush_leave_finish(int status, const char *out);
static int hush_leave_parse_status(const char *buf);
static const char *hush_leave_parse_out(const char *buf);
static void hush_leave_poll(void);
static void hush_leave_spawn(void);
static void hush_relay_watch_app(void);
static int hush_child_cmdline_matches(pid_t pid, uint16_t port);
static void hush_child_sweep_proc(uint16_t port);
static void hush_child_sweep_entry(const char *name, uint16_t port);
static void hush_relay_cleanup(int ls);
static int hush_fill_pollfds(struct pollfd *fds, int ls);
static int hush_poll_should_stop(int pr);

void hush_relay_request_shutdown(void)
{
    g_shutdown = 1;
}

void hush_relay_track_child(pid_t pid)
{
    int i;

    if (pid <= 0)
        return;
    if (pid == getpid())
        return;
    for (i = 0; i < g_nchildren; ++i) {
        if (g_children[i] == pid)
            return;
    }
    if (g_nchildren >= HUSH_CHILD_MAX)
        return;
    g_children[g_nchildren] = pid;
    g_nchildren++;
}

void hush_relay_note_leave(int is_exit)
{
    g_leave_ack = 1;
    if (is_exit)
        hush_relay_request_shutdown();
}

void hush_relay_reap_children(void)
{
    int i;

    for (i = 0; i < g_nchildren; ++i)
        hush_child_stop_one(g_children[i]);
    hush_child_sweep_proc(g_listen_port);
    hush_child_reset();
}

hush_status_t hush_relay_quit(uint16_t port)
{
    pid_t pid = 0;
    hush_status_t st;

    if (port == 0)
        port = (uint16_t)HUSH_DEFAULT_PORT;
    st = hush_read_pidfile(port, &pid);
    if (st != HUSH_OK)
        return HUSH_OK;
    if (!hush_pid_is_alive(pid)) {
        hush_pidfile_path(g_pidfile_path, sizeof(g_pidfile_path), port);
        unlink(g_pidfile_path);
        return HUSH_OK;
    }
    if (kill(pid, SIGTERM) != 0 && errno != ESRCH)
        return HUSH_ERR_IO;
    hush_wait_pid_gone(pid);
    hush_pidfile_path(g_pidfile_path, sizeof(g_pidfile_path), port);
    unlink(g_pidfile_path);
    return HUSH_OK;
}

hush_status_t hush_relay_run(uint16_t port, int open_ui)
{
    int ls = HUSH_FD_NONE;
    hush_status_t st;

    hush_relay_prepare(port);
    st = hush_relay_bind(port, open_ui, &ls);
    if (st != HUSH_OK)
        return st;
    if (ls == HUSH_FD_NONE)
        return HUSH_OK;
    if (hush_store_create(&g_store) != HUSH_OK) {
        close(ls);
        return HUSH_ERR_FULL;
    }
    hush_write_pidfile(port);
    hush_relay_announce(port, open_ui);
    hush_relay_pump(ls);
    hush_relay_cleanup(ls);
    return HUSH_OK;
}

static void hush_clients_reset(void)
{
    int i;

    for (i = 0; i < HUSH_MAX_CLIENTS; ++i) {
        memset(&clients[i], 0, sizeof(clients[i]));
        clients[i].fd = HUSH_FD_NONE;
    }
}

static int hush_listen_on(uint16_t port)
{
    int ls;
    int yes = 1;
    struct sockaddr_in addr;

    ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls < 0)
        return -1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(ls, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(ls);
        return -1;
    }
    if (listen(ls, HUSH_LISTEN_BACKLOG) < 0) {
        close(ls);
        return -1;
    }
    hush_set_nonblock(ls);
    return ls;
}

static void hush_set_nonblock(int fd)
{
    int fl = fcntl(fd, F_GETFL, 0);

    if (fl >= 0)
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

static void hush_open_app_window(uint16_t port)
{
    char url[HUSH_UI_URL_MAX];
    char app_arg[HUSH_UI_APP_ARG_MAX];
    pid_t pid;
    int n;

    n = snprintf(url, sizeof(url), "http://127.0.0.1:%u/", (unsigned)port);
    if (n <= 0 || (size_t)n >= sizeof(url))
        return;
    n = snprintf(app_arg, sizeof(app_arg), "--app=%s", url);
    if (n <= 0 || (size_t)n >= sizeof(app_arg))
        return;
    pid = fork();
    if (pid < 0)
        return;
    if (pid > 0) {
        hush_relay_track_child(pid);
        return;
    }
    hush_exec_app_browser(url, app_arg);
    _exit(127);
}

static void hush_exec_app_browser(const char *url, const char *app_arg)
{
    static const char *const browsers[] = {
        "chromium",
        "chromium-browser",
        "google-chrome",
        "google-chrome-stable",
        "brave-browser",
        "microsoft-edge",
        "microsoft-edge-stable",
        "vivaldi",
        NULL
    };
    size_t i;

    for (i = 0; browsers[i] != NULL; ++i) {
        /* --app + extra flags to suppress browser title/tab bar in standalone mode.
         * Combined with Motif undecorate (hush_win_undecorate) and manifest "standalone".
         * This addresses the request to remove the standard browser bar ("windowless"). */
        execlp(browsers[i], browsers[i],
               "--class=hush-relay", "--name=Hush",
               "--ozone-platform=x11",
               "--disable-features=TabStrip,WindowControlsOverlay",
               app_arg, (char *)NULL);
    }
    execlp("epiphany", "epiphany", "--application-mode", url, (char *)NULL);
    execlp("flatpak", "flatpak", "run", "com.brave.Browser",
           "--class=hush-relay", "--ozone-platform=x11", app_arg, (char *)NULL);
    execlp("flatpak", "flatpak", "run", "org.chromium.Chromium",
           "--class=hush-relay", "--ozone-platform=x11", app_arg, (char *)NULL);
    execlp("flatpak", "flatpak", "run", "com.google.Chrome",
           "--class=hush-relay", "--ozone-platform=x11", app_arg, (char *)NULL);
    execlp("xdg-open", "xdg-open", url, (char *)NULL);
}

static int hush_active_clients(void)
{
    int n = 0;
    int i;

    for (i = 0; i < HUSH_MAX_CLIENTS; ++i) {
        if (clients[i].fd != HUSH_FD_NONE)
            n++;
    }
    return n;
}

static hush_status_t hush_accept_new(int ls)
{
    int cfd;
    int i;

    cfd = accept(ls, NULL, NULL);
    if (cfd < 0)
        return HUSH_ERR_IO;
    hush_set_nonblock(cfd);
    for (i = 0; i < HUSH_MAX_CLIENTS; ++i) {
        if (clients[i].fd == HUSH_FD_NONE) {
            clients[i].fd = cfd;
            clients[i].len = 0;
            clients[i].is_http = 0;
            clients[i].has_sub = 0;
            clients[i].sub_id[0] = '\0';
            return HUSH_OK;
        }
    }
    close(cfd);
    return HUSH_ERR_FULL;
}

static void hush_drop_client(struct client *c)
{
    if (c->fd != HUSH_FD_NONE)
        close(c->fd);
    c->fd = HUSH_FD_NONE;
    c->len = 0;
    c->is_http = 0;
    c->has_sub = 0;
}

static void hush_service_clients(struct pollfd *fds, int nf)
{
    int i;

    for (i = 1; i < nf; ++i) {
        struct client *c = NULL;
        int j;
        ssize_t n;

        if ((fds[i].revents & (POLLIN | POLLHUP | POLLERR)) == 0)
            continue;
        for (j = 0; j < HUSH_MAX_CLIENTS; ++j) {
            if (clients[j].fd == fds[i].fd) {
                c = &clients[j];
                break;
            }
        }
        if (c == NULL)
            continue;
        n = read(c->fd, c->buf + c->len, HUSH_BUF_SZ - c->len - 1);
        if (n <= 0) {
            hush_drop_client(c);
            continue;
        }
        c->len += (size_t)n;
        c->buf[c->len] = '\0';
        hush_on_bytes(c);
    }
}

static void hush_on_bytes(struct client *c)
{
    char *start;
    char *nl;

    if (!c->is_http && hush_http_looks_like(c->buf, c->len))
        c->is_http = 1;
    if (c->is_http) {
        hush_event_t posted;

        if (!hush_http_is_complete(c->buf, c->len))
            return;
        memset(&posted, 0, sizeof(posted));
        (void)hush_http_serve(c->fd, c->buf, c->len, g_store, &posted);
        if (posted.id[0] != '\0')
            hush_fanout(&posted);
        hush_drop_client(c);
        return;
    }
    start = c->buf;
    while ((nl = strchr(start, '\n')) != NULL) {
        *nl = '\0';
        hush_on_nostr_line(c, start);
        start = nl + 1;
    }
    {
        size_t remain = c->len - (size_t)(start - c->buf);
        memmove(c->buf, start, remain);
        c->len = remain;
    }
}

static void hush_on_nostr_line(struct client *c, const char *line)
{
    hush_client_msg_t msg;

    if (hush_proto_parse_line(line, &msg) != HUSH_OK)
        return;
    if (msg.type == HUSH_MSG_EVENT)
        hush_handle_event_msg(c, &msg);
    else if (msg.type == HUSH_MSG_REQ)
        hush_handle_req_msg(c, &msg);
    else if (msg.type == HUSH_MSG_CLOSE)
        c->has_sub = 0;
}

static void hush_send_str(int fd, const char *s)
{
    size_t n = strlen(s);
    size_t off = 0;

    while (off < n) {
        ssize_t w = write(fd, s + off, n - off);
        if (w <= 0)
            break;
        off += (size_t)w;
    }
}

static void hush_handle_event_msg(struct client *c, const hush_client_msg_t *msg)
{
    char line[HUSH_BUF_SZ];

    (void)hush_store_insert(g_store, &msg->event);
    if (hush_proto_format_ok(msg->event.id, 1, "", line, sizeof(line), NULL) == HUSH_OK)
        hush_send_str(c->fd, line);
    hush_fanout(&msg->event);
}

static void hush_handle_req_msg(struct client *c, const hush_client_msg_t *msg)
{
    hush_event_t results[64];
    size_t n;
    size_t i;
    char line[HUSH_BUF_SZ];

    memcpy(c->sub_id, msg->sub_id, sizeof(c->sub_id));
    c->filter = msg->filters[0];
    c->has_sub = 1;
    n = hush_store_query(g_store, msg->filters, msg->nfilters, results, 64);
    for (i = 0; i < n; ++i) {
        if (hush_proto_format_event(c->sub_id, &results[i], line, sizeof(line), NULL) == HUSH_OK)
            hush_send_str(c->fd, line);
    }
    if (hush_proto_format_eose(c->sub_id, line, sizeof(line), NULL) == HUSH_OK)
        hush_send_str(c->fd, line);
}

static void hush_fanout(const hush_event_t *ev)
{
    int i;
    char line[HUSH_BUF_SZ];

    for (i = 0; i < HUSH_MAX_CLIENTS; ++i) {
        if (clients[i].fd == HUSH_FD_NONE || !clients[i].has_sub)
            continue;
        if (!hush_filter_match(&clients[i].filter, ev))
            continue;
        if (hush_proto_format_event(clients[i].sub_id, ev, line, sizeof(line), NULL) == HUSH_OK)
            hush_send_str(clients[i].fd, line);
    }
}

static void hush_shutdown_handler(int sig)
{
    (void)sig;
    g_shutdown = 1;
}

static void hush_install_shutdown_handlers(void)
{
    signal(SIGINT, hush_shutdown_handler);
    signal(SIGTERM, hush_shutdown_handler);
}

static void hush_relay_prepare(uint16_t port)
{
    g_shutdown = 0;
    g_pidfile_ready = 0;
    g_pidfile_path[0] = '\0';
    g_listen_port = port;
    g_leave_ack = 0;
    g_saw_app = 0;
    g_leave_pid = 0;
    g_leave_rd = HUSH_FD_NONE;
    hush_child_reset();
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);
    hush_install_shutdown_handlers();
    hush_clients_reset();
    hush_http_set_listen_port(port);
    hush_launch_init(&g_launch);
    (void)hush_launch_restore_identity(&g_launch);
    (void)hush_launch_restore_vibe(&g_launch);
    hush_http_set_launch(&g_launch);
    hush_turn_init(&g_turn);
    hush_http_set_turn(&g_turn);
    hush_agent_init();
    hush_canvas_init();
    hush_intel_init();
}

static hush_status_t hush_relay_bind(uint16_t port, int open_ui, int *out_ls)
{
    int ls;

    assert(out_ls != NULL);
    *out_ls = HUSH_FD_NONE;
    ls = hush_listen_on(port);
    if (ls >= 0) {
        *out_ls = ls;
        return HUSH_OK;
    }
    if (errno == EADDRINUSE && open_ui) {
        fprintf(stdout,
                "hush-relay already running on http://127.0.0.1:%u/ — opening UI...\n"
                "This is the process already listening. Exit or hush-relay --quit "
                "before a new install can take the port.\n",
                (unsigned)port);
        hush_open_app_window(port);
        return HUSH_OK;
    }
    fprintf(stderr, "hush-relay: cannot bind :%u: %s\n",
            (unsigned)port, strerror(errno));
    return HUSH_ERR_IO;
}

static void hush_relay_announce(uint16_t port, int open_ui)
{
    fprintf(stdout, "listening on http://127.0.0.1:%u/\n", (unsigned)port);
    fprintf(stdout, "  chat UI:  frameless standalone app window\n");
    fprintf(stdout, "  nostr:    newline JSON on the same port\n");
    fprintf(stdout, "  close:    Close in the hive (GUI gone, hive stays)\n");
    fprintf(stdout, "  exit:     Exit in the hive, --quit, or Ctrl+C\n");
    fflush(stdout);
    if (open_ui)
        hush_open_app_window(port);
}

static int hush_fill_pollfds(struct pollfd *fds, int ls)
{
    int nf = 1;
    int i;

    assert(fds != NULL);
    fds[0].fd = ls;
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    for (i = 0; i < HUSH_MAX_CLIENTS; ++i) {
        if (clients[i].fd == HUSH_FD_NONE)
            continue;
        fds[nf].fd = clients[i].fd;
        fds[nf].events = POLLIN;
        fds[nf].revents = 0;
        nf++;
    }
    return nf;
}

static int hush_poll_should_stop(int pr)
{
    if (g_shutdown)
        return 1;
    if (pr < 0 && errno != EINTR)
        return 1;
    return 0;
}

static void hush_relay_pump(int ls)
{
    struct pollfd fds[1 + HUSH_MAX_CLIENTS];

    /* Event pump: ends on g_shutdown or a non-EINTR poll error. */
    for (;;) {
        int nf;
        int pr;

        if (g_shutdown)
            break;
        hush_http_set_client_count(hush_active_clients());
        hush_turn_refresh(&g_turn);
        hush_agent_poll(g_store);
        hush_canvas_poll();
        hush_intel_poll(g_store, &g_launch);
        hush_relay_watch_app();
        nf = hush_fill_pollfds(fds, ls);
        pr = poll(fds, (nfds_t)nf, HUSH_POLL_TIMEOUT_MS);
        if (hush_poll_should_stop(pr))
            break;
        if (pr < 0)
            continue;
        if (fds[0].revents & POLLIN)
            (void)hush_accept_new(ls);
        hush_service_clients(fds, nf);
    }
}

static void hush_relay_cleanup(int ls)
{
    hush_leave_close_rd();
    hush_relay_reap_children();
    hush_agent_shutdown();
    hush_canvas_shutdown();
    hush_turn_shutdown(&g_turn);
    hush_store_destroy(g_store);
    close(ls);
    hush_remove_pidfile();
}

static void hush_pidfile_dir(char *out, size_t sz)
{
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    const char *home = getenv("HOME");
    int n;

    assert(out != NULL);
    assert(sz > 0);
    if (runtime != NULL && runtime[0] != '\0') {
        n = snprintf(out, sz, "%s/hush", runtime);
        if (n > 0 && (size_t)n < sz)
            return;
    }
    if (home != NULL && home[0] != '\0') {
        n = snprintf(out, sz, "%s/.local/state/hush", home);
        if (n > 0 && (size_t)n < sz)
            return;
    }
    snprintf(out, sz, "/tmp/hush");
}

static void hush_pidfile_path(char *out, size_t sz, uint16_t port)
{
    char dir[HUSH_PIDFILE_PATH_MAX];
    int n;

    assert(out != NULL);
    assert(sz > 0);
    hush_pidfile_dir(dir, sizeof(dir));
    n = snprintf(out, sz, "%s/" HUSH_PIDFILE_NAME_FMT, dir, (unsigned)port);
    if (n <= 0 || (size_t)n >= sz)
        out[0] = '\0';
}

static void hush_write_pidfile(uint16_t port)
{
    char dir[HUSH_PIDFILE_PATH_MAX];
    char body[HUSH_PIDFILE_BODY_MAX];
    int fd;
    int n;

    hush_pidfile_dir(dir, sizeof(dir));
    (void)mkdir(dir, 0700);
    hush_pidfile_path(g_pidfile_path, sizeof(g_pidfile_path), port);
    if (g_pidfile_path[0] == '\0')
        return;
    fd = open(g_pidfile_path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (fd < 0)
        return;
    n = snprintf(body, sizeof(body), "%ld\n", (long)getpid());
    if (n <= 0 || (size_t)n >= sizeof(body) ||
        !hush_write_pid_bytes(fd, body, (size_t)n)) {
        close(fd);
        unlink(g_pidfile_path);
        return;
    }
    close(fd);
    g_pidfile_ready = 1;
}

static int hush_write_pid_bytes(int fd, const char *body, size_t len)
{
    ssize_t wrote;

    assert(body != NULL);
    wrote = write(fd, body, len);
    return wrote == (ssize_t)len;
}

static void hush_remove_pidfile(void)
{
    if (!g_pidfile_ready)
        return;
    if (g_pidfile_path[0] != '\0')
        unlink(g_pidfile_path);
    g_pidfile_ready = 0;
}

static hush_status_t hush_read_pidfile(uint16_t port, pid_t *out_pid)
{
    char path[HUSH_PIDFILE_PATH_MAX];
    char body[HUSH_PIDFILE_BODY_MAX];
    FILE *fp;
    long value = 0;

    assert(out_pid != NULL);
    hush_pidfile_path(path, sizeof(path), port);
    if (path[0] == '\0')
        return HUSH_ERR_NOT_FOUND;
    fp = fopen(path, "r");
    if (fp == NULL)
        return HUSH_ERR_NOT_FOUND;
    if (fgets(body, (int)sizeof(body), fp) == NULL) {
        fclose(fp);
        return HUSH_ERR_PARSE;
    }
    fclose(fp);
    value = strtol(body, NULL, 10);
    if (value <= 0 || value > (long)INT_MAX)
        return HUSH_ERR_PARSE;
    *out_pid = (pid_t)value;
    return HUSH_OK;
}

static int hush_pid_is_alive(pid_t pid)
{
    if (pid <= 0)
        return 0;
    if (kill(pid, 0) == 0)
        return 1;
    return errno != ESRCH;
}

static void hush_wait_pid_gone(pid_t pid)
{
    int i;

    for (i = 0; i < HUSH_QUIT_WAIT_TRIES; ++i) {
        struct timespec pause;

        if (!hush_pid_is_alive(pid))
            return;
        pause.tv_sec = 0;
        pause.tv_nsec = (long)HUSH_QUIT_WAIT_MS * 1000000L;
        nanosleep(&pause, NULL);
    }
}

static int hush_child_is_app(pid_t pid)
{
    if (pid <= 1)
        return 0;
    if (pid == g_leave_pid)
        return 0;
    return hush_child_cmdline_matches(pid, g_listen_port);
}

static int hush_leave_app_alive(void)
{
    int i;
    int alive = 0;

    for (i = 0; i < g_nchildren; ++i) {
        if (!hush_child_is_app(g_children[i]))
            continue;
        if (hush_pid_is_alive(g_children[i]))
            alive = 1;
    }
    return alive;
}

static void hush_leave_forget_dead(void)
{
    int i;
    int n = 0;

    for (i = 0; i < g_nchildren; ++i) {
        if (g_children[i] <= 0)
            continue;
        if (!hush_pid_is_alive(g_children[i]))
            continue;
        g_children[n] = g_children[i];
        n++;
    }
    g_nchildren = n;
}

static void hush_leave_print_hint(void)
{
    fprintf(stdout,
            "The --app window closed. Hive stays standing. "
            "Exit or hush-relay --quit before a new install can take the port.\n");
    fflush(stdout);
}

static void hush_leave_write_result(int wr, int status, const char *out)
{
    char line[HUSH_LEAVE_OUT_MAX];
    int n;

    assert(out != NULL);
    n = snprintf(line, sizeof(line), "%d\n%s", status, out);
    if (n < 0)
        return;
    if ((size_t)n >= sizeof(line))
        n = (int)sizeof(line) - 1;
    if (write(wr, line, (size_t)n) < 0)
        return;
}

static void hush_leave_exec_dialog(int wr)
{
    dup2(wr, STDOUT_FILENO);
    close(wr);
    execlp(HUSH_LEAVE_BIN, HUSH_LEAVE_BIN,
           "--question",
           "--title=" HUSH_LEAVE_TITLE,
           "--text=" HUSH_LEAVE_TEXT,
           "--ok-label=" HUSH_LEAVE_OK,
           "--extra-button=" HUSH_LEAVE_EXTRA,
           "--cancel-label=" HUSH_LEAVE_CANCEL,
           (char *)NULL);
    _exit(HUSH_LEAVE_MISSING);
}

/* Waiter: SIGCHLD default so waitpid can reap zenity.
 * zenity 4: OK=0 empty; extra-button=1 + label; Cancel=1 empty. */
static void hush_leave_run_zenity(int wr)
{
    int out[2];
    pid_t pid;
    char buf[HUSH_LEAVE_OUT_MAX];
    ssize_t n;
    int st = 0;

    signal(SIGCHLD, SIG_DFL);
    if (pipe(out) != 0) {
        hush_leave_write_result(wr, HUSH_LEAVE_MISSING, "");
        _exit(0);
    }
    pid = fork();
    if (pid < 0) {
        hush_leave_write_result(wr, HUSH_LEAVE_MISSING, "");
        _exit(0);
    }
    if (pid == 0) {
        close(out[0]);
        hush_leave_exec_dialog(out[1]);
    }
    close(out[1]);
    n = read(out[0], buf, sizeof(buf) - 1);
    close(out[0]);
    if (n < 0)
        n = 0;
    buf[n] = '\0';
    (void)waitpid(pid, &st, 0);
    hush_leave_write_result(wr, WIFEXITED(st) ? WEXITSTATUS(st) : HUSH_LEAVE_MISSING,
                            buf);
    _exit(0);
}

static void hush_leave_apply(int status, const char *out)
{
    assert(out != NULL);
    if (status == HUSH_LEAVE_MISSING) {
        hush_leave_print_hint();
        g_leave_ack = 1;
        return;
    }
    if (strstr(out, HUSH_LEAVE_EXTRA) != NULL) {
        g_leave_ack = 1;
        return;
    }
    if (status == 0) {
        g_leave_ack = 1;
        hush_relay_request_shutdown();
        return;
    }
    g_leave_ack = 0;
    hush_open_app_window(g_listen_port);
}

static void hush_leave_close_rd(void)
{
    if (g_leave_rd == HUSH_FD_NONE)
        return;
    close(g_leave_rd);
    g_leave_rd = HUSH_FD_NONE;
}

static void hush_leave_finish(int status, const char *out)
{
    hush_leave_close_rd();
    g_leave_pid = 0;
    hush_leave_apply(status, out);
}

static int hush_leave_parse_status(const char *buf)
{
    assert(buf != NULL);
    if (buf[0] == '\0')
        return HUSH_LEAVE_MISSING;
    return atoi(buf);
}

static const char *hush_leave_parse_out(const char *buf)
{
    const char *nl;

    assert(buf != NULL);
    nl = strchr(buf, '\n');
    if (nl == NULL)
        return "";
    return nl + 1;
}

static void hush_leave_poll(void)
{
    char buf[HUSH_LEAVE_OUT_MAX];
    ssize_t n;

    if (g_leave_pid <= 0)
        return;
    if (hush_pid_is_alive(g_leave_pid))
        return;
    n = 0;
    if (g_leave_rd != HUSH_FD_NONE)
        n = read(g_leave_rd, buf, sizeof(buf) - 1);
    if (n < 0)
        n = 0;
    buf[n] = '\0';
    hush_leave_finish(hush_leave_parse_status(buf), hush_leave_parse_out(buf));
}

static void hush_leave_spawn(void)
{
    int pfd[2];
    pid_t pid;

    if (g_leave_pid > 0)
        return;
    if (pipe(pfd) != 0) {
        hush_leave_print_hint();
        g_leave_ack = 1;
        return;
    }
    pid = fork();
    if (pid < 0) {
        close(pfd[0]);
        close(pfd[1]);
        hush_leave_print_hint();
        g_leave_ack = 1;
        return;
    }
    if (pid == 0) {
        close(pfd[0]);
        hush_leave_run_zenity(pfd[1]);
    }
    close(pfd[1]);
    g_leave_rd = pfd[0];
    g_leave_pid = pid;
    hush_relay_track_child(pid);
}

static void hush_relay_watch_app(void)
{
    hush_leave_forget_dead();
    hush_leave_poll();
    if (hush_leave_app_alive()) {
        if (!g_saw_app)
            (void)hush_win_undecorate();
        g_saw_app = 1;
        return;
    }
    if (g_shutdown || g_leave_ack || g_leave_pid > 0)
        return;
    if (!g_saw_app)
        return;
    g_saw_app = 0;
    hush_leave_spawn();
}

static void hush_child_reset(void)
{
    int i;

    for (i = 0; i < HUSH_CHILD_MAX; ++i)
        g_children[i] = 0;
    g_nchildren = 0;
}

static void hush_child_stop_one(pid_t pid)
{
    assert(pid != getpid());
    if (pid <= 1)
        return;
    if (!hush_pid_is_alive(pid))
        return;
    (void)kill(pid, SIGTERM);
    hush_wait_pid_gone(pid);
    if (hush_pid_is_alive(pid))
        (void)kill(pid, SIGKILL);
}

#ifdef __linux__
static int hush_child_read_cmdline(pid_t pid, char *out, size_t outsz)
{
    char path[HUSH_PROC_PATH_MAX];
    int fd;
    ssize_t n;
    ssize_t i;

    assert(out != NULL);
    assert(outsz > 0);
    if (snprintf(path, sizeof(path), "/proc/%ld/cmdline", (long)pid) >= (int)sizeof(path))
        return 0;
    fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    n = read(fd, out, outsz - 1);
    close(fd);
    if (n <= 0)
        return 0;
    for (i = 0; i < n; ++i) {
        if (out[i] == '\0')
            out[i] = ' ';
    }
    out[n] = '\0';
    return 1;
}

static int hush_child_cmdline_matches(pid_t pid, uint16_t port)
{
    char line[HUSH_CHILD_CMDLINE_MAX];
    char needle[HUSH_UI_APP_ARG_MAX];
    int n;

    if (port == 0)
        return 0;
    if (pid == getpid())
        return 0;
    n = snprintf(needle, sizeof(needle),
                 "--app=http://127.0.0.1:%u/", (unsigned)port);
    if (n <= 0 || (size_t)n >= sizeof(needle))
        return 0;
    if (!hush_child_read_cmdline(pid, line, sizeof(line)))
        return 0;
    if (strstr(line, "--class=hush-relay") == NULL)
        return 0;
    return strstr(line, needle) != NULL;
}

static void hush_child_sweep_entry(const char *name, uint16_t port)
{
    char *end = NULL;
    long value;
    pid_t pid;

    assert(name != NULL);
    if (name[0] < '1' || name[0] > '9')
        return;
    value = strtol(name, &end, 10);
    if (end == NULL || *end != '\0' || value <= 1)
        return;
    pid = (pid_t)value;
    if (!hush_child_cmdline_matches(pid, port))
        return;
    hush_child_stop_one(pid);
}

static void hush_child_sweep_proc(uint16_t port)
{
    DIR *dir;
    struct dirent *ent;

    if (port == 0)
        return;
    dir = opendir("/proc");
    if (dir == NULL)
        return;
    while ((ent = readdir(dir)) != NULL)
        hush_child_sweep_entry(ent->d_name, port);
    closedir(dir);
}
#else
static int hush_child_cmdline_matches(pid_t pid, uint16_t port)
{
    (void)pid;
    (void)port;
    return 0;
}

static void hush_child_sweep_proc(uint16_t port)
{
    (void)port;
}
#endif
