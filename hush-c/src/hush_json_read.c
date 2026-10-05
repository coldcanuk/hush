/* hush_json_read.c: owns bounded JSON selection and Unicode string decoding. */

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "hush_json.h"

enum {
    HUSH_JSON_PATH_MAX = 128,
    HUSH_JSON_ASCII_MAX = 0x7f,
    HUSH_JSON_CONTROL_END = 0x20,
    HUSH_JSON_CONT_MASK = 0xc0,
    HUSH_JSON_CONT_TAG = 0x80,
    HUSH_JSON_CONT_BITS = 6,
    HUSH_JSON_CONT_VALUE = 0x3f,
    HUSH_JSON_TWO_TAG = 0xc0,
    HUSH_JSON_TWO_MASK = 0x1f,
    HUSH_JSON_TWO_MIN = 0x80,
    HUSH_JSON_THREE_TAG = 0xe0,
    HUSH_JSON_THREE_MASK = 0x0f,
    HUSH_JSON_THREE_MIN = 0x800,
    HUSH_JSON_FOUR_TAG = 0xf0,
    HUSH_JSON_FOUR_MASK = 0x07,
    HUSH_JSON_FOUR_MIN = 0x10000,
    HUSH_JSON_SCALAR_MAX = 0x10ffff,
    HUSH_JSON_HIGH_START = 0xd800,
    HUSH_JSON_HIGH_END = 0xdbff,
    HUSH_JSON_LOW_START = 0xdc00,
    HUSH_JSON_LOW_END = 0xdfff,
    HUSH_JSON_SURROGATE_BITS = 10,
    HUSH_JSON_HEX_DIGITS = 4,
    HUSH_JSON_HEX_BASE = 16,
    HUSH_JSON_DECIMAL_BASE = 10,
    HUSH_JSON_PAIR_BYTES = 2,
    HUSH_JSON_THREE_BYTES = 3,
    HUSH_JSON_FOUR_BYTES = 4
};

/* The cursor borrows [text,end); offset never exceeds end. */
typedef struct {
    const char *text;
    size_t offset;
    size_t end;
} hush_json_cursor_t;

/* Scanner owns a fixed delimiter stack; quoted content never changes nesting. */
typedef struct {
    char stack[HUSH_JSON_DEPTH_MAX];
    size_t depth;
    int quoted;
    int escaped;
    int done;
} hush_json_scan_t;

/* Consumes one structural character in required scanner; PARSE on mismatch. */
static hush_status_t hush_json_scan_char(hush_json_scan_t *scan, char ch);
/* Advances one required slash-delimited path segment and selects its child. */
static hush_status_t hush_json_select_path(hush_json_value_t *out, const char **part);
/* Reads a required string scalar, rejecting bare quotes, controls and NUL. */
static hush_status_t hush_json_take_string_scalar(hush_json_cursor_t *cursor, uint32_t *out);
/* Reads an object entry or array element; outputs a borrowed key/value pair. */
static hush_status_t hush_json_take_entry(hush_json_cursor_t *cursor, int object,
                                         hush_json_value_t *key, hush_json_value_t *value);
/* Consumes an optional final comma; rejects a trailing comma. */
static hush_status_t hush_json_next_entry(hush_json_cursor_t *cursor);
/* Advances a required cursor over bounded JSON whitespace. */
static void hush_json_skip_space(hush_json_cursor_t *cursor);
/* Selects the next value span; required borrowed cursor/output. PARSE on imbalance. */
static hush_status_t hush_json_take_value(hush_json_cursor_t *cursor, hush_json_value_t *out);
/* Selects a child by required path segment in a borrowed container. */
static hush_status_t hush_json_select(hush_json_value_t *out,
                                     const hush_json_value_t *parent, const char *key);
/* Reads one required UTF-8 scalar, advancing cursor. PARSE on invalid encoding. */
static hush_status_t hush_json_take_scalar(hush_json_cursor_t *cursor, uint32_t *out);
/* Reads one required hex quartet. PARSE on nonhex or short input. */
static hush_status_t hush_json_take_hex(hush_json_cursor_t *cursor, uint32_t *out);
/* Reads a required Unicode escape including a possible surrogate pair. */
static hush_status_t hush_json_take_escape(hush_json_cursor_t *cursor, uint32_t *out);
/* Encodes scalar into required bounded output. FULL if capacity is insufficient. */
static hush_status_t hush_json_put_scalar(char *out, size_t outsz, size_t *offset,
                                         uint32_t scalar);
