/* hush_pass.c: owns the unix `pass` helper invocation for Hush secrets. */

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "hush_pass.h"

#define HUSH_PASS_DEFAULT_HELPER "hush-pass"
#define HUSH_PASS_REPO_HELPER "../scripts/hush-pass"

enum {
    HUSH_PASS_EXIT_MISSING = 127
};

static char g_helper[HUSH_PASS_CMD_MAX];
static char g_last_error[HUSH_PASS_ERR_MAX];

/* True when path is a non-empty relative store key. */
static int hush_pass_path_is_ok(const char *path);

/* Resolves the helper binary into cmd. */
static hush_status_t hush_pass_resolve_helper(char *cmd, size_t cmdsz);

/* Records a short error and returns HUSH_ERR_IO. */
static hush_status_t hush_pass_fail(const char *msg);

/* Copies src into dst, NUL-terminated. */
static void hush_pass_copy(char *dst, size_t dstsz, const char *src);

/* Opens stdin/stdout pipes for a child. */
static hush_status_t hush_pass_open_pipes(int in_pipe[2], int out_pipe[2]);

/* Closes all four pipe ends. */
static void hush_pass_close_pipes(int in_pipe[2], int out_pipe[2]);

/* Child: remap stdio onto the pipes and exec the helper. */
static void hush_pass_exec_child(int in_pipe[2], int out_pipe[2],
                                 char *const argv[]);

/* Writes stdin_text to the child stdin pipe. */
static hush_status_t hush_pass_write_secret(int fd, const char *stdin_text);

/* Reads child stdout into out. */
static void hush_pass_read_out(int fd, char *out, size_t outsz);

/* Parent: write stdin_text, read stdout, wait. */
static hush_status_t hush_pass_finish_parent(int in_pipe[2], int out_pipe[2],
                                             pid_t pid,
                                             char *out, size_t outsz,
                                             const char *stdin_text);

/* Runs helper argv with optional stdin_text. Captures stdout into out. */
static hush_status_t hush_pass_run(char *out, size_t outsz,
                                   const char *stdin_text,
                                   char *const argv[]);

/* Builds the three-word helper argv. */
static void hush_pass_fill_argv(char *argv[4], char *helper, const char *verb,
                                const char *path);

/* Strips trailing CR/LF from a captured secret. */
static void hush_pass_trim_secret(char *text);

void hush_pass_set_helper(const char *cmd)
{
    if (cmd == NULL || cmd[0] == '\0') {
        g_helper[0] = '\0';
        return;
    }
    hush_pass_copy(g_helper, sizeof(g_helper), cmd);
}

void hush_pass_last_error(char *out, size_t outsz)
{
    if (out == NULL || outsz == 0)
        return;
    hush_pass_copy(out, outsz, g_last_error);
}

hush_status_t hush_pass_save(const char *path, const char *secret)
{
    char helper[HUSH_PASS_CMD_MAX];
    char *argv[4];
    hush_status_t st;

    g_last_error[0] = '\0';
    if (!hush_pass_path_is_ok(path) || secret == NULL || secret[0] == '\0')
        return HUSH_ERR_ARG;
    st = hush_pass_resolve_helper(helper, sizeof(helper));
    if (st != HUSH_OK)
        return st;
    hush_pass_fill_argv(argv, helper, "save", path);
    return hush_pass_run(NULL, 0, secret, argv);
}

hush_status_t hush_pass_get(char *out, size_t outsz, const char *path)
{
    char helper[HUSH_PASS_CMD_MAX];
    char *argv[4];
    hush_status_t st;

    g_last_error[0] = '\0';
    if (out == NULL || outsz == 0 || !hush_pass_path_is_ok(path))
        return HUSH_ERR_ARG;
    out[0] = '\0';
    st = hush_pass_resolve_helper(helper, sizeof(helper));
    if (st != HUSH_OK)
        return st;
    hush_pass_fill_argv(argv, helper, "get", path);
    st = hush_pass_run(out, outsz, NULL, argv);
    if (st != HUSH_OK)
        return st;
    hush_pass_trim_secret(out);
    if (out[0] == '\0')
        return hush_pass_fail("empty secret");
    return HUSH_OK;
}

int hush_pass_has(const char *path)
{
    char helper[HUSH_PASS_CMD_MAX];
    char *argv[4];

    g_last_error[0] = '\0';
    if (!hush_pass_path_is_ok(path))
        return 0;
    if (hush_pass_resolve_helper(helper, sizeof(helper)) != HUSH_OK)
        return 0;
    hush_pass_fill_argv(argv, helper, "has", path);
    return hush_pass_run(NULL, 0, NULL, argv) == HUSH_OK;
}

static int hush_pass_path_is_ok(const char *path)
{
    if (path == NULL || path[0] == '\0')
        return 0;
    if (path[0] == '/' || path[0] == '.')
        return 0;
    if (strstr(path, "..") != NULL)
        return 0;
    return 1;
}

static hush_status_t hush_pass_resolve_helper(char *cmd, size_t cmdsz)
{
    assert(cmd != NULL);
    assert(cmdsz > 0);
    if (g_helper[0] != '\0') {
        hush_pass_copy(cmd, cmdsz, g_helper);
        return HUSH_OK;
    }
    if (access(HUSH_PASS_REPO_HELPER, X_OK) == 0) {
        hush_pass_copy(cmd, cmdsz, HUSH_PASS_REPO_HELPER);
        return HUSH_OK;
    }
    hush_pass_copy(cmd, cmdsz, HUSH_PASS_DEFAULT_HELPER);
    return HUSH_OK;
}

