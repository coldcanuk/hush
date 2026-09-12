/* tests/test_ws.c: RFC 6455 handshake and frame codec vectors. */

#include <stdio.h>
#include <string.h>

#include "hush_ws.h"

enum {
    HUSH_TEST_FRAME_MAX = HUSH_WS_MSG_MAX + 16
};

static int g_fail;

static void expect(int cond, const char *msg);

/* Builds a masked client frame: fin/opcode header, minimal length, zero mask
 * key, then payload. Returns the total length. */
static size_t build_masked(unsigned char *out, int fin, unsigned int opcode,
                           const unsigned char *payload, size_t payload_len);

int main(void)
{
    char text[HUSH_WS_MSG_MAX];
    char accept[HUSH_WS_ACCEPT_LEN + 1];
    unsigned char frame[HUSH_TEST_FRAME_MAX];
    unsigned char payload256[256];
    static const unsigned char masked_hello[] = {
        0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d,
        0x7f, 0x9f, 0x4d, 0x51, 0x58
    };
    static const unsigned char masked_frag_hel[] = {0x01, 0x83, 0, 0, 0, 0,
                                                    0x48, 0x65, 0x6c};
    static const unsigned char masked_frag_lo[] = {0x80, 0x82, 0, 0, 0, 0,
                                                   0x6c, 0x6f};
    hush_ws_frame_t parsed;
    hush_ws_parse_t parse_status;
    hush_ws_frame_out_t request;
    size_t consumed = 0;
    size_t i;
    size_t total;

    /* Handshake accept: the RFC 6455 §1.3 example. */
    expect(hush_ws_accept("dGhlIHNhbXBsZSBub25jZQ==", accept, sizeof(accept)) ==
               HUSH_OK,
           "accept computes");
    expect(strcmp(accept, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") == 0, "accept vector");
    expect(hush_ws_accept("dGhlIHNhbXBsZSBub25jZQ==", accept, 16) ==
               HUSH_ERR_FULL,
           "accept short buffer");
    expect(hush_ws_accept(NULL, accept, sizeof(accept)) == HUSH_ERR_ARG,
           "accept NULL key");

    {
        static const char good_req[] =
            "GET / HTTP/1.1\r\nHost: localhost\r\n"
            "Upgrade: websocket\r\nConnection: Upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n";
        static const char case_req[] =
            "GET / HTTP/1.1\r\nHost: localhost\r\n"
            "Upgrade: WebSocket\r\nConnection: keep-alive, Upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n";
        static const char no_upgrade[] =
            "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
        static const char bad_connection[] =
            "GET / HTTP/1.1\r\nUpgrade: websocket\r\n"
            "Connection: keep-alive\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n";
        static const char bad_version[] =
            "GET / HTTP/1.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 8\r\n\r\n";
        static const char bad_key[] =
            "GET / HTTP/1.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
            "Sec-WebSocket-Key: short\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n";
        char key[HUSH_WS_KEY_LEN + 1];
        hush_ws_handshake_out_t hs = {.key = key, .key_size = sizeof(key)};

        expect(hush_ws_handshake_take(good_req, sizeof(good_req) - 1, &hs) ==
                   HUSH_OK,
               "handshake good");
        expect(strcmp(key, "dGhlIHNhbXBsZSBub25jZQ==") == 0, "key extracted");
        expect(hs.req_len == sizeof(good_req) - 1, "handshake length");
        expect(hush_ws_handshake_take(case_req, sizeof(case_req) - 1, &hs) ==
                   HUSH_OK,
               "handshake case-insensitive");
        expect(hush_ws_handshake_take(no_upgrade, sizeof(no_upgrade) - 1, &hs) ==
                   HUSH_ERR_DENIED,
               "missing upgrade denied");
        expect(hush_ws_handshake_take(bad_connection,
                                      sizeof(bad_connection) - 1,
                                      &hs) == HUSH_ERR_DENIED,
               "missing connection token denied");
        expect(hush_ws_handshake_take(bad_version, sizeof(bad_version) - 1,
                                      &hs) == HUSH_ERR_DENIED,
               "wrong version denied");
        expect(hush_ws_handshake_take(bad_key, sizeof(bad_key) - 1, &hs) ==
                   HUSH_ERR_DENIED,
               "short key denied");
        hs.key_size = 16;
        expect(hush_ws_handshake_take(good_req, sizeof(good_req) - 1, &hs) ==
                   HUSH_ERR_DENIED,
               "short key buffer denied");

        expect(hush_ws_format_reply(accept, text, sizeof(text), &consumed) ==
                   HUSH_OK,
               "reply formats");
        expect(strstr(text, "101 Switching Protocols") != NULL, "reply status");
        expect(strstr(text, "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") !=
                   NULL,
               "reply accept line");
    }

    /* Frame parsing: the RFC 6455 §5.7 masked "Hello". */
    memcpy(frame, masked_hello, sizeof(masked_hello));
    parse_status = hush_ws_parse_frame(frame, sizeof(masked_hello), &consumed,
                                       &parsed);
    expect(parse_status == HUSH_WS_PARSE_OK, "masked hello parses");
    expect(parsed.fin == 1 && parsed.opcode == HUSH_WS_OP_TEXT,
           "hello fin/opcode");
    expect(parsed.payload_len == 5 && memcmp(parsed.payload, "Hello", 5) == 0,
           "hello payload");
    expect(consumed == sizeof(masked_hello), "hello consumed");

    /* Unmasked client frames are a protocol error. */
    {
        static const unsigned char unmasked[] = {0x81, 0x05, 'H', 'e', 'l',
                                                 'l', 'o'};

        parse_status = hush_ws_parse_frame(frame, sizeof(unmasked), &consumed,
                                           &parsed);
        memcpy(frame, unmasked, sizeof(unmasked));
        parse_status = hush_ws_parse_frame(frame, sizeof(unmasked), &consumed,
                                           &parsed);
        expect(parse_status == HUSH_WS_PARSE_ERROR, "unmasked frame error");
    }

    /* 16-bit length: 256-byte binary payload, zero mask. */
    for (i = 0; i < sizeof(payload256); ++i)
        payload256[i] = (unsigned char)i;
    total = build_masked(frame, 1, HUSH_WS_OP_BINARY, payload256,
                         sizeof(payload256));
    parse_status = hush_ws_parse_frame(frame, total, &consumed, &parsed);
    expect(parse_status == HUSH_WS_PARSE_OK, "16-bit length parses");
    expect(parsed.payload_len == sizeof(payload256), "16-bit length value");
    expect(consumed == total, "16-bit consumed");
    expect(memcmp(parsed.payload, payload256, sizeof(payload256)) == 0,
           "16-bit payload intact");

    /* 64-bit length headers always exceed the message cap: TOO_BIG. */
    {
        static const unsigned char big_header[] = {
            0x81, 0xff, 0, 0, 0, 0, 0, 1, 0, 0
        };

        parse_status = hush_ws_parse_frame(frame, sizeof(big_header),
                                           &consumed, &parsed);
        memcpy(frame, big_header, sizeof(big_header));
        parse_status = hush_ws_parse_frame(frame, sizeof(big_header),
                                           &consumed, &parsed);
        expect(parse_status == HUSH_WS_PARSE_TOO_BIG, "64-bit length too big");
    }

    /* Non-minimal lengths are protocol errors. */
    {
        static const unsigned char short16[] = {0x81, 0x7e, 0, 5, 0, 0, 0, 0,
                                                'H', 'e', 'l', 'l', 'o'};
        static const unsigned char short64[] = {0x81, 0xff, 0, 0, 0, 0, 0, 0,
                                                0, 5};

        memcpy(frame, short16, sizeof(short16));
        parse_status = hush_ws_parse_frame(frame, sizeof(short16), &consumed,
                                           &parsed);
        expect(parse_status == HUSH_WS_PARSE_ERROR, "non-minimal 16-bit");
        memcpy(frame, short64, sizeof(short64));
        parse_status = hush_ws_parse_frame(frame, sizeof(short64), &consumed,
                                           &parsed);
        expect(parse_status == HUSH_WS_PARSE_ERROR, "non-minimal 64-bit");
    }

    /* Control-frame rules. */
    {
        static const unsigned char ping_nofin[] = {
            0x09, 0x85, 0x37, 0xfa, 0x21, 0x3d,
            0x7f, 0x9f, 0x4d, 0x51, 0x58
        };
        static const unsigned char close_len1[] = {0x88, 0x81, 0, 0, 0, 0, 0};

        memcpy(frame, ping_nofin, sizeof(ping_nofin));
        parse_status = hush_ws_parse_frame(frame, sizeof(ping_nofin),
                                           &consumed, &parsed);
        expect(parse_status == HUSH_WS_PARSE_ERROR, "ping without FIN");
        memcpy(frame, close_len1, sizeof(close_len1));
        parse_status = hush_ws_parse_frame(frame, sizeof(close_len1),
                                           &consumed, &parsed);
        expect(parse_status == HUSH_WS_PARSE_ERROR, "one-byte close");
    }

    /* Reserved bits and unknown opcodes. */
    {
        static const unsigned char rsv[] = {0xc1, 0x85, 0, 0, 0, 0, 0, 0, 0,
                                            0, 0};
        static const unsigned char unknown_op[] = {0x83, 0x85, 0, 0, 0, 0, 0,
                                                   0, 0, 0, 0};

        memcpy(frame, rsv, sizeof(rsv));
        parse_status = hush_ws_parse_frame(frame, sizeof(rsv), &consumed,
                                           &parsed);
        expect(parse_status == HUSH_WS_PARSE_ERROR, "reserved bits");
        memcpy(frame, unknown_op, sizeof(unknown_op));
        parse_status = hush_ws_parse_frame(frame, sizeof(unknown_op),
                                           &consumed, &parsed);
        expect(parse_status == HUSH_WS_PARSE_ERROR, "unknown opcode");
    }

    /* Truncated frames ask for more bytes. */
    {
        static const unsigned char partial[] = {0x81, 0x85, 0x37, 0xfa};
        static const unsigned char partial16[] = {0x81, 0xfe, 0x01};
        static const unsigned char one_byte[] = {0x81};

        memcpy(frame, partial, sizeof(partial));
        parse_status = hush_ws_parse_frame(frame, sizeof(partial), &consumed,
                                           &parsed);
        expect(parse_status == HUSH_WS_PARSE_NEED_MORE, "partial frame");
        memcpy(frame, partial16, sizeof(partial16));
        parse_status = hush_ws_parse_frame(frame, sizeof(partial16),
                                           &consumed, &parsed);
        expect(parse_status == HUSH_WS_PARSE_NEED_MORE, "partial 16-bit");
        memcpy(frame, one_byte, sizeof(one_byte));
        parse_status = hush_ws_parse_frame(frame, sizeof(one_byte), &consumed,
                                           &parsed);
        expect(parse_status == HUSH_WS_PARSE_NEED_MORE, "one byte");
    }

    /* Fragmentation vectors: text "Hel" (FIN=0) then continuation "lo". */
    memcpy(frame, masked_frag_hel, sizeof(masked_frag_hel));
    parse_status = hush_ws_parse_frame(frame, sizeof(masked_frag_hel),
                                       &consumed, &parsed);
    expect(parse_status == HUSH_WS_PARSE_OK && parsed.fin == 0 &&
               parsed.opcode == HUSH_WS_OP_TEXT,
           "fragment start");
    expect(parsed.payload_len == 3 && memcmp(parsed.payload, "Hel", 3) == 0,
           "fragment start payload");
    memcpy(frame, masked_frag_lo, sizeof(masked_frag_lo));
    parse_status = hush_ws_parse_frame(frame, sizeof(masked_frag_lo),
                                       &consumed, &parsed);
    expect(parse_status == HUSH_WS_PARSE_OK && parsed.fin == 1 &&
               parsed.opcode == HUSH_WS_OP_CONT,
           "fragment end");
    expect(parsed.payload_len == 2 && memcmp(parsed.payload, "lo", 2) == 0,
           "fragment end payload");

    /* Server frame writer: exact bytes and minimal lengths. */
    request = (hush_ws_frame_out_t){.opcode = HUSH_WS_OP_TEXT,
                                    .payload = "Hello", .payload_len = 5};
    expect(hush_ws_format_frame(&request, text, sizeof(text), &consumed) ==
               HUSH_OK,
           "format text");
    expect(consumed == 7 && (unsigned char)text[0] == 0x81 &&
               (unsigned char)text[1] == 0x05 &&
               memcmp(text + 2, "Hello", 5) == 0,
           "text frame bytes");
    request = (hush_ws_frame_out_t){.opcode = HUSH_WS_OP_BINARY,
                                    .payload = (const char *)payload256,
                                    .payload_len = sizeof(payload256)};
    expect(hush_ws_format_frame(&request, text, sizeof(text), &consumed) ==
               HUSH_OK,
           "format 256");
    expect(consumed == sizeof(payload256) + 4 &&
               (unsigned char)text[0] == 0x82 &&
               (unsigned char)text[1] == 126 &&
               (unsigned char)text[2] == 0x01 && (unsigned char)text[3] == 0x00,
           "256 header bytes");
    expect(hush_ws_format_frame(&request, text, 4, &consumed) == HUSH_ERR_FULL,
           "format short buffer");

    /* UTF-8 validation matrix. */
    expect(hush_ws_utf8_ok("plain ascii", 11) == 1, "utf8 ascii");
    expect(hush_ws_utf8_ok("\xc3\xa9", 2) == 1, "utf8 two byte");
    expect(hush_ws_utf8_ok("\xe2\x82\xac", 3) == 1, "utf8 three byte");
    expect(hush_ws_utf8_ok("\xf0\x9f\x98\x80", 4) == 1, "utf8 four byte");
    expect(hush_ws_utf8_ok("\xf4\x8f\xbf\xbf", 4) == 1, "utf8 U+10FFFF");
    expect(hush_ws_utf8_ok("\x80", 1) == 0, "utf8 bare continuation");
    expect(hush_ws_utf8_ok("\xc0\x80", 2) == 0, "utf8 overlong");
    expect(hush_ws_utf8_ok("\xed\xa0\x80", 3) == 0, "utf8 surrogate");
    expect(hush_ws_utf8_ok("\xf0\x80\x80\x80", 4) == 0, "utf8 overlong four");
    expect(hush_ws_utf8_ok("\xf4\x90\x80\x80", 4) == 0, "utf8 past max");
    expect(hush_ws_utf8_ok("\xc3", 1) == 0, "utf8 truncated");
    expect(hush_ws_utf8_ok("\xf5\x80\x80\x80", 4) == 0, "utf8 bad lead");

    if (g_fail)
        return 1;
    printf("test_ws ok\n");
    return 0;
}

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

static size_t build_masked(unsigned char *out, int fin, unsigned int opcode,
                           const unsigned char *payload, size_t payload_len)
{
    size_t header = 2;

    memset(out, 0, HUSH_TEST_FRAME_MAX);
    out[0] = (unsigned char)((fin ? 0x80 : 0) | (opcode & 0x0F));
    if (payload_len < 126) {
        out[1] = (unsigned char)(0x80 | payload_len);
    } else {
        out[1] = (unsigned char)(0x80 | 126);
        out[2] = (unsigned char)((payload_len >> 8) & 0xFF);
        out[3] = (unsigned char)(payload_len & 0xFF);
        header += 2;
    }
    if (payload_len != 0)
        memcpy(out + header + 4, payload, payload_len);
    return header + 4 + payload_len;
}
