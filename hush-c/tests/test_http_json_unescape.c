/* tests/test_http_json_unescape.c: pin HTTP JSON field unescape. */

#include <stdio.h>
#include <string.h>

#include "hush_http_internal.h"

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

static void check(const char *src, const char *want, const char *msg)
{
    char out[64];

    hush_http_json_unescape_copy(src, out, sizeof(out));
    expect(strcmp(out, want) == 0, msg);
}

int main(void)
{
    char out[64];
    char tiny[3];
    unsigned char leaf[] = {0xF0, 0x9F, 0x8C, 0xBF, 0}; /* U+1F33F */
    struct {
        char buf[3];
        unsigned char canary;
    } three;
    struct {
        char buf[4];
        unsigned char canary;
    } four;
    struct {
        char buf[5];
        unsigned char canary;
    } five;

    /* Short escapes. */
    check("a\\nb\"", "a\nb", "newline");
    check("a\\rb\"", "a\rb", "return");
    check("a\\tb\"", "a\tb", "tab");
    check("a\\bb\"", "a\bb", "backspace");
    check("a\\fb\"", "a\fb", "formfeed");

    /* \\u00XX lowercase and uppercase. */
    check("x\\u001by", "x\x1by", "esc lower");
    check("x\\u001By", "x\x1by", "esc upper");
    check("x\\u0007y", "x\x07y", "bell");

    /* Above 0x7FF needs three UTF-8 bytes (U+0800). */
    {
        unsigned char want[] = {'a', 0xE0, 0xA0, 0x80, 'b', 0};
        check("a\\u0800b\"", (const char *)want, ">0x7FF");
    }

    /* Two-byte Latin-1. */
    {
        unsigned char want[] = {'C', 'a', 'f', 0xC3, 0xA9, 0};
        check("Caf\\u00e9\"", (const char *)want, "latin1");
    }

    /* Surrogate pair joins to one code point (🌿). */
    check("\\uD83C\\uDF3F\"", (const char *)leaf, "surrogate pair");
    check("\\ud83c\\udf3f\"", (const char *)leaf, "surrogate pair lower");

    /* Lone halves are rejected, not written as CESU-8. */
    hush_http_json_unescape_copy("\\uD800\"", out, sizeof(out));
    expect(out[0] == '\0', "lone high empty");
    hush_http_json_unescape_copy("\\uDC00\"", out, sizeof(out));
    expect(out[0] == '\0', "lone low empty");
    hush_http_json_unescape_copy("a\\uD800b\"", out, sizeof(out));
    expect(strcmp(out, "ab") == 0, "lone high skipped");

    /* Size guard: 1-byte short escape. */
    memset(tiny, 0x5A, sizeof(tiny));
    hush_http_json_unescape_copy("\\n\\n\\n\"", tiny, sizeof(tiny));
    expect(tiny[sizeof(tiny) - 1] == '\0', "size guard NUL");
    expect((unsigned char)tiny[0] == '\n' || tiny[0] == '\0', "size guard byte");

    /* M12: 3-byte BMP write must not overrun when dstsz == 3 (2 data + NUL). */
    memset(&three, 0x5A, sizeof(three));
    three.canary = 0xA5;
    hush_http_json_unescape_copy("\\u0800\"", three.buf, sizeof(three.buf));
    expect(three.canary == 0xA5, "3-byte no overrun");
    expect(three.buf[0] == '\0', "3-byte too small stays empty");

    /* Enough room for U+0800 (3 UTF-8 bytes + NUL). */
    memset(&four, 0x5A, sizeof(four));
    four.canary = 0xA5;
    hush_http_json_unescape_copy("\\u0800\"", four.buf, sizeof(four.buf));
    expect(four.canary == 0xA5, "3-byte fit no overrun");
    expect((unsigned char)four.buf[0] == 0xE0, "3-byte fit b0");
    expect((unsigned char)four.buf[1] == 0xA0, "3-byte fit b1");
    expect((unsigned char)four.buf[2] == 0x80, "3-byte fit b2");
    expect(four.buf[3] == '\0', "3-byte fit NUL");

    /* M18: 4-byte surrogate write must not overrun when dstsz == 4. */
    memset(&four, 0x5A, sizeof(four));
    four.canary = 0xA5;
    hush_http_json_unescape_copy("\\uD83C\\uDF3F\"", four.buf, sizeof(four.buf));
    expect(four.canary == 0xA5, "4-byte no overrun");
    expect(four.buf[0] == '\0', "4-byte too small stays empty");

    /* Enough room for 🌿 (4 UTF-8 bytes + NUL). */
    memset(&five, 0x5A, sizeof(five));
    five.canary = 0xA5;
    hush_http_json_unescape_copy("\\uD83C\\uDF3F\"", five.buf, sizeof(five.buf));
    expect(five.canary == 0xA5, "4-byte fit no overrun");
    expect(memcmp(five.buf, leaf, 4) == 0, "4-byte fit bytes");
    expect(five.buf[4] == '\0', "4-byte fit NUL");

    /* NUL escape becomes the non-zero stand-in. */
    check("a\\u0000b\"", "a\x01" "b", "nul stand-in");

    if (g_fail)
        return 1;
    printf("test_http_json_unescape ok\n");
    return 0;
}
