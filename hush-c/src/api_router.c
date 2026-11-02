/* api_router.c: HTTP adapter for the Router graph. */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "hush_home.h"
#include "hush_http_internal.h"
#include "hush_json.h"
#include "hush_router.h"

enum {
    HUSH_API_ROUTER_BODY = 4096,
    HUSH_API_ROUTER_FIELD = 160
};

static const char *hush_api_router_name(hush_router_route_t route);
static hush_status_t hush_api_router_root(char *out, size_t outsz);
static void hush_api_router_fail(int fd, hush_status_t status);

hush_status_t hush_api_router_get(int fd)
{
    char root[HUSH_HOME_PATH_MAX];
    char body[HUSH_API_ROUTER_BODY];
    char line[HUSH_ROUTER_LINE_MAX];
    hush_router_graph_t graph;
    size_t off = 0;
    int n;

    if (hush_api_router_root(root, sizeof root) != HUSH_OK)
        return HUSH_ERR_FULL;
    /* A GET may write to disk: load creates router/lock, rolls forward a save
     * interrupted after its commit mark and drops unmarked staged files
     * (hush_router.h). On a read-only home it only reads; a 500 there means
     * a commit mark is pending. */
    if (hush_router_load(&graph, root) != HUSH_OK) {
        hush_api_router_fail(fd, HUSH_ERR_IO);
        return HUSH_ERR_IO;
    }
    if (hush_router_context_line(&graph, line, sizeof line) != HUSH_OK)
        return HUSH_ERR_FULL;
    n = snprintf(body, sizeof body, "{\"ok\":true,\"context\":\"%s\",\"items\":[",
                 line);
    if (n < 0 || (size_t)n >= sizeof body)
        return HUSH_ERR_FULL;
    off = (size_t)n;
    for (size_t i = 0; i < graph.nitems; ++i) {
        const hush_router_item_t *item = &graph.items[i];
        char title[HUSH_API_ROUTER_FIELD];
        char row[512];
        size_t room = sizeof body - off;

        if (hush_json_escape(item->title, title, sizeof title) == 0 &&
            item->title[0] != '\0')
            return HUSH_ERR_FULL;
        n = snprintf(row, sizeof row,
                     "%s{\"id\":\"%s\",\"title\":\"%s\",\"worker\":\"%s\"}",
                     i == 0 ? "" : ",", item->id, title, item->worker_id);
        if (n < 0 || (size_t)n >= sizeof row || (size_t)n >= room)
            return HUSH_ERR_FULL;
        memcpy(body + off, row, (size_t)n);
        off += (size_t)n;
    }
    if (off + 4 > sizeof body)
        return HUSH_ERR_FULL;
    memcpy(body + off, "]}\n", 4);
    off += 4;
    hush_http_reply(fd, "200 OK", "application/json", body, off);
    return HUSH_OK;
}

hush_status_t hush_api_router_post(int fd, const char *req, size_t len)
{
    const char *raw;
    char content[HUSH_API_ROUTER_FIELD];
    char channel[HUSH_ROUTER_CHANNEL_MAX];
    char specialist[HUSH_ROUTER_NAME_MAX];
    char root[HUSH_HOME_PATH_MAX];
    char body[HUSH_API_ROUTER_BODY];
    char line[HUSH_ROUTER_LINE_MAX * 2];
    hush_json_value_t value;
    hush_router_graph_t graph;
    hush_router_note_t note;
    hush_router_result_t result;
    hush_status_t status;
    int n;

    if (req == NULL)
        return HUSH_ERR_ARG;
    raw = hush_http_body(req, len);
    if (hush_json_lookup(&value, raw, "/content") != HUSH_OK ||
        hush_json_decode(content, sizeof content, &value) != HUSH_OK ||
        content[0] == '\0') {
        hush_api_router_fail(fd, HUSH_ERR_ARG);
        return HUSH_ERR_ARG;
    }
    channel[0] = '\0';
    specialist[0] = '\0';
    if (hush_json_lookup(&value, raw, "/channel") == HUSH_OK)
        (void)hush_json_decode(channel, sizeof channel, &value);
    if (hush_json_lookup(&value, raw, "/specialist") == HUSH_OK)
        (void)hush_json_decode(specialist, sizeof specialist, &value);
    if (hush_api_router_root(root, sizeof root) != HUSH_OK)
        return HUSH_ERR_FULL;
    if (hush_router_load(&graph, root) != HUSH_OK) {
        hush_api_router_fail(fd, HUSH_ERR_IO);
        return HUSH_ERR_IO;
    }
    note.text = content;
    note.channel = channel;
    note.specialist = specialist;
    note.now = (int64_t)time(NULL);
    status = hush_router_accept(&graph, &result, &note);
    if (status != HUSH_OK) {
        hush_api_router_fail(fd, status);
        return status;
    }
    if (hush_router_save(&graph, root) != HUSH_OK) {
        hush_api_router_fail(fd, HUSH_ERR_IO);
        return HUSH_ERR_IO;
    }
    if (hush_json_escape(result.line, line, sizeof line) == 0 &&
        result.line[0] != '\0')
        return HUSH_ERR_FULL;
    n = snprintf(body, sizeof body,
                 "{\"ok\":true,\"route\":\"%s\",\"id\":\"%s\",\"line\":\"%s\"}\n",
                 hush_api_router_name(result.route), result.item_id, line);
    if (n < 0 || (size_t)n >= sizeof body)
        return HUSH_ERR_FULL;
    hush_http_reply(fd, "200 OK", "application/json", body, (size_t)n);
    return HUSH_OK;
}

static const char *hush_api_router_name(hush_router_route_t route)
{
    switch (route) {
    case HUSH_ROUTER_ROUTE_LEGACY:
        return "legacy";
    case HUSH_ROUTER_ROUTE_ANSWER:
        return "answer";
    case HUSH_ROUTER_ROUTE_WORK:
        return "work";
    case HUSH_ROUTER_ROUTE_DECISION:
        return "decision";
    case HUSH_ROUTER_ROUTE_AMBIGUOUS:
        return "ambiguous";
    default:
        return "answer";
    }
}

static hush_status_t hush_api_router_root(char *out, size_t outsz)
{
    hush_home_root(out, outsz);
    if (out[0] == '\0')
        return HUSH_ERR_FULL;
    return HUSH_OK;
}

static void hush_api_router_fail(int fd, hush_status_t status)
{
    const char *msg = "{\"ok\":false}\n";
    const char *http = "400 Bad Request";

    if (status == HUSH_ERR_IO)
        http = "500 Internal Server Error";
    if (status == HUSH_ERR_FULL)
        http = "507 Insufficient Storage";
    hush_http_reply(fd, http, "application/json", msg, strlen(msg));
}
