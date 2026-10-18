/* hush_cli_gate.c: provider CLI permission policy and readiness (#356). */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "hush_cli_gate.h"

enum {
    HUSH_CLI_MS_PER_S = 1000,
    HUSH_CLI_NS_PER_MS = 1000000,
    HUSH_CLI_DECIMAL = 10,
    /* A probe child's exit when the jail refused to run the CLI. */
    HUSH_CLI_JAIL_EXIT = 125
};

/* Every flag Hush's Grok permission argv uses, and the mode it selects. */
static const char *const hush_cli_grok_flags[] = {
    "--permission-mode", "dontAsk", "--allow", "--deny", "--disallowed-tools", NULL
};

/* Every Copilot flag that keeps the shell, MCP and temp dir away (G5). */
static const char *const hush_cli_copilot_flags[] = {
    "--deny-tool", "--excluded-tools", "--allow-tool", "--disable-builtin-mcps",
    "--disallow-temp-dir", NULL
};

/* Grok 0.2.50 is the first build seen with --permission-mode, --allow,
 * --deny and --disallowed-tools; the help probe checks each one anyway. */
const hush_cli_spec_t hush_cli_spec_grok = {
    "grok", "--version", "--help", {0, 2, 50}, hush_cli_grok_flags,
    "grok permission mode unverified"
};

/* Copilot 0.0.370 added --excluded-tools; 1.0.0 is the first GA build. */
const hush_cli_spec_t hush_cli_spec_copilot = {
    "copilot", "--version", "help", {1, 0, 0}, hush_cli_copilot_flags,
    "copilot version unverified"
};

/* dontAsk denies every tool without an allow rule; deny always wins. */
static const char *const hush_cli_grok_head[] = {
    "--permission-mode", "dontAsk", "--allow", "Read", "--allow", "Grep"
};
static const char *const hush_cli_grok_tail[] = {
    "--deny", "Bash", "--deny", "MCPTool", "--deny", "WebFetch", "--deny", "WebSearch"
};

size_t hush_cli_grok_policy(char **argv, size_t cap, int gated)
{
    size_t n = 0;

    assert(argv != NULL);
    if (cap < (size_t)HUSH_CLI_GROK_POLICY_SLOTS)
        return 0;
    for (size_t i = 0; i < sizeof(hush_cli_grok_head) / sizeof(hush_cli_grok_head[0]); i++)
        argv[n++] = (char *)hush_cli_grok_head[i];
    argv[n++] = (char *)(gated ? "--allow" : "--deny");
    argv[n++] = (char *)"Edit";
    for (size_t i = 0; i < sizeof(hush_cli_grok_tail) / sizeof(hush_cli_grok_tail[0]); i++)
        argv[n++] = (char *)hush_cli_grok_tail[i];
    assert(n == (size_t)HUSH_CLI_GROK_POLICY_SLOTS);
    return n;
}

/* Reads one decimal field at *p; advances *p. 0 when no digit is there. */
static int hush_cli_field(const char **p, unsigned long *out)
{
    char *end = NULL;

    if (!isdigit((unsigned char)**p))
        return 0;
    *out = strtoul(*p, &end, HUSH_CLI_DECIMAL);
    *p = end;
    return 1;
}

/* Parses "N.N.N" at p. 0 when p does not start one. */
static int hush_cli_parse_at(const char *p, hush_cli_version_t *v)
{
    if (!hush_cli_field(&p, &v->major) || *p++ != '.')
        return 0;
    if (!hush_cli_field(&p, &v->minor) || *p++ != '.')
        return 0;
    return hush_cli_field(&p, &v->patch);
}

int hush_cli_version_ok(const char *text, const hush_cli_version_t *min)
{
    hush_cli_version_t v = {0, 0, 0};
    const char *p = text;

    assert(text != NULL && min != NULL);
    while (*p != '\0' && !hush_cli_parse_at(p, &v))
        p++;
    if (*p == '\0')
        return 0;
    if (v.major != min->major)
        return v.major > min->major;
    if (v.minor != min->minor)
        return v.minor > min->minor;
    return v.patch >= min->patch;
}

static int hush_cli_word_char(char c)
{
    return isalnum((unsigned char)c) || c == '-' || c == '_';
}

/* 1 when flag occurs in text with no word character on either side. */
static int hush_cli_has_token(const char *text, const char *flag)
{
    size_t len = strlen(flag);

    for (const char *p = strstr(text, flag); p != NULL; p = strstr(p + 1, flag)) {
        int left = p == text || !hush_cli_word_char(p[-1]);

        if (left && !hush_cli_word_char(p[len]))
            return 1;
    }
    return 0;
}

int hush_cli_help_ok(const char *text, const char *const *flags)
{
    assert(text != NULL && flags != NULL);
    for (size_t i = 0; flags[i] != NULL; i++) {
        if (!hush_cli_has_token(text, flags[i]))
            return 0;
    }
    return 1;
}

static long hush_cli_now_ms(void)
{
    struct timespec ts = {0, 0};

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * HUSH_CLI_MS_PER_S + ts.tv_nsec / HUSH_CLI_NS_PER_MS;
}

