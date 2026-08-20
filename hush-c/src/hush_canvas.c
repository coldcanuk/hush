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

#include "hush_canvas.h"
#include "hush_event.h"
#include "hush_provider.h"
#include "hush_relay.h"
#include "hush_roster.h"

enum {
    HUSH_CANVAS_FD_NONE = -1,
    HUSH_CANVAS_TIMEOUT_S = 20,
    HUSH_CANVAS_ARGV_MAX = 24,
    HUSH_CANVAS_PATH_MAX = 256,
    HUSH_CANVAS_CWD_MODE = 0700,
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
    unsigned seq;
    char token[HUSH_CANVAS_TOKEN_MAX];
    char cwd[HUSH_CANVAS_PATH_MAX];
    char note[HUSH_EVENT_MAX_CONTENT + 1];
    char out[HUSH_EVENT_MAX_CONTENT + 1];
    size_t out_n;
} hush_canvas_job_t;

static hush_canvas_job_t g_job;
static unsigned g_seq;

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
/* Resets job and fills note, token, cwd. job is never NULL. */
static void hush_canvas_fill_job(hush_canvas_job_t *job,
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
/* True when token names g_job. */
static int hush_canvas_token_matches(const char *token);

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
    hush_canvas_fill_job(&g_job, prefix, suffix);
    if (hush_canvas_spawn(&g_job) != HUSH_OK) {
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
        return HUSH_ERR_IO;
    }
    hush_canvas_clip(out, outsz, g_job.out);
    hush_canvas_close_job(&g_job);
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
    if (n < 0 || (size_t)n >= outsz) {
        hush_canvas_copy(out, outsz, HUSH_CANVAS_TMP_FALLBACK);
        return;
    }
    (void)mkdir(out, (mode_t)HUSH_CANVAS_CWD_MODE);
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

static void hush_canvas_fill_job(hush_canvas_job_t *job,
                                 const char *prefix, const char *suffix)
{
    assert(job != NULL);
    memset(job, 0, sizeof(*job));
    job->fd = HUSH_CANVAS_FD_NONE;
    job->busy = 1;
    job->started = time(NULL);
    g_seq += 1;
    job->seq = g_seq;
    (void)snprintf(job->token, sizeof(job->token), "c%u", job->seq);
    hush_canvas_prepare_cwd(job->cwd, sizeof(job->cwd));
    hush_canvas_fill_note(job->note, sizeof(job->note), prefix, suffix);
}

static void hush_canvas_exec_grok(int write_fd, const hush_canvas_job_t *job)
{
    char *argv[HUSH_CANVAS_ARGV_MAX];
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
    argv[0] = (char *)HUSH_CANVAS_GROK_BIN;
    argv[1] = (char *)"-p";
    argv[2] = (char *)job->note;
    argv[3] = (char *)"--system-prompt-override";
    argv[4] = (char *)HUSH_CANVAS_PROMPT;
    argv[5] = (char *)"--output-format";
    argv[6] = (char *)"plain";
    argv[7] = (char *)"--always-approve";
    argv[8] = (char *)"--no-plan";
    argv[9] = (char *)"--no-subagents";
    argv[10] = (char *)"--disable-web-search";
    argv[11] = (char *)"--max-turns";
    argv[12] = (char *)HUSH_CANVAS_TURNS;
    argv[13] = (char *)"--reasoning-effort";
    argv[14] = (char *)HUSH_CANVAS_EFFORT;
    argv[15] = (char *)"--cwd";
    argv[16] = (char *)job->cwd;
    argv[17] = (char *)"--disallowed-tools";
    argv[18] = (char *)HUSH_CANVAS_DISALLOWED;
    argv[19] = (char *)"--rules";
    argv[20] = (char *)HUSH_CANVAS_RULES;
    argv[21] = (char *)HUSH_CANVAS_NOMEM;
    argv[22] = NULL;
    execvp(HUSH_CANVAS_GROK_BIN, argv);
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
    pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
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
    return strcmp(g_job.token, token) == 0;
}
