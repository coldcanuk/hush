/* hush_canvas.c: owns one non-blocking Fill-in-the-Middle grok job. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "hush_auth.h"
#include "hush_canvas.h"
#include "hush_dir.h"
#include "hush_event.h"
#include "hush_mem.h"
#include "hush_provider.h"
#include "hush_relay.h"
#include "hush_roster.h"
#include "hush_http.h"
#include "hush_sandbox.h"
#include "hush_cli_gate.h"

enum {
    HUSH_CANVAS_FD_NONE = -1,
    HUSH_CANVAS_TIMEOUT_S = 20,
    /* 21 fixed entries, the Grok permission policy, then NULL. */
    HUSH_CANVAS_FIXED_SLOTS = 21,
    HUSH_CANVAS_ARGV_MAX = HUSH_CANVAS_FIXED_SLOTS + HUSH_CLI_GROK_POLICY_SLOTS + 1,
    HUSH_CANVAS_PATH_MAX = 256,
    HUSH_CANVAS_HALF_MAX = 1800,
    HUSH_CANVAS_READ_MAX = 256,
    HUSH_CANVAS_READ_STEPS = 32
};

#define HUSH_CANVAS_GROK_BIN "grok"
#define HUSH_CANVAS_DEVNULL "/dev/null"
#define HUSH_CANVAS_ENV_CONFIG "HUSH_CONFIG_DIR"
#define HUSH_CANVAS_CWD_LEAF "agent-cwd"
#define HUSH_CANVAS_CWD_TMP "hush-agent-cwd"
#define HUSH_CANVAS_TMP_FALLBACK "/tmp"
#define HUSH_CANVAS_TURNS "1"
#define HUSH_CANVAS_EFFORT "low"
#define HUSH_CANVAS_NOMEM "--no-memory"
#define HUSH_CANVAS_PROMPT \
    "Fill only the missing middle between PREFIX and SUFFIX. " \
    "Return only that middle. No fences. No preamble."
#define HUSH_CANVAS_RULES \
    "Return only the missing middle. No markdown fences. No chatter."
#define HUSH_CANVAS_DISALLOWED \
    "run_terminal_cmd,web_search,web_fetch,read_file,search_replace," \
    "list_dir,grep,todo_write,task,Agent"
#define HUSH_CANVAS_HEAD "PREFIX:\n"
#define HUSH_CANVAS_MID "\n\nSUFFIX:\n"

typedef struct {
    int busy;
    int done;
    int ok;
    pid_t pid;
    int fd;
    time_t started;
    char token[HUSH_CANVAS_TOKEN_MAX];
    char cwd[HUSH_CANVAS_PATH_MAX];
    char note[HUSH_EVENT_MAX_CONTENT + 1];
    char out[HUSH_EVENT_MAX_CONTENT + 1];
    size_t out_n;
    /* Jailed stub HOME, made before fork; removed when the job ends. */
    char stub_home[HUSH_SANDBOX_PATH_MAX];
} hush_canvas_job_t;

static hush_canvas_job_t g_job;

_Static_assert((int)HUSH_CANVAS_TOKEN_MAX >= (int)HUSH_AUTH_SECRET_BUF,
               "a canvas job token holds a full CSPRNG secret");

/* Copies src into dst. dst is never NULL. src NULL becomes empty. */
static void hush_canvas_copy(char *dst, size_t dstsz, const char *src);
/* Strips trailing space, tab, CR, LF. text is never NULL. */
static void hush_canvas_trim(char *text);
/* Copies src into dst, at most HUSH_CANVAS_PRED_MAX bytes. */
static void hush_canvas_clip(char *dst, size_t dstsz, const char *src);
/* Writes a grok cwd under HUSH_CONFIG_DIR or TMPDIR. */
static void hush_canvas_prepare_cwd(char *out, size_t outsz);
/* True when grok-build has a home and a binary. */
static int hush_canvas_grok_ready(void);
/* Writes PREFIX/SUFFIX note. prefix and suffix may be NULL. */
static void hush_canvas_fill_note(char *dst, size_t dstsz,
                                 const char *prefix, const char *suffix);
/* Resets job and fills note, token, cwd. job is never NULL. Fails
 * HUSH_ERR_IO when no CSPRNG token can be minted (token stays ""). */
static hush_status_t hush_canvas_fill_job(hush_canvas_job_t *job,
                                          const char *prefix, const char *suffix);
