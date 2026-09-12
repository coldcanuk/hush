/* hush_http.c: owns HTTP status/events/PWA UI serving for hush-relay. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "hush_agent.h"
#include "hush_auth.h"
#include "hush_canvas.h"
#include "hush_cevent.h"
#include "hush_home.h"
#include "hush_http.h"
#include "hush_http_internal.h"
#include "hush_intel.h"
#include "hush_json.h"
#include "hush_limiter.h"
#include "hush_presence.h"
#include "hush_provider.h"
#include "hush_relay.h"
#include "hush_skill.h"
#include "hush_skillui.h"
#include "hush_thread.h"
#include "hush_ui_html.h"
#include "hush_wake.h"
#include "hush_win.h"
#include "hush_icon_panels.h"

enum {
    HUSH_HTTP_WRITE_RETRIES = 8,
    HUSH_HTTP_WRITE_WAIT_MS = 250,
    HUSH_HTTP_JSON_MAX = 65536,
    HUSH_HTTP_KIND_BYTES = 16,
    HUSH_HTTP_HDR_MAX = 8192,
    HUSH_HTTP_LOGIN_REPLY_MAX = 384,
    HUSH_HTTP_MENTIONS_MAX = 8,
    HUSH_HTTP_CHAN_LIST_MAX = 8,
    HUSH_HTTP_CANVAS_PATH_MAX = 384,
    HUSH_HTTP_FIXUP_ASK_MAX = 500,
    HUSH_HTTP_FIXUP_JSON_MAX = 16384,
    HUSH_HTTP_FIXUP_SLEEP_NS = 50000000,
    HUSH_HTTP_FIXUP_WAIT_MAX = 1800,
    HUSH_HTTP_FIXUP_TOKEN_MAX = 16,
    HUSH_HTTP_WINDOW_ACT_MAX = 16,
    HUSH_HTTP_COMPLETE_JSON_MAX = 1536,
    HUSH_HTTP_HDR_VALUE_MAX = 512,
    HUSH_HTTP_HOST_MAX = 128,
    /* Ingress throttling: per-IP API requests and per-hive provider quotas.
     * Generous on purpose; the 429 is a flood defense, not a UX gate. */
    HUSH_HTTP_IP_LIMITS_MAX = 16,
    HUSH_HTTP_IP_REQ_PER_S = 60,
    HUSH_HTTP_IP_REQ_BURST = 120,
    HUSH_HTTP_COMPLETE_PER_MIN = 12,
    HUSH_HTTP_COMPLETE_BURST = 12,
    HUSH_HTTP_FIXUP_PER_MIN = 12,
    HUSH_HTTP_FIXUP_BURST = 12,
    HUSH_HTTP_SECONDS_PER_MIN = 60
};

#define HUSH_HTTP_CLOSE_JSON "{\"ok\":true,\"action\":\"close\"}\n"
#define HUSH_HTTP_EXIT_JSON  "{\"ok\":true,\"action\":\"exit\"}\n"
#define HUSH_HTTP_CANVAS_OK "{\"ok\":true}\n"
#define HUSH_HTTP_FIXUP_FAIL "{\"ok\":false,\"error\":\"fixup failed\"}\n"
#define HUSH_HTTP_COMPLETE_FAIL "{\"ok\":false,\"error\":\"complete failed\"}\n"
#define HUSH_HTTP_COMPLETE_PEND "{\"ok\":true,\"pending\":true}\n"
#define HUSH_HTTP_WINDOW_MIN_JSON "{\"ok\":true,\"action\":\"minimize\"}\n"
#define HUSH_HTTP_WINDOW_MAX_JSON "{\"ok\":true,\"action\":\"maximize\"}\n"
#define HUSH_HTTP_WINDOW_PREPARE_JSON "{\"ok\":true,\"action\":\"prepare\"}\n"
#define HUSH_HTTP_WINDOW_FAIL "{\"ok\":false,\"error\":\"window failed\"}\n"
#define HUSH_HTTP_WINDOW_MIN "minimize"
#define HUSH_HTTP_WINDOW_MAX "maximize"
#define HUSH_HTTP_WINDOW_PREPARE "prepare"
#define HUSH_HTTP_RATE_BODY "{\"ok\":false,\"error\":\"rate-limited\"}\n"

/* Per-source-IP API request bucket. addr 0 marks a free slot (a real peer
 * can never be 0.0.0.0). */
typedef struct {
    uint32_t addr;
    hush_limiter_t req_lim;
} hush_http_ip_limit_t;

typedef struct {
    char api_key[HUSH_PROVIDER_KEY_MAX];
    char username[HUSH_PROVIDER_KEY_MAX];
    char password[HUSH_PROVIDER_KEY_MAX];
    char token[HUSH_PROVIDER_KEY_MAX];
    char passkey[HUSH_PROVIDER_KEY_MAX];
} hush_http_provider_buf_t;

static uint16_t g_listen_port;
static int g_client_count;
static hush_launch_t *g_launch;
static hush_turn_t *g_turn;
static char g_bind_addr[HUSH_HTTP_HOST_MAX];
static int g_set_cookie;
static hush_http_ip_limit_t g_ip_req[HUSH_HTTP_IP_LIMITS_MAX];
static hush_limiter_t g_complete_lim;
static hush_limiter_t g_fixup_lim;
static int g_limits_ready;

hush_launch_t *hush_http_launch(void)
{
    return g_launch;
}

hush_turn_t *hush_http_turn(void)
{
    return g_turn;
}

int hush_http_client_count(void)
{
    return g_client_count;
}

uint16_t hush_http_listen_port(void)
{
    return g_listen_port;
}

const char *hush_http_headers_end(const char *buf, size_t len);
static long hush_http_content_length(const char *buf, size_t hlen);
static void hush_http_path(const char *req, char *out, size_t outsz);
/* Writes required bytes with bounded backpressure; IO on disconnect or stalled reader. */
hush_status_t hush_http_write_all(int fd, const char *buf, size_t len);
/* Returns the borrowed h-tag value regardless of tag order, or general if absent. */
const char *hush_http_event_channel(const hush_event_t *event);
void hush_http_reply(int fd, const char *status, const char *ctype,
                            const char *body, size_t blen);
void hush_http_json_unescape_copy(const char *src, char *dst, size_t dstsz);
int hush_http_json_field(const char *body, const char *key, char *out, size_t outsz);
int hush_http_json_has_key(const char *body, const char *key);
int hush_http_json_bare_field(const char *body, const char *key,
                                char *out, size_t outsz);
/* Decodes optional required room guidance into borrowed caller storage.
 * Missing field preserves the supplied default; invalid or oversized fields fail. */
static const hush_launch_project_t *hush_http_find_project(const char *slug);
static int hush_http_canvas_rel_ok(const char *rel);
static hush_status_t hush_http_canvas_join(char *out, size_t outsz,
                                           const char *root, const char *rel);
static hush_status_t hush_http_canvas_write(const char *path,
                                            const char *content);
static hush_status_t hush_http_serve_canvas(int fd, const char *body);
static hush_status_t hush_http_serve_cancel(int fd, const char *body);
static hush_status_t hush_http_serve_reply(int fd, const char *body);
static hush_status_t hush_http_serve_fixup(int fd, const char *body);
static void hush_http_reply_fixup_ok(int fd, const char *text);
static hush_status_t hush_http_serve_complete_post(int fd, const char *body);
static hush_status_t hush_http_serve_complete_get(int fd, const char *req);
static void hush_http_complete_token(char *out, size_t outsz, const char *req);
static void hush_http_reply_complete_token(int fd, const char *token);
static void hush_http_reply_complete_text(int fd, const char *text);
static hush_status_t hush_http_wait_fixup(const char *token, char *out,
                                          size_t outsz);
static void hush_http_pause_fixup(void);
static hush_status_t hush_http_serve_turn_post(int fd, const char *body);
static void hush_http_serve_turn_get(int fd);
static void hush_http_serve_ice(int fd);
static hush_status_t hush_http_serve_api_post(int fd, const char *path,
                                              const char *req, size_t len,
                                              hush_store_t *store,
                                              hush_event_t *out_posted);
