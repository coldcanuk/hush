/* hush_seg.h: structural plain-text/Markdown chunker for context budgets.
 *
 * Splits a file into spans that (1) stay under a target byte budget where a
 * safe boundary exists, (2) never exceed a hard cap, and (3) preserve
 * structural intent by cutting only at blank-line, line, sentence, or word
 * boundaries. Markdown mode keeps fenced code blocks (``` / ~~~) atomic.
 */

#ifndef HUSH_SEG_H
#define HUSH_SEG_H

#include <stddef.h>

/* One structural span into the source text, in byte offsets. */
typedef struct {
    size_t off;   /* start offset, inclusive            */
    size_t len;   /* length in bytes                    */
} hush_seg_span_t;

/* Splits text[0..textsz) into structural chunks.
 *
 * - text must be NUL-terminated (or at least readable one byte past textsz).
 * - Each span ends at a blank-line, line, sentence, or space boundary when
 *   one exists inside the soft target; otherwise the hard cap (max_bytes) is
 *   applied, backed off to a UTF-8 codepoint boundary so no span splits a
 *   multi-byte character.
 * - is_markdown != 0 keeps fenced code blocks atomic: a span never ends on a
 *   newline inside a ``` / ~~~ fence.
 * - Returns the number of spans written (0..max_spans). A return smaller than
 *   what is needed to cover text simply stops; the caller can compare the
 *   total covered bytes against textsz to detect truncation.
 *
 * Safe on NULL/empty input (returns 0). */
size_t hush_seg_split(const char *text, size_t textsz, int is_markdown,
                      size_t target_bytes, size_t max_bytes,
                      hush_seg_span_t *spans, size_t max_spans);

#endif /* HUSH_SEG_H */