/* execvp grok -p. Does not return. */
static void hush_canvas_exec_grok(int write_fd, const hush_canvas_job_t *job);
/* Forks grok with a non-blocking stdout pipe. Fails with HUSH_ERR_IO. */
static hush_status_t hush_canvas_spawn(hush_canvas_job_t *job);
/* Closes the pipe and clears busy. job is never NULL. */
static void hush_canvas_close_job(hush_canvas_job_t *job);
/* SIGTERM + WNOHANG. job is never NULL. */
static void hush_canvas_kill_job(hush_canvas_job_t *job);
/* Non-blocking read into job->out. Bounded steps. */
static void hush_canvas_read_job(hush_canvas_job_t *job);
/* Trims out, sets ok, clears busy. Does not insert a note. */
static void hush_canvas_finish_job(hush_canvas_job_t *job, int ok);
/* True when now is past started + TIMEOUT_S. */
static int hush_canvas_job_timed_out(const hush_canvas_job_t *job, time_t now);
/* True when token names g_job: exact length, constant-time compare. */
static int hush_canvas_token_matches(const char *token);
/* Zeros the job's token and result so a taken job cannot be read again. */
static void hush_canvas_forget(hush_canvas_job_t *job);

void hush_canvas_init(void)
{
    memset(&g_job, 0, sizeof(g_job));
    g_job.fd = HUSH_CANVAS_FD_NONE;
}

void hush_canvas_shutdown(void)
{
    if (g_job.busy || g_job.pid > 0)
        hush_canvas_kill_job(&g_job);
    hush_canvas_close_job(&g_job);
}

void hush_canvas_poll(void)
{
    int status;
    time_t now;

    if (!g_job.busy)
        return;
    hush_canvas_read_job(&g_job);
    if (g_job.pid > 0)
        (void)waitpid(g_job.pid, &status, WNOHANG);
    now = time(NULL);
    if (hush_canvas_job_timed_out(&g_job, now)) {
        hush_canvas_kill_job(&g_job);
        hush_canvas_finish_job(&g_job, 0);
        return;
    }
    if (g_job.fd == HUSH_CANVAS_FD_NONE)
        hush_canvas_finish_job(&g_job, 1);
}

hush_status_t hush_canvas_start(char *token, size_t tokensz,
                                const char *prefix,
                                const char *suffix)
{
    if (token == NULL || tokensz < 2)
        return HUSH_ERR_ARG;
    token[0] = '\0';
    if (!hush_canvas_grok_ready())
        return HUSH_ERR_IO;
    if (g_job.busy)
        hush_canvas_kill_job(&g_job);
    hush_canvas_close_job(&g_job);
    if (hush_canvas_fill_job(&g_job, prefix, suffix) != HUSH_OK ||
        hush_canvas_spawn(&g_job) != HUSH_OK) {
        hush_canvas_close_job(&g_job);
        return HUSH_ERR_IO;
    }
    hush_canvas_copy(token, tokensz, g_job.token);
    return HUSH_OK;
}

int hush_canvas_is_busy(const char *token)
{
    if (!hush_canvas_token_matches(token))
        return 0;
    return g_job.busy;
}

hush_status_t hush_canvas_take(const char *token, char *out, size_t outsz)
{
    if (token == NULL || token[0] == '\0' || out == NULL || outsz == 0)
        return HUSH_ERR_ARG;
    out[0] = '\0';
    if (!hush_canvas_token_matches(token))
        return HUSH_ERR_NOT_FOUND;
    if (g_job.busy)
        return HUSH_ERR_NOT_FOUND;
    if (!g_job.ok || g_job.out[0] == '\0') {
        hush_canvas_close_job(&g_job);
        hush_canvas_forget(&g_job);
        return HUSH_ERR_IO;
    }
    hush_canvas_clip(out, outsz, g_job.out);
    hush_canvas_close_job(&g_job);
    hush_canvas_forget(&g_job);
    return HUSH_OK;
}

static void hush_canvas_copy(char *dst, size_t dstsz, const char *src)
{
    size_t n;

    assert(dst != NULL);
    assert(dstsz > 0);
    if (src == NULL)
        src = "";
    n = strlen(src);
    if (n >= dstsz)
        n = dstsz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void hush_canvas_trim(char *text)
{
    size_t n;

    assert(text != NULL);
    n = strlen(text);
    while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r' ||
                     text[n - 1] == ' ' || text[n - 1] == '\t')) {
        text[n - 1] = '\0';
        n--;
    }
}

static void hush_canvas_clip(char *dst, size_t dstsz, const char *src)
{
    size_t cap;

    assert(dst != NULL);
    assert(dstsz > 0);
    cap = dstsz;
    if (cap > (size_t)HUSH_CANVAS_PRED_MAX + 1)
        cap = (size_t)HUSH_CANVAS_PRED_MAX + 1;
    hush_canvas_copy(dst, cap, src);
}