/* Matches required object key to a borrowed JSON string. */
static int hush_json_key_matches(const hush_json_value_t *value, const char *key);

hush_status_t hush_json_lookup(hush_json_value_t *out, const char *json, const char *path)
{
    if (out == NULL || json == NULL || path == NULL)
        return HUSH_ERR_ARG;
    size_t len = 0;
    for (; len < (size_t)HUSH_JSON_INPUT_MAX && json[len] != '\0'; ++len) {}
    if (len == (size_t)HUSH_JSON_INPUT_MAX)
        return HUSH_ERR_FULL;
    hush_json_cursor_t cursor = {.text = json, .end = len};
    HUSH_TRY(hush_json_take_value(&cursor, out));
    hush_json_skip_space(&cursor);
    if (cursor.offset != len)
        return HUSH_ERR_PARSE;
    const char *part = path;
    for (size_t depth = 0; depth < (size_t)HUSH_JSON_DEPTH_MAX && *part != '\0'; ++depth) {
        HUSH_TRY(hush_json_select_path(out, &part));
    }
    return *part == '\0' ? HUSH_OK : HUSH_ERR_FULL;
}

hush_status_t hush_json_decode(char *out, size_t outsz, const hush_json_value_t *value)
{
    if (out == NULL || outsz == 0 || value == NULL || value->start == NULL)
        return HUSH_ERR_ARG;
    out[0] = '\0';
    if (value->len < (size_t)HUSH_JSON_PAIR_BYTES || value->start[0] != '"' ||
        value->start[value->len - 1] != '"')
        return HUSH_ERR_PARSE;
    hush_json_cursor_t cursor = {.text = value->start, .offset = 1, .end = value->len - 1};
    size_t offset = 0;
    for (size_t i = 0; i < value->len && cursor.offset < cursor.end; ++i) {
        uint32_t scalar = 0;
        HUSH_TRY(hush_json_take_string_scalar(&cursor, &scalar));
        HUSH_TRY(hush_json_put_scalar(out, outsz, &offset, scalar));
    }
    return HUSH_OK;
}

hush_status_t hush_json_count_chars(size_t *out, const char *text, size_t capacity)
{
    if (out == NULL || text == NULL || capacity == 0)
        return HUSH_ERR_ARG;
    size_t len = 0;
    for (; len < capacity && text[len] != '\0'; ++len) {}
    if (len == capacity)
        return HUSH_ERR_FULL;
    hush_json_cursor_t cursor = {.text = text, .end = len};
    *out = 0;
    for (size_t i = 0; i < len && cursor.offset < len; ++i) {
        uint32_t scalar = 0;
        HUSH_TRY(hush_json_take_scalar(&cursor, &scalar));
        ++*out;
    }
    return HUSH_OK;
}

static void hush_json_skip_space(hush_json_cursor_t *cursor)
{
    assert(cursor != NULL);
    assert(cursor->offset <= cursor->end);
    for (size_t i = cursor->offset; i < cursor->end; ++i) {
        if (strchr(" \t\r\n", cursor->text[i]) == NULL)
            break;
        ++cursor->offset;
    }
}

static hush_status_t hush_json_take_value(hush_json_cursor_t *cursor, hush_json_value_t *out)
{
    assert(cursor != NULL);
    assert(out != NULL);
    hush_json_skip_space(cursor);
    size_t start = cursor->offset;
    hush_json_scan_t scan = {0};
    for (size_t i = start; i < cursor->end; ++i) {
        char ch = cursor->text[i];
        HUSH_TRY(hush_json_scan_char(&scan, ch));
        if (scan.done) break;
        cursor->offset = i + 1;
        if (!scan.quoted && scan.depth == 0 && strchr("\"}]", ch) != NULL) break;
    }
    if (scan.quoted || scan.depth != 0 || cursor->offset == start)
        return HUSH_ERR_PARSE;
    *out = (hush_json_value_t){.start = cursor->text + start, .len = cursor->offset - start};
    return HUSH_OK;
}

