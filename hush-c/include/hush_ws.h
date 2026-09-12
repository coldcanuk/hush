/* hush_ws.h: RFC 6455 WebSocket handshake and frame codec for the wire. */

#ifndef HUSH_WS_H
#define HUSH_WS_H

#include <stddef.h>

#include "hush_status.h"

enum {
    HUSH_WS_OP_CONT     = 0x0,
    HUSH_WS_OP_TEXT     = 0x1,
    HUSH_WS_OP_BINARY   = 0x2,
    HUSH_WS_OP_CLOSE    = 0x8,
    HUSH_WS_OP_PING     = 0x9,
    HUSH_WS_OP_PONG     = 0xA,
    HUSH_WS_CLOSE_NORMAL      = 1000,
    HUSH_WS_CLOSE_PROTOCOL    = 1002,
    HUSH_WS_CLOSE_UNSUPPORTED = 1003,
    HUSH_WS_CLOSE_INVALID     = 1007,
    HUSH_WS_CLOSE_TOO_BIG     = 1009,
    HUSH_WS_CLOSE_INTERNAL    = 1011,
    HUSH_WS_KEY_LEN     = 24,
    HUSH_WS_ACCEPT_LEN  = 28,
    HUSH_WS_CONTROL_MAX = 125,
    /* One wire message cap; mirrors the line protocol's receive bound. */
    HUSH_WS_MSG_MAX     = 32768
};

/* One parsed client frame. payload borrows the caller's buffer (the masking
 * key has been applied in place) and stays valid until that buffer moves. */
typedef struct {
    int fin;
    unsigned int opcode;
    unsigned char *payload;
    size_t payload_len;
} hush_ws_frame_t;

/* One outbound server frame. */
typedef struct {
    unsigned int opcode;
    const char *payload;
    size_t payload_len;
} hush_ws_frame_out_t;

/* One handshake parse result. */
typedef struct {
    size_t req_len;       /* out: handshake byte length through the blank line */
    char *key;            /* in/out: buffer for the 24-char base64 key */
    size_t key_size;      /* in: key buffer capacity */
} hush_ws_handshake_out_t;

typedef enum {
    HUSH_WS_PARSE_OK = 0,
    HUSH_WS_PARSE_NEED_MORE = 1,
    HUSH_WS_PARSE_ERROR = 2,
    HUSH_WS_PARSE_TOO_BIG = 3
} hush_ws_parse_t;

/* Computes Sec-WebSocket-Accept = base64(SHA-1(key || WS_GUID)). Fails
 * HUSH_ERR_ARG on NULL or an overlong key, HUSH_ERR_CRYPTO on a digest
 * failure, HUSH_ERR_FULL on a short output buffer. */
hush_status_t hush_ws_accept(const char *key, char *out, size_t outsz);

/* Validates a client upgrade handshake: Upgrade websocket, an upgrade token
 * in Connection, a 24-char base64 key, version 13. On success writes the
 * handshake length and copies the key into out. DENIED on any missing or
 * mismatched field. */
hush_status_t hush_ws_handshake_take(const char *req, size_t len,
                                     hush_ws_handshake_out_t *out);

/* Serializes the 101 Switching Protocols response for accept. Fails
 * HUSH_ERR_FULL on a short buffer. */
hush_status_t hush_ws_format_reply(const char *accept, char *out, size_t outsz,
                                   size_t *out_written);

/* Serializes one unmasked server frame. Fails HUSH_ERR_ARG on NULL or an
 * opcode above 0xF, HUSH_ERR_FULL on a short buffer. */
hush_status_t hush_ws_format_frame(const hush_ws_frame_out_t *request,
                                   char *out, size_t outsz,
                                   size_t *out_written);

/* Parses one client frame from data, unmasking its payload in place.
 * NEED_MORE when the frame is incomplete; ERROR on protocol violations
 * (unmasked frame, reserved bits, unknown opcode, malformed control frames,
 * non-minimal length); TOO_BIG when the payload exceeds HUSH_WS_MSG_MAX.
 * Advances *consumed only on OK. */
hush_ws_parse_t hush_ws_parse_frame(unsigned char *data, size_t len,
                                    size_t *consumed, hush_ws_frame_t *out);

/* True when text is well-formed UTF-8 per RFC 3629. */
int hush_ws_utf8_ok(const char *text, size_t len);

#endif /* HUSH_WS_H */
