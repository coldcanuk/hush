/* hush_relay.c: owns the poll-based relay server and connection dispatch for Hush. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
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
#include <time.h>
#include <unistd.h>

#include "hush_http.h"
#include "hush_launch.h"
#include "hush_proto.h"
#include "hush_relay.h"
#include "hush_status.h"
#include "hush_store.h"
#include "hush_turn.h"

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
    HUSH_QUIT_WAIT_MS = 100
};

#define HUSH_PIDFILE_NAME_FMT "relay-%u.pid"

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
static void hush_relay_cleanup(int ls);
static int hush_fill_pollfds(struct pollfd *fds, int ls);
static int hush_poll_should_stop(int pr);

void hush_relay_request_shutdown(void)
{
    g_shutdown = 1;
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
    if (pid != 0)
        return;
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
        execlp(browsers[i], browsers[i],
               "--class=hush-relay", "--name=Hush", app_arg, (char *)NULL);
    }
    execlp("epiphany", "epiphany", "--application-mode", url, (char *)NULL);
    execlp("flatpak", "flatpak", "run", "com.brave.Browser",
           "--class=hush-relay", app_arg, (char *)NULL);
    execlp("flatpak", "flatpak", "run", "org.chromium.Chromium",
           "--class=hush-relay", app_arg, (char *)NULL);
    execlp("flatpak", "flatpak", "run", "com.google.Chrome",
           "--class=hush-relay", app_arg, (char *)NULL);
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
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);
    hush_install_shutdown_handlers();
    hush_clients_reset();
    hush_http_set_listen_port(port);
    hush_launch_init(&g_launch);
    (void)hush_launch_restore_identity(&g_launch);
    hush_http_set_launch(&g_launch);
    hush_turn_init(&g_turn);
    hush_http_set_turn(&g_turn);
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
                "hush-relay already running on http://127.0.0.1:%u/ — opening UI...\n",
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
    fprintf(stdout, "  chat UI:  standalone app window (no browser chrome)\n");
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
