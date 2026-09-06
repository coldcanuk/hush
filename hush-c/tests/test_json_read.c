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
/* Verifies each required selected value without permitting truncation. */
static void hush_json_test_case(const hush_json_test_case_t *test);

/* C runtime ABI requires an integer status and unprefixed entry point. */
int main(void)
{
    hush_json_test_selection();
    hush_json_test_rejection();
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
