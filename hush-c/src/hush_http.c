/* hush_http.c: owns HTTP status/events/UI serving for the hush-relay desktop UI. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "hush_http.h"
#include "hush_ui_html.h"

enum {
    HUSH_HTTP_JSON_MAX = 65536,
    HUSH_HTTP_PATH_MAX = 128,
    HUSH_HTTP_EVENTS_MAX = 64,
    HUSH_HTTP_HDR_MAX = 8192,
    HUSH_ID_HEX_WIDTH = 64
};

static uint16_t g_listen_port;
static int g_client_count;

static const char *hush_find_headers_end(const char *buf, size_t len);
static long hush_http_content_length(const char *buf, size_t hlen);
static void hush_http_path(const char *req, char *out, size_t outsz);
static void hush_http_write_all(int fd, const char *buf, size_t len);
static void hush_http_reply(int fd, const char *status, const char *ctype,
                            const char *body, size_t blen);
static void hush_json_unescape_copy(const char *src, char *dst, size_t dstsz);
static int hush_json_field(const char *body, const char *key, char *out, size_t outsz);
static size_t hush_json_escape(const char *in, char *out, size_t outsz);
static void hush_make_event_id(char *out65);
static void hush_http_serve_status(int fd, const hush_store_t *store);
static void hush_http_serve_events(int fd, const hush_store_t *store);
static hush_status_t hush_http_serve_post(int fd, const char *req, size_t len,
                                          hush_store_t *store, hush_event_t *out);

void hush_http_set_listen_port(uint16_t port)
{
    g_listen_port = port;
}

void hush_http_set_client_count(int n)
{
    g_client_count = n;
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
    end = hush_find_headers_end(buf, len);
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
    if (len >= 7 && memcmp(req, "OPTIONS", 7) == 0) {
        hush_http_reply(fd, "204 No Content", "text/plain", "", 0);
        return HUSH_OK;
    }
    hush_http_path(req, path, sizeof(path));
    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        hush_http_reply(fd, "200 OK", "text/html; charset=utf-8",
                        HUSH_UI_HTML, strlen(HUSH_UI_HTML));
        return HUSH_OK;
    }
    if (strcmp(path, "/api/status") == 0) {
        hush_http_serve_status(fd, store);
        return HUSH_OK;
    }
    if (strcmp(path, "/api/events") == 0) {
        hush_http_serve_events(fd, store);
        return HUSH_OK;
    }
    if (strcmp(path, "/api/event") == 0 && memcmp(req, "POST", 4) == 0)
        return hush_http_serve_post(fd, req, len, store, out_posted);
    hush_http_reply(fd, "404 Not Found", "text/plain", "not found\n", 10);
    return HUSH_ERR_NOT_FOUND;
}

static const char *hush_find_headers_end(const char *buf, size_t len)
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

static void hush_http_write_all(int fd, const char *buf, size_t len)
{
    size_t off = 0;
    ssize_t w;

    if (buf == NULL)
        return;
    while (off < len) {
        w = write(fd, buf + off, len - off);
        if (w <= 0)
            break;
        off += (size_t)w;
    }
}

static void hush_http_reply(int fd, const char *status, const char *ctype,
                            const char *body, size_t blen)
{
    char hdr[320];
    int n;

    n = snprintf(hdr, sizeof(hdr),
                 "HTTP/1.1 %s\r\n"
                 "Content-Type: %s\r\n"
                 "Content-Length: %zu\r\n"
                 "Connection: close\r\n"
                 "Access-Control-Allow-Origin: *\r\n"
                 "Access-Control-Allow-Headers: Content-Type\r\n"
                 "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                 "\r\n",
                 status, ctype, blen);
    if (n > 0 && (size_t)n < sizeof(hdr))
        hush_http_write_all(fd, hdr, (size_t)n);
    hush_http_write_all(fd, body, blen);
}

static void hush_json_unescape_copy(const char *src, char *dst, size_t dstsz)
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

static int hush_json_field(const char *body, const char *key, char *out, size_t outsz)
{
    char pat[64];
    const char *p;

    out[0] = '\0';
    if (snprintf(pat, sizeof(pat), "\"%s\":\"", key) >= (int)sizeof(pat))
        return 0;
    p = strstr(body, pat);
    if (p == NULL)
        return 0;
    hush_json_unescape_copy(p + strlen(pat), out, outsz);
    return out[0] != '\0';
}

static size_t hush_json_escape(const char *in, char *out, size_t outsz)
{
    size_t o = 0;

    if (outsz == 0)
        return 0;
    while (*in != '\0' && o + 2 < outsz) {
        if (*in == '"' || *in == '\\') {
            out[o++] = '\\';
            out[o++] = *in++;
        } else if (*in == '\n') {
            out[o++] = '\\';
            out[o++] = 'n';
            in++;
        } else {
            out[o++] = *in++;
        }
    }
    out[o] = '\0';
    return o;
}

static void hush_make_event_id(char *out65)
{
    static unsigned seq;
    time_t now;
    unsigned n;

    now = time(NULL);
    seq++;
    n = seq;
    (void)snprintf(out65, HUSH_ID_HEX_WIDTH + 1,
                   "%016llx%016llx%016x%016x",
                   (unsigned long long)now,
                   (unsigned long long)n,
                   n, n ^ 0x9e3779b9u);
}

static void hush_http_serve_status(int fd, const hush_store_t *store)
{
    char body[192];
    hush_event_t tmp[HUSH_HTTP_EVENTS_MAX];
    size_t n;
    int w;

    n = hush_store_query(store, NULL, 0, tmp, HUSH_HTTP_EVENTS_MAX);
    w = snprintf(body, sizeof(body),
                 "{\"ok\":true,\"version\":\"0.0.1\",\"events\":%zu,"
                 "\"clients\":%d,\"port\":%u}\n",
                 n, g_client_count, (unsigned)g_listen_port);
    if (w < 0)
        w = 0;
    hush_http_reply(fd, "200 OK", "application/json", body, (size_t)w);
}

static void hush_http_serve_events(int fd, const hush_store_t *store)
{
    static char body[HUSH_HTTP_JSON_MAX];
    hush_event_t evs[HUSH_HTTP_EVENTS_MAX];
    char esc[HUSH_EVENT_MAX_CONTENT + 8];
    size_t n;
    size_t i;
    size_t off;
    int w;

    n = hush_store_query(store, NULL, 0, evs, HUSH_HTTP_EVENTS_MAX);
    off = (size_t)snprintf(body, sizeof(body), "{\"events\":[");
    for (i = 0; i < n && off + 64 < sizeof(body); ++i) {
        hush_json_escape(evs[i].content, esc, sizeof(esc));
        w = snprintf(body + off, sizeof(body) - off,
                     "%s{\"id\":\"%s\",\"pubkey\":\"%s\",\"kind\":%u,"
                     "\"created_at\":%lld,\"content\":\"%s\",\"channel\":\"%s\"}",
                     (i == 0) ? "" : ",",
                     evs[i].id, evs[i].pubkey, evs[i].kind,
                     (long long)evs[i].created_at, esc,
                     evs[i].tags[0][1][0] ? evs[i].tags[0][1] : "general");
        if (w < 0)
            break;
        off += (size_t)w;
    }
    if (off + 3 < sizeof(body)) {
        memcpy(body + off, "]}\n", 3);
        off += 3;
    }
    hush_http_reply(fd, "200 OK", "application/json", body, off);
}

static hush_status_t hush_http_serve_post(int fd, const char *req, size_t len,
                                          hush_store_t *store, hush_event_t *out)
{
    const char *end;
    const char *body;
    char content[HUSH_EVENT_MAX_CONTENT + 1];
    char channel[64];
    char kindbuf[16];

    end = hush_find_headers_end(req, len);
    if (end == NULL)
        return HUSH_ERR_PARSE;
    body = end + 4;
    memset(out, 0, sizeof(*out));
    if (!hush_json_field(body, "content", content, sizeof(content))) {
        hush_http_reply(fd, "400 Bad Request", "text/plain", "need content\n", 13);
        return HUSH_ERR_PARSE;
    }
    if (!hush_json_field(body, "channel", channel, sizeof(channel)))
        memcpy(channel, "general", 8);
    hush_make_event_id(out->id);
    memcpy(out->pubkey,
           "0000000000000000000000000000000000000000000000000000000000000001",
           65);
    out->kind = 1;
    if (hush_json_field(body, "kind", kindbuf, sizeof(kindbuf)))
        out->kind = (uint32_t)atoi(kindbuf);
    out->created_at = (int64_t)time(NULL);
    memcpy(out->content, content, strlen(content) + 1);
    out->tag_count = 1;
    memcpy(out->tags[0][0], "h", 2);
    memcpy(out->tags[0][1], channel, strlen(channel) + 1);
    if (hush_store_insert(store, out) != HUSH_OK) {
        hush_http_reply(fd, "507 Insufficient Storage", "text/plain", "full\n", 5);
        return HUSH_ERR_FULL;
    }
    hush_http_reply(fd, "200 OK", "application/json", "{\"ok\":true}\n", 12);
    return HUSH_OK;
}
