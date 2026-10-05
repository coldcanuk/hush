/* hush_json.h: RFC 8259 string escape for Hush JSON writers. */

#ifndef HUSH_JSON_H
#define HUSH_JSON_H

#include <stddef.h>

#include "hush_status.h"

enum {
    HUSH_JSON_U_LEN = 6,
    HUSH_JSON_INPUT_MAX = 262144,
    HUSH_JSON_DEPTH_MAX = 32,
    HUSH_JSON_UTF8_MAX = 4
};

/* A borrowed JSON value span; len excludes surrounding whitespace. */
typedef struct {
    const char *start;
    size_t len;
} hush_json_value_t;

/* Writes a JSON string body (no quotes) into out. NUL in, empty out. */
size_t hush_json_escape(const char *in, char *out, size_t outsz);

/* Selects a slash-separated object/array path in borrowed JSON (no ~ escapes).
 * All pointers required. Returns ARG, PARSE, FULL, or NOT_FOUND on failure. */
hush_status_t hush_json_lookup(hush_json_value_t *out, const char *json,
                                const char *path);

/* Decodes a borrowed JSON string into required output; rejects truncation,
 * embedded NUL, invalid UTF-8, and malformed escapes with PARSE or FULL. */
hush_status_t hush_json_decode(char *out, size_t outsz,
                                const hush_json_value_t *value);

/* Counts Unicode scalar values in required bounded UTF-8 text. Rejects invalid
 * encoding with PARSE or a missing terminator within capacity with FULL. */
hush_status_t hush_json_count_chars(size_t *out, const char *text, size_t capacity);

/* Bytes in one well-formed UTF-8 scalar at text (E0/ED/F0/F4-aware), else 0. */
size_t hush_json_utf8_scalar(const char *text, size_t remain);

/* Copies src into dst (at most dstsz-1 bytes + NUL). Stops on a UTF-8
 * character boundary: incomplete trailing sequences are dropped; lone
 * invalid bytes become U+FFFD so live session JSON stays valid UTF-8. */
size_t hush_json_copy_bounded(char *dst, size_t dstsz, const char *src);

/* Like hush_json_decode, but never blanks a non-empty JSON string on PARSE
 * (invalid UTF-8, mid-character cut from an older build, etc.). Good escapes
 * still decode; undecodable bytes become U+FFFD. FULL truncates to fit. */
hush_status_t hush_json_decode_keep(char *out, size_t outsz,
                                    const hush_json_value_t *value);

#endif /* HUSH_JSON_H */
