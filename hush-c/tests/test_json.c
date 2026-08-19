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
    if (g_fail)
        return 1;
    printf("test_json ok\n");
    return 0;
}
