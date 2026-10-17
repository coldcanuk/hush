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

/* #361: the one-shot secret mint is 128-bit CSPRNG hex, fresh each call,
 * and refuses a short buffer without writing past it. */
static void check_secret_mint(void)
{
    char a[HUSH_AUTH_SECRET_BUF] = {0};
    char b[HUSH_AUTH_SECRET_BUF] = {0};
    char tiny[4] = {'x', 'x', 'x', '\0'};
    size_t i = 0;

    expect(hush_auth_secret_mint(a, sizeof(a)) == HUSH_OK, "secret mint");
    expect(hush_auth_secret_mint(b, sizeof(b)) == HUSH_OK, "secret mint again");
    expect(strlen(a) == (size_t)HUSH_AUTH_SECRET_HEX, "secret is 32 hex");
    for (i = 0; i < (size_t)HUSH_AUTH_SECRET_HEX; ++i)
        expect(isxdigit((unsigned char)a[i]) != 0 && !isupper((unsigned char)a[i]),
               "secret is lowercase hex");
    expect(strcmp(a, b) != 0, "two secrets differ");
    expect(hush_auth_secret_mint(tiny, sizeof(tiny)) == HUSH_ERR_ARG, "secret short buffer");
    expect(strcmp(tiny, "xxx") == 0, "a short buffer is left alone");
    expect(hush_auth_secret_mint(NULL, sizeof(a)) == HUSH_ERR_ARG, "secret NULL buffer");
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
    expect(hush_auth_tokens_equal("ab", "abc") == 0, "longer presented token");
    expect(hush_auth_tokens_equal("abc", "") == 0, "empty presented token");
    expect(hush_auth_tokens_equal(NULL, "ab") == 0, "NULL is unequal");
    expect(hush_auth_token_copy(again, sizeof(again)) == HUSH_OK, "copy again");
    expect(strcmp(token, again) == 0, "token is stable");
    expect(hush_auth_token_copy(again, 4) == HUSH_ERR_ARG, "short output");
    {
        char challenge[HUSH_AUTH_CHALLENGE_BUF];
        char other[HUSH_AUTH_CHALLENGE_BUF];

        expect(hush_auth_challenge_mint(challenge, sizeof(challenge)) == HUSH_OK,
               "challenge mint");
        expect(strlen(challenge) == (size_t)HUSH_AUTH_CHALLENGE_HEX,
               "challenge length");
        for (i = 0; i < (size_t)HUSH_AUTH_CHALLENGE_HEX; ++i)
            expect(isxdigit((unsigned char)challenge[i]) != 0, "challenge hex");
        expect(hush_auth_challenge_mint(other, sizeof(other)) == HUSH_OK,
               "second mint");
        expect(strcmp(challenge, other) != 0, "challenges differ");
        expect(hush_auth_challenge_mint(challenge, 4) == HUSH_ERR_ARG,
               "challenge short buffer");
    }
    {
        char hash[HUSH_AUTH_SHA256_BUF];
        static const char abc[] =
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

        expect(hush_auth_sha256_hex("abc", hash, sizeof(hash)) == HUSH_OK,
               "sha256");
        expect(strcmp(hash, abc) == 0, "sha256(abc) vector");
        expect(hush_auth_sha256_hex("abc", hash, 4) == HUSH_ERR_ARG,
               "sha256 short buffer");
        expect(hush_auth_sha256_hex(NULL, hash, sizeof(hash)) == HUSH_ERR_ARG,
               "sha256 NULL text");
        expect(hush_auth_join_matches("abc", hash) == 1, "join matches");
        expect(hush_auth_join_matches("abd", hash) == 0, "join mismatch");
        expect(hush_auth_join_matches(NULL, hash) == 0, "join NULL");
        expect(hush_auth_join_matches("", hash) == 0, "join empty");
    }
    {
        /* #336: one in-memory browser-session secret, rotated per pairing. */
        char ui[HUSH_AUTH_UI_BUF];
        char old[HUSH_AUTH_UI_BUF];
        char pair[HUSH_AUTH_PAIR_BUF];

        expect(hush_auth_ui_copy(ui, sizeof(ui)) == HUSH_ERR_IO, "no ui secret before pairing");
        expect(hush_auth_ui_matches("") == 0, "no ui secret matches nothing");
        expect(hush_auth_ui_rotate() == HUSH_OK, "ui rotate");
        expect(hush_auth_ui_copy(old, sizeof(old)) == HUSH_OK, "ui copy");
        expect(strlen(old) == (size_t)HUSH_AUTH_UI_HEX, "ui hex length");
        expect(strcmp(old, token) != 0, "ui secret is not the token");
        expect(hush_auth_ui_matches(old) == 1, "live ui secret matches");
        expect(hush_auth_ui_matches(token) == 0, "the token is no ui secret");
        {
            char near[HUSH_AUTH_UI_BUF];

            memcpy(near, old, sizeof(near));
            near[HUSH_AUTH_UI_HEX - 1] = near[HUSH_AUTH_UI_HEX - 1] == '0' ? '1' : '0';
            expect(hush_auth_ui_matches(near) == 0, "a one-digit miss does not match");
        }
        expect(hush_auth_ui_rotate() == HUSH_OK, "ui rotate again");
        expect(hush_auth_ui_copy(ui, sizeof(ui)) == HUSH_OK, "ui copy again");
        expect(strcmp(ui, old) != 0 && hush_auth_ui_matches(old) == 0,
               "rotation ends the old ui secret");
        expect(hush_auth_ui_copy(ui, 4) == HUSH_ERR_ARG, "ui short buffer");
        expect(hush_auth_pair_init() == HUSH_OK, "pair init");
        expect(hush_auth_pair_copy(pair, sizeof(pair)) == HUSH_OK, "pair copy");
        expect(strcmp(pair, token) != 0 && strcmp(pair, ui) != 0, "pair code is its own");
        expect(hush_auth_ui_copy(old, sizeof(old)) == HUSH_OK, "ui before pairing");
        expect(hush_auth_pair_take(pair) == 1, "pair code redeems once");
        expect(hush_auth_pair_take(pair) == 0, "a spent pair code is dead");
        expect(hush_auth_ui_matches(old) == 0, "pairing ends the earlier ui secret");
        expect(hush_auth_ui_copy(ui, sizeof(ui)) == HUSH_OK && hush_auth_ui_matches(ui) == 1,
               "the paired window's ui secret is live");
        expect(hush_auth_pair_copy(pair, sizeof(pair)) == HUSH_OK, "re-minted pair code");
        memcpy(old, ui, sizeof(old));
        expect(hush_auth_pair_take(pair) == 1, "a second pairing redeems");
        expect(hush_auth_ui_matches(old) == 0, "a second pairing ends the first window's secret");
        expect(hush_auth_ui_copy(ui, sizeof(ui)) == HUSH_OK && strcmp(ui, old) != 0 &&
               hush_auth_ui_matches(ui) == 1, "the second window holds the only live secret");
        {
            char cut[HUSH_AUTH_UI_BUF];

            memcpy(cut, ui, sizeof(cut));
            cut[HUSH_AUTH_UI_HEX - 1] = '\0';
            expect(hush_auth_ui_matches(cut) == 0, "a truncated ui secret does not match");
        }
    }
    check_secret_mint();
    if (g_fail)
        return 1;
    printf("test_auth ok\n");
    return 0;
}
