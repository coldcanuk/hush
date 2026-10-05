/* hush_json.c: RFC 8259 string escape. */

#include <assert.h>
#include <string.h>

#include "hush_json.h"

enum {
    HUSH_JSON_CTRL_MAX = 0x1F,
    HUSH_JSON_PAIR_LEN = 2,
    HUSH_JSON_NIBBLE = 0xF,
    HUSH_JSON_NIBBLE_SHIFT = 4
};

#define HUSH_JSON_HEX "0123456789abcdef"

static int hush_json_is_ctrl(unsigned char ch);
static size_t hush_json_put_pair(char *out, size_t outsz, size_t off,
                                 char mark);
static size_t hush_json_put_u(char *out, size_t outsz, size_t off,
                              unsigned char ch);
static size_t hush_json_put_byte(char *out, size_t outsz, size_t off,
                                 unsigned char ch);

size_t hush_json_escape(const char *in, char *out, size_t outsz)
{
    const unsigned char *src;
    size_t off;
    size_t next;

    if (out == NULL || outsz == 0)
        return 0;
    if (in == NULL)
        in = "";
    src = (const unsigned char *)in;
    off = 0;
    while (*src != '\0' && off + 1 < outsz) {
        next = hush_json_put_byte(out, outsz, off, *src);
        if (next == off)
            break;
        off = next;
        src++;
    }
    out[off] = '\0';
    return off;
}

static int hush_json_is_ctrl(unsigned char ch)
{
    return ch <= (unsigned char)HUSH_JSON_CTRL_MAX;
}

static size_t hush_json_put_pair(char *out, size_t outsz, size_t off,
                                 char mark)
{
    assert(out != NULL);
    if (off + (size_t)HUSH_JSON_PAIR_LEN >= outsz)
        return off;
    out[off] = '\\';
    out[off + 1] = mark;
    return off + (size_t)HUSH_JSON_PAIR_LEN;
}

static size_t hush_json_put_u(char *out, size_t outsz, size_t off,
                              unsigned char ch)
{
    static const char hex[] = HUSH_JSON_HEX;

    assert(out != NULL);
    if (off + (size_t)HUSH_JSON_U_LEN >= outsz)
        return off;
    out[off] = '\\';
    out[off + 1] = 'u';
    out[off + 2] = '0';
    out[off + 3] = '0';
    out[off + 4] = hex[(ch >> HUSH_JSON_NIBBLE_SHIFT) & HUSH_JSON_NIBBLE];
    out[off + 5] = hex[ch & (unsigned char)HUSH_JSON_NIBBLE];
    return off + (size_t)HUSH_JSON_U_LEN;
}

static size_t hush_json_put_byte(char *out, size_t outsz, size_t off,
                                 unsigned char ch)
{
    assert(out != NULL);
    if (ch == '"' || ch == '\\')
        return hush_json_put_pair(out, outsz, off, (char)ch);
    if (ch == '\n')
        return hush_json_put_pair(out, outsz, off, 'n');
    if (ch == '\r')
        return hush_json_put_pair(out, outsz, off, 'r');
    if (ch == '\t')
        return hush_json_put_pair(out, outsz, off, 't');
    if (hush_json_is_ctrl(ch))
        return hush_json_put_u(out, outsz, off, ch);
    if (off + 1 >= outsz)
        return off;
    out[off] = (char)ch;
    return off + 1;
}

static int hush_json_utf8_cont(unsigned char b)
{
    return (b & 0xC0u) == 0x80u;
}

/* Bytes in one well-formed UTF-8 scalar at p, else 0. */
static size_t hush_json_utf8_seq(const unsigned char *p, size_t remain)
{
    unsigned char b0;

    if (p == NULL || remain == 0)
        return 0;
    b0 = p[0];
    if (b0 <= 0x7Fu)
        return 1;
    if (b0 >= 0xC2u && b0 <= 0xDFu && remain >= 2 && hush_json_utf8_cont(p[1]))
        return 2;
    if (b0 >= 0xE0u && b0 <= 0xEFu && remain >= 3 && hush_json_utf8_cont(p[1])
        && hush_json_utf8_cont(p[2])) {
        if (b0 == 0xE0u && p[1] < 0xA0u)
            return 0;
        if (b0 == 0xEDu && p[1] >= 0xA0u)
            return 0;
        return 3;
    }
    if (b0 >= 0xF0u && b0 <= 0xF4u && remain >= 4 && hush_json_utf8_cont(p[1])
        && hush_json_utf8_cont(p[2]) && hush_json_utf8_cont(p[3])) {
        if (b0 == 0xF0u && p[1] < 0x90u)
            return 0;
        if (b0 == 0xF4u && p[1] >= 0x90u)
            return 0;
        return 4;
    }
    return 0;
}

/* True when b0 starts a multi-byte sequence that needs more bytes than remain. */
static int hush_json_utf8_incomplete(unsigned char b0, size_t remain)
{
    if (b0 >= 0xC2u && b0 <= 0xDFu)
        return remain < 2;
    if (b0 >= 0xE0u && b0 <= 0xEFu)
        return remain < 3;
    if (b0 >= 0xF0u && b0 <= 0xF4u)
        return remain < 4;
    return 0;
}

size_t hush_json_utf8_scalar(const char *text, size_t remain)
{
    if (text == NULL)
        return 0;
    return hush_json_utf8_seq((const unsigned char *)text, remain);
}

size_t hush_json_copy_bounded(char *dst, size_t dstsz, const char *src)
{
    static const char repl[] = "\xEF\xBF\xBD"; /* U+FFFD */
    size_t i = 0;
    size_t off = 0;

    if (dst == NULL || dstsz == 0)
        return 0;
    if (src == NULL)
        src = "";
    while (src[i] != '\0' && off + 1 < dstsz) {
        const unsigned char *p = (const unsigned char *)(src + i);
        size_t src_remain = 0;
        size_t dst_room = dstsz - 1 - off;
        size_t seq = 0;
        size_t k = 0;

        while (src[i + src_remain] != '\0')
            src_remain++;
        seq = hush_json_utf8_seq(p, src_remain);
        if (seq == 0) {
            /* Truncated only when src ends mid-sequence (remaining bytes are
             * all continuations). Otherwise the lead is invalid → U+FFFD. */
            if (hush_json_utf8_incomplete(p[0], src_remain)) {
                size_t j;
                int all_cont = 1;

                for (j = 1; j < src_remain; j++) {
                    if (!hush_json_utf8_cont(p[j])) {
                        all_cont = 0;
                        break;
                    }
                }
                if (all_cont)
                    break;
            }
            /* Invalid byte: U+FFFD so live session JSON stays valid UTF-8. */
            if (dst_room < 3)
                break;
            dst[off] = repl[0];
            dst[off + 1] = repl[1];
            dst[off + 2] = repl[2];
            off += 3;
            i++;
            continue;
        }
        if (seq > dst_room)
            break;
        for (k = 0; k < seq; k++)
            dst[off + k] = src[i + k];
        off += seq;
        i += seq;
    }
    dst[off] = '\0';
    return off;
}
