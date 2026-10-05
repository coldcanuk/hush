/* test_json_read.c: verifies scoped selection, Unicode decoding, and capacity errors. */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "hush_json.h"

enum { HUSH_JSON_TEST_BUFFER_BYTES = 128 };

typedef struct {
    const char *json;
    const char *path;
    const char *expected;
} hush_json_test_case_t;

/* Checks complete selection/decoding for every required fixture. */
static void hush_json_test_selection(void);
/* Checks malformed encoding, missing paths, and insufficient capacity. */
static void hush_json_test_rejection(void);
static void hush_json_test_decode_keep(void);
/* Verifies each required selected value without permitting truncation. */
static void hush_json_test_case(const hush_json_test_case_t *test);


/* Invalid UTF-8 in a JSON string must not blank under decode_keep. */
static void hush_json_test_decode_keep(void)
{
    char decoded[HUSH_JSON_TEST_BUFFER_BYTES] = {0};
    /* "A" + lone 0xC3 + "B" — strict decode fails; keep must retain content. */
    char raw[] = { '"', 'A', (char)0xC3, 'B', '"', 0 };
    hush_json_value_t value = { .start = raw, .len = 5 };

    assert(hush_json_decode(decoded, sizeof(decoded), &value) == HUSH_ERR_PARSE);
    assert(hush_json_decode_keep(decoded, sizeof(decoded), &value) == HUSH_OK);
    assert(decoded[0] == 'A');
    /* Lone 0xC3 becomes U+FFFD (ef bf bd), then 'B'. */
    assert((unsigned char)decoded[1] == 0xEF);
    assert((unsigned char)decoded[2] == 0xBF);
    assert((unsigned char)decoded[3] == 0xBD);
    assert(decoded[4] == 'B');
    /* Good escapes still decode on the STRICT path (valid UTF-8 body). */
    {
        const char *esc = "\"x\\b\\fy\"";
        hush_json_value_t v = { .start = esc, .len = strlen(esc) };
        assert(hush_json_decode_keep(decoded, sizeof(decoded), &v) == HUSH_OK);
        assert(strcmp(decoded, "x\b\fy") == 0);
    }
    /* Fallback path (strict fails on invalid UTF-8): \b \f \t and \u still decode. */
    {
        /* "x\b\f\t\u0041" + lone 0xFF + "y" */
        char fb[] = {
            '"', 'x', '\\', 'b', '\\', 'f', '\\', 't', '\\', 'u', '0', '0', '4', '1',
            (char)0xFF, 'y', '"', 0
        };
        hush_json_value_t v = { .start = fb, .len = sizeof(fb) - 1 };
        assert(hush_json_decode(decoded, sizeof(decoded), &v) == HUSH_ERR_PARSE);
        assert(hush_json_decode_keep(decoded, sizeof(decoded), &v) == HUSH_OK);
        assert(decoded[0] == 'x');
        assert(decoded[1] == '\b');
        assert(decoded[2] == '\f');
        assert(decoded[3] == '\t');
        assert(decoded[4] == 'A');
        assert((unsigned char)decoded[5] == 0xEF);
        assert((unsigned char)decoded[6] == 0xBF);
        assert((unsigned char)decoded[7] == 0xBD);
        assert(decoded[8] == 'y');
        assert(decoded[9] == '\0');
    }
    /* CESU / overlong / OOR raw sequences become U+FFFD, not copied raw. */
    {
        char cesu[] = {
            '"', (char)0xED, (char)0xA0, (char)0x80, 'X',
            (char)0xE0, (char)0x80, (char)0x80, 'Y',
            (char)0xF4, (char)0x90, (char)0x80, (char)0x80, 'Z', '"', 0
        };
        hush_json_value_t v = { .start = cesu, .len = sizeof(cesu) - 1 };
        assert(hush_json_decode_keep(decoded, sizeof(decoded), &v) == HUSH_OK);
        /* Each invalid byte → U+FFFD; ASCII separators remain. */
        /* ED A0 80 → 3×FFFD, X; E0 80 80 → 3×FFFD, Y; F4… → 4×FFFD, Z */
        assert((unsigned char)decoded[0] == 0xEF);
        assert((unsigned char)decoded[9] == 'X');
        assert((unsigned char)decoded[10] == 0xEF);
        assert((unsigned char)decoded[19] == 'Y');
        assert((unsigned char)decoded[20] == 0xEF);
        assert((unsigned char)decoded[32] == 'Z');
        assert(decoded[33] == '\0');
    }
    /* keep_put guard must stay >= (not >): "AB" + 0xFF into 5-byte buf.
     * FFFD needs 3 bytes after "AB"; o+n >= outsz refuses; mutant ">" overflows. */
    {
        char tiny[5];
        char overflow[] = { '"', 'A', 'B', (char)0xFF, '"', 0 };
        hush_json_value_t v = { .start = overflow, .len = 5 };
        memset(tiny, 0xA5, sizeof(tiny));
        assert(hush_json_decode_keep(tiny, sizeof(tiny), &v) == HUSH_OK);
        assert(tiny[0] == 'A' && tiny[1] == 'B' && tiny[2] == '\0');
        /* Bytes past the NUL must stay untouched (no overflow write). */
        assert((unsigned char)tiny[3] == 0xA5);
        assert((unsigned char)tiny[4] == 0xA5);
    }
}

