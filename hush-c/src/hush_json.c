/* hush_json.c: RFC 8259 string escape. */

#include <assert.h>

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