static void hush_canvas_prepare_cwd(char *out, size_t outsz)
{
    const char *cfg;
    const char *base;
    const char *leaf;
    int n;

    assert(out != NULL);
    assert(outsz > 0);
    out[0] = '\0';
    cfg = getenv(HUSH_CANVAS_ENV_CONFIG);
    base = getenv("TMPDIR");
    leaf = HUSH_CANVAS_CWD_TMP;
    if (base == NULL || base[0] == '\0')
        base = HUSH_CANVAS_TMP_FALLBACK;
    if (cfg != NULL && cfg[0] != '\0') {
        base = cfg;
        leaf = HUSH_CANVAS_CWD_LEAF;
    }
    n = snprintf(out, outsz, "%s/%s", base, leaf);
    if (n < 0 || (size_t)n >= outsz ||
        hush_dir_ensure_private(out) != HUSH_OK) {
        /* A pre-created or foreign path is not a safe cwd; use a per-process
         * private directory instead of trusting it. */
        n = snprintf(out, outsz, "%s/%s-%d", base, leaf, (int)getpid());
        if (n < 0 || (size_t)n >= outsz ||
            hush_dir_ensure_private(out) != HUSH_OK)
            out[0] = '\0';
    }
}

static int hush_canvas_grok_ready(void)
{
    hush_provider_status_t st;

    if (hush_provider_status(&st, HUSH_ROSTER_PROVIDER_GROK_BUILD) != HUSH_OK)
        return 0;
    return st.has_home && st.has_binary;
}

static void hush_canvas_fill_note(char *dst, size_t dstsz,
                                 const char *prefix, const char *suffix)
{
    char left[HUSH_CANVAS_HALF_MAX + 1];
    char right[HUSH_CANVAS_HALF_MAX + 1];

    assert(dst != NULL);
    assert(dstsz > 0);
    hush_canvas_copy(left, sizeof(left), prefix);
    hush_canvas_copy(right, sizeof(right), suffix);
    if (snprintf(dst, dstsz, "%s%s%s%s",
                 HUSH_CANVAS_HEAD, left, HUSH_CANVAS_MID, right)
        >= (int)dstsz)
        hush_canvas_copy(dst, dstsz, left);
}

static hush_status_t hush_canvas_fill_job(hush_canvas_job_t *job,
                                          const char *prefix, const char *suffix)
{
    assert(job != NULL);
    memset(job, 0, sizeof(*job));
    job->fd = HUSH_CANVAS_FD_NONE;
    if (hush_auth_secret_mint(job->token, sizeof(job->token)) != HUSH_OK)
        return HUSH_ERR_IO;
    assert(strlen(job->token) == (size_t)HUSH_CANVAS_TOKEN_HEX);
    job->busy = 1;
    job->started = time(NULL);
    hush_canvas_prepare_cwd(job->cwd, sizeof(job->cwd));
    hush_canvas_fill_note(job->note, sizeof(job->note), prefix, suffix);
    return HUSH_OK;
}

/* Writes the canvas Grok argv: text only, never --always-approve, and the
 * ungated hush_cli_grok_policy rules (#356). Returns the slot count, NULL
 * included, or 0 when cap is too small. */
static size_t hush_canvas_grok_argv(const hush_canvas_job_t *job, char **argv, size_t cap)
{
    const char *fixed[HUSH_CANVAS_FIXED_SLOTS] = {
        HUSH_CANVAS_GROK_BIN, "-p", job->note, "--system-prompt-override", HUSH_CANVAS_PROMPT,
        "--output-format", "plain", "--no-plan", "--no-subagents", "--disable-web-search",
        "--max-turns", HUSH_CANVAS_TURNS, "--reasoning-effort", HUSH_CANVAS_EFFORT,
        "--cwd", job->cwd, "--disallowed-tools", HUSH_CANVAS_DISALLOWED,
        "--rules", HUSH_CANVAS_RULES, HUSH_CANVAS_NOMEM
    };
    size_t n = 0;

    if (cap < (size_t)HUSH_CANVAS_ARGV_MAX)
        return 0;
    /* POSIX exec borrows mutable argv; the strings are never written. */
    for (size_t i = 0; i < (size_t)HUSH_CANVAS_FIXED_SLOTS; i++)
        argv[n++] = (char *)fixed[i];
    n += hush_cli_grok_policy(argv + n, cap - n, 0);
    argv[n++] = NULL;
    assert(n == (size_t)HUSH_CANVAS_ARGV_MAX);
    return n;
}

