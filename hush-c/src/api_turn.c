/* api_turn.c: owns the coturn control and ICE routes. */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hush_canvas.h"
#include "hush_http_internal.h"
#include "hush_turn.h"

void hush_http_serve_turn_get(int fd)
{
    char body[HUSH_TURN_JSON_MAX];
    size_t n = 0;
    hush_turn_t empty;

    if (hush_http_turn() == NULL) {
        hush_turn_init(&empty);
        if (hush_turn_format_status(&empty, body, sizeof(body), &n) != HUSH_OK)
            n = 0;
    } else {
        hush_turn_refresh(hush_http_turn());
        if (hush_turn_format_status(hush_http_turn(), body, sizeof(body), &n) != HUSH_OK)
            n = 0;
    }
    hush_http_reply(fd, "200 OK", "application/json", body, n);
}

void hush_http_serve_ice(int fd)
{
    char body[HUSH_TURN_JSON_MAX];
    size_t n = 0;
    hush_turn_t empty;

    if (hush_http_turn() == NULL) {
        hush_turn_init(&empty);
        if (hush_turn_format_ice(&empty, body, sizeof(body), &n) != HUSH_OK)
            n = 0;
    } else if (hush_turn_format_ice(hush_http_turn(), body, sizeof(body), &n) != HUSH_OK) {
        n = 0;
    }
    hush_http_reply(fd, "200 OK", "application/json", body, n);
}

hush_status_t hush_http_serve_turn_post(int fd, const char *body)
{
    char enabled[8];
    char daemon[8];
    char host[HUSH_TURN_HOST_MAX];
    hush_turn_mode_t mode;
    hush_status_t st;

    if (hush_http_turn() == NULL || body == NULL) {
        hush_http_reply(fd, "503 Service Unavailable", "text/plain",
                        "turn off\n", 9);
        return HUSH_ERR_NOT_FOUND;
    }
    if (hush_http_json_field(body, "host", host, sizeof(host)))
        (void)hush_turn_set_public_host(hush_http_turn(), host);
    if (hush_http_json_field(body, "enabled", enabled, sizeof(enabled)) &&
        (strcmp(enabled, "false") == 0 || strcmp(enabled, "0") == 0)) {
        st = hush_turn_disable(hush_http_turn());
        hush_http_serve_turn_get(fd);
        return st;
    }
    mode = HUSH_TURN_MODE_CHILD;
    if (hush_http_json_field(body, "daemon", daemon, sizeof(daemon)) &&
        (strcmp(daemon, "true") == 0 || strcmp(daemon, "1") == 0))
        mode = HUSH_TURN_MODE_DAEMON;
    st = hush_turn_enable(hush_http_turn(), mode);
    hush_http_serve_turn_get(fd);
    return (st == HUSH_OK) ? HUSH_OK : st;
}