const char *hush_http_body(const char *req, size_t len);
static hush_status_t hush_http_serve_close(int fd);
static hush_status_t hush_http_serve_exit(int fd);
static hush_status_t hush_http_serve_window(int fd, const char *body);
static hush_status_t hush_http_window_run(const char *action);
static void hush_http_reply_window(int fd, const char *action);
/* Enumeration serializer retains its existing output-offset/first-item ABI. */
static void hush_http_append_provider(char *body, size_t bodysz, size_t *n,
                                      const hush_provider_status_t *st,
                                      int first);
static hush_status_t hush_http_serve_provider_get(int fd);
/* Copies host, model, use_home, and optional secrets from body into in.
 * Secret bytes live in buf; in holds borrowed pointers into buf. */
static void hush_http_fill_provider_in(hush_provider_in_t *in,
                                       hush_http_provider_buf_t *buf,
                                       const char *body);
/* Points *dst at buf when body contains kind. buf is KEY_MAX. */
static void hush_http_take_secret(const char **dst, char *buf,
                                  const char *body, const char *kind);
static hush_status_t hush_http_serve_provider_post(int fd, const char *body);
static hush_status_t hush_http_serve_provider_scan(int fd, const char *body);
static hush_status_t hush_http_serve_provider_login(int fd, const char *body);
static void hush_http_reply_scan(int fd, const hush_provider_scan_t *scan,
                                 hush_status_t st);

/* True when line begins with name followed by ':'. Case-insensitive. */
static int hush_http_line_has_name(const char *line, const char *name,
                                   size_t name_len);
/* Copies a header value trimmed of leading blanks and trailing CR/LF. */
static void hush_http_copy_value(const char *src, char *out, size_t outsz);
/* Copies the ?k= credential from the request line. 0 when absent. */
static int hush_http_query_token(const char *req, char *out, size_t outsz);
/* Copies cookie name's value from a Cookie header. 0 when absent. */
static int hush_http_cookie_token(const char *cookie, const char *name,
                                  char *out, size_t outsz);
/* True when any accepted credential matches the live session token. */
static int hush_http_request_is_authed(const char *req, size_t len);
/* True when the peer address is loopback. */
static int hush_http_peer_is_loopback(int fd);
/* True when host names the local machine. */
static int hush_http_host_is_loopback(const char *host);
/* Truncates a host at its port, keeping [v6] brackets. */
static void hush_http_host_strip_port(char *host);
/* True when the Host header may serve this request. */
static int hush_http_host_ok(const char *req, size_t len);
/* True when path is an API route that requires the session credential. */
static int hush_http_needs_auth(const char *req, const char *path);
/* Returns the per-IP request bucket for fd, creating it on first sight;
 * NULL when the peer or the table is unavailable (fail open). */
static hush_limiter_t *hush_http_ip_req_lim(int fd);
/* Replies 429 with the rate-limit JSON. */
static hush_status_t hush_http_reply_rate_limited(int fd);
/* Replies 401/403 and returns DENIED when host or token checks fail. */
static hush_status_t hush_http_guard(int fd, const char *req, size_t len,
                                     const char *path);

void hush_http_set_listen_port(uint16_t port)
{
    g_listen_port = port;
}

void hush_http_set_client_count(int n)
{
    g_client_count = n;
}

void hush_http_init_limits(void)
{
    if (g_limits_ready)
        return;
    hush_limiter_init(&g_complete_lim,
                      (double)HUSH_HTTP_COMPLETE_PER_MIN /
                          (double)HUSH_HTTP_SECONDS_PER_MIN,
                      (double)HUSH_HTTP_COMPLETE_BURST);
    hush_limiter_init(&g_fixup_lim,
                      (double)HUSH_HTTP_FIXUP_PER_MIN /
                          (double)HUSH_HTTP_SECONDS_PER_MIN,
                      (double)HUSH_HTTP_FIXUP_BURST);
    g_limits_ready = 1;
}

void hush_http_set_bind_addr(const char *addr)
{
    size_t len = 0;

    if (addr != NULL)
        len = strlen(addr);
    if (len >= sizeof(g_bind_addr))
        len = sizeof(g_bind_addr) - 1;
    if (len > 0)
        memcpy(g_bind_addr, addr, len);
    g_bind_addr[len] = '\0';
}

void hush_http_set_launch(hush_launch_t *launch)
{
    g_launch = launch;
}

void hush_http_set_turn(hush_turn_t *turn)
{
    g_turn = turn;
}

int hush_http_looks_like(const char *buf, size_t len)
{
    if (buf == NULL)
        return 0;
    if (len >= 3 && memcmp(buf, "GET", 3) == 0)
        return 1;
    if (len >= 4 && memcmp(buf, "POST", 4) == 0)
        return 1;
    if (len >= 4 && memcmp(buf, "HEAD", 4) == 0)
        return 1;
    if (len >= 7 && memcmp(buf, "OPTIONS", 7) == 0)
        return 1;
    return 0;
}

int hush_http_is_complete(const char *buf, size_t len)
{
    const char *end;
    long cl;
    size_t hlen;

    if (buf == NULL)
        return 0;
    end = hush_http_headers_end(buf, len);
    if (end == NULL)
        return 0;
    hlen = (size_t)(end - buf) + 4;
    cl = hush_http_content_length(buf, hlen);
    if (cl < 0)
        return 1;
    return len >= hlen + (size_t)cl;
}

hush_status_t hush_http_serve(int fd, const char *req, size_t len,
                              hush_store_t *store, hush_event_t *out_posted)
{
    char path[HUSH_HTTP_PATH_MAX];

    if (req == NULL || store == NULL || out_posted == NULL)
        return HUSH_ERR_ARG;
    memset(out_posted, 0, sizeof(*out_posted));
    g_set_cookie = 0;
    if (len >= 7 && memcmp(req, "OPTIONS", 7) == 0) {
        hush_http_reply(fd, "204 No Content", "text/plain", "", 0);
        return HUSH_OK;
    }
    hush_http_path(req, path, sizeof(path));
    /* A loopback browser earns the session cookie on any first response, so
     * the fetch after page load already carries it. Remotes present the token. */
    if (hush_http_peer_is_loopback(fd) &&
        !hush_http_request_is_authed(req, len))
        g_set_cookie = 1;
    if (hush_http_guard(fd, req, len, path) != HUSH_OK)
        return HUSH_ERR_DENIED;
    if (hush_http_serve_asset(fd, path))
        return HUSH_OK;
    if (strcmp(path, "/api/status") == 0 && memcmp(req, "GET", 3) == 0) {
        hush_http_serve_status(fd, store);
        return HUSH_OK;
    }
    if (strcmp(path, "/api/events") == 0 && memcmp(req, "GET", 3) == 0) {
        hush_http_serve_events(fd, store);
        return HUSH_OK;
    }
    if (strcmp(path, "/api/session") == 0 && memcmp(req, "GET", 3) == 0) {
        hush_http_serve_session(fd);
        return HUSH_OK;
    }
    if (strcmp(path, "/api/chan-events") == 0 && memcmp(req, "GET", 3) == 0) {
        hush_http_serve_chan_events(fd, req);
        return HUSH_OK;
    }
    if (strcmp(path, "/api/presence") == 0 && memcmp(req, "GET", 3) == 0) {
        hush_http_serve_presence_get(fd);
        return HUSH_OK;
    }
    if (strcmp(path, "/api/skills") == 0 && memcmp(req, "GET", 3) == 0) {
        hush_http_serve_skills_get(fd);
        return HUSH_OK;
    }
    if (strcmp(path, "/api/turn") == 0 && memcmp(req, "GET", 3) == 0) {
        hush_http_serve_turn_get(fd);
        return HUSH_OK;
    }
    if (strcmp(path, "/api/ice") == 0 && memcmp(req, "GET", 3) == 0) {
        hush_http_serve_ice(fd);
        return HUSH_OK;
    }
    if (strcmp(path, "/api/provider") == 0 && memcmp(req, "GET", 3) == 0)
        return hush_http_serve_provider_get(fd);
    if (strcmp(path, "/api/complete") == 0 && memcmp(req, "GET", 3) == 0)
        return hush_http_serve_complete_get(fd, req);
    if (memcmp(req, "POST", 4) == 0)
        return hush_http_serve_api_post(fd, path, req, len, store, out_posted);
    hush_http_reply(fd, "404 Not Found", "text/plain", "not found\n", 10);
    return HUSH_ERR_NOT_FOUND;
}