/* C runtime ABI requires an integer status and unprefixed entry point. */
int main(void)
{
    hush_json_test_selection();
    hush_json_test_rejection();
    hush_json_test_decode_keep();
    return puts("test_json_read ok") < 0 ? 1 : 0;
}

static void hush_json_test_case(const hush_json_test_case_t *test)
{
    assert(test != NULL);
    hush_json_value_t value = {0};
    assert(hush_json_lookup(&value, test->json, test->path) == HUSH_OK);
    char decoded[HUSH_JSON_TEST_BUFFER_BYTES] = {0};
    assert(hush_json_decode(decoded, sizeof(decoded), &value) == HUSH_OK);
    assert(strcmp(decoded, test->expected) == 0);
}

static void hush_json_test_selection(void)
{
    static const hush_json_test_case_t cases[] = {
        {"{\"a\":{\"text\":\"wrong\"},\"text\":\"right\"}", "/text", "right"},
        {"{\"content\":[{\"text\":\"first\"},{\"text\":\"second\"}]}", "/content/1/text", "second"},
        {"{\"text\":\"embedded \\\"text\\\": is content\"}", "/text", "embedded \"text\": is content"},
        {"{\"t\\u0065xt\":\"\\u00e9 \\uD83C\\uDF3F\"}", "/text", "é 🌿"},
        {"{\"text\":\"a\\n\\t\\\\\\\"b\"}", "/text", "a\n\t\\\"b"}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
        hush_json_test_case(&cases[i]);
    size_t count = 0;
    assert(hush_json_count_chars(&count, "aé🌿", sizeof("aé🌿")) == HUSH_OK);
    assert(count == strlen("abc"));
}

static void hush_json_test_rejection(void)
{
    static const char *const invalid[] = {
        "\"\\ud800\"", "\"\\udc00\"", "\"\\ud800\\u0041\"", "\"\\u0000\"",
        "\"\\q\"", "\"\\u12zz\"", "\"\xc0\xaf\"", "\"\xf4\x90\x80\x80\"", "null", "42"
    };
    char decoded[HUSH_JSON_TEST_BUFFER_BYTES] = {0};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        hush_json_value_t value = {.start = invalid[i], .len = strlen(invalid[i])};
        assert(hush_json_decode(decoded, sizeof(decoded), &value) == HUSH_ERR_PARSE);
    }
    hush_json_value_t value = {0};
    assert(hush_json_lookup(&value, "{\"x\":\"value\"}", "/missing") == HUSH_ERR_NOT_FOUND);
    assert(hush_json_lookup(&value, "{\"x\":\"value\"}junk", "/x") == HUSH_ERR_PARSE);
    assert(hush_json_lookup(&value, "{\"x\":[}", "/x") == HUSH_ERR_PARSE);
    assert(hush_json_lookup(&value, "\"🌿\"", "") == HUSH_OK);
    char short_buffer[HUSH_JSON_UTF8_MAX] = {0};
    assert(hush_json_decode(short_buffer, sizeof(short_buffer), &value) == HUSH_ERR_FULL);
    size_t count = 0;
    assert(hush_json_count_chars(&count, "long", 1) == HUSH_ERR_FULL);
}
