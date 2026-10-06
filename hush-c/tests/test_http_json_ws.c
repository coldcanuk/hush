/* tests/test_http_json_ws.c: whitespace around JSON keys and values. */

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

static void field_is(const char *body, const char *key, const char *want,
                     const char *msg)
{
    char out[64];

    expect(hush_http_json_has_key(body, key) == 1, msg);
    expect(hush_http_json_field(body, key, out, sizeof(out)) == 1, msg);
    expect(strcmp(out, want) == 0, msg);
}

int main(void)
{
    char out[64];

    field_is("{\"approval_mode\": \"auto\"}", "approval_mode", "auto",
             "space after colon");
    field_is("{\"approval_mode\" : \"auto_approve\"}", "approval_mode",
             "auto_approve", "space before colon");
    field_is("{\"approval_mode\":\t\"auto_approve\"}", "approval_mode",
             "auto_approve", "tab after colon");
    field_is("{\"first_name\": \"Ada Lovelace\"}", "first_name",
             "Ada Lovelace", "space inside string");
    field_is("{\n\"first_name\":\"Ada\"\n}", "first_name", "Ada",
             "newline before key");
    field_is("{\t\"first_name\": \"Ada\"}", "first_name", "Ada",
             "tab before key");
    field_is("{\"theme\":\"dark\", \"first_name\": \"Ada\"}", "first_name",
             "Ada", "comma space before key");
    expect(hush_http_json_has_key("{\"theme\":\"dark\"}", "first_name") == 0,
           "missing key stays missing");
    expect(hush_http_json_field("{\"theme\":\"dark\"}", "first_name", out,
                                sizeof(out)) == 0, "missing field stays empty");
    expect(out[0] == '\0', "missing field buffer empty");
    expect(hush_http_json_bare_field("{\"approval_mode\": \"auto\"}",
                                     "approval_mode", out, sizeof(out)) == 0,
           "bare field rejects a string");
    field_is("{\"approval_mode\":\"auto_approve\"}", "approval_mode",
             "auto_approve", "compact still works");
    expect(hush_http_json_bare_field("{\"dev_log_enabled\": 0}",
                                     "dev_log_enabled", out, sizeof(out)) == 1,
           "bare zero");
    expect(strcmp(out, "0") == 0, "bare zero text");
    field_is("{\"save_pass\":false}", "save_pass", "false", "bare false");
    field_is("{\"save_pass\": false}", "save_pass", "false",
             "spaced bare false");
    field_is("{\"enabled\":true}", "enabled", "true", "bare true");
    expect(hush_http_json_has_key("{\"name\":\"\"}", "name") == 1,
           "empty string is present");
    expect(hush_http_json_field("{\"name\":\"\"}", "name", out, sizeof(out)) == 0,
           "empty string reads empty");
    if (g_fail)
        return 1;
    printf("test_http_json_ws ok\n");
    return 0;
}
