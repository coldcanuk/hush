/* hush_http.c: owns HTTP status/events/PWA UI serving for hush-relay. */

#include <assert.h>
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
    HUSH_ID_HEX_WIDTH = 64,
    HUSH_HTTP_KIND_SIGNAL = 25000
};

static uint16_t g_listen_port;
static int g_client_count;
static hush_launch_t *g_launch;
static hush_turn_t *g_turn;
static char g_context_text[HUSH_ROSTER_CONTEXT_MAX][HUSH_ROSTER_CONTEXT_BYTES];

static const char *hush_find_headers_end(const char *buf, size_t len);
static long hush_http_content_length(const char *buf, size_t hlen);
static void hush_http_path(const char *req, char *out, size_t outsz);
static void hush_http_write_all(int fd, const char *buf, size_t len);
static void hush_http_reply(int fd, const char *status, const char *ctype,
                            const char *body, size_t blen);
static int hush_http_serve_asset(int fd, const char *path);
static void hush_json_unescape_copy(const char *src, char *dst, size_t dstsz);
static int hush_json_field(const char *body, const char *key, char *out, size_t outsz);
static int hush_json_bare_field(const char *body, const char *key,
                                char *out, size_t outsz);
static size_t hush_json_escape(const char *in, char *out, size_t outsz);
static void hush_make_event_id(char *out65);
static void hush_http_serve_status(int fd, const hush_store_t *store);
static void hush_http_serve_events(int fd, const hush_store_t *store);
static void hush_http_serve_session(int fd);
static hush_status_t hush_http_serve_post(int fd, const char *req, size_t len,
                                          hush_store_t *store, hush_event_t *out);
static int hush_http_want_save_pass(const char *body);
static hush_status_t hush_http_serve_identity(int fd, const char *body);
static hush_status_t hush_http_serve_profile(int fd, const char *body);
static hush_status_t hush_http_serve_member(int fd, const char *body);
static hush_status_t hush_http_serve_agent(int fd, const char *body,
                                           hush_store_t *store);
static hush_status_t hush_http_delete_agent(int fd, const char *body);
static hush_status_t hush_http_fill_agent_context(hush_roster_agent_in_t *in,
                                                  const char *body);
static hush_status_t hush_http_read_context_slot(hush_roster_context_in_t *slot,
                                                 const char *body, size_t idx);
static hush_status_t hush_http_read_legacy_context(hush_roster_context_in_t *slot,
                                                   const char *body);
static hush_status_t hush_http_serve_vibe(int fd, const char *body,
                                          hush_store_t *store);
static hush_status_t hush_http_serve_channel(int fd, const char *body);
static hush_status_t hush_http_serve_project(int fd, const char *body,
                                             hush_store_t *store);
static hush_status_t hush_http_serve_turn_post(int fd, const char *body);
static void hush_http_serve_turn_get(int fd);
static void hush_http_serve_ice(int fd);
static hush_status_t hush_http_serve_signal(int fd, const char *body,
                                            hush_store_t *store,
                                            hush_event_t *out);
static hush_status_t hush_http_serve_api_post(int fd, const char *path,
                                              const char *req, size_t len,
                                              hush_store_t *store,
                                              hush_event_t *out_posted);
static const char *hush_http_body(const char *req, size_t len);

void hush_http_set_listen_port(uint16_t port)
{
    g_listen_port = port;
}

