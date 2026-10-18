/* tests/test_canvas.c: #361 job-token pins below the HTTP layer.
 *
 * Includes the unit so the test can stage a finished job and read the slot
 * after a take, with no product hook and no child: nothing is forked or
 * exec'd here. The compare is routed through a counting spy, which pins that
 * the canvas names a job only through hush_auth_tokens_equal (exact length,
 * constant time) and never through strcmp/strncmp. */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>

#include "hush_auth.h"

/* Counts, then answers like hush_auth_tokens_equal. */
int test_canvas_spy_equal(const char *a, const char *b);

#define hush_auth_tokens_equal test_canvas_spy_equal
#include "../src/hush_canvas.c"
#undef hush_auth_tokens_equal

enum { TEST_CANVAS_PROBE_MAX = 2 * HUSH_CANVAS_TOKEN_HEX + 1 };

static int g_fail;
static int g_spy_calls;

/* Records a failure. msg is never NULL. */
static void expect(int cond, const char *msg);
/* Stages a finished job (ok, middle) in the slot and copies its token out. */
static void stage_done(char *token, int ok, const char *middle);
/* True when every byte of buf is zero. buf is never NULL. */
static int all_zero(const void *buf, size_t n);
/* A finished job is taken once; the slot keeps no token and no result. */
static void check_single_take(void);
/* A failed job is also forgotten on its one take. */
static void check_failed_take(void);
/* Refuses one near-miss through the spy; the job stays takeable. */
static void expect_refused(const char *probe, const char *msg);
/* Near-miss tokens never name the job; the real one still does. */
static void check_exact_compare(void);

int test_canvas_spy_equal(const char *a, const char *b)
{
    g_spy_calls++;
    return hush_auth_tokens_equal(a, b);
}

int main(void)
{
    check_single_take();
    check_failed_take();
    check_exact_compare();
    hush_canvas_init();
    if (g_fail)
        return 1;
    puts("test_canvas: ok");
    return 0;
}

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

static void stage_done(char *token, int ok, const char *middle)
{
    hush_canvas_init();
    expect(hush_auth_secret_mint(g_job.token, sizeof(g_job.token)) == HUSH_OK,
           "stage: mint a job token");
    memcpy(token, g_job.token, sizeof(g_job.token));
    hush_canvas_copy(g_job.out, sizeof(g_job.out), middle);
    g_job.out_n = strlen(g_job.out);
    g_job.ok = ok;
    g_job.busy = 0;
}

static int all_zero(const void *buf, size_t n)
{
    const unsigned char *p = buf;
    size_t i = 0;

    for (i = 0; i < n; ++i) {
        if (p[i] != 0)
            return 0;
    }
    return 1;
}

static void check_single_take(void)
{
    char token[HUSH_CANVAS_TOKEN_MAX] = {0};
    char out[HUSH_CANVAS_PRED_MAX + 1] = {0};

    stage_done(token, 1, "MIDDLE");
    expect(hush_canvas_take(token, out, sizeof(out)) == HUSH_OK, "first take ok");
    expect(strcmp(out, "MIDDLE") == 0, "first take returns the middle");
    expect(all_zero(g_job.token, sizeof(g_job.token)), "token zeroed after take");
    expect(all_zero(g_job.out, sizeof(g_job.out)), "result zeroed after take");
    expect(g_job.out_n == 0 && g_job.ok == 0, "result length and ok cleared");
    out[0] = 'x';
    expect(hush_canvas_take(token, out, sizeof(out)) == HUSH_ERR_NOT_FOUND,
           "second take of the same token is not found");
    expect(out[0] == '\0', "second take writes nothing");
    expect(hush_canvas_is_busy(token) == 0, "taken token names no job");
}

static void check_failed_take(void)
{
    char token[HUSH_CANVAS_TOKEN_MAX] = {0};
    char out[HUSH_CANVAS_PRED_MAX + 1] = {0};

    stage_done(token, 0, "partial");
    expect(hush_canvas_take(token, out, sizeof(out)) == HUSH_ERR_IO, "failed job take is IO");
    expect(all_zero(g_job.token, sizeof(g_job.token)), "failed job token zeroed");
    expect(all_zero(g_job.out, sizeof(g_job.out)), "failed job result zeroed");
    expect(hush_canvas_take(token, out, sizeof(out)) == HUSH_ERR_NOT_FOUND,
           "failed job is not found on replay");
}

static void expect_refused(const char *probe, const char *msg)
{
    char out[HUSH_CANVAS_PRED_MAX + 1] = {0};
    int before = g_spy_calls;

    expect(hush_canvas_take(probe, out, sizeof(out)) == HUSH_ERR_NOT_FOUND, msg);
    expect(out[0] == '\0', msg);
    expect(hush_canvas_is_busy(probe) == 0, msg);
    expect(g_spy_calls >= before + 2, "compare goes through hush_auth_tokens_equal");
    expect(g_job.out_n != 0 && g_job.token[0] != '\0', "a wrong guess leaves the job");
}

static void check_exact_compare(void)
{
    char token[HUSH_CANVAS_TOKEN_MAX] = {0};
    char probe[TEST_CANVAS_PROBE_MAX] = {0};
    char out[HUSH_CANVAS_PRED_MAX + 1] = {0};

    stage_done(token, 1, "MIDDLE");
    (void)snprintf(probe, sizeof(probe), "%s0", token);
    expect_refused(probe, "suffix-extended token refused (below the parser)");
    (void)snprintf(probe, sizeof(probe), "%s%s", token, token);
    expect_refused(probe, "doubled token refused");
    (void)snprintf(probe, sizeof(probe), "%.31s", token);
    expect_refused(probe, "prefix (one short) refused");
    (void)snprintf(probe, sizeof(probe), "%s", token);
    probe[HUSH_CANVAS_TOKEN_HEX - 1] = probe[HUSH_CANVAS_TOKEN_HEX - 1] == '0' ? '1' : '0';
    expect_refused(probe, "same prefix, last digit off, refused");
    expect_refused("c1", "sequential c1 refused");
    expect(hush_canvas_take(token, out, sizeof(out)) == HUSH_OK, "real token still takes");
    expect(strcmp(out, "MIDDLE") == 0, "real token returns the middle");
}
