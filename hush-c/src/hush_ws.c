/* hush_ws.c: owns the RFC 6455 handshake and frame codec. */

#include <assert.h>
#include <limits.h>
#include <openssl/evp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "hush_http.h"
#include "hush_ws.h"

enum {
    HUSH_WS_FIN_BIT = 0x80,
    HUSH_WS_RSV_MASK = 0x70,
    HUSH_WS_MASK_BIT = 0x80,
    HUSH_WS_OPCODE_MASK = 0x0F,
    HUSH_WS_LEN_MASK = 0x7F,
    HUSH_WS_LEN_16 = 126,
    HUSH_WS_LEN_64 = 127,
    HUSH_WS_EXT16_BYTES = 2,
    HUSH_WS_EXT64_BYTES = 8,
    HUSH_WS_MASK_BYTES = 4,
    HUSH_WS_MIN_HEADER = 2,
    HUSH_WS_SHA1_BYTES = 20,
    HUSH_WS_HEADER_MAX = 128,
    HUSH_WS_UPGRADE_TOKEN_LEN = 7
};

#define HUSH_WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

/* Encodes raw_len bytes as base64 into out. FULL on a short buffer. */
static hush_status_t hush_ws_base64(const unsigned char *raw, size_t raw_len,
                                    char *out, size_t outsz);

/* True when connection lists an upgrade token (case-insensitive). */
static int hush_ws_has_upgrade_token(const char *connection);

/* True when opcode is a defined RFC 6455 opcode. */
static int hush_ws_opcode_ok(unsigned int opcode);

/* Reads a big-endian uint16. */
static uint16_t hush_ws_read_u16(const unsigned char *p);

/* Reads a big-endian uint64. */
static uint64_t hush_ws_read_u64(const unsigned char *p);

/* Writes value as a big-endian uint16 at out. */
static void hush_ws_put_u16(char *out, uint16_t value);

/* Writes value as a big-endian uint64 at out. */
static void hush_ws_put_u64(char *out, uint64_t value);

/* Header bytes needed for a payload of this length. */
static size_t hush_ws_frame_header_len(size_t payload_len);

/* Writes the length field (1/3/9 bytes) at out. */
static void hush_ws_write_length(char *out, size_t payload_len);

/* XORs the masking key into payload in place. */
static void hush_ws_mask_apply(unsigned char *payload, size_t len,
                               const unsigned char mask[HUSH_WS_MASK_BYTES]);

/* Reads the payload length after b1, advancing *off past the length bytes.
 * NEED_MORE on a truncated header, ERROR on non-minimal encodings, TOO_BIG
 * past the message cap. */
static hush_ws_parse_t hush_ws_parse_length(const unsigned char *data,
                                            size_t len, unsigned char b1,
                                            size_t *off, size_t *out_plen);

/* True when a control frame meets the FIN/len/shape rules. */
static int hush_ws_control_ok(int fin, unsigned int opcode, size_t payload_len);

/* True when b is a UTF-8 continuation byte. */
static int hush_ws_utf8_cont(unsigned char b);

/* Bytes consumed by one well-formed UTF-8 sequence at p, else 0. */
static size_t hush_ws_utf8_seq(const unsigned char *p, size_t remain);

hush_status_t hush_ws_accept(const char *key, char *out, size_t outsz)
{
    char combined[HUSH_WS_KEY_LEN + sizeof(HUSH_WS_GUID)];
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    EVP_MD_CTX *ctx;
    size_t key_len;
    int n;

    if (key == NULL || out == NULL)
        return HUSH_ERR_ARG;
    key_len = strlen(key);
    if (key_len > (size_t)HUSH_WS_KEY_LEN)
        return HUSH_ERR_ARG;
    n = snprintf(combined, sizeof(combined), "%s%s", key, HUSH_WS_GUID);
    if (n <= 0 || (size_t)n >= sizeof(combined))
        return HUSH_ERR_FULL;
    ctx = EVP_MD_CTX_new();
    if (ctx == NULL)
        return HUSH_ERR_CRYPTO;
    if (EVP_DigestInit_ex(ctx, EVP_sha1(), NULL) != 1 ||
        EVP_DigestUpdate(ctx, combined, strlen(combined)) != 1 ||
        EVP_DigestFinal_ex(ctx, digest, &digest_len) != 1) {
        EVP_MD_CTX_free(ctx);
        return HUSH_ERR_CRYPTO;
    }
    EVP_MD_CTX_free(ctx);
    assert(digest_len == (unsigned int)HUSH_WS_SHA1_BYTES);
    return hush_ws_base64(digest, digest_len, out, outsz);
}

