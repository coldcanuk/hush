/* tests/test_auth.c: session token mint, file mode, and constant-time compare. */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hush_auth.h"

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
    char home[] = "/tmp/hush-auth-XXXXXX";
    char path[256];
    char token[HUSH_AUTH_TOKEN_BUF];
    char again[HUSH_AUTH_TOKEN_BUF];
    struct stat st;
    size_t i;

    if (mkdtemp(home) == NULL)
        return 1;
    if (setenv("HUSH_HOME", home, 1) != 0 ||
        setenv("HUSH_CONFIG_DIR", home, 1) != 0)
        return 1;
    expect(hush_auth_init() == HUSH_OK, "init");
    expect(hush_auth_token_copy(token, sizeof(token)) == HUSH_OK, "copy");
    expect(strlen(token) == (size_t)HUSH_AUTH_TOKEN_HEX, "hex length");
    for (i = 0; i < (size_t)HUSH_AUTH_TOKEN_HEX; ++i)
        expect(isxdigit((unsigned char)token[i]) != 0, "hex digit");
    snprintf(path, sizeof(path), "%s/%s", home, HUSH_AUTH_FILE);
    expect(stat(path, &st) == 0, "token file");
    expect((st.st_mode & (S_IRWXG | S_IRWXO)) == 0, "token file is 0600");
    expect(hush_auth_token_matches(token) == 1, "matches live token");
    expect(hush_auth_token_matches("deadbeef") == 0, "rejects wrong token");
    expect(hush_auth_token_matches("") == 0, "rejects empty token");
    expect(hush_auth_token_matches(NULL) == 0, "rejects NULL token");
    expect(hush_auth_tokens_equal("abc", "abc") == 1, "equal strings");
    expect(hush_auth_tokens_equal("abc", "abd") == 0, "different strings");
    expect(hush_auth_tokens_equal("abc", "ab") == 0, "different lengths");
    expect(hush_auth_tokens_equal(NULL, "ab") == 0, "NULL is unequal");
    expect(hush_auth_token_copy(again, sizeof(again)) == HUSH_OK, "copy again");
    expect(strcmp(token, again) == 0, "token is stable");
    expect(hush_auth_token_copy(again, 4) == HUSH_ERR_ARG, "short output");
    if (g_fail)
        return 1;
    printf("test_auth ok\n");
    return 0;
}