void hush_http_set_client_count(int n)
{
    g_client_count = n;
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
    if (hush_http_serve_asset(fd, path))
        return HUSH_OK;
    if (strcmp(path, "/api/status") == 0) {
        hush_http_serve_status(fd, store);
        return HUSH_OK;
    }
    if (strcmp(path, "/api/events") == 0) {
        hush_http_serve_events(fd, store);
        return HUSH_OK;
    }
    if (strcmp(path, "/api/session") == 0) {
        hush_http_serve_session(fd);
        return HUSH_OK;
    }
    if (strcmp(path, "/api/turn") == 0) {
        hush_http_serve_turn_get(fd);
        return HUSH_OK;
    }
    if (strcmp(path, "/api/ice") == 0) {
        hush_http_serve_ice(fd);
        return HUSH_OK;
    }
    if (memcmp(req, "POST", 4) == 0)
        return hush_http_serve_api_post(fd, path, req, len, store, out_posted);
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

static int hush_http_serve_asset(int fd, const char *path)
{
    struct hush_http_asset {
        const char *path;
        const char *ctype;
        const char *body;
        size_t len;
    };
    static const struct hush_http_asset assets[] = {
        { "/", "text/html; charset=utf-8", HUSH_UI_HTML, 0 },
        { "/index.html", "text/html; charset=utf-8", HUSH_UI_HTML, 0 },
        { "/manifest.webmanifest", "application/manifest+json",
          HUSH_UI_MANIFEST, 0 },
        { "/sw.js", "application/javascript; charset=utf-8", HUSH_UI_SW, 0 },
        { "/icon-192.png", "image/png",
          (const char *)HUSH_UI_ICON_192, (size_t)HUSH_UI_ICON_192_LEN },
        { "/icon-512.png", "image/png",
          (const char *)HUSH_UI_ICON_512, (size_t)HUSH_UI_ICON_512_LEN },
        { "/apple-touch-icon.png", "image/png",
          (const char *)HUSH_UI_ICON_180, (size_t)HUSH_UI_ICON_180_LEN }
    };
    size_t i;
    size_t n;
    const char *body;

    for (i = 0; i < sizeof(assets) / sizeof(assets[0]); ++i) {
        if (strcmp(path, assets[i].path) != 0)
            continue;
        body = assets[i].body;
        n = assets[i].len != 0 ? assets[i].len : strlen(body);
        hush_http_reply(fd, "200 OK", assets[i].ctype, body, n);
        return 1;
    }
    return 0;
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
        hush_json_unescape_copy(hit + strlen(quoted), out, outsz);
        return out[0] != '\0';
    }
    return hush_json_bare_field(body, key, out, outsz);
}