static hush_status_t hush_json_select(hush_json_value_t *out,
                                     const hush_json_value_t *parent, const char *key)
{
    assert(out != NULL);
    assert(parent != NULL);
    int object = parent->start[0] == '{';
    if (!object && parent->start[0] != '[')
        return HUSH_ERR_NOT_FOUND;
    hush_json_cursor_t cursor = {.text = parent->start, .offset = 1, .end = parent->len - 1};
    char *end = NULL;
    unsigned long wanted = strtoul(key, &end, HUSH_JSON_DECIMAL_BASE);
    if (!object && (*key == '\0' || *end != '\0'))
        return HUSH_ERR_NOT_FOUND;
    hush_json_skip_space(&cursor);
    for (size_t i = 0; i < parent->len && cursor.offset < cursor.end; ++i) {
        hush_json_value_t name = {0}, value = {0};
        HUSH_TRY(hush_json_take_entry(&cursor, object, &name, &value));
        int matches = object ? hush_json_key_matches(&name, key) : i == wanted;
        HUSH_TRY(hush_json_next_entry(&cursor));
        if (matches) { *out = value; return HUSH_OK; }
    }
    return HUSH_ERR_NOT_FOUND;
}

static hush_status_t hush_json_scan_char(hush_json_scan_t *scan, char ch)
{
    assert(scan != NULL);
    if (scan->quoted) {
        if (ch == '"' && !scan->escaped) scan->quoted = 0;
        scan->escaped = ch == '\\' && !scan->escaped;
    } else if (ch == '"') {
        scan->quoted = 1;
    } else if (ch == '{' || ch == '[') {
        if (scan->depth == sizeof(scan->stack)) return HUSH_ERR_FULL;
        scan->stack[scan->depth++] = ch == '{' ? '}' : ']';
    } else if (ch == '}' || ch == ']') {
        if (scan->depth == 0) { scan->done = 1; return HUSH_OK; }
        if (scan->stack[--scan->depth] != ch) return HUSH_ERR_PARSE;
    } else if (scan->depth == 0 && strchr(",: \t\r\n", ch) != NULL) {
        scan->done = 1;
    }
    return HUSH_OK;
}

static hush_status_t hush_json_select_path(hush_json_value_t *out, const char **part)
{
    assert(out != NULL && part != NULL && *part != NULL);
    if (**part != '/') return HUSH_ERR_PARSE;
    ++*part;
    size_t count = strcspn(*part, "/");
    if (count == 0 || count >= (size_t)HUSH_JSON_PATH_MAX) return HUSH_ERR_PARSE;
    char key[HUSH_JSON_PATH_MAX] = {0};
    memcpy(key, *part, count);
    hush_json_value_t parent = *out;
    HUSH_TRY(hush_json_select(out, &parent, key));
    *part += count;
    return HUSH_OK;
}

static hush_status_t hush_json_take_string_scalar(hush_json_cursor_t *cursor, uint32_t *out)
{
    assert(cursor != NULL && out != NULL && cursor->offset < cursor->end);
    if (cursor->text[cursor->offset] == '\\') {
        ++cursor->offset;
        HUSH_TRY(hush_json_take_escape(cursor, out));
    } else {
        if ((unsigned char)cursor->text[cursor->offset] < HUSH_JSON_CONTROL_END ||
            cursor->text[cursor->offset] == '"') return HUSH_ERR_PARSE;
        HUSH_TRY(hush_json_take_scalar(cursor, out));
    }
    return *out == 0 ? HUSH_ERR_PARSE : HUSH_OK;
}

static hush_status_t hush_json_take_entry(hush_json_cursor_t *cursor, int object,
                                         hush_json_value_t *key, hush_json_value_t *value)
{
    assert(cursor != NULL && key != NULL && value != NULL);
    HUSH_TRY(hush_json_take_value(cursor, value));
    if (!object) return HUSH_OK;
    *key = *value;
    if (key->start[0] != '"') return HUSH_ERR_PARSE;
    hush_json_skip_space(cursor);
    if (cursor->offset == cursor->end || cursor->text[cursor->offset++] != ':')
        return HUSH_ERR_PARSE;
    return hush_json_take_value(cursor, value);
}

static hush_status_t hush_json_next_entry(hush_json_cursor_t *cursor)
{
    assert(cursor != NULL);
    hush_json_skip_space(cursor);
    if (cursor->offset == cursor->end) return HUSH_OK;
    if (cursor->text[cursor->offset++] != ',') return HUSH_ERR_PARSE;
    hush_json_skip_space(cursor);
    return cursor->offset == cursor->end ? HUSH_ERR_PARSE : HUSH_OK;
}

