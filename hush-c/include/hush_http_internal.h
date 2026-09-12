/* hush_http_internal.h: shared surface between the HTTP core and its
 * per-family API modules. Internal to the relay; not installed. */

#ifndef HUSH_HTTP_INTERNAL_H
#define HUSH_HTTP_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "hush_event.h"
#include "hush_launch.h"

enum {
    HUSH_HTTP_PATH_MAX = 128
};
#include "hush_status.h"
#include "hush_store.h"
#include "hush_turn.h"

/* Borrowed live launch state. Never NULL once the relay prepared. */
hush_launch_t *hush_http_launch(void);

/* Borrowed live TURN state. Never NULL once the relay prepared. */
hush_turn_t *hush_http_turn(void);

/* Live client count and listen port for the status route. */
int hush_http_client_count(void);
uint16_t hush_http_listen_port(void);

/* Writes one response with an optional session cookie. */
void hush_http_reply(int fd, const char *status, const char *ctype,
                     const char *body, size_t blen);

/* Serializes the session JSON reply for st. */
hush_status_t hush_http_reply_session(int fd, hush_status_t st);

/* Writes required bytes with bounded backpressure; IO on a stalled reader. */
hush_status_t hush_http_write_all(int fd, const char *buf, size_t len);

/* Points at the request body, or "" when absent. */
const char *hush_http_body(const char *req, size_t len);

/* Returns the borrowed h-tag value regardless of tag order, or general. */
const char *hush_http_event_channel(const hush_event_t *event);

/* Writes the event's reply-to root id into out. */
void hush_http_event_reply_to(char *out, size_t outsz,
                              const hush_event_t *event);

/* Copies the JSON string at key into out. 0 when absent or non-string. */
int hush_http_json_field(const char *body, const char *key, char *out,
                         size_t outsz);

/* True when body contains key. */
int hush_http_json_has_key(const char *body, const char *key);

/* Copies the JSON scalar at key (no quotes) into out. 0 when absent. */
int hush_http_json_bare_field(const char *body, const char *key, char *out,
                              size_t outsz);

/* Decodes JSON escapes in src into dst. */
void hush_http_json_unescape_copy(const char *src, char *dst, size_t dstsz);

/* Serves a PWA asset or icon panel for path. 0 when path is not ours. */
int hush_http_serve_asset(int fd, const char *path);

/* api_status.c entry points. */
void hush_http_serve_status(int fd, const hush_store_t *store);
void hush_http_serve_events(int fd, const hush_store_t *store);
void hush_http_serve_session(int fd);
void hush_http_serve_chan_events(int fd, const char *req);
void hush_http_serve_presence_get(int fd);
hush_status_t hush_http_serve_presence_post(int fd, const char *body,
                                            hush_store_t *store);

/* True when body requests pass-backed backup. */
int hush_http_want_save_pass(const char *body);

/* api_identity.c entry points. */
hush_status_t hush_http_serve_identity(int fd, const char *body);
hush_status_t hush_http_serve_profile(int fd, const char *body);
hush_status_t hush_http_serve_member(int fd, const char *body);
hush_status_t hush_http_serve_vibe(int fd, const char *body,
                                   hush_store_t *store);

/* api_agents.c entry points. */
hush_status_t hush_http_serve_agent(int fd, const char *body,
                                    hush_store_t *store);
void hush_http_serve_skills_get(int fd);
hush_status_t hush_http_serve_skill_post(int fd, const char *body);
hush_status_t hush_http_serve_skillui(int fd, const char *body);

/* api_channels.c entry points. */
hush_status_t hush_http_serve_post(int fd, const char *req, size_t len,
                                   hush_store_t *store, hush_event_t *out);
hush_status_t hush_http_serve_channel(int fd, const char *body);
hush_status_t hush_http_serve_group(int fd, const char *body);
hush_status_t hush_http_serve_project(int fd, const char *body,
                                      hush_store_t *store);
hush_status_t hush_http_serve_signal(int fd, const char *body,
                                     hush_store_t *store,
                                     hush_event_t *out);

#endif /* HUSH_HTTP_INTERNAL_H */
