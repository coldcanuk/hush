/* hush_http.h: tiny HTTP adapter so the desktop launcher can open a UI. */

#ifndef HUSH_HTTP_H
#define HUSH_HTTP_H

#include <stddef.h>
#include <stdint.h>
#include "hush_event.h"
#include "hush_store.h"
#include "hush_status.h"

void hush_http_set_listen_port(uint16_t port);
void hush_http_set_client_count(int n);

/* 1 if the first bytes are an HTTP method. */
int hush_http_looks_like(const char *buf, size_t len);

/* 1 if headers (and Content-Length body, if any) are fully buffered. */
int hush_http_is_complete(const char *buf, size_t len);

/* Serve one request and close-friendly response. If POST /api/event succeeds,
 * copies the stored event into out_posted (id[0] != 0). */
hush_status_t hush_http_serve(int fd, const char *req, size_t len,
                              hush_store_t *store, hush_event_t *out_posted);

#endif /* HUSH_HTTP_H */
