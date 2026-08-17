/* hush_relay.c: owns the poll-based relay server and connection dispatch for Hush. */

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
#include "hush_status.h"
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

static void hush_set_nonblock(int fd);
static hush_status_t hush_accept_new(int ls);
static void hush_service_clients(struct pollfd *fds, int nf);
static hush_status_t hush_handle_line(int fd, const char *line);
static hush_status_t hush_handle_msg(int fd, const hush_client_msg_t *msg);

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
    listen(ls, HUSH_LISTEN_BACKLOG);
    hush_set_nonblock(ls);

    if (hush_store_create(&g_store) != HUSH_OK) {
        close(ls);
        return HUSH_ERR_FULL;
    }

    struct pollfd fds[1 + HUSH_MAX_CLIENTS];
    for (;;) {
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
            if (errno == EINTR) continue;
            break;
        }
        if (fds[0].revents & POLLIN) {
            (void)hush_accept_new(ls);
        }
        hush_service_clients(fds, nf);
    }
    hush_store_destroy(g_store);
    close(ls);
    return HUSH_OK;
}

static void hush_set_nonblock(int fd)
{
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0)
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

static hush_status_t hush_accept_new(int ls)
{
    int cfd = accept(ls, NULL, NULL);
    if (cfd < 0)
        return HUSH_ERR_IO;
    hush_set_nonblock(cfd);
    for (int i = 0; i < HUSH_MAX_CLIENTS; ++i) {
        if (clients[i].fd < 0) {
            clients[i].fd = cfd;
            clients[i].len = 0;
            return HUSH_OK;
        }
    }
    close(cfd);
    return HUSH_ERR_FULL;
}

static void hush_service_clients(struct pollfd *fds, int nf)
{
    for (int i = 1; i < nf; ++i) {
        if ((fds[i].revents & POLLIN) == 0)
            continue;
        int fd = fds[i].fd;
        /* find client slot */
        struct client *c = NULL;
        for (int j = 0; j < HUSH_MAX_CLIENTS; ++j) {
            if (clients[j].fd == fd) { c = &clients[j]; break; }
        }
        if (!c) continue;
        /* read */
        ssize_t n = read(fd, c->buf + c->len, HUSH_BUF_SZ - c->len - 1);
        if (n <= 0) {
            close(fd);
            c->fd = -1;
            continue;
        }
        c->len += (size_t)n;
        c->buf[c->len] = '\0';
        /* process lines */
        char *start = c->buf;
        char *nl;
        while ((nl = strchr(start, '\n')) != NULL) {
            *nl = '\0';
            (void)hush_handle_line(fd, start);
            start = nl + 1;
        }
        /* shift remainder */
        size_t remain = c->len - (size_t)(start - c->buf);
        memmove(c->buf, start, remain);
        c->len = remain;
    }
}

static hush_status_t hush_handle_line(int fd, const char *line)
{
    hush_client_msg_t msg;
    hush_status_t st = hush_proto_parse_line(line, &msg);
    if (st != HUSH_OK)
        return st;
    return hush_handle_msg(fd, &msg);
}

static hush_status_t hush_handle_msg(int fd, const hush_client_msg_t *msg)
{
    (void)fd;
    if (msg->type == HUSH_MSG_EVENT) {
        (void)hush_store_insert(g_store, &msg->event);
        return HUSH_OK;
    } else if (msg->type == HUSH_MSG_REQ) {
        hush_event_t results[64];
        size_t n = hush_store_query(g_store, msg->filters, msg->nfilters, results, 64);
        (void)n;
        /* In full impl: send ["EVENT", sub, ...] + ["EOSE", sub] */
        return HUSH_OK;
    }
    return HUSH_OK;
}