/* Reads fd into out until EOF or the deadline. 1 on EOF in time. */
static int hush_cli_drain(int fd, char *out, size_t outsz, long deadline)
{
    size_t used = 0;

    for (;;) {
        struct pollfd pfd = {fd, POLLIN, 0};
        long left = deadline - hush_cli_now_ms();
        char sink[HUSH_CLI_DECIMAL * HUSH_CLI_DECIMAL];

        if (left <= 0 || poll(&pfd, 1, (int)left) <= 0)
            return 0;
        size_t room = outsz - 1 - used;
        ssize_t got = room > 0 ? read(fd, out + used, room) : read(fd, sink, sizeof(sink));
        if (got < 0)
            return 0;
        if (got == 0)
            return 1;
        if (room > 0)
            used += (size_t)got;
        out[used] = '\0';
    }
}

/* Child side of one probe: output to the pipe, then the jailed exec. When
 * the jail refuses, its reason goes to the pipe and the exit is
 * HUSH_CLI_JAIL_EXIT. */
static void hush_cli_probe_child(int wfd, const hush_sandbox_job_t *jail, const char *bin,
                                 const char *arg)
{
    char *argv[3] = {(char *)bin, (char *)arg, NULL};

    if (dup2(wfd, STDOUT_FILENO) < 0 || dup2(wfd, STDERR_FILENO) < 0)
        _exit(EXIT_FAILURE);
    if (wfd != STDOUT_FILENO && wfd != STDERR_FILENO)
        (void)close(wfd);
    if (hush_sandbox_exec(jail, bin, argv) == HUSH_ERR_DENIED) {
        const char *why = hush_sandbox_why();

        if (write(STDOUT_FILENO, why, strlen(why)) < 0)
            _exit(EXIT_FAILURE);
        _exit(HUSH_CLI_JAIL_EXIT);
    }
    _exit(EXIT_FAILURE);
}

/* Maps a reaped probe status: exit 0 is HUSH_OK, a jail refusal HUSH_ERR_IO,
 * anything else HUSH_ERR_DENIED. */
static hush_status_t hush_cli_exit_status(int status)
{
    if (!WIFEXITED(status))
        return HUSH_ERR_DENIED;
    if (WEXITSTATUS(status) == 0)
        return HUSH_OK;
    return WEXITSTATUS(status) == HUSH_CLI_JAIL_EXIT ? HUSH_ERR_IO : HUSH_ERR_DENIED;
}

/* Runs "bin arg" jailed; captures its stdout and stderr into out
 * (HUSH_CLI_PROBE_MAX bytes). HUSH_OK only when it exits 0 before
 * HUSH_CLI_PROBE_MS passes; HUSH_ERR_IO when the jail refused it. */
static hush_status_t hush_cli_probe(char *out, const hush_sandbox_job_t *jail, const char *bin,
                                    const char *arg)
{
    int fds[2];
    int status = 0;

    out[0] = '\0';
    if (pipe(fds) != 0)
        return HUSH_ERR_DENIED;
    pid_t pid = fork();
    if (pid < 0) {
        (void)close(fds[0]);
        (void)close(fds[1]);
        return HUSH_ERR_DENIED;
    }
    if (pid == 0) {
        (void)close(fds[0]);
        hush_cli_probe_child(fds[1], jail, bin, arg);
    }
    (void)close(fds[1]);
    int in_time = hush_cli_drain(fds[0], out, HUSH_CLI_PROBE_MAX,
                                 hush_cli_now_ms() + HUSH_CLI_PROBE_MS);
    (void)close(fds[0]);
    if (!in_time)
        (void)kill(pid, SIGKILL);
    if (waitpid(pid, &status, 0) != pid || !in_time)
        return HUSH_ERR_DENIED;
    return hush_cli_exit_status(status);
}

/* Copies the first line of text into why (cut to whysz). */
static void hush_cli_first_line(char *why, size_t whysz, const char *text)
{
    size_t n = strcspn(text, "\n");

    if (n >= whysz)
        n = whysz - 1;
    memcpy(why, text, n);
    why[n] = '\0';
}

/* Runs spec's version probe (help 0) or help probe (help 1) and checks its
 * output. On a jail refusal the jail's reason goes to why. */
static hush_status_t hush_cli_step(char *out, const hush_cli_spec_t *spec,
                                   const hush_sandbox_job_t *jail, int help)
{
    hush_status_t st = hush_cli_probe(out, jail, spec->bin,
                                      help ? spec->help_arg : spec->version_arg);

    if (st != HUSH_OK)
        return st;
    if (help)
        return hush_cli_help_ok(out, spec->flags) ? HUSH_OK : HUSH_ERR_DENIED;
    return hush_cli_version_ok(out, &spec->min) ? HUSH_OK : HUSH_ERR_DENIED;
}

hush_status_t hush_cli_ready(const hush_cli_spec_t *spec, const hush_sandbox_job_t *jail,
                             char *why, size_t whysz)
{
    char *out = NULL;
    hush_status_t st = HUSH_ERR_DENIED;

    assert(spec != NULL && jail != NULL && why != NULL && whysz > 0);
    why[0] = '\0';
    /* The relay ignores SIGCHLD; this child must reap its own probes. */
    if (signal(SIGCHLD, SIG_DFL) == SIG_ERR)
        return HUSH_ERR_DENIED;
    out = malloc(HUSH_CLI_PROBE_MAX);
    if (out == NULL)
        return HUSH_ERR_DENIED;
    st = hush_cli_step(out, spec, jail, 0);
    if (st == HUSH_OK)
        st = hush_cli_step(out, spec, jail, 1);
    if (st == HUSH_ERR_IO)
        hush_cli_first_line(why, whysz, out);
    free(out);
    return st;
}
