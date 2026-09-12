/* api_canvas.c: owns canvas FIM and the one-shot provider routes. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

#include "hush_agent.h"
#include "hush_canvas.h"
#include "hush_http_internal.h"
#include "hush_store.h"

#define HUSH_HTTP_CANVAS_OK "{\"ok\":true}\n"

#define HUSH_HTTP_FIXUP_FAIL "{\"ok\":false,\"error\":\"fixup failed\"}\n"
#define HUSH_HTTP_COMPLETE_FAIL "{\"ok\":false,\"error\":\"complete failed\"}\n"
#define HUSH_HTTP_COMPLETE_PEND "{\"ok\":true,\"pending\":true}\n"

enum {
    HUSH_HTTP_CANVAS_PATH_MAX = 384,
    HUSH_HTTP_FIXUP_ASK_MAX = 500,
    HUSH_HTTP_FIXUP_JSON_MAX = 16384,
    HUSH_HTTP_FIXUP_SLEEP_NS = 50000000,
    HUSH_HTTP_FIXUP_WAIT_MAX = 1800,
    HUSH_HTTP_FIXUP_TOKEN_MAX = 16,
    HUSH_HTTP_COMPLETE_JSON_MAX = 1536
};

static const hush_launch_project_t *hush_http_find_project(const char *slug);
static int hush_http_canvas_rel_ok(const char *rel);
static hush_status_t hush_http_canvas_join(char *out, size_t outsz,
                                           const char *root, const char *rel);
static hush_status_t hush_http_canvas_write(const char *path,
                                            const char *content);
static void hush_http_reply_fixup_ok(int fd, const char *text);
static void hush_http_complete_token(char *out, size_t outsz, const char *req);
static void hush_http_reply_complete_token(int fd, const char *token);
static void hush_http_reply_complete_text(int fd, const char *text);
static hush_status_t hush_http_wait_fixup(const char *token, char *out,
                                          size_t outsz);
static void hush_http_pause_fixup(void);

static const hush_launch_project_t *hush_http_find_project(const char *slug)
{
    size_t i;

    if (hush_http_launch() == NULL || slug == NULL || slug[0] == '\0')
        return NULL;
    for (i = 0; i < hush_http_launch()->nprojects; i++) {
        if (strcmp(hush_http_launch()->projects[i].slug, slug) == 0)
            return &hush_http_launch()->projects[i];
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

hush_status_t hush_http_serve_canvas(int fd, const char *body)
{
    char slug[HUSH_LAUNCH_NAME_MAX];
    char rel[HUSH_LAUNCH_PATH_MAX];
    char content[HUSH_EVENT_MAX_CONTENT + 1];
    char path[HUSH_HTTP_CANVAS_PATH_MAX];
    const hush_launch_project_t *proj;
    hush_status_t st;

    if (hush_http_launch() == NULL || body == NULL)
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

hush_status_t hush_http_serve_fixup(int fd, const char *body)
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

hush_status_t hush_http_serve_complete_post(int fd, const char *body)
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

hush_status_t hush_http_serve_complete_get(int fd, const char *req)
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


hush_status_t hush_http_serve_reply(int fd, const char *body)
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
hush_status_t hush_http_serve_cancel(int fd, const char *body)
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

