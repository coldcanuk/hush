/* tests/test_canvas_take.c: #361 pins below the HTTP layer.
 *   - the job token is compared at its exact length (token+"x", token
 *     minus one char never name the busy job);
 *   - every compare goes through hush_auth_tokens_equal (constant time);
 *   - a take is single use: the second take of the same token is
 *     HUSH_ERR_NOT_FOUND, never HUSH_ERR_IO or HUSH_OK;
 *   - the take wipes the token and the result (hush_secure_zero seen with
 *     both buffer sizes).
 * Linked with -Wl,--wrap=hush_auth_tokens_equal -Wl,--wrap=hush_secure_zero. */
#define _XOPEN_SOURCE 700

#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "hush_canvas.h"
#include "hush_event.h"

/* Readiness probes (#356): the stub answers --version and --help, runs nothing. */
#define CT_GROK_READY \
    "case \"$1\" in --version) echo \"grok 0.2.50\"; exit 0;; --help) echo " \
    "\"--permission-mode dontAsk --allow --deny --disallowed-tools\"; exit 0;; esac\n"

enum {
    CT_PATH_MAX = 1024,
    CT_POLL_STEPS = 200,
    CT_POLL_NS = 50000000,
    CT_DIR_MODE = 0700,
    CT_EXE_MODE = 0755,
    CT_FTW_FDS = 16
};

static int g_fail = 0;
static int g_equal_calls = 0;
static int g_zero_token = 0;
static int g_zero_out = 0;

int __real_hush_auth_tokens_equal(const char *a, const char *b);
void __real_hush_secure_zero(void *buf, size_t n);

int __wrap_hush_auth_tokens_equal(const char *a, const char *b)
{
    g_equal_calls++;
    return __real_hush_auth_tokens_equal(a, b);
}

void __wrap_hush_secure_zero(void *buf, size_t n)
{
    if (n == (size_t)HUSH_CANVAS_TOKEN_MAX)
        g_zero_token++;
    if (n == (size_t)HUSH_EVENT_MAX_CONTENT + 1)
        g_zero_out++;
    __real_hush_secure_zero(buf, n);
}

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

/* root/{user/.grok/auth.json, bin/grok, home}; HOME, HUSH_HOME, PATH set. */
static void fixture(const char *root)
{
    char p[CT_PATH_MAX] = {0};
    char path[CT_PATH_MAX * 2] = {0};
    FILE *f = NULL;

    (void)snprintf(p, sizeof(p), "%s/user", root);
    expect(mkdir(p, CT_DIR_MODE) == 0 && setenv("HOME", p, 1) == 0, "HOME");
    (void)snprintf(p, sizeof(p), "%s/user/.grok", root);
    expect(mkdir(p, CT_DIR_MODE) == 0, ".grok");
    (void)snprintf(p, sizeof(p), "%s/user/.grok/auth.json", root);
    f = fopen(p, "w");
    expect(f != NULL && fputs("{\"ok\":true}\n", f) >= 0 && fclose(f) == 0, "auth.json");
    (void)snprintf(p, sizeof(p), "%s/home", root);
    expect(mkdir(p, CT_DIR_MODE) == 0 && setenv("HUSH_HOME", p, 1) == 0, "HUSH_HOME");
    (void)snprintf(p, sizeof(p), "%s/bin", root);
    expect(mkdir(p, CT_DIR_MODE) == 0, "bin");
    (void)snprintf(path, sizeof(path), "%s:%s", p, getenv("PATH") ? getenv("PATH") : "");
    expect(setenv("PATH", path, 1) == 0, "PATH");
    (void)snprintf(p, sizeof(p), "%s/bin/grok", root);
    f = fopen(p, "w");
    expect(f != NULL && fputs("#!/bin/sh\n" CT_GROK_READY "sleep 0.3\nprintf 'int x;\\n'\n", f) >= 0 &&
           fclose(f) == 0 && chmod(p, CT_EXE_MODE) == 0, "stub grok");
}

/* While the job is busy: only the exact token names it. */
static void check_exact(const char *token)
{
    char longer[HUSH_CANVAS_TOKEN_MAX + 1] = {0};
    char shorter[HUSH_CANVAS_TOKEN_MAX] = {0};
    size_t n = strlen(token);

    if (n == 0) {
        expect(0, "start hands out a token"); /* no token: fail, do not abort */
        return;
    }
    (void)snprintf(longer, sizeof(longer), "%sx", token);
    memcpy(shorter, token, n - 1);
    g_equal_calls = 0;
    expect(hush_canvas_is_busy(token) == 1, "the exact token names the busy job");
    expect(hush_canvas_is_busy(longer) == 0, "token+x does not name the job");
    expect(hush_canvas_is_busy(shorter) == 0, "token minus one char does not name the job");
    expect(g_equal_calls == 3, "every compare goes through hush_auth_tokens_equal");
}

static void wait_done(const char *token)
{
    struct timespec ns = {0, CT_POLL_NS};

    for (int i = 0; i < CT_POLL_STEPS && hush_canvas_is_busy(token); ++i) {
        hush_canvas_poll();
        (void)nanosleep(&ns, NULL);
    }
    expect(hush_canvas_is_busy(token) == 0, "the job finishes");
}

static int rm_entry(const char *path, const struct stat *sb, int flag, struct FTW *ftw)
{
    (void)sb;
    (void)ftw;
    if (flag == FTW_DP)
        (void)rmdir(path);
    else
        (void)unlink(path);
    return 0;
}

int main(void)
{
    char root[] = "/tmp/hush-canvas-take-XXXXXX";
    char token[HUSH_CANVAS_TOKEN_MAX] = {0};
    char out[HUSH_CANVAS_PRED_MAX + 1] = {0};
    hush_status_t first = HUSH_ERR_IO;

    if (mkdtemp(root) == NULL)
        return 1;
    fixture(root);
    hush_canvas_init();
    expect(hush_canvas_start(token, sizeof(token), "int ", ";") == HUSH_OK, "start");
    check_exact(token);
    wait_done(token);
    g_zero_token = 0;
    g_zero_out = 0;
    first = hush_canvas_take(token, out, sizeof(out));
    expect(first == HUSH_OK || first == HUSH_ERR_IO, "first take");
    expect(g_zero_token > 0 && g_zero_out > 0, "a take wipes the token and the result");
    expect(hush_canvas_take(token, out, sizeof(out)) == HUSH_ERR_NOT_FOUND,
           "a second take of the same token finds nothing");
    expect(out[0] == '\0', "a second take hands out nothing");
    hush_canvas_shutdown();
    (void)nftw(root, rm_entry, CT_FTW_FDS, FTW_DEPTH | FTW_PHYS);
    if (g_fail)
        return 1;
    printf("canvas take: exact-length constant-time compare, single use, wiped OK (first=%d)\n",
           (int)first);
    return 0;
}