const char *hush_http_headers_end(const char *buf, size_t len)
{
    size_t i;

    if (len < 4)
        return NULL;
    for (i = 0; i + 3 < len; ++i) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' &&
            buf[i + 2] == '\r' && buf[i + 3] == '\n')
            return buf + i;
    }
    return NULL;
}

static long hush_http_content_length(const char *buf, size_t hlen)
{
    const char *p;
    char tmp[HUSH_HTTP_HDR_MAX];

    if (hlen >= sizeof(tmp))
        return -1;
    memcpy(tmp, buf, hlen);
    tmp[hlen] = '\0';
    p = strstr(tmp, "Content-Length:");
    if (p == NULL)
        p = strstr(tmp, "content-length:");
    if (p == NULL)
        return -1;
    return strtol(p + 15, NULL, 10);
}

static void hush_http_path(const char *req, char *out, size_t outsz)
{
    const char *sp;
    size_t i = 0;

    out[0] = '\0';
    sp = strchr(req, ' ');
    if (sp == NULL || outsz == 0)
        return;
    sp++;
    while (sp[i] != '\0' && sp[i] != ' ' && sp[i] != '?' && i + 1 < outsz) {
        out[i] = sp[i];
        i++;
    }
    out[i] = '\0';
}

static int hush_http_line_has_name(const char *line, const char *name,
                                   size_t name_len)
{
    size_t i;

    assert(line != NULL);
    assert(name != NULL);
    for (i = 0; i < name_len; ++i) {
        if (line[i] == '\0' ||
            tolower((unsigned char)line[i]) !=
                tolower((unsigned char)name[i]))
            return 0;
    }
    return line[name_len] == ':';
}

static void hush_http_copy_value(const char *src, char *out, size_t outsz)
{
    size_t n = 0;

    assert(src != NULL);
    assert(out != NULL);
    while (*src == ' ' || *src == '\t')
        ++src;
    while (*src != '\0' && *src != '\r' && *src != '\n' && n + 1 < outsz)
        out[n++] = *src++;
    out[n] = '\0';
}

int hush_http_header_value(const char *req, size_t len, const char *name,
                            char *out, size_t outsz)
{
    char block[HUSH_HTTP_HDR_MAX];
    const char *end;
    char *line;
    size_t name_len;
    size_t hlen;

    assert(req != NULL);
    assert(name != NULL);
    assert(out != NULL);
    if (outsz == 0)
        return 0;
    out[0] = '\0';
    end = hush_http_headers_end(req, len);
    if (end == NULL)
        return 0;
    hlen = (size_t)(end - req) + 4;
    if (hlen >= sizeof(block))
        return 0;
    memcpy(block, req, hlen);
    block[hlen] = '\0';
    name_len = strlen(name);
    line = strchr(block, '\n');
    if (line == NULL)
        return 0;
    for (;;) {
        char *next;

        line++;
        if (*line == '\r' || *line == '\n' || *line == '\0')
            break;
        if (hush_http_line_has_name(line, name, name_len)) {
            hush_http_copy_value(line + name_len + 1, out, outsz);
            return out[0] != '\0';
        }
        next = strchr(line, '\n');
        if (next == NULL)
            break;
        line = next;
    }
    return 0;
}

static int hush_http_query_token(const char *req, char *out, size_t outsz)
{
    const char *p;
    size_t n = 0;

    assert(req != NULL);
    assert(out != NULL);
    if (outsz == 0)
        return 0;
    out[0] = '\0';
    p = strchr(req, '?');
    if (p == NULL)
        return 0;
    p++;
    for (;;) {
        if (p[0] == 'k' && p[1] == '=') {
            const char *value = p + 2;

            while (*value != '\0' && *value != ' ' && *value != '&' &&
                   *value != '#' && n + 1 < outsz)
                out[n++] = *value++;
            out[n] = '\0';
            return n > 0;
        }
        p = strchr(p, '&');
        if (p == NULL)
            return 0;
        p++;
    }
}

static int hush_http_cookie_token(const char *cookie, const char *name,
                                  char *out, size_t outsz)
{
    size_t name_len;
    const char *p;

    assert(cookie != NULL);
    assert(name != NULL);
    assert(out != NULL);
    if (outsz == 0)
        return 0;
    out[0] = '\0';
    name_len = strlen(name);
    p = cookie;
    while (*p != '\0') {
        size_t n = 0;

        while (*p == ' ' || *p == ';' || *p == '\t')
            ++p;
        if (strncmp(p, name, name_len) == 0 && p[name_len] == '=') {
            const char *value = p + name_len + 1;

            while (*value != '\0' && *value != ';' && n + 1 < outsz)
                out[n++] = *value++;
            out[n] = '\0';
            return n > 0;
        }
        p = strchr(p, ';');
        if (p == NULL)
            return 0;
        p++;
    }
    return 0;
}

static int hush_http_request_is_authed(const char *req, size_t len)
{
    static const char bearer[] = "Bearer ";
    char header[HUSH_HTTP_HDR_VALUE_MAX];
    char value[HUSH_AUTH_TOKEN_BUF];

    assert(req != NULL);
    if (hush_http_header_value(req, len, "Cookie", header, sizeof(header)) &&
        hush_http_cookie_token(header, HUSH_AUTH_COOKIE, value,
                               sizeof(value)) &&
        hush_auth_token_matches(value))
        return 1;
    if (hush_http_header_value(req, len, "Authorization", header,
                               sizeof(header)) &&
        strncmp(header, bearer, sizeof(bearer) - 1) == 0 &&
        hush_auth_token_matches(header + sizeof(bearer) - 1))
        return 1;
    if (hush_http_header_value(req, len, HUSH_AUTH_HEADER, value,
                               sizeof(value)) &&
        hush_auth_token_matches(value))
        return 1;
    if (hush_http_query_token(req, value, sizeof(value)) &&
        hush_auth_token_matches(value))
        return 1;
    return 0;
}

static int hush_http_peer_is_loopback(int fd)
{
    struct sockaddr_storage peer;
    socklen_t peer_len = (socklen_t)sizeof(peer);

    if (getpeername(fd, (struct sockaddr *)&peer, &peer_len) != 0)
        return 0;
    if (peer.ss_family == AF_INET) {
        const struct sockaddr_in *addr = (const struct sockaddr_in *)&peer;

        return addr->sin_addr.s_addr == htonl(INADDR_LOOPBACK);
    }
    if (peer.ss_family == AF_INET6) {
        const struct sockaddr_in6 *addr = (const struct sockaddr_in6 *)&peer;

        return IN6_IS_ADDR_LOOPBACK(&addr->sin6_addr);
    }
    return 0;
}

static hush_limiter_t *hush_http_ip_req_lim(int fd)
{
    struct sockaddr_storage peer;
    socklen_t peer_len = (socklen_t)sizeof(peer);
    uint32_t addr = 0;
    size_t idx;

    if (getpeername(fd, (struct sockaddr *)&peer, &peer_len) != 0)
        return NULL;
    if (peer.ss_family != AF_INET)
        return NULL; /* IPv6 peers are not tracked; fail open */
    addr = ((const struct sockaddr_in *)&peer)->sin_addr.s_addr;
    for (idx = 0; idx < (size_t)HUSH_HTTP_IP_LIMITS_MAX; ++idx) {
        if (g_ip_req[idx].addr == addr && addr != 0)
            return &g_ip_req[idx].req_lim;
    }
    for (idx = 0; idx < (size_t)HUSH_HTTP_IP_LIMITS_MAX; ++idx) {
        if (g_ip_req[idx].addr == 0) {
            g_ip_req[idx].addr = addr;
            hush_limiter_init(&g_ip_req[idx].req_lim,
                              (double)HUSH_HTTP_IP_REQ_PER_S,
                              (double)HUSH_HTTP_IP_REQ_BURST);
            return &g_ip_req[idx].req_lim;
        }
    }
    return NULL; /* table full: fail open */
}

