/* hush_http.c: owns HTTP status/events/PWA UI serving for hush-relay. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "hush_agent.h"
#include "hush_canvas.h"
#include "hush_home.h"
#include "hush_http.h"
#include "hush_intel.h"
#include "hush_json.h"
#include "hush_provider.h"
#include "hush_relay.h"
#include "hush_skill.h"
#include "hush_ui_html.h"
#include "hush_win.h"
#include "hush_icon_panels.h"

enum {
    HUSH_HTTP_JSON_MAX = 65536,
    HUSH_HTTP_PATH_MAX = 128,
    HUSH_HTTP_EVENTS_MAX = 64,
    HUSH_HTTP_HDR_MAX = 8192,
    HUSH_ID_HEX_WIDTH = 64,
    HUSH_HTTP_KIND_SIGNAL = 25000,
    HUSH_HTTP_LOGIN_REPLY_MAX = 384,
    HUSH_HTTP_MENTIONS_MAX = 8,
    HUSH_HTTP_CHAN_LIST_MAX = 8,
    HUSH_HTTP_STATUS_MAX = 2048,
    HUSH_HTTP_THINKING_MAX = 1024,
    HUSH_HTTP_CANVAS_PATH_MAX = 384,
    HUSH_HTTP_FIXUP_ASK_MAX = 500,
    HUSH_HTTP_FIXUP_JSON_MAX = 16384,
    HUSH_HTTP_FIXUP_SLEEP_NS = 50000000,
    HUSH_HTTP_FIXUP_WAIT_MAX = 1800,
    HUSH_HTTP_FIXUP_TOKEN_MAX = 16,
    HUSH_HTTP_WINDOW_ACT_MAX = 16,
    HUSH_HTTP_COMPLETE_JSON_MAX = 1536
};

#define HUSH_HTTP_CLOSE_JSON "{\"ok\":true,\"action\":\"close\"}\n"
#define HUSH_HTTP_EXIT_JSON  "{\"ok\":true,\"action\":\"exit\"}\n"
#define HUSH_HTTP_CANVAS_OK "{\"ok\":true}\n"
#define HUSH_HTTP_FIXUP_FAIL "{\"ok\":false,\"error\":\"fixup failed\"}\n"
#define HUSH_HTTP_COMPLETE_FAIL "{\"ok\":false,\"error\":\"complete failed\"}\n"
#define HUSH_HTTP_COMPLETE_PEND "{\"ok\":true,\"pending\":true}\n"
#define HUSH_HTTP_WINDOW_MIN_JSON "{\"ok\":true,\"action\":\"minimize\"}\n"
#define HUSH_HTTP_WINDOW_MAX_JSON "{\"ok\":true,\"action\":\"maximize\"}\n"
#define HUSH_HTTP_WINDOW_FAIL "{\"ok\":false,\"error\":\"window failed\"}\n"
#define HUSH_HTTP_WINDOW_MIN "minimize"
#define HUSH_HTTP_WINDOW_MAX "maximize"

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
static char g_context_text[HUSH_ROSTER_CONTEXT_MAX][HUSH_ROSTER_CONTEXT_BYTES];

static const char *hush_find_headers_end(const char *buf, size_t len);
static long hush_http_content_length(const char *buf, size_t hlen);
static void hush_http_path(const char *req, char *out, size_t outsz);
static void hush_http_write_all(int fd, const char *buf, size_t len);
static void hush_http_reply(int fd, const char *status, const char *ctype,
                            const char *body, size_t blen);
static int hush_http_serve_asset(int fd, const char *path);
static int hush_http_serve_icon_panel(int fd, const char *path);
static void hush_json_unescape_copy(const char *src, char *dst, size_t dstsz);
static int hush_json_field(const char *body, const char *key, char *out, size_t outsz);
static int hush_json_has_key(const char *body, const char *key);
static int hush_json_bare_field(const char *body, const char *key,
                                char *out, size_t outsz);
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
static hush_status_t hush_http_update_payne(int fd, const char *body);
static hush_status_t hush_http_update_agent(int fd, const char *body);
static int hush_http_is_payne_slug(const char *body);
static void hush_http_fill_agent_extras(hush_roster_agent_in_t *in,
                                        const char *body);
static void hush_http_fill_agent_skills(hush_roster_agent_in_t *in,
                                        const char *body);
static void hush_http_serve_skills_get(int fd);
static hush_status_t hush_http_serve_skill_post(int fd, const char *body);
static hush_status_t hush_http_fill_agent_context(hush_roster_agent_in_t *in,
                                                  const char *body);
static hush_status_t hush_http_read_context_slot(hush_roster_context_in_t *slot,
                                                 const char *body, size_t idx);
static hush_status_t hush_http_read_legacy_context(hush_roster_context_in_t *slot,
                                                   const char *body);
static hush_status_t hush_http_serve_vibe(int fd, const char *body,
                                          hush_store_t *store);
static hush_status_t hush_http_serve_channel(int fd, const char *body);
static hush_status_t hush_http_serve_group(int fd, const char *body);
static hush_status_t hush_http_channel_manage(int fd, const char *body,
                                              const char *slug);
static hush_status_t hush_http_channel_policy(const char *body,
                                              const char *slug);
static const hush_launch_channel_t *hush_http_find_channel(const char *slug);
static int hush_http_read_int(const char *body, const char *key, int fallback);
static void hush_http_fill_policy_text(hush_launch_policy_t *policy,
                                       const hush_launch_channel_t *ch,
                                       const char *body);
static void hush_http_fill_policy_nums(hush_launch_policy_t *policy,
                                       const hush_launch_channel_t *ch,
                                       const char *body);
static void hush_http_collect_indexed(const char *body, const char *stem,
                                      char (*out)[HUSH_IDENTITY_NPUB_MAX],
                                      size_t *out_n, size_t maxn);
static void hush_http_add_mentions(hush_event_t *out, const char *body);
static void hush_http_add_reply_to(hush_event_t *out, const char *body);
static void hush_http_event_reply_to(char *out, size_t outsz,
                                     const hush_event_t *ev);
static hush_status_t hush_http_serve_project(int fd, const char *body,
                                             hush_store_t *store);
static const hush_launch_project_t *hush_http_find_project(const char *slug);
static int hush_http_canvas_rel_ok(const char *rel);
static hush_status_t hush_http_canvas_join(char *out, size_t outsz,
                                           const char *root, const char *rel);
static hush_status_t hush_http_canvas_write(const char *path,
                                            const char *content);
static hush_status_t hush_http_serve_canvas(int fd, const char *body);
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
static hush_status_t hush_http_serve_signal(int fd, const char *body,
                                            hush_store_t *store,
                                            hush_event_t *out);
static hush_status_t hush_http_serve_api_post(int fd, const char *path,
                                              const char *req, size_t len,
                                              hush_store_t *store,
                                              hush_event_t *out_posted);
static const char *hush_http_body(const char *req, size_t len);
static hush_status_t hush_http_serve_close(int fd);
static hush_status_t hush_http_serve_exit(int fd);
static hush_status_t hush_http_serve_window(int fd, const char *body);
static hush_status_t hush_http_window_run(const char *action);
static void hush_http_reply_window(int fd, const char *action);
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
    if (strcmp(path, "/api/skills") == 0) {
        hush_http_serve_skills_get(fd);
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
    if (strcmp(path, "/api/provider") == 0 && memcmp(req, "GET", 3) == 0)
        return hush_http_serve_provider_get(fd);
    if (strcmp(path, "/api/complete") == 0 && memcmp(req, "GET", 3) == 0)
        return hush_http_serve_complete_get(fd, req);
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
          (const char *)HUSH_UI_ICON_180, (size_t)HUSH_UI_ICON_180_LEN },
        { "/agent-atlas.png", "image/png",
          (const char *)HUSH_UI_AGENT_ATLAS, (size_t)HUSH_UI_AGENT_ATLAS_LEN }
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
    return hush_http_serve_icon_panel(fd, path);
}

static int hush_http_serve_icon_panel(int fd, const char *path)
{
    struct hush_http_panel {
        const char *path;
        const unsigned char *start;
        const unsigned char *end;
    };
    static const struct hush_http_panel panels[] = {
        { "/icons/icon_panel_dogs.png",
          _binary_demo_icons_icon_panel_dogs_png_start,
          _binary_demo_icons_icon_panel_dogs_png_end },
        { "/icons/icon_panel_cats.png",
          _binary_demo_icons_icon_panel_cats_png_start,
          _binary_demo_icons_icon_panel_cats_png_end },
        { "/icons/icon_panel_sheep.png",
          _binary_demo_icons_icon_panel_sheep_png_start,
          _binary_demo_icons_icon_panel_sheep_png_end },
        { "/icons/icon_panel_virus.png",
          _binary_demo_icons_icon_panel_virus_png_start,
          _binary_demo_icons_icon_panel_virus_png_end },
        { "/icons/icon_panel_robots.png",
          _binary_demo_icons_icon_panel_robots_png_start,
          _binary_demo_icons_icon_panel_robots_png_end },
        { "/icons/icon_panel_angevin.png",
          _binary_demo_icons_icon_panel_angevin_png_start,
          _binary_demo_icons_icon_panel_angevin_png_end }
    };
    size_t i;
    size_t n;

    if (path == NULL)
        return 0;
    for (i = 0; i < sizeof(panels) / sizeof(panels[0]); ++i) {
        if (strcmp(path, panels[i].path) != 0)
            continue;
        n = (size_t)(panels[i].end - panels[i].start);
        hush_http_reply(fd, "200 OK", "image/png",
                        (const char *)panels[i].start, n);
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

static int hush_json_has_key(const char *body, const char *key)
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
    char body[HUSH_HTTP_STATUS_MAX];
    char thinking[HUSH_HTTP_THINKING_MAX];
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
    hush_agent_status(thinking, sizeof(thinking));
    w = snprintf(body, sizeof(body),
                 "{\"ok\":true,\"version\":\"0.0.1\",\"events\":%zu,"
                 "\"clients\":%d,\"port\":%u,\"whisper\":%s,"
                 "\"turn_running\":%s,\"vibe_public\":%s,\"thinking\":%s}\n",
                 n, g_client_count, (unsigned)g_listen_port,
                 whisper ? "true" : "false",
                 turn_on ? "true" : "false",
                 vibe_pub ? "true" : "false",
                 thinking[0] ? thinking : "[]");
    if (w < 0)
        w = 0;
    hush_http_reply(fd, "200 OK", "application/json", body, (size_t)w);
}

static void hush_http_serve_events(int fd, const hush_store_t *store)
{
    static char body[HUSH_HTTP_JSON_MAX];
    hush_event_t evs[HUSH_HTTP_EVENTS_MAX];
    char esc[HUSH_EVENT_MAX_CONTENT + 8];
    char reply_to[HUSH_EVENT_ID_HEX_LEN + 1];
    size_t n;
    size_t i;
    size_t off;
    int w;

    n = hush_store_query(store, NULL, 0, evs, HUSH_HTTP_EVENTS_MAX);
    off = (size_t)snprintf(body, sizeof(body), "{\"events\":[");
    for (i = 0; i < n && off + 512 < sizeof(body); ++i) {
        hush_json_escape(evs[i].content, esc, sizeof(esc));
        hush_http_event_reply_to(reply_to, sizeof(reply_to), &evs[i]);

        /* Collect p-mentions (the authoritative tags that triggered robot dispatch).
         * Produces a valid JSON array (or empty) so UI acks are truthful.
         * Never emits a trailing comma even on truncation.
         */
        char ment[1024];
        ment[0] = '\0';
        size_t mentoff = 0;
        int has_mention = 0;
        for (size_t t = 0; t < evs[i].tag_count && t < HUSH_EVENT_MAX_TAGS; ++t) {
            if (strcmp(evs[i].tags[t][0], "p") != 0 || evs[i].tags[t][1][0] == '\0')
                continue;
            char mval[256];
            hush_json_escape(evs[i].tags[t][1], mval, sizeof(mval));
            size_t need = strlen(mval) + 3; /* quote + quote + optional leading comma */
            if (mentoff + need + 1 >= sizeof(ment))
                break; /* stop; array so far is valid */
            if (has_mention) {
                ment[mentoff++] = ',';
            }
            int nw = snprintf(ment + mentoff, sizeof(ment) - mentoff,
                              "\"%s\"", mval);
            if (nw < 0 || (size_t)nw >= (sizeof(ment) - mentoff))
                break;
            mentoff += (size_t)nw;
            has_mention = 1;
        }
        if (!has_mention)
            ment[0] = '\0';

        w = snprintf(body + off, sizeof(body) - off,
                     "%s{\"id\":\"%s\",\"pubkey\":\"%s\",\"kind\":%u,"
                     "\"created_at\":%lld,\"content\":\"%s\",\"channel\":\"%s\","
                     "\"reply_to\":\"%s\",\"mentions\":[%s]}",
                     (i == 0) ? "" : ",",
                     evs[i].id, evs[i].pubkey, evs[i].kind,
                     (long long)evs[i].created_at, esc,
                     evs[i].tags[0][1][0] ? evs[i].tags[0][1] : "general",
                     reply_to, ment);
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
    hush_http_add_reply_to(out, body);
    hush_http_add_mentions(out, body);
    if (hush_store_insert(store, out) != HUSH_OK) {
        hush_http_reply(fd, "507 Insufficient Storage", "text/plain", "full\n", 5);
        return HUSH_ERR_FULL;
    }
    hush_intel_consider(store, g_launch, out);
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
    if (hush_http_is_payne_slug(body))
        return hush_http_update_payne(fd, body);
    if (hush_json_field(body, "action", action, sizeof(action)) &&
        strcmp(action, "update") == 0)
        return hush_http_update_agent(fd, body);
    memset(&in, 0, sizeof(in));
    if (!hush_json_field(body, "name", in.name, sizeof(in.name)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    if (!hush_json_field(body, "system_prompt", in.prompt, sizeof(in.prompt)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    if (!hush_json_field(body, "provider", in.provider, sizeof(in.provider)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    hush_http_fill_agent_extras(&in, body);
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

static int hush_http_is_payne_slug(const char *body)
{
    char slug[HUSH_ROSTER_NAME_MAX];

    if (body == NULL)
        return 0;
    if (!hush_json_field(body, "slug", slug, sizeof(slug)))
        return 0;
    return strcmp(slug, HUSH_LAUNCH_PAYNE_SLUG) == 0;
}

static hush_status_t hush_http_update_payne(int fd, const char *body)
{
    char ids[HUSH_LAUNCH_PAYNE_PROVIDERS_MAX][HUSH_ROSTER_PROVIDER_MAX];
    const char *ptrs[HUSH_LAUNCH_PAYNE_PROVIDERS_MAX];
    hush_roster_agent_in_t in;
    char key[24];
    size_t n = 0;
    size_t i;
    hush_status_t st;

    if (g_launch == NULL || body == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_ARG);
    memset(ids, 0, sizeof(ids));
    for (i = 0; i < (size_t)HUSH_LAUNCH_PAYNE_PROVIDERS_MAX; ++i) {
        if (snprintf(key, sizeof(key), "provider_%zu", i) >= (int)sizeof(key))
            return hush_http_reply_session(fd, HUSH_ERR_FULL);
        if (!hush_json_field(body, key, ids[n], sizeof(ids[n])))
            continue;
        if (ids[n][0] == '\0')
            continue;
        ptrs[n] = ids[n];
        n++;
    }
    st = hush_launch_set_payne_providers(g_launch, ptrs, n);
    if (st != HUSH_OK)
        return hush_http_reply_session(fd, st);
    memset(&in, 0, sizeof(in));
    (void)hush_json_field(body, "name", in.name, sizeof(in.name));
    (void)hush_json_field(body, "system_prompt", in.prompt, sizeof(in.prompt));
    hush_http_fill_agent_extras(&in, body);
    if (in.name[0] != '\0' || in.prompt[0] != '\0' || in.picture[0] != '\0'
        || in.voice[0] != '\0' || in.nskills > 0)
        st = hush_launch_update_payne_profile(g_launch, &in);
    return hush_http_reply_session(fd, st);
}

static hush_status_t hush_http_update_agent(int fd, const char *body)
{
    hush_roster_agent_in_t in;
    char slug[HUSH_ROSTER_NAME_MAX];

    if (g_launch == NULL || body == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_ARG);
    if (!hush_json_field(body, "slug", slug, sizeof(slug)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    memset(&in, 0, sizeof(in));
    (void)hush_json_field(body, "name", in.name, sizeof(in.name));
    (void)hush_json_field(body, "system_prompt", in.prompt, sizeof(in.prompt));
    (void)hush_json_field(body, "provider", in.provider, sizeof(in.provider));
    hush_http_fill_agent_extras(&in, body);
    return hush_http_reply_session(fd,
                                   hush_launch_update_agent(g_launch, slug, &in));
}

static void hush_http_fill_agent_extras(hush_roster_agent_in_t *in,
                                        const char *body)
{
    assert(in != NULL);
    assert(body != NULL);
    (void)hush_json_field(body, "picture", in->picture, sizeof(in->picture));
    (void)hush_json_field(body, "voice", in->voice, sizeof(in->voice));
    hush_http_fill_agent_skills(in, body);
}

static void hush_http_fill_agent_skills(hush_roster_agent_in_t *in,
                                        const char *body)
{
    char key[24];
    size_t i;

    assert(in != NULL);
    assert(body != NULL);
    in->nskills = 0;
    for (i = 0; i < (size_t)HUSH_SKILL_EQUIP_MAX; ++i) {
        if (snprintf(key, sizeof(key), "skill_%zu", i) >= (int)sizeof(key))
            return;
        if (!hush_json_field(body, key, in->skills[in->nskills],
                             sizeof(in->skills[0])))
            continue;
        if (in->skills[in->nskills][0] == '\0')
            continue;
        in->nskills++;
    }
}

static void hush_http_serve_skills_get(int fd)
{
    hush_skill_catalog_t cat;
    char body[HUSH_SKILL_JSON_MAX];
    size_t n = 0;

    hush_skill_init_catalog(&cat);
    if (hush_skill_load_catalog(&cat) != HUSH_OK) {
        hush_http_reply(fd, "500 Internal Server Error", "application/json",
                        "{\"ok\":false}\n", 14);
        return;
    }
    if (hush_skill_format_json(&cat, 1, body, sizeof(body), &n) != HUSH_OK) {
        hush_http_reply(fd, "500 Internal Server Error", "application/json",
                        "{\"ok\":false}\n", 14);
        return;
    }
    hush_http_reply(fd, "200 OK", "application/json", body, n);
}

static hush_status_t hush_http_serve_skill_post(int fd, const char *body)
{
    hush_skill_forge_in_t in;
    char id[HUSH_SKILL_ID_MAX];
    char reply[HUSH_SKILL_ID_MAX + 64];
    int n;

    if (body == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_ARG);
    memset(&in, 0, sizeof(in));
    if (!hush_json_field(body, "name", in.name, sizeof(in.name)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    (void)hush_json_field(body, "summary", in.summary, sizeof(in.summary));
    (void)hush_json_field(body, "body", in.body, sizeof(in.body));
    if (!hush_json_field(body, "scope", in.scope, sizeof(in.scope)))
        memcpy(in.scope, HUSH_SKILL_SCOPE_USER, sizeof(HUSH_SKILL_SCOPE_USER));
    (void)hush_json_field(body, "robot", in.robot, sizeof(in.robot));
    if (hush_skill_forge(&in, id, sizeof(id)) != HUSH_OK)
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    n = snprintf(reply, sizeof(reply), "{\"ok\":true,\"id\":\"%s\"}\n", id);
    if (n < 0 || (size_t)n >= sizeof(reply))
        return hush_http_reply_session(fd, HUSH_ERR_FULL);
    hush_http_reply(fd, "200 OK", "application/json", reply, (size_t)n);
    return HUSH_OK;
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
    char slug[HUSH_LAUNCH_NAME_MAX];
    char action[16];
    char group_id[HUSH_LAUNCH_ID_HEX + 1];
    char group_name[HUSH_LAUNCH_NAME_MAX];

    if (g_launch == NULL || body == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_ARG);
    action[0] = '\0';
    (void)hush_json_field(body, "action", action, sizeof(action));
    if (action[0] == '\0' || strcmp(action, "create") == 0) {
        if (!hush_json_field(body, "name", name, sizeof(name)))
            return hush_http_reply_session(fd, HUSH_ERR_PARSE);
        return hush_http_reply_session(fd,
                                       hush_launch_add_channel(g_launch, name));
    }
    if (!hush_json_field(body, "slug", slug, sizeof(slug)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    if (strcmp(action, "delete") == 0)
        return hush_http_reply_session(fd,
                                       hush_launch_remove_channel(g_launch, slug));
    if (strcmp(action, "ungroup") == 0)
        return hush_http_reply_session(fd,
                                       hush_launch_set_channel_group(g_launch,
                                                                     slug, ""));
    if (strcmp(action, "group") == 0) {
        if (hush_json_field(body, "group_id", group_id, sizeof(group_id)))
            return hush_http_reply_session(fd,
                                           hush_launch_set_channel_group(
                                               g_launch, slug, group_id));
        if (!hush_json_field(body, "group", group_name, sizeof(group_name)))
            return hush_http_reply_session(fd, HUSH_ERR_PARSE);
        if (hush_launch_add_group(g_launch, group_name) != HUSH_OK)
            return hush_http_reply_session(fd, HUSH_ERR_FULL);
        if (g_launch->ngroups == 0)
            return hush_http_reply_session(fd, HUSH_ERR_FULL);
        return hush_http_reply_session(fd,
                                       hush_launch_set_channel_group(
                                           g_launch, slug,
                                           g_launch->groups[g_launch->ngroups - 1].id));
    }
    if (strcmp(action, "manage") == 0)
        return hush_http_channel_manage(fd, body, slug);
    return hush_http_reply_session(fd, HUSH_ERR_PARSE);
}

static hush_status_t hush_http_serve_group(int fd, const char *body)
{
    char name[HUSH_LAUNCH_NAME_MAX];

    if (g_launch == NULL || body == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_ARG);
    if (!hush_json_field(body, "name", name, sizeof(name)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    return hush_http_reply_session(fd, hush_launch_add_group(g_launch, name));
}

static hush_status_t hush_http_channel_manage(int fd, const char *body,
                                              const char *slug)
{
    char humans[HUSH_HTTP_CHAN_LIST_MAX][HUSH_IDENTITY_NPUB_MAX];
    char robots[HUSH_HTTP_CHAN_LIST_MAX][HUSH_IDENTITY_NPUB_MAX];
    const char *human_ptrs[HUSH_HTTP_CHAN_LIST_MAX];
    const char *robot_ptrs[HUSH_HTTP_CHAN_LIST_MAX];
    size_t nhumans = 0;
    size_t nrobots = 0;
    size_t i;
    hush_status_t st;

    hush_http_collect_indexed(body, "human", humans, &nhumans,
                              (size_t)HUSH_HTTP_CHAN_LIST_MAX);
    hush_http_collect_indexed(body, "robot", robots, &nrobots,
                              (size_t)HUSH_HTTP_CHAN_LIST_MAX);
    for (i = 0; i < nhumans; ++i)
        human_ptrs[i] = humans[i];
    for (i = 0; i < nrobots; ++i)
        robot_ptrs[i] = robots[i];
    st = hush_launch_set_channel_roster(g_launch, slug, human_ptrs, nhumans,
                                        robot_ptrs, nrobots);
    if (st != HUSH_OK)
        return hush_http_reply_session(fd, st);
    st = hush_http_channel_policy(body, slug);
    if (st != HUSH_OK)
        return hush_http_reply_session(fd, st);
    if (hush_json_has_key(body, "about")) {
        char about[HUSH_LAUNCH_ABOUT_MAX];

        about[0] = '\0';
        (void)hush_json_field(body, "about", about, sizeof(about));
        st = hush_launch_set_channel_about(g_launch, slug, about);
        if (st != HUSH_OK)
            return hush_http_reply_session(fd, st);
    }
    return hush_http_reply_session(fd, HUSH_OK);
}

static const hush_launch_channel_t *hush_http_find_channel(const char *slug)
{
    size_t i;

    if (g_launch == NULL || slug == NULL)
        return NULL;
    for (i = 0; i < g_launch->nchannels; ++i) {
        if (strcmp(g_launch->channels[i].slug, slug) == 0)
            return &g_launch->channels[i];
    }
    return NULL;
}

static int hush_http_read_int(const char *body, const char *key, int fallback)
{
    char text[HUSH_LAUNCH_POLICY_MAX];

    if (!hush_json_field(body, key, text, sizeof(text)))
        return fallback;
    if (text[0] == '\0')
        return fallback;
    return atoi(text);
}

static void hush_http_fill_policy_text(hush_launch_policy_t *policy,
                                       const hush_launch_channel_t *ch,
                                       const char *body)
{
    assert(policy != NULL);
    assert(ch != NULL);
    if (!hush_json_field(body, "kind", policy->kind, sizeof(policy->kind))
        || policy->kind[0] == '\0')
        memcpy(policy->kind, ch->kind, sizeof(policy->kind));
    if (!hush_json_field(body, "robot_reply", policy->robot_reply,
                         sizeof(policy->robot_reply))
        || policy->robot_reply[0] == '\0')
        memcpy(policy->robot_reply, ch->robot_reply,
               sizeof(policy->robot_reply));
}

static void hush_http_fill_policy_nums(hush_launch_policy_t *policy,
                                       const hush_launch_channel_t *ch,
                                       const char *body)
{
    assert(policy != NULL);
    assert(ch != NULL);
    policy->robot_talk = hush_http_read_int(body, "robot_talk", ch->robot_talk);
    policy->burst_ms = hush_http_read_int(body, "burst_ms", ch->burst_ms);
    policy->max_jobs = hush_http_read_int(body, "max_jobs", ch->max_jobs);
    policy->cooldown_s = hush_http_read_int(body, "cooldown_s", ch->cooldown_s);
    policy->robot_hops = hush_http_read_int(body, "robot_hops", ch->robot_hops);
}

static hush_status_t hush_http_channel_policy(const char *body,
                                              const char *slug)
{
    const hush_launch_channel_t *ch;
    hush_launch_policy_t policy;

    ch = hush_http_find_channel(slug);
    if (ch == NULL)
        return HUSH_ERR_NOT_FOUND;
    memset(&policy, 0, sizeof(policy));
    hush_http_fill_policy_text(&policy, ch, body);
    hush_http_fill_policy_nums(&policy, ch, body);
    return hush_launch_set_channel_policy(g_launch, slug, &policy);
}

static void hush_http_collect_indexed(const char *body, const char *stem,
                                      char (*out)[HUSH_IDENTITY_NPUB_MAX],
                                      size_t *out_n, size_t maxn)
{
    char key[32];
    size_t i;

    assert(out != NULL);
    assert(out_n != NULL);
    *out_n = 0;
    if (body == NULL || stem == NULL)
        return;
    for (i = 0; i < maxn; ++i) {
        if (snprintf(key, sizeof(key), "%s_%zu", stem, i) >= (int)sizeof(key))
            break;
        if (!hush_json_field(body, key, out[*out_n], HUSH_IDENTITY_NPUB_MAX))
            continue;
        (*out_n)++;
    }
}

static void hush_http_add_reply_to(hush_event_t *out, const char *body)
{
    char reply_to[HUSH_EVENT_ID_HEX_LEN + 1];

    assert(out != NULL);
    if (body == NULL)
        return;
    if (out->tag_count >= (size_t)HUSH_EVENT_MAX_TAGS)
        return;
    if (!hush_json_field(body, "reply_to", reply_to, sizeof(reply_to)))
        return;
    if (reply_to[0] == '\0')
        return;
    memcpy(out->tags[out->tag_count][0], "e", 2);
    memcpy(out->tags[out->tag_count][1], reply_to, strlen(reply_to) + 1);
    out->tag_count++;
}

static void hush_http_add_mentions(hush_event_t *out, const char *body)
{
    char key[24];
    char mention[HUSH_IDENTITY_NPUB_MAX];
    size_t i;

    assert(out != NULL);
    if (body == NULL)
        return;
    for (i = 0; i < (size_t)HUSH_HTTP_MENTIONS_MAX; ++i) {
        if (out->tag_count >= (size_t)HUSH_EVENT_MAX_TAGS)
            return;
        if (snprintf(key, sizeof(key), "mention_%zu", i) >= (int)sizeof(key))
            return;
        if (!hush_json_field(body, key, mention, sizeof(mention)))
            continue;
        memcpy(out->tags[out->tag_count][0], "p", 2);
        memcpy(out->tags[out->tag_count][1], mention, strlen(mention) + 1);
        out->tag_count++;
    }
}

static void hush_http_event_reply_to(char *out, size_t outsz,
                                     const hush_event_t *ev)
{
    size_t i;

    assert(out != NULL);
    assert(outsz > 0);
    out[0] = '\0';
    if (ev == NULL)
        return;
    for (i = 0; i < ev->tag_count; i++) {
        if (strcmp(ev->tags[i][0], "e") != 0)
            continue;
        if (ev->tags[i][1][0] == '\0')
            continue;
        if (strlen(ev->tags[i][1]) >= outsz)
            return;
        memcpy(out, ev->tags[i][1], strlen(ev->tags[i][1]) + 1);
        return;
    }
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
    if (!hush_json_field(body, "project", slug, sizeof(slug)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    if (!hush_json_field(body, "path", rel, sizeof(rel)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    if (!hush_json_field(body, "content", content, sizeof(content)))
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
    (void)hush_json_field(body, "instruction", ask, sizeof(ask));
    (void)hush_json_field(body, "text", text, sizeof(text));
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
    (void)hush_json_field(body, "prefix", prefix, sizeof(prefix));
    (void)hush_json_field(body, "suffix", suffix, sizeof(suffix));
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
    if (strcmp(path, "/api/fixup") == 0)
        return hush_http_serve_fixup(fd, hush_http_body(req, len));
    if (strcmp(path, "/api/complete") == 0)
        return hush_http_serve_complete_post(fd, hush_http_body(req, len));
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

    if (!hush_json_field(body, "action", action, sizeof(action))) {
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
    if (strcmp(action, HUSH_HTTP_WINDOW_MIN) == 0)
        return hush_win_minimize();
    if (strcmp(action, HUSH_HTTP_WINDOW_MAX) == 0)
        return hush_win_maximize();
    return HUSH_ERR_ARG;
}

static void hush_http_reply_window(int fd, const char *action)
{
    assert(action != NULL);
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

static void hush_http_append_provider(char *body, size_t bodysz, size_t *n,
                                      const hush_provider_status_t *st,
                                      int first)
{
    char host[HUSH_PROVIDER_HOST_MAX * 2];
    char model[HUSH_PROVIDER_MODEL_MAX * 2];
    char home_model[HUSH_PROVIDER_MODEL_MAX * 2];
    int wr;

    assert(body != NULL);
    assert(n != NULL);
    assert(st != NULL);
    hush_json_escape(st->host, host, sizeof(host));
    hush_json_escape(st->model, model, sizeof(model));
    hush_json_escape(st->home_model, home_model, sizeof(home_model));
    wr = snprintf(body + *n, bodysz - *n,
                  "%s\"%s\":{\"label\":\"%s\",\"family\":\"%s\","
                  "\"has_binary\":%s,\"has_home\":%s,\"has_key\":%s,"
                  "\"has_username\":%s,\"has_password\":%s,"
                  "\"has_token\":%s,\"has_passkey\":%s,"
                  "\"use_home\":%s,\"host\":\"%s\",\"model\":\"%s\","
                  "\"home_model\":\"%s\",\"configured\":%s}",
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
                  st->configured ? "true" : "false");
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
    if (hush_json_field(body, kind, buf, HUSH_PROVIDER_KEY_MAX))
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
    (void)hush_json_field(body, "host", in->host, sizeof(in->host));
    (void)hush_json_field(body, "model", in->model, sizeof(in->model));
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
    if (hush_json_field(body, "use_home", flag, sizeof(flag)))
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
    if (!hush_json_field(body, "provider", in.id, sizeof(in.id))) {
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

    if (!hush_json_field(body, "provider", id, sizeof(id))) {
        hush_http_reply(fd, "400 Bad Request", "text/plain", "bad request\n", 12);
        return HUSH_ERR_PARSE;
    }
    host[0] = '\0';
    key[0] = '\0';
    (void)hush_json_field(body, "host", host, sizeof(host));
    (void)hush_json_field(body, "api_key", key, sizeof(key));
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

    if (!hush_json_field(body, "provider", id, sizeof(id))) {
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