static hush_status_t hush_pass_fail(const char *msg)
{
    assert(msg != NULL);
    hush_pass_copy(g_last_error, sizeof(g_last_error), msg);
    return HUSH_ERR_IO;
}

static void hush_pass_copy(char *dst, size_t dstsz, const char *src)
{
    size_t n;

    assert(dst != NULL);
    assert(dstsz > 0);
    if (src == NULL)
        src = "";
    n = strlen(src);
    if (n + 1 > dstsz)
        n = dstsz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void hush_pass_fill_argv(char *argv[4], char *helper, const char *verb,
                                const char *path)
{
    assert(argv != NULL);
    assert(helper != NULL);
    assert(verb != NULL);
    assert(path != NULL);
    argv[0] = helper;
    argv[1] = (char *)verb;
    argv[2] = (char *)path;
    argv[3] = NULL;
}

static void hush_pass_trim_secret(char *text)
{
    size_t n;

    assert(text != NULL);
    n = strlen(text);
    while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r')) {
        text[n - 1] = '\0';
        n--;
    }
}

static hush_status_t hush_pass_open_pipes(int in_pipe[2], int out_pipe[2])
{
    assert(in_pipe != NULL);
    assert(out_pipe != NULL);
    if (pipe(in_pipe) != 0)
        return hush_pass_fail("pipe failed");
    if (pipe(out_pipe) != 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        return hush_pass_fail("pipe failed");
    }
    return HUSH_OK;
}

static void hush_pass_close_pipes(int in_pipe[2], int out_pipe[2])
{
    assert(in_pipe != NULL);
    assert(out_pipe != NULL);
    close(in_pipe[0]);
    close(in_pipe[1]);
    close(out_pipe[0]);
    close(out_pipe[1]);
}

static void hush_pass_exec_child(int in_pipe[2], int out_pipe[2],
                                 char *const argv[])
{
    assert(in_pipe != NULL);
    assert(out_pipe != NULL);
    assert(argv != NULL);
    close(in_pipe[1]);
    close(out_pipe[0]);
    if (dup2(in_pipe[0], STDIN_FILENO) < 0)
        _exit(HUSH_PASS_EXIT_MISSING);
    if (dup2(out_pipe[1], STDOUT_FILENO) < 0)
        _exit(HUSH_PASS_EXIT_MISSING);
    if (dup2(out_pipe[1], STDERR_FILENO) < 0)
        _exit(HUSH_PASS_EXIT_MISSING);
    close(in_pipe[0]);
    close(out_pipe[1]);
    execvp(argv[0], argv);
    _exit(HUSH_PASS_EXIT_MISSING);
}

static hush_status_t hush_pass_write_secret(int fd, const char *stdin_text)
{
    size_t want;
    ssize_t wrote;

    if (stdin_text == NULL)
        return HUSH_OK;
    want = strlen(stdin_text);
    wrote = write(fd, stdin_text, want);
    if (wrote < 0 || (size_t)wrote != want)
        return hush_pass_fail("write secret failed");
    return HUSH_OK;
}

static void hush_pass_read_out(int fd, char *out, size_t outsz)
{
    size_t nread = 0;
    ssize_t r;

    if (out == NULL || outsz == 0)
        return;
    while (nread + 1 < outsz) {
        r = read(fd, out + nread, outsz - 1 - nread);
        if (r <= 0)
            break;
        nread += (size_t)r;
    }
    out[nread] = '\0';
}

static hush_status_t hush_pass_finish_parent(int in_pipe[2], int out_pipe[2],
                                             pid_t pid,
                                             char *out, size_t outsz,
                                             const char *stdin_text)
{
    hush_status_t st;
    int status = 0;

    assert(in_pipe != NULL);
    assert(out_pipe != NULL);
    close(in_pipe[0]);
    close(out_pipe[1]);
    st = hush_pass_write_secret(in_pipe[1], stdin_text);
    close(in_pipe[1]);
    hush_pass_read_out(out_pipe[0], out, outsz);
    close(out_pipe[0]);
    if (st != HUSH_OK)
        return st;
    if (waitpid(pid, &status, 0) < 0)
        return hush_pass_fail("wait failed");
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return hush_pass_fail("pass helper failed");
    return HUSH_OK;
}

static hush_status_t hush_pass_run(char *out, size_t outsz,
                                   const char *stdin_text,
                                   char *const argv[])
{
    int in_pipe[2];
    int out_pipe[2];
    pid_t pid;
    hush_status_t st;

    assert(argv != NULL);
    assert(argv[0] != NULL);
    st = hush_pass_open_pipes(in_pipe, out_pipe);
    if (st != HUSH_OK)
        return st;
    /* The relay ignores SIGCHLD (hush_relay.c) so background children never
     * become zombies. An ignored SIGCHLD also makes the kernel auto-reap any
     * child, so waitpid() would fail with ECHILD. Reset to SIG_DFL for the
     * duration of this fork + waitpid so the helper can be reaped, then
     * restore whatever was there before (SIG_IGN in the relay, SIG_DFL in
     * unit tests). The server is single-threaded, so this is safe. */
    {
        void (*old_chld)(int) = signal(SIGCHLD, SIG_DFL);
        pid = fork();
        if (pid < 0) {
            if (old_chld != SIG_ERR)
                signal(SIGCHLD, old_chld);
            hush_pass_close_pipes(in_pipe, out_pipe);
            return hush_pass_fail("fork failed");
        }
        if (pid == 0)
            hush_pass_exec_child(in_pipe, out_pipe, argv);
        st = hush_pass_finish_parent(in_pipe, out_pipe, pid, out, outsz,
                                     stdin_text);
        if (old_chld != SIG_ERR)
            signal(SIGCHLD, old_chld);
        return st;
    }
}