static hush_status_t hush_http_reply_rate_limited(int fd)
{
    hush_http_reply(fd, "429 Too Many Requests", "application/json",
                    HUSH_HTTP_RATE_BODY, strlen(HUSH_HTTP_RATE_BODY));
    return HUSH_ERR_DENIED;
}

static int hush_http_host_is_loopback(const char *host)
{
    assert(host != NULL);
    return strcasecmp(host, "127.0.0.1") == 0 ||
           strcasecmp(host, "localhost") == 0 ||
           strcasecmp(host, "::1") == 0 ||
           strcasecmp(host, "[::1]") == 0;
}

static void hush_http_host_strip_port(char *host)
{
    char *close;
    char *colon;

    assert(host != NULL);
    if (host[0] == '[') {
        close = strchr(host, ']');
        if (close != NULL)
            close[1] = '\0';
        return;
    }
    colon = strrchr(host, ':');
    if (colon != NULL && strchr(host, ':') == colon)
        *colon = '\0';
}

static int hush_http_host_ok(const char *req, size_t len)
{
    char host[HUSH_HTTP_HOST_MAX];

    assert(req != NULL);
    if (g_bind_addr[0] != '\0' && !hush_http_host_is_loopback(g_bind_addr))
        return 1; /* An operator-chosen non-loopback bind makes Host advisory. */
    if (!hush_http_header_value(req, len, "Host", host, sizeof(host)))
        return 0;
    hush_http_host_strip_port(host);
    return hush_http_host_is_loopback(host);
}

static int hush_http_needs_auth(const char *req, const char *path)
{
    assert(req != NULL);
    assert(path != NULL);
    if (strncmp(path, "/api/", 5) != 0)
        return 0;
    if (strcmp(path, "/api/status") == 0)
        return 0;
    if (strcmp(path, "/api/complete") == 0 && memcmp(req, "GET", 3) == 0)
        return 0;
    return 1;
}

static hush_status_t hush_http_guard(int fd, const char *req, size_t len,
                                     const char *path)
{
    const char *denied;

    assert(req != NULL);
    assert(path != NULL);
    if (!hush_http_host_ok(req, len)) {
        denied = "bad host\n";
        hush_http_reply(fd, "403 Forbidden", "text/plain", denied,
                        strlen(denied));
        return HUSH_ERR_DENIED;
    }
    if (!hush_http_needs_auth(req, path))
        return HUSH_OK; /* status and one-shot flows stay exempt */
    /* Per-IP throttle before the auth compare: an unauthenticated flood
     * costs the same 429 as an authenticated one. /api/event is exempt: it
     * is a cheap store insert and message floods are paced by the intel
     * burst holds and robot budgets on the dispatch path. */
    if (strcmp(path, "/api/event") != 0) {
        hush_limiter_t *req_lim = hush_http_ip_req_lim(fd);

        if (req_lim != NULL &&
            !hush_limiter_take(req_lim, hush_limiter_now_ms()))
            return hush_http_reply_rate_limited(fd);
    }
    if (hush_http_request_is_authed(req, len))
        return HUSH_OK;
    denied = "{\"ok\":false,\"error\":\"session token required\"}\n";
    hush_http_reply(fd, "401 Unauthorized", "application/json", denied,
                    strlen(denied));
    return HUSH_ERR_DENIED;
}


hush_status_t hush_http_write_all(int fd, const char *buf, size_t len)
{
    assert(buf != NULL);
    size_t offset = 0;
    for (size_t tries = 0; tries < len + HUSH_HTTP_WRITE_RETRIES && offset < len; ++tries) {
        ssize_t count = write(fd, buf + offset, len - offset);
        if (count > 0) { offset += (size_t)count; continue; }
        if (count < 0 && errno == EINTR) continue;
        if (count == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) return HUSH_ERR_IO;
        struct pollfd ready = {.fd = fd, .events = POLLOUT};
        if (poll(&ready, 1, HUSH_HTTP_WRITE_WAIT_MS) <= 0) return HUSH_ERR_IO;
    }
    return offset == len ? HUSH_OK : HUSH_ERR_IO;
}

const char *hush_http_event_channel(const hush_event_t *event)
{
    assert(event != NULL);
    for (size_t i = 0; i < event->tag_count && i < (size_t)HUSH_EVENT_MAX_TAGS; ++i) {
        if (strcmp(event->tags[i][0], "h") == 0 && event->tags[i][1][0] != '\0')
            return event->tags[i][1];
    }
    return "general";
}

void hush_http_reply(int fd, const char *status, const char *ctype,
                            const char *body, size_t blen)
{
    char hdr[512];
    char cookie[160];
    char token[HUSH_AUTH_TOKEN_BUF];
    const char *extra = "";
    int n;

    if (g_set_cookie && hush_auth_token_copy(token, sizeof(token)) == HUSH_OK) {
        int cn = snprintf(cookie, sizeof(cookie),
                          "Set-Cookie: %s=%s; HttpOnly; SameSite=Strict; "
                          "Path=/\r\n",
                          HUSH_AUTH_COOKIE, token);
        if (cn > 0 && (size_t)cn < sizeof(cookie))
            extra = cookie;
    }
    n = snprintf(hdr, sizeof(hdr),
                 "HTTP/1.1 %s\r\n"
                 "Content-Type: %s\r\n"
                 "Content-Length: %zu\r\n"
                 "Connection: close\r\n"
                 "%s"
                 "\r\n",
                 status, ctype, blen, extra);
    if (n <= 0 || (size_t)n >= sizeof(hdr))
        return;
    if (hush_http_write_all(fd, hdr, (size_t)n) != HUSH_OK)
        return;
    if (hush_http_write_all(fd, body, blen) != HUSH_OK)
        return;
}

void hush_http_json_unescape_copy(const char *src, char *dst, size_t dstsz)
{
    size_t i = 0;

    if (dstsz == 0)
        return;
    while (*src != '\0' && *src != '"' && i + 1 < dstsz) {
        if (*src == '\\' && src[1] != '\0')
            src++;
        dst[i++] = *src++;
    }
    dst[i] = '\0';
}

int hush_http_json_field(const char *body, const char *key, char *out, size_t outsz)
{
    char quoted[64];
    const char *p;
    const char *hit = NULL;

    out[0] = '\0';
    if (snprintf(quoted, sizeof(quoted), "\"%s\":\"", key) >= (int)sizeof(quoted))
        return 0;
    p = body;
    while ((p = strstr(p, quoted)) != NULL) {
        if (p == body || p[-1] == '{' || p[-1] == ',' || p[-1] == ' ') {
            hit = p;
            break;
        }
        p += 1;
    }
    if (hit != NULL) {
        hush_http_json_unescape_copy(hit + strlen(quoted), out, outsz);
        return out[0] != '\0';
    }
    return hush_http_json_bare_field(body, key, out, outsz);
}

int hush_http_json_has_key(const char *body, const char *key)
{
    char needle[64];
    const char *p;

    if (body == NULL || key == NULL || key[0] == '\0')
        return 0;
    if (snprintf(needle, sizeof(needle), "\"%s\":", key) >= (int)sizeof(needle))
        return 0;
    p = body;
    while ((p = strstr(p, needle)) != NULL) {
        if (p == body || p[-1] == '{' || p[-1] == ',' || p[-1] == ' ')
            return 1;
        p += 1;
    }
    return 0;
}

int hush_http_json_bare_field(const char *body, const char *key,
                                char *out, size_t outsz)
{
    char bare[64];
    const char *p;
    size_t i = 0;

    if (snprintf(bare, sizeof(bare), "\"%s\":", key) >= (int)sizeof(bare))
        return 0;
    p = body;
    while ((p = strstr(p, bare)) != NULL) {
        if (p == body || p[-1] == '{' || p[-1] == ',' || p[-1] == ' ')
            break;
        p += 1;
    }
    if (p == NULL)
        return 0;
    p += strlen(bare);
    while (*p == ' ')
        p++;
    while (*p != '\0' && *p != ',' && *p != '}' && *p != ' ' && i + 1 < outsz)
        out[i++] = *p++;
    out[i] = '\0';
    return out[0] != '\0';
}