static void hush_canvas_exec_grok(int write_fd, const hush_canvas_job_t *job)
{
    char *argv[HUSH_CANVAS_ARGV_MAX] = {0};
    int dn;

    assert(job != NULL);
    if (write_fd >= 0)
        (void)dup2(write_fd, STDOUT_FILENO);
    if (write_fd >= 0)
        close(write_fd);
    dn = open(HUSH_CANVAS_DEVNULL, O_WRONLY);
    if (dn >= 0) {
        (void)dup2(dn, STDERR_FILENO);
        close(dn);
    }
    /* The canvas robot is an agent child too: jailed, or it does not run (#336). */
    hush_sandbox_job_t jail = {job->cwd, job->stub_home, hush_http_listen_port()};
    /* An unready Grok yields no completion rather than an unchecked run (#356). */
    char why[HUSH_CANVAS_READ_MAX] = {0};
    if (hush_canvas_grok_argv(job, argv, HUSH_CANVAS_ARGV_MAX) == 0 ||
        hush_cli_ready(&hush_cli_spec_grok, &jail, why, sizeof(why)) != HUSH_OK)
        _exit(127);
    (void)hush_sandbox_exec(&jail, HUSH_CANVAS_GROK_BIN, argv);
    _exit(127);
}

static hush_status_t hush_canvas_spawn(hush_canvas_job_t *job)
{
    int fds[2];
    pid_t pid;
    int flags;

    assert(job != NULL);
    if (pipe(fds) != 0)
        return HUSH_ERR_IO;
    /* No HOME made here: the child's jail refuses. */
    (void)hush_sandbox_home_make(job->stub_home, sizeof(job->stub_home));
    (void)hush_sandbox_net_prepare(hush_http_listen_port());
    pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        hush_sandbox_home_remove(job->stub_home, sizeof(job->stub_home));
        return HUSH_ERR_IO;
    }
    if (pid == 0) {
        close(fds[0]);
        hush_canvas_exec_grok(fds[1], job);
    }
    close(fds[1]);
    flags = fcntl(fds[0], F_GETFL, 0);
    if (flags >= 0)
        (void)fcntl(fds[0], F_SETFL, flags | O_NONBLOCK);
    job->pid = pid;
    job->fd = fds[0];
    hush_relay_track_child(pid);
    return HUSH_OK;
}

static void hush_canvas_close_job(hush_canvas_job_t *job)
{
    assert(job != NULL);
    if (job->fd >= 0)
        close(job->fd);
    job->fd = HUSH_CANVAS_FD_NONE;
    job->pid = 0;
    job->busy = 0;
    hush_sandbox_home_remove(job->stub_home, sizeof(job->stub_home));
}

static void hush_canvas_kill_job(hush_canvas_job_t *job)
{
    int status;

    assert(job != NULL);
    if (job->pid > 1) {
        (void)kill(job->pid, SIGTERM);
        (void)waitpid(job->pid, &status, WNOHANG);
    }
}

static void hush_canvas_read_job(hush_canvas_job_t *job)
{
    char buf[HUSH_CANVAS_READ_MAX];
    ssize_t n;
    size_t room;
    size_t i;

    assert(job != NULL);
    if (job->fd < 0)
        return;
    for (i = 0; i < (size_t)HUSH_CANVAS_READ_STEPS; i++) {
        room = sizeof(job->out) - 1 - job->out_n;
        if (room == 0)
            break;
        n = read(job->fd, buf,
                 (size_t)HUSH_CANVAS_READ_MAX < room
                     ? (size_t)HUSH_CANVAS_READ_MAX : room);
        if (n > 0) {
            memcpy(job->out + job->out_n, buf, (size_t)n);
            job->out_n += (size_t)n;
            job->out[job->out_n] = '\0';
            continue;
        }
        if (n == 0) {
            close(job->fd);
            job->fd = HUSH_CANVAS_FD_NONE;
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;
        close(job->fd);
        job->fd = HUSH_CANVAS_FD_NONE;
        return;
    }
}

static void hush_canvas_finish_job(hush_canvas_job_t *job, int ok)
{
    assert(job != NULL);
    hush_canvas_trim(job->out);
    job->ok = ok && job->out[0] != '\0';
    job->busy = 0;
    if (job->fd >= 0)
        close(job->fd);
    job->fd = HUSH_CANVAS_FD_NONE;
    job->pid = 0;
    hush_sandbox_home_remove(job->stub_home, sizeof(job->stub_home));
}

static int hush_canvas_job_timed_out(const hush_canvas_job_t *job, time_t now)
{
    assert(job != NULL);
    if (job->started <= 0)
        return 0;
    return now >= job->started + (time_t)HUSH_CANVAS_TIMEOUT_S;
}

static int hush_canvas_token_matches(const char *token)
{
    if (token == NULL || token[0] == '\0')
        return 0;
    if (g_job.token[0] == '\0')
        return 0;
    return hush_auth_tokens_equal(g_job.token, token);
}

static void hush_canvas_forget(hush_canvas_job_t *job)
{
    assert(job != NULL);
    assert(!job->busy);
    hush_secure_zero(job->token, sizeof(job->token));
    hush_secure_zero(job->out, sizeof(job->out));
    job->out_n = 0;
    job->ok = 0;
}