hush_status_t hush_ws_handshake_take(const char *req, size_t len,
                                     hush_ws_handshake_out_t *out)
{
    char upgrade[HUSH_WS_HEADER_MAX];
    char connection[HUSH_WS_HEADER_MAX];
    char version[HUSH_WS_HEADER_MAX];
    char key[HUSH_WS_KEY_LEN + 1];
    const char *end;
    size_t key_len;

    if (req == NULL || out == NULL || out->key == NULL)
        return HUSH_ERR_ARG;
    if (!hush_http_header_value(req, len, "Upgrade", upgrade,
                                sizeof(upgrade)) ||
        strcasecmp(upgrade, "websocket") != 0)
        return HUSH_ERR_DENIED;
    if (!hush_http_header_value(req, len, "Connection", connection,
                                sizeof(connection)) ||
        !hush_ws_has_upgrade_token(connection))
        return HUSH_ERR_DENIED;
    if (!hush_http_header_value(req, len, "Sec-WebSocket-Version", version,
                                sizeof(version)) ||
        strcmp(version, "13") != 0)
        return HUSH_ERR_DENIED;
    if (!hush_http_header_value(req, len, "Sec-WebSocket-Key", key,
                                sizeof(key)))
        return HUSH_ERR_DENIED;
    key_len = strlen(key);
    if (key_len != (size_t)HUSH_WS_KEY_LEN || key_len + 1 > out->key_size)
        return HUSH_ERR_DENIED;
    end = hush_http_headers_end(req, len);
    if (end == NULL)
        return HUSH_ERR_DENIED;
    memcpy(out->key, key, key_len + 1);
    out->req_len = (size_t)(end - req) + 4;
    return HUSH_OK;
}