static const hush_launch_project_t *hush_http_find_project(const char *slug)
{
    size_t i;

    if (g_launch == NULL || slug == NULL || slug[0] == '\0')
        return NULL;
    for (i = 0; i < g_launch->nprojects; i++) {
        if (strcmp(g_launch->projects[i].slug, slug) == 0)
            return &g_launch->projects[i];
    }
    return NULL;
}

static int hush_http_canvas_rel_ok(const char *rel)
{
    if (rel == NULL || rel[0] == '\0' || rel[0] == '/' || rel[0] == '\\')
        return 0;
    if (strstr(rel, "..") != NULL)
        return 0;
    return 1;
}

static hush_status_t hush_http_canvas_join(char *out, size_t outsz,
                                          const char *root, const char *rel)
{
    int n;

    assert(out != NULL);
    assert(outsz > 0);
    if (root == NULL || root[0] == '\0' || !hush_http_canvas_rel_ok(rel))
        return HUSH_ERR_DENIED;
    n = snprintf(out, outsz, "%s/%s", root, rel);
    if (n < 0 || (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    return HUSH_OK;
}

static hush_status_t hush_http_canvas_write(const char *path,
                                           const char *content)
{
    FILE *fp;
    size_t n;
    size_t wr;

    assert(path != NULL);
    assert(content != NULL);
    fp = fopen(path, "w");
    if (fp == NULL)
        return HUSH_ERR_IO;
    n = strlen(content);
    wr = fwrite(content, 1, n, fp);
    if (fclose(fp) != 0 || wr != n)
        return HUSH_ERR_IO;
    return HUSH_OK;
}

static hush_status_t hush_http_serve_canvas(int fd, const char *body)
{
    char slug[HUSH_LAUNCH_NAME_MAX];
    char rel[HUSH_LAUNCH_PATH_MAX];
    char content[HUSH_EVENT_MAX_CONTENT + 1];
    char path[HUSH_HTTP_CANVAS_PATH_MAX];
    const hush_launch_project_t *proj;
    hush_status_t st;

    if (g_launch == NULL || body == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_ARG);
    if (!hush_http_json_field(body, "project", slug, sizeof(slug)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    if (!hush_http_json_field(body, "path", rel, sizeof(rel)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    if (!hush_http_json_field(body, "content", content, sizeof(content)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    proj = hush_http_find_project(slug);
    if (proj == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_NOT_FOUND);
    st = hush_http_canvas_join(path, sizeof(path), proj->path, rel);
    if (st != HUSH_OK)
        return hush_http_reply_session(fd, st);
    st = hush_http_canvas_write(path, content);
    if (st != HUSH_OK)
        return hush_http_reply_session(fd, st);
    hush_http_reply(fd, "200 OK", "application/json",
                    HUSH_HTTP_CANVAS_OK, sizeof(HUSH_HTTP_CANVAS_OK) - 1);
    return HUSH_OK;
}

static void hush_http_reply_fixup_ok(int fd, const char *text)
{
    char esc[HUSH_EVENT_MAX_CONTENT * 2 + 8];
    char body[HUSH_HTTP_FIXUP_JSON_MAX];
    int n;

    hush_json_escape(text != NULL ? text : "", esc, sizeof(esc));
    n = snprintf(body, sizeof(body), "{\"ok\":true,\"text\":\"%s\"}\n", esc);
    if (n < 0 || (size_t)n >= sizeof(body)) {
        hush_http_reply(fd, "200 OK", "application/json",
                        HUSH_HTTP_FIXUP_FAIL, sizeof(HUSH_HTTP_FIXUP_FAIL) - 1);
        return;
    }
    hush_http_reply(fd, "200 OK", "application/json", body, (size_t)n);
}

static void hush_http_pause_fixup(void)
{
    struct timespec pause;

    pause.tv_sec = 0;
    pause.tv_nsec = (long)HUSH_HTTP_FIXUP_SLEEP_NS;
    (void)nanosleep(&pause, NULL);
}

static hush_status_t hush_http_wait_fixup(const char *token, char *out,
                                          size_t outsz)
{
    size_t i;
    hush_status_t st;

    assert(token != NULL);
    assert(out != NULL);
    for (i = 0; i < (size_t)HUSH_HTTP_FIXUP_WAIT_MAX; i++) {
        hush_agent_poll(NULL);
        st = hush_agent_take_fixup(token, out, outsz);
        if (st == HUSH_OK)
            return HUSH_OK;
        if (st == HUSH_ERR_IO)
            return st;
        hush_http_pause_fixup();
    }
    return HUSH_ERR_IO;
}

static hush_status_t hush_http_serve_fixup(int fd, const char *body)
{
    char ask[HUSH_HTTP_FIXUP_ASK_MAX + 1];
    char text[HUSH_EVENT_MAX_CONTENT + 1];
    char token[HUSH_HTTP_FIXUP_TOKEN_MAX];
    char out[HUSH_EVENT_MAX_CONTENT + 1];
    hush_status_t st;

    if (body == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_ARG);
    ask[0] = '\0';
    text[0] = '\0';
    (void)hush_http_json_field(body, "instruction", ask, sizeof(ask));
    (void)hush_http_json_field(body, "text", text, sizeof(text));
    st = hush_agent_start_fixup(token, sizeof(token), ask, text);
    if (st != HUSH_OK)
        return hush_http_reply_session(fd, st);
    st = hush_http_wait_fixup(token, out, sizeof(out));
    if (st != HUSH_OK) {
        hush_http_reply(fd, "200 OK", "application/json",
                        HUSH_HTTP_FIXUP_FAIL, sizeof(HUSH_HTTP_FIXUP_FAIL) - 1);
        return st;
    }
    hush_http_reply_fixup_ok(fd, out);
    return HUSH_OK;
}

static void hush_http_reply_complete_token(int fd, const char *token)
{
    char esc[HUSH_CANVAS_TOKEN_MAX * 2];
    char body[HUSH_HTTP_COMPLETE_JSON_MAX];
    int n;

    hush_json_escape(token != NULL ? token : "", esc, sizeof(esc));
    n = snprintf(body, sizeof(body), "{\"ok\":true,\"token\":\"%s\"}\n", esc);
    if (n < 0 || (size_t)n >= sizeof(body)) {
        hush_http_reply(fd, "200 OK", "application/json",
                        HUSH_HTTP_COMPLETE_FAIL,
                        sizeof(HUSH_HTTP_COMPLETE_FAIL) - 1);
        return;
    }
    hush_http_reply(fd, "200 OK", "application/json", body, (size_t)n);
}

static void hush_http_reply_complete_text(int fd, const char *text)
{
    char esc[HUSH_CANVAS_PRED_MAX * 2 + 8];
    char body[HUSH_HTTP_COMPLETE_JSON_MAX];
    int n;

    hush_json_escape(text != NULL ? text : "", esc, sizeof(esc));
    n = snprintf(body, sizeof(body), "{\"ok\":true,\"text\":\"%s\"}\n", esc);
    if (n < 0 || (size_t)n >= sizeof(body)) {
        hush_http_reply(fd, "200 OK", "application/json",
                        HUSH_HTTP_COMPLETE_FAIL,
                        sizeof(HUSH_HTTP_COMPLETE_FAIL) - 1);
        return;
    }
    hush_http_reply(fd, "200 OK", "application/json", body, (size_t)n);
}

static void hush_http_complete_token(char *out, size_t outsz, const char *req)
{
    const char *q;
    size_t i;

    assert(out != NULL);
    assert(outsz > 0);
    out[0] = '\0';
    if (req == NULL)
        return;
    q = strstr(req, "?t=");
    if (q == NULL)
        return;
    q += 3;
    i = 0;
    while (q[i] != '\0' && q[i] != ' ' && q[i] != '&' && i + 1 < outsz) {
        out[i] = q[i];
        i++;
    }
    out[i] = '\0';
}

static hush_status_t hush_http_serve_complete_post(int fd, const char *body)
{
    char prefix[HUSH_EVENT_MAX_CONTENT + 1];
    char suffix[HUSH_EVENT_MAX_CONTENT + 1];
    char token[HUSH_CANVAS_TOKEN_MAX];
    hush_status_t st;

    if (body == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_ARG);
    prefix[0] = '\0';
    suffix[0] = '\0';
    (void)hush_http_json_field(body, "prefix", prefix, sizeof(prefix));
    (void)hush_http_json_field(body, "suffix", suffix, sizeof(suffix));
    st = hush_canvas_start(token, sizeof(token), prefix, suffix);
    if (st != HUSH_OK) {
        hush_http_reply(fd, "200 OK", "application/json",
                        HUSH_HTTP_COMPLETE_FAIL,
                        sizeof(HUSH_HTTP_COMPLETE_FAIL) - 1);
        return st;
    }
    hush_http_reply_complete_token(fd, token);
    return HUSH_OK;
}

static hush_status_t hush_http_serve_complete_get(int fd, const char *req)
{
    char token[HUSH_CANVAS_TOKEN_MAX];
    char out[HUSH_CANVAS_PRED_MAX + 1];
    hush_status_t st;

    hush_http_complete_token(token, sizeof(token), req);
    if (token[0] == '\0')
        return hush_http_reply_session(fd, HUSH_ERR_ARG);
    hush_canvas_poll();
    if (hush_canvas_is_busy(token)) {
        hush_http_reply(fd, "200 OK", "application/json",
                        HUSH_HTTP_COMPLETE_PEND,
                        sizeof(HUSH_HTTP_COMPLETE_PEND) - 1);
        return HUSH_OK;
    }
    st = hush_canvas_take(token, out, sizeof(out));
    if (st == HUSH_OK) {
        hush_http_reply_complete_text(fd, out);
        return HUSH_OK;
    }
    hush_http_reply(fd, "200 OK", "application/json",
                    HUSH_HTTP_COMPLETE_FAIL,
                    sizeof(HUSH_HTTP_COMPLETE_FAIL) - 1);
    return st;
}

static hush_status_t hush_http_serve_api_post(int fd, const char *path,
                                              const char *req, size_t len,
                                              hush_store_t *store,
                                              hush_event_t *out_posted)
{
    if (strcmp(path, "/api/event") == 0)
        return hush_http_serve_post(fd, req, len, store, out_posted);
    if (strcmp(path, "/api/presence") == 0)
        return hush_http_serve_presence_post(fd, hush_http_body(req, len),
                                            store);
    if (strcmp(path, "/api/identity") == 0)
        return hush_http_serve_identity(fd, hush_http_body(req, len));
    if (strcmp(path, "/api/profile") == 0)
        return hush_http_serve_profile(fd, hush_http_body(req, len));
    if (strcmp(path, "/api/member") == 0)
        return hush_http_serve_member(fd, hush_http_body(req, len));
    if (strcmp(path, "/api/agent") == 0)
        return hush_http_serve_agent(fd, hush_http_body(req, len), store);
    if (strcmp(path, "/api/skill") == 0)
        return hush_http_serve_skill_post(fd, hush_http_body(req, len));
    if (strcmp(path, "/api/skillui") == 0)
        return hush_http_serve_skillui(fd, hush_http_body(req, len));
    if (strcmp(path, "/api/vibe") == 0)
        return hush_http_serve_vibe(fd, hush_http_body(req, len), store);
    if (strcmp(path, "/api/channel") == 0)
        return hush_http_serve_channel(fd, hush_http_body(req, len));
    if (strcmp(path, "/api/group") == 0)
        return hush_http_serve_group(fd, hush_http_body(req, len));
    if (strcmp(path, "/api/project") == 0)
        return hush_http_serve_project(fd, hush_http_body(req, len), store);
    if (strcmp(path, "/api/canvas") == 0)
        return hush_http_serve_canvas(fd, hush_http_body(req, len));
    if (strcmp(path, "/api/cancel") == 0)
        return hush_http_serve_cancel(fd, hush_http_body(req, len));
    if (strcmp(path, "/api/reply") == 0)
        return hush_http_serve_reply(fd, hush_http_body(req, len));
    if (strcmp(path, "/api/fixup") == 0) {
        if (!hush_limiter_take(&g_fixup_lim, hush_limiter_now_ms()))
            return hush_http_reply_rate_limited(fd);
        return hush_http_serve_fixup(fd, hush_http_body(req, len));
    }
    if (strcmp(path, "/api/complete") == 0) {
        if (!hush_limiter_take(&g_complete_lim, hush_limiter_now_ms()))
            return hush_http_reply_rate_limited(fd);
        return hush_http_serve_complete_post(fd, hush_http_body(req, len));
    }
    if (strcmp(path, "/api/turn") == 0)
        return hush_http_serve_turn_post(fd, hush_http_body(req, len));
    if (strcmp(path, "/api/signal") == 0)
        return hush_http_serve_signal(fd, hush_http_body(req, len),
                                      store, out_posted);
    if (strcmp(path, "/api/close") == 0)
        return hush_http_serve_close(fd);
    if (strcmp(path, "/api/exit") == 0)
        return hush_http_serve_exit(fd);
    if (strcmp(path, "/api/window") == 0)
        return hush_http_serve_window(fd, hush_http_body(req, len));
    if (strcmp(path, "/api/provider") == 0)
        return hush_http_serve_provider_post(fd, hush_http_body(req, len));
    if (strcmp(path, "/api/provider/scan") == 0)
        return hush_http_serve_provider_scan(fd, hush_http_body(req, len));
    if (strcmp(path, "/api/provider/login") == 0)
        return hush_http_serve_provider_login(fd, hush_http_body(req, len));
    hush_http_reply(fd, "404 Not Found", "text/plain", "not found\n", 10);
    return HUSH_ERR_NOT_FOUND;
}

/* Reports the live answer for one thread so the pane can paint partials. */
static hush_status_t hush_http_serve_reply(int fd, const char *body)
{
    char root[HUSH_EVENT_ID_HEX_LEN + 1] = {0};
    char robot[HUSH_EVENT_PUBKEY_HEX_LEN + 1] = {0};
    char partial[HUSH_EVENT_MAX_CONTENT + 1] = {0};
    char escaped[HUSH_EVENT_MAX_CONTENT * HUSH_JSON_U_LEN + 1];
    char payload[HUSH_EVENT_MAX_CONTENT * HUSH_JSON_U_LEN + 64];
    const char *error = "{\"ok\":false,\"error\":\"root and robot are required\"}\n";
    const char *idle = "{\"ok\":true,\"running\":false,\"text\":\"\"}\n";
    const char *failed = "{\"ok\":false,\"error\":\"answer too large\"}\n";
    int n;

    if (!hush_http_json_field(body, "root", root, sizeof(root)) ||
        !hush_http_json_field(body, "robot", robot, sizeof(robot))) {
        hush_http_reply(fd, "400 Bad Request", "application/json", error,
                        strlen(error));
        return HUSH_ERR_ARG;
    }
    if (hush_agent_partial(partial, sizeof(partial), root, robot) != HUSH_OK) {
        hush_http_reply(fd, "200 OK", "application/json", idle, strlen(idle));
        return HUSH_OK;
    }
    if ((hush_json_escape(partial, escaped, sizeof(escaped)) == 0 && partial[0] != '\0')) {
        hush_http_reply(fd, "507 Insufficient Storage", "application/json", failed,
                        strlen(failed));
        return HUSH_ERR_FULL;
    }
    n = snprintf(payload, sizeof(payload),
                 "{\"ok\":true,\"running\":true,\"text\":\"%s\"}\n", escaped);
    if (n <= 0 || (size_t)n >= sizeof(payload)) {
        hush_http_reply(fd, "507 Insufficient Storage", "application/json", failed,
                        strlen(failed));
        return HUSH_ERR_FULL;
    }
    hush_http_reply(fd, "200 OK", "application/json", payload, (size_t)n);
    return HUSH_OK;
}

/* Stops the robot's live job on one thread; absent jobs are not an error. */
static hush_status_t hush_http_serve_cancel(int fd, const char *body)
{
    char root[HUSH_EVENT_ID_HEX_LEN + 1] = {0};
    char robot[HUSH_EVENT_PUBKEY_HEX_LEN + 1] = {0};
    const char *error = "{\"ok\":false,\"error\":\"root and robot are required\"}\n";

    if (!hush_http_json_field(body, "root", root, sizeof(root)) ||
        !hush_http_json_field(body, "robot", robot, sizeof(robot))) {
        hush_http_reply(fd, "400 Bad Request", "application/json", error,
                        strlen(error));
        return HUSH_ERR_ARG;
    }
    if (hush_agent_cancel(root, robot) == HUSH_OK) {
        const char *stopped = "{\"ok\":true,\"stopped\":true}\n";

        hush_http_reply(fd, "200 OK", "application/json", stopped,
                        strlen(stopped));
        return HUSH_OK;
    }
    const char *idle = "{\"ok\":true,\"stopped\":false}\n";
    hush_http_reply(fd, "200 OK", "application/json", idle, strlen(idle));
    return HUSH_OK;
}

static hush_status_t hush_http_serve_close(int fd)
{
    hush_relay_note_leave(0);
    hush_http_reply(fd, "200 OK", "application/json",
                    HUSH_HTTP_CLOSE_JSON, sizeof(HUSH_HTTP_CLOSE_JSON) - 1);
    return HUSH_OK;
}

static hush_status_t hush_http_serve_exit(int fd)
{
    hush_relay_note_leave(1);
    hush_http_reply(fd, "200 OK", "application/json",
                    HUSH_HTTP_EXIT_JSON, sizeof(HUSH_HTTP_EXIT_JSON) - 1);
    return HUSH_OK;
}

static hush_status_t hush_http_serve_window(int fd, const char *body)
{
    char action[HUSH_HTTP_WINDOW_ACT_MAX];
    hush_status_t st;

    if (!hush_http_json_field(body, "action", action, sizeof(action))) {
        hush_http_reply(fd, "400 Bad Request", "application/json",
                        HUSH_HTTP_WINDOW_FAIL, sizeof(HUSH_HTTP_WINDOW_FAIL) - 1);
        return HUSH_ERR_PARSE;
    }
    st = hush_http_window_run(action);
    if (st == HUSH_ERR_ARG) {
        hush_http_reply(fd, "400 Bad Request", "application/json",
                        HUSH_HTTP_WINDOW_FAIL, sizeof(HUSH_HTTP_WINDOW_FAIL) - 1);
        return st;
    }
    if (st != HUSH_OK) {
        hush_http_reply(fd, "200 OK", "application/json",
                        HUSH_HTTP_WINDOW_FAIL, sizeof(HUSH_HTTP_WINDOW_FAIL) - 1);
        return st;
    }
    hush_http_reply_window(fd, action);
    return HUSH_OK;
}

static hush_status_t hush_http_window_run(const char *action)
{
    assert(action != NULL);
    if (strcmp(action, HUSH_HTTP_WINDOW_PREPARE) == 0)
        return hush_win_undecorate();
    if (strcmp(action, HUSH_HTTP_WINDOW_MIN) == 0)
        return hush_win_minimize();
    if (strcmp(action, HUSH_HTTP_WINDOW_MAX) == 0)
        return hush_win_maximize();
    return HUSH_ERR_ARG;
}

static void hush_http_reply_window(int fd, const char *action)
{
    assert(action != NULL);
    if (strcmp(action, HUSH_HTTP_WINDOW_PREPARE) == 0) {
        hush_http_reply(fd, "200 OK", "application/json",
                        HUSH_HTTP_WINDOW_PREPARE_JSON,
                        sizeof(HUSH_HTTP_WINDOW_PREPARE_JSON) - 1);
        return;
    }
    if (strcmp(action, HUSH_HTTP_WINDOW_MIN) == 0) {
        hush_http_reply(fd, "200 OK", "application/json",
                        HUSH_HTTP_WINDOW_MIN_JSON,
                        sizeof(HUSH_HTTP_WINDOW_MIN_JSON) - 1);
        return;
    }
    hush_http_reply(fd, "200 OK", "application/json",
                    HUSH_HTTP_WINDOW_MAX_JSON,
                    sizeof(HUSH_HTTP_WINDOW_MAX_JSON) - 1);
}

/* Enumeration serializer retains its existing output-offset/first-item ABI. */
static void hush_http_append_provider(char *body, size_t bodysz, size_t *n,
                                      const hush_provider_status_t *st,
                                      int first)
{
    char host[HUSH_PROVIDER_HOST_MAX * 2];
    char model[HUSH_PROVIDER_MODEL_MAX * 2];
    char home_model[HUSH_PROVIDER_MODEL_MAX * 2];
    int wr;
    assert(body != NULL && n != NULL && st != NULL);
    hush_json_escape(st->host, host, sizeof(host));
    hush_json_escape(st->model, model, sizeof(model));
    hush_json_escape(st->home_model, home_model, sizeof(home_model));
    wr = snprintf(body + *n, bodysz - *n,
                  "%s\"%s\":{\"label\":\"%s\",\"family\":\"%s\","
                  "\"has_binary\":%s,\"has_home\":%s,\"has_key\":%s,"
                  "\"has_username\":%s,\"has_password\":%s,"
                  "\"has_token\":%s,\"has_passkey\":%s,"
                  "\"use_home\":%s,\"host\":\"%s\",\"model\":\"%s\","
                  "\"home_model\":\"%s\",\"caps\":%u,\"flags\":%u,"
                  "\"configured\":%s,\"ready\":%s}",
                  first ? "" : ",",
                  st->id, st->label, st->family,
                  st->has_binary ? "true" : "false",
                  st->has_home ? "true" : "false",
                  st->has_key ? "true" : "false",
                  st->has_username ? "true" : "false",
                  st->has_password ? "true" : "false",
                  st->has_token ? "true" : "false",
                  st->has_passkey ? "true" : "false",
                  st->use_home ? "true" : "false",
                  host, model, home_model,
                  st->caps,
                  st->flags,
                  st->configured ? "true" : "false",
                  hush_provider_ready(st) ? "true" : "false");
    if (wr > 0 && (size_t)wr < bodysz - *n)
        *n += (size_t)wr;
}

static hush_status_t hush_http_serve_provider_get(int fd)
{
    hush_provider_status_t all[HUSH_PROVIDER_COUNT];
    char body[HUSH_HTTP_JSON_MAX];
    size_t n = 0;
    size_t count = 0;
    size_t i;
    int wr;

    if (hush_provider_status_all(all, &count) != HUSH_OK) {
        hush_http_reply(fd, "500 Internal Server Error", "text/plain",
                        "io error\n", 9);
        return HUSH_ERR_IO;
    }
    wr = snprintf(body, sizeof(body), "{\"ok\":true,\"providers\":{");
    if (wr > 0)
        n = (size_t)wr;
    for (i = 0; i < count; i++)
        hush_http_append_provider(body, sizeof(body), &n, &all[i], i == 0);
    if (n + 3 < sizeof(body)) {
        memcpy(body + n, "}}\n", 3);
        n += 3;
    }
    hush_http_reply(fd, "200 OK", "application/json", body, n);
    return HUSH_OK;
}

static void hush_http_take_secret(const char **dst, char *buf,
                                  const char *body, const char *kind)
{
    assert(dst != NULL);
    assert(buf != NULL);
    if (hush_http_json_field(body, kind, buf, HUSH_PROVIDER_KEY_MAX))
        *dst = buf;
}

static void hush_http_fill_provider_in(hush_provider_in_t *in,
                                       hush_http_provider_buf_t *buf,
                                       const char *body)
{
    char flag[8];

    assert(in != NULL);
    assert(buf != NULL);
    memset(buf, 0, sizeof(*buf));
    (void)hush_http_json_field(body, "host", in->host, sizeof(in->host));
    (void)hush_http_json_field(body, "model", in->model, sizeof(in->model));
    hush_http_take_secret(&in->api_key, buf->api_key, body,
                          HUSH_PROVIDER_SECRET_API_KEY);
    hush_http_take_secret(&in->username, buf->username, body,
                          HUSH_PROVIDER_SECRET_USERNAME);
    hush_http_take_secret(&in->password, buf->password, body,
                          HUSH_PROVIDER_SECRET_PASSWORD);
    hush_http_take_secret(&in->token, buf->token, body,
                          HUSH_PROVIDER_SECRET_TOKEN);
    hush_http_take_secret(&in->passkey, buf->passkey, body,
                          HUSH_PROVIDER_SECRET_PASSKEY);
    if (hush_http_json_field(body, "use_home", flag, sizeof(flag)))
        in->use_home = strcmp(flag, "true") == 0 || strcmp(flag, "1") == 0;
}

static hush_status_t hush_http_serve_provider_post(int fd, const char *body)
{
    hush_provider_in_t in;
    hush_provider_status_t st;
    hush_http_provider_buf_t buf;
    char reply[2048];
    size_t n = 0;
    int wr;

    memset(&in, 0, sizeof(in));
    if (!hush_http_json_field(body, "provider", in.id, sizeof(in.id))) {
        hush_http_reply(fd, "400 Bad Request", "text/plain", "bad request\n", 12);
        return HUSH_ERR_PARSE;
    }
    hush_http_fill_provider_in(&in, &buf, body);
    if (hush_provider_save(&in) != HUSH_OK) {
        hush_http_reply(fd, "400 Bad Request", "text/plain", "bad request\n", 12);
        return HUSH_ERR_PARSE;
    }
    if (hush_provider_status(&st, in.id) != HUSH_OK)
        return hush_http_serve_provider_get(fd);
    wr = snprintf(reply, sizeof(reply), "{\"ok\":true,\"providers\":{");
    if (wr > 0)
        n = (size_t)wr;
    hush_http_append_provider(reply, sizeof(reply), &n, &st, 1);
    if (n + 3 < sizeof(reply)) {
        memcpy(reply + n, "}}\n", 3);
        n += 3;
    }
    hush_http_reply(fd, "200 OK", "application/json", reply, n);
    return HUSH_OK;
}

static void hush_http_reply_scan(int fd, const hush_provider_scan_t *scan,
                                 hush_status_t st)
{
    char body[HUSH_HTTP_JSON_MAX];
    char err[HUSH_PROVIDER_ERR_MAX * 2];
    size_t n = 0;
    size_t i;
    int wr;

    assert(scan != NULL);
    hush_json_escape(scan->error, err, sizeof(err));
    wr = snprintf(body, sizeof(body),
                  "{\"ok\":%s,\"error\":\"%s\"",
                  st == HUSH_OK ? "true" : "false", err);
    if (wr > 0)
        n = (size_t)wr;
    for (i = 0; i < scan->nmodels; i++) {
        char name[HUSH_PROVIDER_MODEL_MAX * 2];

        hush_json_escape(scan->models[i], name, sizeof(name));
        wr = snprintf(body + n, sizeof(body) - n, ",\"model_%zu\":\"%s\"",
                      i, name);
        if (wr > 0 && (size_t)wr < sizeof(body) - n)
            n += (size_t)wr;
    }
    if (n + 2 < sizeof(body)) {
        memcpy(body + n, "}\n", 2);
        n += 2;
    }
    hush_http_reply(fd, "200 OK", "application/json", body, n);
}

static hush_status_t hush_http_serve_provider_scan(int fd, const char *body)
{
    char id[HUSH_PROVIDER_ID_MAX];
    char host[HUSH_PROVIDER_HOST_MAX];
    char key[HUSH_PROVIDER_KEY_MAX];
    hush_provider_scan_t scan;
    hush_status_t st;

    if (!hush_http_json_field(body, "provider", id, sizeof(id))) {
        hush_http_reply(fd, "400 Bad Request", "text/plain", "bad request\n", 12);
        return HUSH_ERR_PARSE;
    }
    host[0] = '\0';
    key[0] = '\0';
    (void)hush_http_json_field(body, "host", host, sizeof(host));
    (void)hush_http_json_field(body, "api_key", key, sizeof(key));
    st = hush_provider_scan(&scan, id, host, key);
    hush_http_reply_scan(fd, &scan, st);
    return HUSH_OK;
}

static hush_status_t hush_http_serve_provider_login(int fd, const char *body)
{
    char id[HUSH_PROVIDER_ID_MAX];
    char err[HUSH_PROVIDER_ERR_MAX];
    char reply[HUSH_HTTP_LOGIN_REPLY_MAX];
    char esc[HUSH_PROVIDER_ERR_MAX * 2];
    hush_status_t st;
    int wr;

    if (!hush_http_json_field(body, "provider", id, sizeof(id))) {
        hush_http_reply(fd, "400 Bad Request", "text/plain", "bad request\n", 12);
        return HUSH_ERR_PARSE;
    }
    st = hush_provider_start_login(id);
    hush_provider_last_error(err, sizeof(err));
    hush_json_escape(err, esc, sizeof(esc));
    wr = snprintf(reply, sizeof(reply),
                  "{\"ok\":%s,\"error\":\"%s\"}\n",
                  st == HUSH_OK ? "true" : "false", esc);
    if (wr <= 0 || (size_t)wr >= sizeof(reply))
        return HUSH_ERR_IO;
    hush_http_reply(fd, "200 OK", "application/json", reply, (size_t)wr);
    return HUSH_OK;
}

const char *hush_http_body(const char *req, size_t len)
{
    const char *end;

    if (req == NULL)
        return "";
    end = hush_http_headers_end(req, len);
    if (end == NULL)
        return "";
    return end + 4;
}

static void hush_http_serve_turn_get(int fd)
{
    char body[HUSH_TURN_JSON_MAX];
    size_t n = 0;
    hush_turn_t empty;

    if (g_turn == NULL) {
        hush_turn_init(&empty);
        if (hush_turn_format_status(&empty, body, sizeof(body), &n) != HUSH_OK)
            n = 0;
    } else {
        hush_turn_refresh(g_turn);
        if (hush_turn_format_status(g_turn, body, sizeof(body), &n) != HUSH_OK)
            n = 0;
    }
    hush_http_reply(fd, "200 OK", "application/json", body, n);
}

static void hush_http_serve_ice(int fd)
{
    char body[HUSH_TURN_JSON_MAX];
    size_t n = 0;
    hush_turn_t empty;

    if (g_turn == NULL) {
        hush_turn_init(&empty);
        if (hush_turn_format_ice(&empty, body, sizeof(body), &n) != HUSH_OK)
            n = 0;
    } else if (hush_turn_format_ice(g_turn, body, sizeof(body), &n) != HUSH_OK) {
        n = 0;
    }
    hush_http_reply(fd, "200 OK", "application/json", body, n);
}

static hush_status_t hush_http_serve_turn_post(int fd, const char *body)
{
    char enabled[8];
    char daemon[8];
    char host[HUSH_TURN_HOST_MAX];
    hush_turn_mode_t mode;
    hush_status_t st;

    if (g_turn == NULL || body == NULL) {
        hush_http_reply(fd, "503 Service Unavailable", "text/plain",
                        "turn off\n", 9);
        return HUSH_ERR_NOT_FOUND;
    }
    if (hush_http_json_field(body, "host", host, sizeof(host)))
        (void)hush_turn_set_public_host(g_turn, host);
    if (hush_http_json_field(body, "enabled", enabled, sizeof(enabled)) &&
        (strcmp(enabled, "false") == 0 || strcmp(enabled, "0") == 0)) {
        st = hush_turn_disable(g_turn);
        hush_http_serve_turn_get(fd);
        return st;
    }
    mode = HUSH_TURN_MODE_CHILD;
    if (hush_http_json_field(body, "daemon", daemon, sizeof(daemon)) &&
        (strcmp(daemon, "true") == 0 || strcmp(daemon, "1") == 0))
        mode = HUSH_TURN_MODE_DAEMON;
    st = hush_turn_enable(g_turn, mode);
    hush_http_serve_turn_get(fd);
    return (st == HUSH_OK) ? HUSH_OK : st;
}