static int hush_json_key_matches(const hush_json_value_t *value, const char *key)
{
    assert(value != NULL);
    assert(key != NULL);
    char decoded[HUSH_JSON_PATH_MAX] = {0};
    if (hush_json_decode(decoded, sizeof(decoded), value) != HUSH_OK)
        return 0;
    return strcmp(decoded, key) == 0;
}

static hush_status_t hush_json_take_scalar(hush_json_cursor_t *cursor, uint32_t *out)
{
    assert(cursor != NULL);
    assert(out != NULL);
    if (cursor->offset >= cursor->end) return HUSH_ERR_PARSE;
    unsigned char first = (unsigned char)cursor->text[cursor->offset++];
    if (first <= HUSH_JSON_ASCII_MAX) { *out = first; return HUSH_OK; }
    size_t bytes = first < HUSH_JSON_THREE_TAG ? HUSH_JSON_PAIR_BYTES : HUSH_JSON_THREE_BYTES;
    if (first >= HUSH_JSON_FOUR_TAG) bytes = HUSH_JSON_FOUR_BYTES;
    uint32_t mask = bytes == HUSH_JSON_PAIR_BYTES ? HUSH_JSON_TWO_MASK : HUSH_JSON_THREE_MASK;
    if (bytes == HUSH_JSON_FOUR_BYTES) mask = HUSH_JSON_FOUR_MASK;
    uint32_t scalar = first & mask;
    if (first < HUSH_JSON_TWO_TAG || first > HUSH_JSON_FOUR_TAG + HUSH_JSON_FOUR_MASK)
        return HUSH_ERR_PARSE;
    for (size_t i = 1; i < (size_t)HUSH_JSON_UTF8_MAX && i < bytes; ++i) {
        if (cursor->offset == cursor->end) return HUSH_ERR_PARSE;
        unsigned char next = (unsigned char)cursor->text[cursor->offset++];
        if ((next & HUSH_JSON_CONT_MASK) != HUSH_JSON_CONT_TAG) return HUSH_ERR_PARSE;
        scalar = (scalar << HUSH_JSON_CONT_BITS) | (next & HUSH_JSON_CONT_VALUE);
    }
    uint32_t minimum = bytes == HUSH_JSON_PAIR_BYTES ? HUSH_JSON_TWO_MIN : HUSH_JSON_THREE_MIN;
    if (bytes == HUSH_JSON_FOUR_BYTES) minimum = HUSH_JSON_FOUR_MIN;
    if (scalar < minimum || scalar > HUSH_JSON_SCALAR_MAX ||
        (scalar >= HUSH_JSON_HIGH_START && scalar <= HUSH_JSON_LOW_END))
        return HUSH_ERR_PARSE;
    *out = scalar;
    return HUSH_OK;
}

static hush_status_t hush_json_take_hex(hush_json_cursor_t *cursor, uint32_t *out)
{
    assert(cursor != NULL);
    assert(out != NULL);
    static const char digits[] = "0123456789abcdef";
    *out = 0;
    for (size_t i = 0; i < (size_t)HUSH_JSON_HEX_DIGITS; ++i) {
        if (cursor->offset == cursor->end) return HUSH_ERR_PARSE;
        char ch = cursor->text[cursor->offset++];
        const char *digit = strchr(digits, ch);
        if (ch >= 'A' && ch <= 'F') digit = strchr(digits, ch - 'A' + 'a');
        if (digit == NULL || ch == '\0') return HUSH_ERR_PARSE;
        *out = *out * HUSH_JSON_HEX_BASE + (uint32_t)(digit - digits);
    }
    return HUSH_OK;
}

