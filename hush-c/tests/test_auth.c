/* tests/test_auth.c: session token mint, file mode, and constant-time compare. */

#define _XOPEN_SOURCE 700 /* nftw, for the private root cleanup */

#include <assert.h>
#include <ctype.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hush_auth.h"

enum {
    /* Longest private root path; every path a test derives from it fits. */
    ROOT_PATH_MAX = 96,
    /* Open-fd budget for the nftw cleanup walk. */
    ROOT_FTW_FDS = 16,
    /* Exit status when the root cannot be removed; atexit cannot return one. */
    ROOT_CLEANUP_FAIL_RC = 1
};

/* <TMPDIR or /tmp>/hush-auth-XXXXXX: this run's private root, 0700 from mkdtemp.
 * Every key, pass store, home and config the test writes lives under it. */
#define ROOT_NAME "hush-auth-XXXXXX"
#define ROOT_TMP_DEFAULT "/tmp"

/* The private root and the pid that made it. File scope because an atexit
 * handler takes no argument. Empty until root_make succeeds and again after
 * root_remove. */
static char g_root[ROOT_PATH_MAX];
static pid_t g_root_pid;

/* Makes g_root under TMPDIR (/tmp when TMPDIR is unset, empty, or not
 * absolute); root_remove, registered with atexit, removes it on every return
 * from main and every exit(). Returns 0, after a FAIL line, when root_holds
 * fails its own check, the path does not fit, mkdtemp fails, or atexit
 * refuses. */
static int root_make(void);

/* atexit handler: removes g_root and everything under it, in the making
 * process only, so a forked child that exits cannot take the root early.
 * When the walk fails it prints FAIL and ends the process with
 * ROOT_CLEANUP_FAIL_RC, so a leftover root fails the test. */
static void root_remove(void);

/* nftw callback: removes one entry under g_root; FTW_DEPTH visits children
 * first. */
static int root_remove_entry(const char *path, const struct stat *st, int flag,
                             struct FTW *ftw);

/* True when path is root or lies below it: root must match whole path
 * components, so "/r" holds "/r" and "/r/k" but not the sibling "/rX/k". */
static int root_holds(const char *root, const char *path);

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
    char path[256];
    char token[HUSH_AUTH_TOKEN_BUF];
    char again[HUSH_AUTH_TOKEN_BUF];
    struct stat st;
    size_t i;

    /* The session token is a secret like a key: it lives only in the
     * private root (0700), which root_remove deletes on every exit. */
    if (!root_make())
        return 1;
    if (setenv("HUSH_HOME", g_root, 1) != 0 ||
        setenv("HUSH_CONFIG_DIR", g_root, 1) != 0)
        return 1;
    expect(hush_auth_init() == HUSH_OK, "init");
    expect(hush_auth_token_copy(token, sizeof(token)) == HUSH_OK, "copy");
    expect(strlen(token) == (size_t)HUSH_AUTH_TOKEN_HEX, "hex length");
    for (i = 0; i < (size_t)HUSH_AUTH_TOKEN_HEX; ++i)
        expect(isxdigit((unsigned char)token[i]) != 0, "hex digit");
    expect(stat(g_root, &st) == 0 && (st.st_mode & (S_IRWXG | S_IRWXO)) == 0,
           "token dir is 0700");
    snprintf(path, sizeof(path), "%s/%s", g_root, HUSH_AUTH_FILE);
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

static int root_make(void)
{
    const char *tmp = getenv("TMPDIR");
    int wrote = 0;

    assert(g_root[0] == '\0');
    /* nftw never hands root_remove_entry a path outside the root, so this
     * direct check is the only place the prefix-sibling case is exercised. */
    if (!root_holds("/r", "/r") || !root_holds("/r", "/r/k") ||
        root_holds("/r", "/rX/k") || root_holds("/r", "/rX")) {
        fprintf(stderr, "FAIL root_holds accepts a prefix sibling\n");
        return 0;
    }
    /* A relative TMPDIR would leave g_root relative, and a test that
     * chdirs would then walk the wrong dir at exit. */
    if (tmp == NULL || tmp[0] != '/')
        tmp = ROOT_TMP_DEFAULT;
    wrote = snprintf(g_root, sizeof(g_root), "%s/%s", tmp, ROOT_NAME);
    if (wrote < 0 || (size_t)wrote >= sizeof(g_root) || mkdtemp(g_root) == NULL) {
        g_root[0] = '\0';
        fprintf(stderr, "FAIL private root under %s\n", tmp);
        return 0;
    }
    g_root_pid = getpid();
    /* Deviation (section 7): atexit takes a bare callback; no table fits. */
    if (atexit(root_remove) != 0) {
        root_remove();
        fprintf(stderr, "FAIL atexit for the private root\n");
        return 0;
    }
    return 1;
}

static void root_remove(void)
{
    if (g_root[0] == '\0' || getpid() != g_root_pid)
        return;
    /* FTW_PHYS never follows a symlink out of the root; FTW_MOUNT never
     * walks into another filesystem mounted under it. Deviation (section 7):
     * nftw takes a bare callback; no table fits. */
    if (nftw(g_root, root_remove_entry, ROOT_FTW_FDS,
             FTW_DEPTH | FTW_PHYS | FTW_MOUNT) != 0) {
        fprintf(stderr, "FAIL private root cleanup: %s\n", g_root);
        (void)fflush(NULL);
        /* exit() may not run inside an atexit handler; _exit may. */
        _exit(ROOT_CLEANUP_FAIL_RC);
    }
    g_root[0] = '\0';
}

static int root_remove_entry(const char *path, const struct stat *st, int flag,
                             struct FTW *ftw)
{
    (void)st;
    (void)flag;
    (void)ftw;
    /* Defense in depth only: FTW_PHYS and FTW_MOUNT keep the walk inside
     * g_root, so this assert cannot fire through nftw. root_make checks
     * root_holds directly instead. */
    assert(root_holds(g_root, path));
    return remove(path);
}

static int root_holds(const char *root, const char *path)
{
    size_t n = 0;

    assert(root != NULL && path != NULL);
    n = strlen(root);
    return n > 0 && strncmp(path, root, n) == 0 &&
           (path[n] == '\0' || path[n] == '/');
}
