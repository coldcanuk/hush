/* hush_relay.c: owns the poll loop, client table, and message dispatch for Hush. */

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "hush_proto.h"
#include "hush_relay.h"
#include "hush_store.h"

enum {
    HUSH_MAX_CLIENTS = 32,
    HUSH_BUF_SZ = 8192,
    HUSH_LISTEN_BACKLOG = 8,
    HUSH_POLL_TIMEOUT_MS = 1000
};

struct client {
    int fd;
    char buf[HUSH_BUF_SZ];
    size_t len;
};

static struct client clients[HUSH_MAX_CLIENTS];
static hush_store_t *g_store = NULL;

/* Sets O_NONBLOCK on fd. Ignores errors for MVP. */
static void hush_set_nonblock(int fd);

/* Handles a parsed client message. For MVP, EVENT inserts; REQ queries (no reply yet). */
static hush_status_t hush_handle_msg(int fd, const hush_client_msg_t *msg);

/* Accepts new client if slot available. */
static void hush_accept_new(int ls);

/* Reads available data for one client; on full line, parses and handles. */
static void hush_service_client(int idx);

/* Initializes client table (all fds invalid). */
static void hush_clients_init(void);

/* One iteration of the poll + dispatch loop. Returns non-zero to continue. */
static int hush_poll_once(int ls);

hush_status_t hush_relay_run(uint16_t port)
{
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls < 0)
        return HUSH_ERR_IO;

    int yes = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(ls, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(ls);
        return HUSH_ERR_IO;
    }

    if (listen(ls, HUSH_LISTEN_BACKLOG) < 0) {
        close(ls);
        return HUSH_ERR_IO;
    }

    hush_set_nonblock(ls);

    if (hush_store_create(&g_store) != HUSH_OK) {
        close(ls);
        return HUSH_ERR_FULL;
    }

    hush_clients_init();

    while (hush_poll_once(ls)) {
        /* loop until error or signal */
    }

    hush_store_destroy(g_store);
    close(ls);
    return HUSH_OK;
}

static void hush_clients_init(void)
{
    for (int i = 0; i < HUSH_MAX_CLIENTS; ++i)
        clients[i].fd = -1;
}

static int hush_poll_once(int ls)
{
    struct pollfd fds[1 + HUSH_MAX_CLIENTS];
    fds[0].fd = ls;
    fds[0].events = POLLIN;
    int nf = 1;

    for (int i = 0; i < HUSH_MAX_CLIENTS; ++i) {
        if (clients[i].fd >= 0) {
            fds[nf].fd = clients[i].fd;
            fds[nf].events = POLLIN;
            nf++;
        }
    }

    int pr = poll(fds, (nfds_t)nf, HUSH_POLL_TIMEOUT_MS);
    if (pr < 0) {
        if (errno == EINTR)
            return 1;
        return 0;
    }

    if (fds[0].revents & POLLIN)
        hush_accept_new(ls);

    for (int i = 0; i < HUSH_MAX_CLIENTS; ++i) {
        if (clients[i].fd >= 0)
            hush_service_client(i);
    }
    return 1;
}

static void hush_set_nonblock(int fd)
{
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0)
        (void)fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

static hush_status_t hush_handle_msg(int fd, const hush_client_msg_t *msg)
{
    (void)fd;
    if (msg == NULL)
        return HUSH_ERR_ARG;

    if (msg->type == HUSH_MSG_EVENT) {
        return hush_store_insert(g_store, &msg->event);
    } else if (msg->type == HUSH_MSG_REQ) {
        hush_event_t results[16];
        (void)hush_store_query(g_store, msg->filters, msg->nfilters, results, 16);
        return HUSH_OK;
    }
    return HUSH_OK;
}

static void hush_accept_new(int ls)
{
    int cfd = accept(ls, NULL, NULL);
    if (cfd < 0)
        return;

    hush_set_nonblock(cfd);

    for (int i = 0; i < HUSH_MAX_CLIENTS; ++i) {
        if (clients[i].fd < 0) {
            clients[i].fd = cfd;
            clients[i].len = 0;
            return;
        }
    }

    close(cfd);
}

static void hush_service_client(int idx)
{
    struct client *c = &clients[idx];
    if (c->fd < 0)
        return;

    ssize_t n = read(c->fd, c->buf + c->len, HUSH_BUF_SZ - c->len - 1);
    if (n <= 0) {
        close(c->fd);
        c->fd = -1;
        return;
    }

    c->len += (size_t)n;
    c->buf[c->len] = '\0';

    char *nl = strchr(c->buf, '\n');
    if (nl != NULL) {
        *nl = '\0';
        hush_client_msg_t msg;
        if (hush_proto_parse_line(c->buf, &msg) == HUSH_OK) {
            (void)hush_handle_msg(c->fd, &msg);
        }
        size_t rest = c->len - (size_t)(nl - c->buf + 1);
        memmove(c->buf, nl + 1, rest);
        c->len = rest;
    }

    if (c->len >= HUSH_BUF_SZ - 1) {
        close(c->fd);
        c->fd = -1;
        c->len = 0;
    }
}