static hush_status_t hush_json_take_escape(hush_json_cursor_t *cursor, uint32_t *out)
{
    assert(cursor != NULL);
    assert(out != NULL);
    if (cursor->offset == cursor->end) return HUSH_ERR_PARSE;
    char ch = cursor->text[cursor->offset++];
    if (ch != 'u') {
        const char *codes = "\"\\/bfnrt";
        const char *values = "\"\\/\b\f\n\r\t";
        const char *hit = strchr(codes, ch);
        if (hit == NULL || ch == '\0') return HUSH_ERR_PARSE;
        *out = (unsigned char)values[hit - codes];
        return HUSH_OK;
    }
    HUSH_TRY(hush_json_take_hex(cursor, out));
    if (*out >= HUSH_JSON_LOW_START && *out <= HUSH_JSON_LOW_END)
        return HUSH_ERR_PARSE;
    if (*out < HUSH_JSON_HIGH_START || *out > HUSH_JSON_HIGH_END)
        return HUSH_OK;
    if (cursor->offset + HUSH_JSON_PAIR_BYTES > cursor->end ||
        memcmp(cursor->text + cursor->offset, "\\u", HUSH_JSON_PAIR_BYTES) != 0)
        return HUSH_ERR_PARSE;
    cursor->offset += HUSH_JSON_PAIR_BYTES;
    uint32_t low = 0;
    HUSH_TRY(hush_json_take_hex(cursor, &low));
    if (low < HUSH_JSON_LOW_START || low > HUSH_JSON_LOW_END) return HUSH_ERR_PARSE;
    *out = HUSH_JSON_FOUR_MIN + ((*out - HUSH_JSON_HIGH_START) << HUSH_JSON_SURROGATE_BITS)
        + low - HUSH_JSON_LOW_START;
    return HUSH_OK;
}

static hush_status_t hush_json_put_scalar(char *out, size_t outsz, size_t *offset,
                                         uint32_t scalar)
{
    assert(out != NULL);
    assert(offset != NULL);
    size_t bytes = 1;
    if (scalar >= HUSH_JSON_TWO_MIN) bytes = HUSH_JSON_PAIR_BYTES;
    if (scalar >= HUSH_JSON_THREE_MIN) bytes = HUSH_JSON_THREE_BYTES;
    if (scalar >= HUSH_JSON_FOUR_MIN) bytes = HUSH_JSON_FOUR_BYTES;
    if (*offset + bytes >= outsz) return HUSH_ERR_FULL;
    for (size_t i = bytes - 1; i > 0 && i < (size_t)HUSH_JSON_UTF8_MAX; --i) {
        out[*offset + i] = (char)(HUSH_JSON_CONT_TAG | (scalar & HUSH_JSON_CONT_VALUE));
        scalar >>= HUSH_JSON_CONT_BITS;
    }
    uint32_t tag = bytes == 1 ? 0 : HUSH_JSON_TWO_TAG;
    if (bytes == HUSH_JSON_THREE_BYTES) tag = HUSH_JSON_THREE_TAG;
    if (bytes == HUSH_JSON_FOUR_BYTES) tag = HUSH_JSON_FOUR_TAG;
    out[*offset] = (char)(scalar | tag);
    *offset += bytes;
    out[*offset] = '\0';
    return HUSH_OK;
}

static int hush_json_keep_hex(unsigned char ch)
{
    if (ch >= '0' && ch <= '9')
        return (int)(ch - '0');
    if (ch >= 'a' && ch <= 'f')
        return (int)(ch - 'a') + 10;
    if (ch >= 'A' && ch <= 'F')
        return (int)(ch - 'A') + 10;
    return -1;
}

/* Lossy body copy: decode RFC 8259 escapes when well-formed; keep content
 * otherwise. Invalid UTF-8 raw bytes become U+FFFD so session JSON stays
 * valid — never blank the field. */
static size_t hush_json_keep_put(char *out, size_t outsz, size_t o,
                                 const char *bytes, size_t n)
{
    size_t k;

    if (o + n >= outsz)
        return o;
    for (k = 0; k < n; k++)
        out[o + k] = bytes[k];
    return o + n;
}