static int hush_json_bare_field(const char *body, const char *key,
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
    char body[384];
    hush_event_t tmp[HUSH_HTTP_EVENTS_MAX];
    size_t n;
    int w;
    int whisper;
    int turn_on;
    int vibe_pub;

    n = hush_store_query(store, NULL, 0, tmp, HUSH_HTTP_EVENTS_MAX);
    whisper = hush_turn_whisper_available();
    turn_on = (g_turn != NULL && g_turn->running);
    vibe_pub = (g_launch == NULL || !g_launch->has_vibe || g_launch->vibe_public);
    w = snprintf(body, sizeof(body),
                 "{\"ok\":true,\"version\":\"0.0.1\",\"events\":%zu,"
                 "\"clients\":%d,\"port\":%u,\"whisper\":%s,"
                 "\"turn_running\":%s,\"vibe_public\":%s}\n",
                 n, g_client_count, (unsigned)g_listen_port,
                 whisper ? "true" : "false",
                 turn_on ? "true" : "false",
                 vibe_pub ? "true" : "false");
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
    if (g_launch != NULL && g_launch->logged_in)
        memcpy(out->pubkey, g_launch->human.pubkey_hex, 65);
    else
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

static void hush_http_serve_session(int fd)
{
    static const char k_empty[] =
        "{\"ok\":true,\"logged_in\":false,\"ready\":false}\n";
    static char body[HUSH_LAUNCH_JSON_MAX];
    size_t n = 0;

    if (g_launch == NULL) {
        hush_http_reply(fd, "200 OK", "application/json",
                        k_empty, sizeof(k_empty) - 1);
        return;
    }
    if (hush_launch_format_session(g_launch, g_listen_port, body,
                                   sizeof(body), &n) != HUSH_OK)
        n = 0;
    hush_http_reply(fd, "200 OK", "application/json", body, n);
}

static hush_status_t hush_http_reply_session(int fd, hush_status_t st)
{
    if (st == HUSH_OK) {
        hush_http_serve_session(fd);
        return HUSH_OK;
    }
    if (st == HUSH_ERR_IO) {
        hush_http_reply(fd, "500 Internal Server Error", "text/plain",
                        "io error\n", 9);
        return st;
    }
    hush_http_reply(fd, "400 Bad Request", "text/plain", "bad request\n", 12);
    return st;
}

static int hush_http_want_save_pass(const char *body)
{
    char flag[8];

    if (!hush_json_field(body, "save_pass", flag, sizeof(flag)))
        return 1;
    if (strcmp(flag, "false") == 0 || strcmp(flag, "0") == 0)
        return 0;
    return 1;
}

static hush_status_t hush_http_serve_identity(int fd, const char *body)
{
    char action[32];
    char secret[HUSH_IDENTITY_NSEC_MAX];

    if (g_launch == NULL || body == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_ARG);
    if (!hush_json_field(body, "action", action, sizeof(action)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    if (strcmp(action, "create") == 0)
        return hush_http_reply_session(fd, hush_launch_create_identity(g_launch));
    if (strcmp(action, "import") == 0) {
        if (!hush_json_field(body, "nsec", secret, sizeof(secret)))
            return hush_http_reply_session(fd, HUSH_ERR_PARSE);
        return hush_http_reply_session(fd,
                                       hush_launch_import_identity(g_launch, secret));
    }
    if (strcmp(action, "ack_backup") == 0)
        return hush_http_reply_session(fd,
                                       hush_launch_ack_backup(g_launch,
                                                              hush_http_want_save_pass(body)));
    if (strcmp(action, "logout") == 0)
        return hush_http_reply_session(fd, hush_launch_logout(g_launch));
    return hush_http_reply_session(fd, HUSH_ERR_PARSE);
}

static hush_status_t hush_http_serve_profile(int fd, const char *body)
{
    hush_roster_profile_t profile;

    if (g_launch == NULL || body == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_ARG);
    memset(&profile, 0, sizeof(profile));
    (void)hush_json_field(body, "first_name", profile.first_name,
                          sizeof(profile.first_name));
    (void)hush_json_field(body, "last_name", profile.last_name,
                          sizeof(profile.last_name));
    (void)hush_json_field(body, "email", profile.email, sizeof(profile.email));
    (void)hush_json_field(body, "organization", profile.organization,
                          sizeof(profile.organization));
    (void)hush_json_field(body, "theme", profile.theme, sizeof(profile.theme));
    (void)hush_json_field(body, "picture", profile.picture,
                          sizeof(profile.picture));
    return hush_http_reply_session(fd,
                                   hush_launch_set_profile(g_launch, &profile));
}

static hush_status_t hush_http_serve_member(int fd, const char *body)
{
    char key[HUSH_IDENTITY_NPUB_MAX];
    char name[HUSH_ROSTER_NAME_MAX];

    if (g_launch == NULL || body == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_ARG);
    if (!hush_json_field(body, "npub", key, sizeof(key)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    if (!hush_json_field(body, "name", name, sizeof(name)))
        memcpy(name, "human", 6);
    return hush_http_reply_session(fd,
                                   hush_launch_add_member(g_launch, key, name));
}

static hush_status_t hush_http_serve_agent(int fd, const char *body,
                                           hush_store_t *store)
{
    hush_roster_agent_in_t in;
    char action[16];
    hush_status_t st;

    if (g_launch == NULL || body == NULL || store == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_ARG);
    if (hush_json_field(body, "action", action, sizeof(action)) &&
        strcmp(action, "delete") == 0)
        return hush_http_delete_agent(fd, body);
    memset(&in, 0, sizeof(in));
    if (!hush_json_field(body, "name", in.name, sizeof(in.name)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    if (!hush_json_field(body, "system_prompt", in.prompt, sizeof(in.prompt)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    if (!hush_json_field(body, "provider", in.provider, sizeof(in.provider)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    (void)hush_json_field(body, "picture", in.picture, sizeof(in.picture));
    st = hush_http_fill_agent_context(&in, body);
    if (st != HUSH_OK)
        return hush_http_reply_session(fd, st);
    return hush_http_reply_session(fd,
                                   hush_launch_add_agent(g_launch, store, &in,
                                                         hush_http_want_save_pass(body)));
}

static hush_status_t hush_http_delete_agent(int fd, const char *body)
{
    char slug[HUSH_ROSTER_NAME_MAX];

    if (!hush_json_field(body, "slug", slug, sizeof(slug)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    return hush_http_reply_session(fd, hush_launch_remove_agent(g_launch, slug));
}

static hush_status_t hush_http_fill_agent_context(hush_roster_agent_in_t *in,
                                                  const char *body)
{
    size_t i;
    hush_status_t st;

    assert(in != NULL);
    assert(body != NULL);
    for (i = 0; i < (size_t)HUSH_ROSTER_CONTEXT_MAX; ++i) {
        st = hush_http_read_context_slot(&in->context[in->ncontext], body, i);
        if (st == HUSH_ERR_NOT_FOUND)
            continue;
        if (st != HUSH_OK)
            return st;
        in->ncontext++;
    }
    return HUSH_OK;
}

static hush_status_t hush_http_read_context_slot(hush_roster_context_in_t *slot,
                                                 const char *body, size_t idx)
{
    char key[24];

    assert(slot != NULL);
    assert(body != NULL);
    assert(idx < (size_t)HUSH_ROSTER_CONTEXT_MAX);
    if (snprintf(key, sizeof(key), "context_name_%zu", idx) >= (int)sizeof(key))
        return HUSH_ERR_FULL;
    if (!hush_json_field(body, key, slot->name, sizeof(slot->name))) {
        if (idx == 0)
            return hush_http_read_legacy_context(slot, body);
        return HUSH_ERR_NOT_FOUND;
    }
    if (snprintf(key, sizeof(key), "context_mime_%zu", idx) >= (int)sizeof(key))
        return HUSH_ERR_FULL;
    if (!hush_json_field(body, key, slot->mime, sizeof(slot->mime)))
        memcpy(slot->mime, HUSH_ROSTER_MIME_PLAIN,
               sizeof(HUSH_ROSTER_MIME_PLAIN));
    if (snprintf(key, sizeof(key), "context_text_%zu", idx) >= (int)sizeof(key))
        return HUSH_ERR_FULL;
    if (!hush_json_field(body, key, g_context_text[idx],
                         sizeof(g_context_text[idx])))
        g_context_text[idx][0] = '\0';
    slot->text = g_context_text[idx];
    slot->bytes = strlen(g_context_text[idx]);
    return HUSH_OK;
}

static hush_status_t hush_http_read_legacy_context(hush_roster_context_in_t *slot,
                                                   const char *body)
{
    assert(slot != NULL);
    assert(body != NULL);
    if (!hush_json_field(body, "context_name", slot->name, sizeof(slot->name)))
        return HUSH_ERR_NOT_FOUND;
    if (!hush_json_field(body, "context_mime", slot->mime, sizeof(slot->mime)))
        memcpy(slot->mime, HUSH_ROSTER_MIME_PLAIN,
               sizeof(HUSH_ROSTER_MIME_PLAIN));
    if (!hush_json_field(body, "context_text", g_context_text[0],
                         sizeof(g_context_text[0])))
        g_context_text[0][0] = '\0';
    slot->text = g_context_text[0];
    slot->bytes = strlen(g_context_text[0]);
    return HUSH_OK;
}

static hush_status_t hush_http_serve_vibe(int fd, const char *body,
                                          hush_store_t *store)
{
    char name[HUSH_LAUNCH_NAME_MAX];
    char about[HUSH_LAUNCH_ABOUT_MAX];
    char vis[16];
    int is_public = 1;

    if (g_launch == NULL || body == NULL || store == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_ARG);
    if (hush_json_field(body, "visibility", vis, sizeof(vis)) &&
        strcmp(vis, "private") == 0)
        is_public = 0;
    if (g_launch->has_vibe &&
        !hush_json_field(body, "name", name, sizeof(name)))
        return hush_http_reply_session(fd,
                                       hush_launch_set_vibe_visibility(g_launch,
                                                                       is_public));
    if (!hush_json_field(body, "name", name, sizeof(name)))
        memcpy(name, "local hive", 11);
    if (!hush_json_field(body, "about", about, sizeof(about)))
        about[0] = '\0';
    if (hush_launch_create_vibe(g_launch, store, name, about) != HUSH_OK)
        return hush_http_reply_session(fd, HUSH_ERR_CRYPTO);
    return hush_http_reply_session(fd,
                                   hush_launch_set_vibe_visibility(g_launch,
                                                                   is_public));
}

static hush_status_t hush_http_serve_channel(int fd, const char *body)
{
    char name[HUSH_LAUNCH_NAME_MAX];

    if (g_launch == NULL || body == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_ARG);
    if (!hush_json_field(body, "name", name, sizeof(name)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    return hush_http_reply_session(fd, hush_launch_add_channel(g_launch, name));
}

static hush_status_t hush_http_serve_project(int fd, const char *body,
                                             hush_store_t *store)
{
    char name[HUSH_LAUNCH_NAME_MAX];
    char path[HUSH_LAUNCH_PATH_MAX];
    char gitbuf[8];
    int init_git = 0;

    if (g_launch == NULL || body == NULL || store == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_ARG);
    if (!hush_json_field(body, "name", name, sizeof(name)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    if (!hush_json_field(body, "path", path, sizeof(path)))
        path[0] = '\0';
    if (hush_json_field(body, "git", gitbuf, sizeof(gitbuf)) &&
        (strcmp(gitbuf, "1") == 0 || strcmp(gitbuf, "true") == 0))
        init_git = 1;
    return hush_http_reply_session(fd,
                                   hush_launch_add_project(g_launch, store,
                                                           name, path, init_git));
}

static hush_status_t hush_http_serve_api_post(int fd, const char *path,
                                              const char *req, size_t len,
                                              hush_store_t *store,
                                              hush_event_t *out_posted)
{
    if (strcmp(path, "/api/event") == 0)
        return hush_http_serve_post(fd, req, len, store, out_posted);
    if (strcmp(path, "/api/identity") == 0)
        return hush_http_serve_identity(fd, hush_http_body(req, len));
    if (strcmp(path, "/api/profile") == 0)
        return hush_http_serve_profile(fd, hush_http_body(req, len));
    if (strcmp(path, "/api/member") == 0)
        return hush_http_serve_member(fd, hush_http_body(req, len));
    if (strcmp(path, "/api/agent") == 0)
        return hush_http_serve_agent(fd, hush_http_body(req, len), store);
    if (strcmp(path, "/api/vibe") == 0)
        return hush_http_serve_vibe(fd, hush_http_body(req, len), store);
    if (strcmp(path, "/api/channel") == 0)
        return hush_http_serve_channel(fd, hush_http_body(req, len));
    if (strcmp(path, "/api/project") == 0)
        return hush_http_serve_project(fd, hush_http_body(req, len), store);
    if (strcmp(path, "/api/turn") == 0)
        return hush_http_serve_turn_post(fd, hush_http_body(req, len));
    if (strcmp(path, "/api/signal") == 0)
        return hush_http_serve_signal(fd, hush_http_body(req, len),
                                      store, out_posted);
    hush_http_reply(fd, "404 Not Found", "text/plain", "not found\n", 10);
    return HUSH_ERR_NOT_FOUND;
}

static const char *hush_http_body(const char *req, size_t len)
{
    const char *end;

    if (req == NULL)
        return "";
    end = hush_find_headers_end(req, len);
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
    if (hush_json_field(body, "host", host, sizeof(host)))
        (void)hush_turn_set_public_host(g_turn, host);
    if (hush_json_field(body, "enabled", enabled, sizeof(enabled)) &&
        (strcmp(enabled, "false") == 0 || strcmp(enabled, "0") == 0)) {
        st = hush_turn_disable(g_turn);
        hush_http_serve_turn_get(fd);
        return st;
    }
    mode = HUSH_TURN_MODE_CHILD;
    if (hush_json_field(body, "daemon", daemon, sizeof(daemon)) &&
        (strcmp(daemon, "true") == 0 || strcmp(daemon, "1") == 0))
        mode = HUSH_TURN_MODE_DAEMON;
    st = hush_turn_enable(g_turn, mode);
    hush_http_serve_turn_get(fd);
    return (st == HUSH_OK) ? HUSH_OK : st;
}

static hush_status_t hush_http_serve_signal(int fd, const char *body,
                                            hush_store_t *store,
                                            hush_event_t *out)
{
    char channel[64];

    if (body == NULL || store == NULL || out == NULL)
        return HUSH_ERR_ARG;
    memset(out, 0, sizeof(*out));
    if (body[0] == '\0' || strlen(body) >= HUSH_EVENT_MAX_CONTENT) {
        hush_http_reply(fd, "400 Bad Request", "text/plain", "need body\n", 10);
        return HUSH_ERR_PARSE;
    }
    if (!hush_json_field(body, "channel", channel, sizeof(channel)))
        memcpy(channel, "general", 8);
    hush_make_event_id(out->id);
    if (g_launch != NULL && g_launch->logged_in)
        memcpy(out->pubkey, g_launch->human.pubkey_hex, 65);
    else
        memcpy(out->pubkey,
               "0000000000000000000000000000000000000000000000000000000000000001",
               65);
    out->kind = (uint32_t)HUSH_HTTP_KIND_SIGNAL;
    out->created_at = (int64_t)time(NULL);
    memcpy(out->content, body, strlen(body) + 1);
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
