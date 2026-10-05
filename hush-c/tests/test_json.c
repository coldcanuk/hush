/* tests/test_json.c: RFC 8259 C0 escape. */

#include <stdio.h>
#include <string.h>
#include "hush_json.h"

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

int main(void)
{
    char out[64];
    char in[8];

    expect(hush_json_escape("hi", out, sizeof(out)) == 2, "plain len");
    expect(strcmp(out, "hi") == 0, "plain");
    expect(hush_json_escape("a\"b\\c", out, sizeof(out)) == 7, "quote len");
    expect(strcmp(out, "a\\\"b\\\\c") == 0, "quote");
    expect(hush_json_escape("a\nb", out, sizeof(out)) == 4, "nl len");
    expect(strcmp(out, "a\\nb") == 0, "nl");
    in[0] = 'a';
    in[1] = '\t';
    in[2] = 'b';
    in[3] = '\r';
    in[4] = '\x01';
    in[5] = '\0';
    expect(hush_json_escape(in, out, sizeof(out)) == 12, "ctrl len");
    expect(strcmp(out, "a\\tb\\r\\u0001") == 0, "ctrl");
    expect(hush_json_escape(NULL, out, sizeof(out)) == 0, "null in");
    expect(out[0] == '\0', "null empty");
    expect(hush_json_escape("x", NULL, 8) == 0, "null out");

    /* UTF-8 boundary copy: drop incomplete trailer; keep invalid 0xFF. */
    {
        char buf[8];
        const char *e_acute = "\xC3\xA9"; /* é */
        char long_e[64];
        size_t n;
        int i;

        n = hush_json_copy_bounded(buf, 2, e_acute); /* room for 1 data byte */
        expect(n == 0 && buf[0] == '\0', "copy drops incomplete é");
        n = hush_json_copy_bounded(buf, 3, e_acute);
        expect(n == 2 && (unsigned char)buf[0] == 0xC3
               && (unsigned char)buf[1] == 0xA9, "copy fits é");
        long_e[0] = (char)0xFF;
        long_e[1] = 'Z';
        long_e[2] = '\0';
        n = hush_json_copy_bounded(buf, sizeof(buf), long_e);
        expect(n == 2 && (unsigned char)buf[0] == 0xFF && buf[1] == 'Z',
               "copy keeps 0xFF");
        for (i = 0; i < 30; i++) {
            long_e[i * 2] = (char)0xC3;
            long_e[i * 2 + 1] = (char)0xA9;
        }
        long_e[60] = '\0';
        n = hush_json_copy_bounded(buf, 5, long_e); /* room for 4 data + NUL */
        expect(n == 4, "copy caps on é boundary");
        expect((unsigned char)buf[0] == 0xC3 && (unsigned char)buf[2] == 0xC3,
               "copy two full é");
    }

    if (g_fail)
        return 1;
    printf("test_json ok\n");
    return 0;
}
