/* hush_proto.h: minimal Nostr wire protocol parser/serializer for Hush (newline JSON arrays). */

#ifndef HUSH_PROTO_H
#define HUSH_PROTO_H

#include <stddef.h>
#include "hush_event.h"
#include "hush_filter.h"
#include "hush_status.h"

typedef enum {
    HUSH_MSG_EVENT,
    HUSH_MSG_REQ,
    HUSH_MSG_CLOSE,
    HUSH_MSG_COUNT,
    HUSH_MSG_UNKNOWN
} hush_msg_type_t;

typedef struct {
    hush_msg_type_t type;
    char sub_id[256 + 1];
    hush_event_t event;          /* valid for EVENT */
    hush_filter_t filters[4];
    size_t nfilters;
} hush_client_msg_t;

/* Parse one line (NUL or \n terminated) into msg. Limited shapes only. */
hush_status_t hush_proto_parse_line(const char *line, hush_client_msg_t *out_msg);

/* Serialize ["EVENT", sub, event_json-ish] for fanout. */
hush_status_t hush_proto_format_event(const char *sub_id, const hush_event_t *ev,
                                      char *out_buf, size_t bufsz, size_t *out_written);

/* Serialize ["OK", id, true|false, "msg"]. */
hush_status_t hush_proto_format_ok(const char *ev_id, int ok, const char *msg,
                                   char *out_buf, size_t bufsz, size_t *out_written);

/* Serialize ["EOSE", sub]. */
hush_status_t hush_proto_format_eose(const char *sub_id, char *out_buf, size_t bufsz,
                                     size_t *out_written);

#endif /* HUSH_PROTO_H */
