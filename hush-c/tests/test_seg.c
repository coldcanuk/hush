/* tests/test_seg.c: structural plain-text/Markdown chunker. */

#include <stdio.h>
#include <string.h>

#include "hush_seg.h"

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

/* True when b is a UTF-8 codepoint boundary in text[0..textsz). */
static int boundary_ok(const char *text, size_t textsz, size_t b)
{
    if (b == 0 || b == textsz)
        return 1;
    return ((unsigned char)text[b] & 0xC0u) != 0x80u;
}

/* Verifies spans are contiguous, non-empty, UTF-8-safe, and within max. */
static void check_spans(const char *text, size_t textsz,
                        const hush_seg_span_t *spans, size_t n,
                        size_t max)
{
    size_t i;
    size_t total = 0;

    if (n == 0) {
        expect(textsz == 0, "zero spans only for empty text");
        return;
    }
    expect(spans[0].off == 0, "first span starts at 0");
    for (i = 0; i < n; i++) {
        expect(spans[i].len > 0, "span non-empty");
        expect(spans[i].len <= max, "span within max");
        expect(boundary_ok(text, textsz, spans[i].off), "start boundary safe");
        expect(boundary_ok(text, textsz, spans[i].off + spans[i].len),
               "end boundary safe");
        if (i > 0)
            expect(spans[i].off == spans[i - 1].off + spans[i - 1].len,
                   "spans contiguous");
        total += spans[i].len;
    }
    expect(total == textsz, "spans cover full text");
}

int main(void)
{
    hush_seg_span_t spans[64];
    size_t n;

    /* Empty / NULL input. */
    n = hush_seg_split(NULL, 0, 0, 16, 64, spans, 64);
    expect(n == 0, "NULL text returns zero");
    n = hush_seg_split("", 0, 0, 16, 64, spans, 64);
    expect(n == 0, "empty text returns zero");
    n = hush_seg_split("hi", 2, 0, 16, 64, NULL, 0);
    expect(n == 0, "NULL spans returns zero");

    /* Short text fits in one span. */
    n = hush_seg_split("hello", 5, 0, 16, 64, spans, 64);
    expect(n == 1, "short text one span");
    if (n == 1) {
        expect(spans[0].off == 0 && spans[0].len == 5, "short span covers all");
    }

    /* Plain text splits at newline boundaries. */
    {
        const char *t = "alpha\nbeta\ngamma\ndelta\n";
        n = hush_seg_split(t, strlen(t), 0, 10, 32, spans, 64);
        check_spans(t, strlen(t), spans, n, 32);
        expect(n == 4, "plain text splits into four lines");
    }

    /* Hard cap on a run with no whitespace. */
    {
        const char *t = "aaaaaaaaaa";
        n = hush_seg_split(t, 10, 0, 4, 6, spans, 64);
        check_spans(t, 10, spans, n, 6);
        expect(n == 2, "no-whitespace run hard-capped to two");
        if (n == 2)
            expect(spans[0].len == 6, "first hard-cap chunk length");
    }

    /* UTF-8 safety: multi-byte chars never split mid-codepoint. */
    {
        const char *t = "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9"; /* four é */
        n = hush_seg_split(t, 8, 0, 1, 3, spans, 64);
        check_spans(t, 8, spans, n, 3);
        expect(n == 4, "four UTF-8 codepoints become four spans");
    }

    /* Markdown: fenced code block stays atomic (split before it). */
    {
        const char *t = "A.\n\n```\nline\n```\n\nB.\n";
        size_t fence_open = 4;
        size_t fence_close = 17;
        size_t i;

        n = hush_seg_split(t, strlen(t), 1, 8, 64, spans, 64);
        check_spans(t, strlen(t), spans, n, 64);
        for (i = 0; i < n; i++) {
            size_t b = spans[i].off + spans[i].len;

            if (b == strlen(t))
                continue;
            expect(!(b > fence_open && b < fence_close),
                   "no split inside fence");
        }
        /* First span must end exactly at the blank line before the fence. */
        if (n >= 2)
            expect(spans[0].len == 4, "first markdown chunk stops pre-fence");
    }

    /* Markdown fence larger than target still never hard-splits a codepoint. */
    {
        const char *t = "para\n\n```\nint x = 1;\n```\n\ntail\n";
        n = hush_seg_split(t, strlen(t), 1, 6, 40, spans, 64);
        check_spans(t, strlen(t), spans, n, 40);
    }

    if (g_fail)
        return 1;
    printf("ok\n");
    return 0;
}