static size_t hush_json_keep_body(char *out, size_t outsz,
                                  const char *body, size_t body_len)
{
    static const char repl[] = "\xEF\xBF\xBD"; /* U+FFFD */
    size_t i = 0;
    size_t o = 0;

    assert(out != NULL && outsz > 0);
    if (body == NULL)
        body_len = 0;
    while (i < body_len && o + 1 < outsz) {
        unsigned char ch = (unsigned char)body[i];

        if (ch == '\\' && i + 1 < body_len) {
            i++;
            ch = (unsigned char)body[i];
            if (ch == '"' || ch == '\\' || ch == '/') {
                o = hush_json_keep_put(out, outsz, o, (const char *)&ch, 1);
                i++;
                continue;
            }
            if (ch == 'n' || ch == 'r' || ch == 't' || ch == 'b' || ch == 'f') {
                char ctrl = '\n';

                if (ch == 'r')
                    ctrl = '\r';
                else if (ch == 't')
                    ctrl = '\t';
                else if (ch == 'b')
                    ctrl = '\b';
                else if (ch == 'f')
                    ctrl = '\f';
                o = hush_json_keep_put(out, outsz, o, &ctrl, 1);
                i++;
                continue;
            }
            if (ch == 'u' && i + 4 < body_len) {
                unsigned code = 0;
                int ok = 1;
                size_t h;

                for (h = 1; h <= 4; h++) {
                    int digit = hush_json_keep_hex((unsigned char)body[i + h]);

                    if (digit < 0) {
                        ok = 0;
                        break;
                    }
                    code = (code << 4) | (unsigned)digit;
                }
                if (ok) {
                    char utf[4];
                    size_t n = 0;

                    i += 5;
                    if (code == 0)
                        code = 0xFFFDu;
                    if (code <= 0x7Fu) {
                        utf[0] = (char)code;
                        n = 1;
                    } else if (code <= 0x7FFu) {
                        utf[0] = (char)(0xC0u | (code >> 6));
                        utf[1] = (char)(0x80u | (code & 0x3Fu));
                        n = 2;
                    } else if (code <= 0xFFFFu) {
                        utf[0] = (char)(0xE0u | (code >> 12));
                        utf[1] = (char)(0x80u | ((code >> 6) & 0x3Fu));
                        utf[2] = (char)(0x80u | (code & 0x3Fu));
                        n = 3;
                    } else {
                        n = 0;
                    }
                    if (n == 0)
                        o = hush_json_keep_put(out, outsz, o, repl, 3);
                    else
                        o = hush_json_keep_put(out, outsz, o, utf, n);
                    continue;
                }
            }
            o = hush_json_keep_put(out, outsz, o, (const char *)&ch, 1);
            i++;
            continue;
        }
        /* Raw byte(s): copy a well-formed UTF-8 scalar, else U+FFFD. */
        {
            size_t remain = body_len - i;
            size_t seq = 0;
            unsigned char b0 = ch;

            if (b0 <= 0x7Fu)
                seq = 1;
            else if (b0 >= 0xC2u && b0 <= 0xDFu && remain >= 2
                     && ((unsigned char)body[i + 1] & 0xC0u) == 0x80u)
                seq = 2;
            else if (b0 >= 0xE0u && b0 <= 0xEFu && remain >= 3
                     && ((unsigned char)body[i + 1] & 0xC0u) == 0x80u
                     && ((unsigned char)body[i + 2] & 0xC0u) == 0x80u)
                seq = 3;
            else if (b0 >= 0xF0u && b0 <= 0xF4u && remain >= 4
                     && ((unsigned char)body[i + 1] & 0xC0u) == 0x80u
                     && ((unsigned char)body[i + 2] & 0xC0u) == 0x80u
                     && ((unsigned char)body[i + 3] & 0xC0u) == 0x80u)
                seq = 4;
            if (seq == 0) {
                o = hush_json_keep_put(out, outsz, o, repl, 3);
                i++;
            } else {
                o = hush_json_keep_put(out, outsz, o, body + i, seq);
                i += seq;
            }
        }
    }
    out[o] = '\0';
    return o;
}

hush_status_t hush_json_decode_keep(char *out, size_t outsz,
                                    const hush_json_value_t *value)
{
    hush_status_t st;

    if (out == NULL || outsz == 0 || value == NULL || value->start == NULL)
        return HUSH_ERR_ARG;
    st = hush_json_decode(out, outsz, value);
    if (st == HUSH_OK)
        return HUSH_OK;
    /* Strict decode refused (invalid UTF-8, mid-character cut from an older
     * save, etc.). Keep the string body so restart cannot erase the field. */
    if (value->len < (size_t)2 || value->start[0] != '"'
        || value->start[value->len - 1] != '"') {
        out[0] = '\0';
        return st;
    }
    (void)hush_json_keep_body(out, outsz, value->start + 1, value->len - 2);
    return out[0] != '\0' ? HUSH_OK : st;
}