hush_status_t hush_ws_format_reply(const char *accept, char *out, size_t outsz,
                                   size_t *out_written)
{
    int n;

    if (accept == NULL || out == NULL)
        return HUSH_ERR_ARG;
    n = snprintf(out, outsz,
                 "HTTP/1.1 101 Switching Protocols\r\n"
                 "Upgrade: websocket\r\n"
                 "Connection: Upgrade\r\n"
                 "Sec-WebSocket-Accept: %s\r\n"
                 "\r\n",
                 accept);
    if (n <= 0 || (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    if (out_written != NULL)
        *out_written = (size_t)n;
    return HUSH_OK;
}

hush_status_t hush_ws_format_frame(const hush_ws_frame_out_t *request,
                                   char *out, size_t outsz,
                                   size_t *out_written)
{
    size_t header_len;
    size_t total;

    if (request == NULL || out == NULL)
        return HUSH_ERR_ARG;
    if (request->payload == NULL && request->payload_len != 0)
        return HUSH_ERR_ARG;
    if (request->opcode > (unsigned int)HUSH_WS_OPCODE_MASK)
        return HUSH_ERR_ARG;
    header_len = hush_ws_frame_header_len(request->payload_len);
    total = header_len + request->payload_len;
    if (total > outsz)
        return HUSH_ERR_FULL;
    out[0] = (char)(HUSH_WS_FIN_BIT |
                    (request->opcode & (unsigned int)HUSH_WS_OPCODE_MASK));
    hush_ws_write_length(out + 1, request->payload_len);
    if (request->payload_len != 0)
        memcpy(out + header_len, request->payload, request->payload_len);
    if (out_written != NULL)
        *out_written = total;
    return HUSH_OK;
}

hush_ws_parse_t hush_ws_parse_frame(unsigned char *data, size_t len,
                                    size_t *consumed, hush_ws_frame_t *out)
{
    unsigned char b0;
    unsigned char b1;
    unsigned char mask[HUSH_WS_MASK_BYTES];
    hush_ws_parse_t length_status;
    size_t off = (size_t)HUSH_WS_MIN_HEADER;
    size_t payload_len = 0;

    if (data == NULL || consumed == NULL || out == NULL)
        return HUSH_WS_PARSE_ERROR;
    if (len < (size_t)HUSH_WS_MIN_HEADER)
        return HUSH_WS_PARSE_NEED_MORE;
    b0 = data[0];
    b1 = data[1];
    if ((b0 & (unsigned char)HUSH_WS_RSV_MASK) != 0)
        return HUSH_WS_PARSE_ERROR;
    if (!hush_ws_opcode_ok(b0 & (unsigned char)HUSH_WS_OPCODE_MASK))
        return HUSH_WS_PARSE_ERROR;
    if ((b1 & (unsigned char)HUSH_WS_MASK_BIT) == 0)
        return HUSH_WS_PARSE_ERROR;
    length_status = hush_ws_parse_length(data, len, b1, &off, &payload_len);
    if (length_status != HUSH_WS_PARSE_OK)
        return length_status;
    if (len < off + (size_t)HUSH_WS_MASK_BYTES)
        return HUSH_WS_PARSE_NEED_MORE;
    memcpy(mask, data + off, sizeof(mask));
    off += (size_t)HUSH_WS_MASK_BYTES;
    if (len < off + payload_len)
        return HUSH_WS_PARSE_NEED_MORE;
    hush_ws_mask_apply(data + off, payload_len, mask);
    out->fin = (b0 & (unsigned char)HUSH_WS_FIN_BIT) != 0;
    out->opcode = (unsigned int)(b0 & (unsigned char)HUSH_WS_OPCODE_MASK);
    out->payload = data + off;
    out->payload_len = payload_len;
    *consumed = off + payload_len;
    if (!hush_ws_control_ok(out->fin, out->opcode, out->payload_len))
        return HUSH_WS_PARSE_ERROR;
    return HUSH_WS_PARSE_OK;
}

int hush_ws_utf8_ok(const char *text, size_t len)
{
    size_t idx = 0;

    if (text == NULL && len != 0)
        return 0;
    while (idx < len) {
        size_t seq = hush_ws_utf8_seq((const unsigned char *)text + idx,
                                      len - idx);

        if (seq == 0)
            return 0;
        idx += seq;
    }
    return 1;
}

static hush_status_t hush_ws_base64(const unsigned char *raw, size_t raw_len,
                                    char *out, size_t outsz)
{
    int n;

    assert(raw != NULL);
    assert(out != NULL);
    if (raw_len > (size_t)(INT_MAX / 4) * 3)
        return HUSH_ERR_FULL;
    n = EVP_EncodeBlock((unsigned char *)out, raw, (int)raw_len);
    if (n < 0 || (size_t)n + 1 > outsz)
        return HUSH_ERR_FULL;
    out[n] = '\0';
    return HUSH_OK;
}

static int hush_ws_has_upgrade_token(const char *connection)
{
    const char *p = connection;

    assert(connection != NULL);
    while (*p != '\0') {
        size_t token_len;

        while (*p == ' ' || *p == ',')
            p++;
        token_len = 0;
        while (p[token_len] != '\0' && p[token_len] != ',' &&
               p[token_len] != ' ')
            token_len++;
        if (token_len == (size_t)HUSH_WS_UPGRADE_TOKEN_LEN &&
            strncasecmp(p, "upgrade", (size_t)HUSH_WS_UPGRADE_TOKEN_LEN) == 0)
            return 1;
        p += token_len;
    }
    return 0;
}

static int hush_ws_opcode_ok(unsigned int opcode)
{
    switch (opcode) {
    case HUSH_WS_OP_CONT:
    case HUSH_WS_OP_TEXT:
    case HUSH_WS_OP_BINARY:
    case HUSH_WS_OP_CLOSE:
    case HUSH_WS_OP_PING:
    case HUSH_WS_OP_PONG:
        return 1;
    default:
        return 0;
    }
}

static uint16_t hush_ws_read_u16(const unsigned char *p)
{
    assert(p != NULL);
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint64_t hush_ws_read_u64(const unsigned char *p)
{
    uint64_t value = 0;
    size_t idx;

    assert(p != NULL);
    for (idx = 0; idx < (size_t)HUSH_WS_EXT64_BYTES; ++idx)
        value = (value << 8) | (uint64_t)p[idx];
    return value;
}

static void hush_ws_put_u16(char *out, uint16_t value)
{
    assert(out != NULL);
    out[0] = (char)((value >> 8) & 0xFFu);
    out[1] = (char)(value & 0xFFu);
}

static void hush_ws_put_u64(char *out, uint64_t value)
{
    size_t idx;

    assert(out != NULL);
    for (idx = 0; idx < (size_t)HUSH_WS_EXT64_BYTES; ++idx) {
        out[HUSH_WS_EXT64_BYTES - 1 - idx] = (char)(value & 0xFFu);
        value >>= 8;
    }
}

static size_t hush_ws_frame_header_len(size_t payload_len)
{
    if (payload_len < (size_t)HUSH_WS_LEN_16)
        return (size_t)HUSH_WS_MIN_HEADER;
    if (payload_len <= (size_t)UINT16_MAX)
        return (size_t)HUSH_WS_MIN_HEADER + (size_t)HUSH_WS_EXT16_BYTES;
    return (size_t)HUSH_WS_MIN_HEADER + (size_t)HUSH_WS_EXT64_BYTES;
}

static void hush_ws_write_length(char *out, size_t payload_len)
{
    assert(out != NULL);
    if (payload_len < (size_t)HUSH_WS_LEN_16) {
        out[0] = (char)payload_len;
        return;
    }
    if (payload_len <= (size_t)UINT16_MAX) {
        out[0] = (char)HUSH_WS_LEN_16;
        hush_ws_put_u16(out + 1, (uint16_t)payload_len);
        return;
    }
    out[0] = (char)HUSH_WS_LEN_64;
    hush_ws_put_u64(out + 1, (uint64_t)payload_len);
}

static void hush_ws_mask_apply(unsigned char *payload, size_t len,
                               const unsigned char mask[HUSH_WS_MASK_BYTES])
{
    size_t idx;

    assert(payload != NULL || len == 0);
    assert(mask != NULL);
    for (idx = 0; idx < len; ++idx)
        payload[idx] ^= mask[idx & ((size_t)HUSH_WS_MASK_BYTES - 1)];
}

static hush_ws_parse_t hush_ws_parse_length(const unsigned char *data,
                                            size_t len, unsigned char b1,
                                            size_t *off, size_t *out_plen)
{
    size_t payload_len = b1 & (unsigned char)HUSH_WS_LEN_MASK;

    assert(data != NULL);
    assert(off != NULL);
    assert(out_plen != NULL);
    if (payload_len == (size_t)HUSH_WS_LEN_16) {
        if (len < *off + (size_t)HUSH_WS_EXT16_BYTES)
            return HUSH_WS_PARSE_NEED_MORE;
        payload_len = (size_t)hush_ws_read_u16(data + *off);
        *off += (size_t)HUSH_WS_EXT16_BYTES;
        if (payload_len < (size_t)HUSH_WS_LEN_16)
            return HUSH_WS_PARSE_ERROR;
    } else if (payload_len == (size_t)HUSH_WS_LEN_64) {
        uint64_t big;

        if (len < *off + (size_t)HUSH_WS_EXT64_BYTES)
            return HUSH_WS_PARSE_NEED_MORE;
        big = hush_ws_read_u64(data + *off);
        *off += (size_t)HUSH_WS_EXT64_BYTES;
        if (big < (uint64_t)1 << 16)
            return HUSH_WS_PARSE_ERROR;
        payload_len = (size_t)big;
    }
    if (payload_len > (size_t)HUSH_WS_MSG_MAX)
        return HUSH_WS_PARSE_TOO_BIG;
    *out_plen = payload_len;
    return HUSH_WS_PARSE_OK;
}

static int hush_ws_control_ok(int fin, unsigned int opcode, size_t payload_len)
{
    if (opcode < (unsigned int)HUSH_WS_OP_CLOSE)
        return 1;
    if (!fin || payload_len > (size_t)HUSH_WS_CONTROL_MAX)
        return 0;
    if (opcode == (unsigned int)HUSH_WS_OP_CLOSE && payload_len == 1)
        return 0;
    return 1;
}

static int hush_ws_utf8_cont(unsigned char b)
{
    return (b & 0xC0u) == 0x80u;
}

static size_t hush_ws_utf8_seq(const unsigned char *p, size_t remain)
{
    unsigned char b0;

    assert(p != NULL);
    if (remain == 0)
        return 0;
    b0 = p[0];
    if (b0 <= 0x7Fu)
        return 1;
    if (b0 >= 0xC2u && b0 <= 0xDFu && remain >= 2 &&
        hush_ws_utf8_cont(p[1]))
        return 2;
    if (b0 >= 0xE0u && b0 <= 0xEFu && remain >= 3 &&
        hush_ws_utf8_cont(p[1]) && hush_ws_utf8_cont(p[2])) {
        if (b0 == 0xE0u && p[1] < 0xA0u)
            return 0;
        if (b0 == 0xEDu && p[1] >= 0xA0u)
            return 0;
        return 3;
    }
    if (b0 >= 0xF0u && b0 <= 0xF4u && remain >= 4 &&
        hush_ws_utf8_cont(p[1]) && hush_ws_utf8_cont(p[2]) &&
        hush_ws_utf8_cont(p[3])) {
        if (b0 == 0xF0u && p[1] < 0x90u)
            return 0;
        if (b0 == 0xF4u && p[1] >= 0x90u)
            return 0;
        return 4;
    }
    return 0;
}
