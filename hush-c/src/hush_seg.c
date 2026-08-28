/* hush_seg.c: structural plain-text/Markdown chunker for context budgets. */

#include <stddef.h>

#include "hush_seg.h"

/* True when b is a UTF-8 continuation byte (0b10xxxxxx). */
static int hush_seg_is_cont(unsigned char b)
{
    return (b & 0xC0u) == 0x80u;
}

/* Backs an exclusive end offset off to the previous UTF-8 lead byte so the
 * resulting boundary never splits a multi-byte codepoint. */
static size_t hush_seg_utf8_back(const char *text, size_t start, size_t end,
                                 size_t textsz)
{
    while (end > start && end < textsz &&
           hush_seg_is_cont((unsigned char)text[end]))
        end--;
    return end;
}

/* True when the line [ls, le) is a fenced-code marker (``` or ~~~, run >= 3).
 * On success *ch holds the marker char. */
static int hush_seg_fence_marker(const char *text, size_t ls, size_t le,
                                 char *ch)
{
    size_t i = ls;
    size_t run = 0;
    char c;

    if (text == NULL || ch == NULL || ls >= le)
        return 0;
    while (i < le && (text[i] == ' ' || text[i] == '\t'))
        i++;
    if (i >= le || (text[i] != '`' && text[i] != '~'))
        return 0;
    c = text[i];
    while (i < le && text[i] == c) {
        run++;
        i++;
    }
    if (run < 3)
        return 0;
    *ch = c;
    return 1;
}

/* Finds the best split point s in (start, end]: the byte offset where the next
 * chunk begins. Preference order is blank line, line, sentence, then space,
 * each one the closest to end. Markdown mode skips newlines/sentences/spaces
 * inside a fenced code block so a fence stays in a single span. Returns 0 when
 * no safe boundary exists inside the region. */
static size_t hush_seg_find_split(const char *text, size_t start, size_t end,
                                  int is_markdown)
{
    size_t line = start;
    size_t blank = 0;
    size_t newline = 0;
    size_t sentence = 0;
    size_t space = 0;
    int in_fence = 0;
    char fence_char = '\0';

    while (line < end) {
        size_t le = line;
        size_t p;
        char ch;

        while (le < end && text[le] != '\n')
            le++;
        if (is_markdown && hush_seg_fence_marker(text, line, le, &ch)) {
            if (!in_fence) {
                in_fence = 1;
                fence_char = ch;
            } else if (ch == fence_char) {
                in_fence = 0;
            }
        }
        for (p = line + 1; p < le; p++) {
            if ((text[p - 1] == '.' || text[p - 1] == '!' ||
                 text[p - 1] == '?') &&
                (text[p] == ' ' || text[p] == '\t')) {
                if (!is_markdown || !in_fence)
                    sentence = p + 1;
            }
            if (text[p] == ' ' || text[p] == '\t') {
                if (!is_markdown || !in_fence)
                    space = p + 1;
            }
        }
        if (le < end) {
            size_t boundary = le + 1;

            if (!is_markdown || !in_fence)
                newline = boundary;
            if (line == le && (!is_markdown || !in_fence))
                blank = boundary;
        }
        line = (le < end) ? le + 1 : end;
    }

    if (blank != 0)
        return blank;
    if (newline != 0)
        return newline;
    if (sentence != 0)
        return sentence;
    if (space != 0)
        return space;
    return 0;
}

size_t hush_seg_split(const char *text, size_t textsz, int is_markdown,
                      size_t target_bytes, size_t max_bytes,
                      hush_seg_span_t *spans, size_t max_spans)
{
    size_t off = 0;
    size_t n = 0;
    size_t target;
    size_t hard;

    if (text == NULL || textsz == 0 || spans == NULL || max_spans == 0)
        return 0;
    target = target_bytes;
    if (target == 0)
        target = 1;
    hard = max_bytes;
    if (hard < target)
        hard = target;

    while (off < textsz && n < max_spans) {
        size_t end = off + target;
        size_t split;

        if (end >= textsz) {
            spans[n].off = off;
            spans[n].len = textsz - off;
            n++;
            break;
        }
        split = hush_seg_find_split(text, off, end, is_markdown);
        if (split == 0) {
            size_t cap = off + hard;

            if (cap > textsz)
                cap = textsz;
            cap = hush_seg_utf8_back(text, off, cap, textsz);
            if (cap <= off) {
                /* Advance one full codepoint to guarantee progress. */
                cap = off + 1;
                while (cap < textsz &&
                       hush_seg_is_cont((unsigned char)text[cap]))
                    cap++;
            }
            spans[n].off = off;
            spans[n].len = cap - off;
            n++;
            off = cap;
        } else {
            spans[n].off = off;
            spans[n].len = split - off;
            n++;
            off = split;
        }
    }
    return n;
}
