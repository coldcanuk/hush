/* agent_process.c: owns provider worker execution and reply capture. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "hush_agent.h"
#include "hush_agent_internal.h"
#include "hush_inference.h"
#include "hush_codex.h"
#include "hush_dir.h"
#include "hush_relay.h"
#include "hush_provider.h"

#define HUSH_AGENT_DEVNULL "/dev/null"
#define HUSH_AGENT_DISALLOWED \
    "run_terminal_cmd,web_search,web_fetch,read_file,search_replace,list_dir,grep,todo_write,task,Agent"
#define HUSH_AGENT_COPILOT_BIN "copilot"
#define HUSH_AGENT_CODEX_BIN "codex"
#define HUSH_AGENT_GOOSE_BIN "goose"
#define HUSH_AGENT_OLLAMA_BIN "ollama"
#define HUSH_AGENT_GROK_NOMEM "--no-memory"
#define HUSH_AGENT_GROK_BIN "grok"
#define HUSH_AGENT_GROK_EFFORT "low"
#define HUSH_AGENT_GROK_TURNS "2"
#define HUSH_AGENT_FIXUP_TURNS "1"

/* Runs the provider CLI in a forked worker. */
static void hush_agent_exec_child(int write_fd, const hush_agent_job_t *job);
static void hush_agent_exec_api(const hush_agent_job_t *job);
static void hush_agent_exec_cline(const hush_agent_job_t *job);
static void hush_agent_exec_grok(const hush_agent_job_t *job);
static void hush_agent_exec_copilot(const hush_agent_job_t *job);
static void hush_agent_exec_codex(const hush_agent_job_t *job);
static void hush_agent_exec_goose(const hush_agent_job_t *job);
static void hush_agent_exec_ollama(const hush_agent_job_t *job);
static void hush_agent_build_combined(char *out, size_t outsz,
                                       const hush_agent_job_t *job);

static hush_status_t hush_agent_prepare_worker(void)
{
    if (setpgid(0, 0) != 0 || signal(SIGCHLD, SIG_DFL) == SIG_ERR ||
        signal(SIGTERM, SIG_DFL) == SIG_ERR || signal(SIGINT, SIG_DFL) == SIG_ERR)
        return HUSH_ERR_IO;
    for (size_t i = 0; i < (size_t)HUSH_AGENT_JOBS_MAX; ++i) {
        if (hush_agent_jobs()[i].busy && hush_agent_jobs()[i].fd >= 0 && close(hush_agent_jobs()[i].fd) != 0)
            return HUSH_ERR_IO;
    }
    return HUSH_OK;
}

static hush_status_t hush_agent_wait_worker(pid_t child)
{
    assert(child > 0);
    int status = 0;
    for (size_t i = 0; i < (size_t)HUSH_AGENT_WAIT_MAX; ++i) {
        pid_t result = waitpid(child, &status, 0);
        if (result == child)
            return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? HUSH_OK : HUSH_ERR_IO;
        if (errno != EINTR)
            return HUSH_ERR_IO;
    }
    if (kill(child, SIGKILL) != 0 && errno != ESRCH)
        return HUSH_ERR_IO;
    return HUSH_ERR_IO;
}

static hush_status_t hush_agent_take_cline_line(char *out, size_t outsz, const char *line)
{
    assert(out != NULL && outsz > 0);
    assert(line != NULL);
    hush_json_value_t value = {0};
    if (hush_json_lookup(&value, line, "/partial") == HUSH_OK &&
        value.len == strlen("true") && memcmp(value.start, "true", value.len) == 0)
        return HUSH_OK;
    char subtype[HUSH_AGENT_TOKEN_MAX * 2] = {0};
    hush_status_t status = hush_json_lookup(&value, line, "/say");
    if (status == HUSH_ERR_NOT_FOUND)
        status = hush_json_lookup(&value, line, "/ask");
    if (status != HUSH_OK)
        return status == HUSH_ERR_NOT_FOUND ? HUSH_OK : status;
    HUSH_TRY(hush_json_decode(subtype, sizeof(subtype), &value));
    if (strcmp(subtype, "text") != 0 && strcmp(subtype, "completion_result") != 0)
        return HUSH_OK;
    HUSH_TRY(hush_json_lookup(&value, line, "/text"));
    return hush_json_decode(out, outsz, &value);
}

static hush_status_t hush_agent_capture_cline(char *out, size_t outsz, char *capture)
{
    assert(out != NULL && outsz > 0);
    assert(capture != NULL);
    char *cursor = capture;
    for (size_t i = 0; i < (size_t)HUSH_AGENT_CAPTURE_MAX && *cursor != '\0'; ++i) {
        char *end = strchr(cursor, '\n');
        if (end != NULL) *end = '\0';
        if (*cursor != '\0') HUSH_TRY(hush_agent_take_cline_line(out, outsz, cursor));
        if (end == NULL) break;
        cursor = end + 1;
    }
    return out[0] == '\0' ? HUSH_ERR_PARSE : HUSH_OK;
}

static hush_status_t hush_agent_read_capture(char *out, size_t outsz, int input_fd)
{
    assert(out != NULL && outsz > 0);
    assert(input_fd >= 0);
    size_t used = 0;
    for (size_t i = 0; i < outsz && used < outsz - 1; ++i) {
        ssize_t count = read(input_fd, out + used, outsz - 1 - used);
        if (count == 0) { out[used] = '\0'; return HUSH_OK; }
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) return HUSH_ERR_IO;
        if (memchr(out + used, '\0', (size_t)count) != NULL) return HUSH_ERR_PARSE;
        used += (size_t)count;
    }
    return HUSH_ERR_FULL;
}

/* Copies a child's streamed stdout to the relay pipe as it arrives. */
static hush_status_t hush_agent_pump_capture(int input_fd, int output_fd)
{
    char buffer[4096];
    size_t sent = 0;

    assert(input_fd >= 0);
    assert(output_fd >= 0);
    for (;;) {
        ssize_t count = read(input_fd, buffer, sizeof(buffer));
        size_t usable;
        size_t off = 0;

        if (count < 0) {
            if (errno == EINTR)
                continue;
            return HUSH_ERR_IO;
        }
        if (count == 0)
            return HUSH_OK;
        /* The relay stores one event of content; drain the rest silently so a
         * long answer can neither overflow it nor block the writer. */
        usable = (size_t)count;
        if (usable > (size_t)HUSH_EVENT_MAX_CONTENT - sent)
            usable = (size_t)HUSH_EVENT_MAX_CONTENT - sent;
        while (off < usable) {
            ssize_t written = write(output_fd, buffer + off, usable - off);
            if (written < 0) {
                if (errno == EINTR)
                    continue;
                return HUSH_ERR_IO;
            }
            off += (size_t)written;
        }
        sent += usable;
    }
}

static hush_status_t hush_agent_capture_worker(char *out, size_t outsz, int forward_fd,
                                               const hush_agent_job_t *job)
{
    assert(out != NULL && outsz > 0);
    assert(job != NULL);
    int capture[2] = {-1, -1};
    if (pipe(capture) != 0) return HUSH_ERR_IO;
    pid_t child = fork();
    if (child == 0) {
        if (close(capture[0]) != 0) _exit(HUSH_AGENT_EXEC_FAILURE);
        hush_agent_exec_child(capture[1], job);
        _exit(HUSH_AGENT_EXEC_FAILURE);
    }
    hush_status_t status = close(capture[1]) == 0 ? HUSH_OK : HUSH_ERR_IO;
    if (child < 0) status = HUSH_ERR_IO;
    if (status == HUSH_OK && forward_fd >= 0)
        status = hush_agent_pump_capture(capture[0], forward_fd);
    else if (status == HUSH_OK)
        status = hush_agent_read_capture(out, outsz, capture[0]);
    if (status != HUSH_OK && child > 0 && kill(child, SIGKILL) != 0 && errno != ESRCH)
        status = HUSH_ERR_IO;
    if (close(capture[0]) != 0) status = HUSH_ERR_IO;
    if (child > 0 && hush_agent_wait_worker(child) != HUSH_OK) status = HUSH_ERR_IO;
    return status;
}

static hush_status_t hush_agent_capture_reply(char *out, size_t outsz, char *capture,
                                             const char *provider)
{
    assert(out != NULL && outsz > 0);
    assert(capture != NULL && provider != NULL);
    if (strcmp(provider, HUSH_ROSTER_PROVIDER_CLINE) == 0)
        return hush_agent_capture_cline(out, outsz, capture);
    size_t len = strlen(capture);
    if (len >= outsz) {
        /* A verbose answer is truncated to the event content cap so the note
         * still lands; the job must not fail for being longer than the cap. */
        len = outsz - 1;
    }
    memcpy(out, capture, len);
    out[len] = '\0';
    return len == 0 ? HUSH_ERR_PARSE : HUSH_OK;
}

static void hush_agent_run_worker(int output_fd, const hush_agent_job_t *job)
{
    assert(output_fd >= 0);
    assert(job != NULL);
    if (hush_agent_prepare_worker() != HUSH_OK)
        _exit(HUSH_AGENT_EXEC_FAILURE);
    /* Only the supervisor keeps the relay pipe: executed harnesses cannot hold it. */
    if (fcntl(output_fd, F_SETFD, FD_CLOEXEC) < 0) _exit(HUSH_AGENT_EXEC_FAILURE);
    char capture[HUSH_AGENT_CAPTURE_MAX + 1] = {0};
    /* API providers stream deltas straight into the relay pipe, so the relay
     * already holds the whole answer and the supervisor writes nothing more. */
    int forward = hush_inference_is_api(job->provider) ? output_fd : -1;
    if (hush_agent_capture_worker(capture, sizeof(capture), forward, job) != HUSH_OK)
        _exit(HUSH_AGENT_EXEC_FAILURE);
    if (forward >= 0) {
        if (close(output_fd) != 0) _exit(HUSH_AGENT_EXEC_FAILURE);
        _exit(0);
    }
    char reply[HUSH_EVENT_MAX_CONTENT + 1] = {0};
    if (hush_agent_capture_reply(reply, sizeof(reply), capture, job->provider) != HUSH_OK)
        _exit(HUSH_AGENT_EXEC_FAILURE);
    /* PIPE_BUF is at least one event on this Linux relay; incomplete writes fail closed. */
    size_t len = strlen(reply);
    if (write(output_fd, reply, len) != (ssize_t)len) _exit(HUSH_AGENT_EXEC_FAILURE);
    if (close(output_fd) != 0) _exit(HUSH_AGENT_EXEC_FAILURE);
    _exit(0);
}

static void hush_agent_exec_child(int write_fd, const hush_agent_job_t *job)
{
    int dn;

    assert(job != NULL);
    assert(write_fd >= 0);
    if (dup2(write_fd, STDOUT_FILENO) < 0 || close(write_fd) != 0)
        _exit(HUSH_AGENT_EXEC_FAILURE);
    dn = open(HUSH_AGENT_DEVNULL, O_WRONLY);
    if (dn < 0) _exit(HUSH_AGENT_EXEC_FAILURE);
    if (dup2(dn, STDERR_FILENO) < 0 || close(dn) != 0)
        _exit(HUSH_AGENT_EXEC_FAILURE);
    if (hush_inference_is_api(job->provider)) {
        hush_agent_exec_api(job);
        return;
    }
    if (strcmp(job->provider, HUSH_ROSTER_PROVIDER_COPILOT) == 0)
        hush_agent_exec_copilot(job);
    else if (strcmp(job->provider, HUSH_ROSTER_PROVIDER_CODEX) == 0)
        hush_agent_exec_codex(job);
    else if (strcmp(job->provider, HUSH_ROSTER_PROVIDER_GOOSE) == 0)
        hush_agent_exec_goose(job);
    else if (strcmp(job->provider, HUSH_ROSTER_PROVIDER_OLLAMA) == 0)
        hush_agent_exec_ollama(job);
    else if (strcmp(job->provider, HUSH_ROSTER_PROVIDER_CLINE) == 0)
        hush_agent_exec_cline(job);
    else if (strcmp(job->provider, HUSH_ROSTER_PROVIDER_GROK_BUILD) == 0)
        hush_agent_exec_grok(job);
    else
        _exit(HUSH_AGENT_EXEC_FAILURE);
}

static void hush_agent_exec_api(const hush_agent_job_t *job)
{
    assert(job != NULL);
    assert(job->provider[0] != '\0');
    hush_inference_request_t request = {
        .provider = job->provider, .system = job->prompt,
        .rules = job->rules, .message = job->note
    };
    /* The worker captures the whole provider answer; the reply cap applies
     * only when the note is assembled. Deltas already forwarded to stdout must
     * not be written a second time. */
    char response[HUSH_INFERENCE_TEXT_MAX + 1] = {0};
    int streamed = 0;
    if (hush_inference_stream(response, sizeof(response), &request, STDOUT_FILENO,
                              &streamed) != HUSH_OK)
        _exit(HUSH_AGENT_EXEC_FAILURE);
    if (!streamed && (fputs(response, stdout) == EOF || fflush(stdout) != 0))
        _exit(HUSH_AGENT_EXEC_FAILURE);
    _exit(0);
}

static void hush_agent_exec_cline(const hush_agent_job_t *job)
{
    assert(job != NULL);
    char combined[HUSH_AGENT_COMBINED_PROMPT_MAX] = {0};
    hush_agent_build_combined(combined, sizeof(combined), job);
    /* POSIX exec borrows mutable argv. Non-TTY stdout selects Cline headless mode. */
    char *arguments[] = {(char *)"cline", (char *)"--json", (char *)"--cwd", (char *)job->cwd,
        (char *)"--auto-approve", (char *)"false", (char *)"--timeout",
        (char *)"80", combined, NULL};
    execvp(arguments[0], arguments);
    _exit(HUSH_AGENT_EXEC_FAILURE);
}

static void hush_agent_exec_grok(const hush_agent_job_t *job)
{
    char *argv[HUSH_AGENT_ARGV_MAX];

    assert(job != NULL);
    argv[0] = (char *)HUSH_AGENT_GROK_BIN;
    argv[1] = (char *)"-p";
    argv[2] = (char *)job->note;
    argv[3] = (char *)"--system-prompt-override";
    argv[4] = (char *)job->prompt;
    argv[5] = (char *)"--output-format";
    argv[6] = (char *)"plain";
    argv[7] = (char *)"--always-approve";
    argv[8] = (char *)"--no-plan";
    argv[9] = (char *)"--no-subagents";
    argv[10] = (char *)"--disable-web-search";
    argv[11] = (char *)"--max-turns";
    argv[12] = (char *)(job->kind == HUSH_AGENT_KIND_FIXUP
                       ? HUSH_AGENT_FIXUP_TURNS : HUSH_AGENT_GROK_TURNS);
    argv[13] = (char *)"--reasoning-effort";
    argv[14] = (char *)HUSH_AGENT_GROK_EFFORT;
    argv[15] = (char *)"--cwd";
    argv[16] = (char *)job->cwd;
    argv[17] = (char *)"--disallowed-tools";
    argv[18] = (char *)HUSH_AGENT_DISALLOWED;
    argv[19] = (char *)"--rules";
    argv[20] = (char *)job->rules;
    argv[21] = (char *)HUSH_AGENT_GROK_NOMEM;
    argv[22] = NULL;
    execvp(HUSH_AGENT_GROK_BIN, argv);
    _exit(127);
}

static void hush_agent_build_combined(char *out, size_t outsz,
                                      const hush_agent_job_t *job)
{
    int n;

    assert(out != NULL);
    assert(outsz > 0);
    assert(job != NULL);
    n = snprintf(out, outsz, "%s\n%s\n%s",
                 job->prompt, job->rules, job->note);
    if (n < 0 || (size_t)n >= outsz)
        out[outsz - 1] = '\0';
}

static void hush_agent_exec_copilot(const hush_agent_job_t *job)
{
    char combined[HUSH_AGENT_COMBINED_PROMPT_MAX];
    char *argv[5];

    assert(job != NULL);
    hush_agent_build_combined(combined, sizeof(combined), job);
    argv[0] = (char *)HUSH_AGENT_COPILOT_BIN;
    argv[1] = (char *)"-p";
    argv[2] = combined;
    argv[3] = (char *)"--allow-all";
    argv[4] = NULL;
    execvp(argv[0], argv);
    _exit(127);
}

static void hush_agent_exec_codex(const hush_agent_job_t *job)
{
    assert(job != NULL);
    if (hush_codex_prepare_skills(job->cwd) != HUSH_OK)
        _exit(HUSH_AGENT_EXEC_FAILURE);
    char combined[HUSH_AGENT_COMBINED_PROMPT_MAX] = {0};
    hush_agent_build_combined(combined, sizeof(combined), job);
    /* POSIX execvp requires a mutable argv; borrowed strings remain unchanged. */
    char *arguments[] = {
        (char *)HUSH_AGENT_CODEX_BIN, (char *)"exec",
        (char *)"--cd", (char *)job->cwd,
        (char *)"--skip-git-repo-check", (char *)"--sandbox", (char *)"read-only",
        combined, NULL
    };
    execvp(arguments[0], arguments);
    _exit(HUSH_AGENT_EXEC_FAILURE);
}

static void hush_agent_exec_goose(const hush_agent_job_t *job)
{
    char combined[HUSH_AGENT_COMBINED_PROMPT_MAX];
    char *argv[5];

    assert(job != NULL);
    hush_agent_build_combined(combined, sizeof(combined), job);
    argv[0] = (char *)HUSH_AGENT_GOOSE_BIN;
    argv[1] = (char *)"run";
    argv[2] = (char *)"--text";
    argv[3] = combined;
    argv[4] = NULL;
    execvp(argv[0], argv);
    _exit(127);
}

static void hush_agent_exec_ollama(const hush_agent_job_t *job)
{
    char combined[HUSH_AGENT_COMBINED_PROMPT_MAX];
    hush_provider_status_t st;
    char *argv[6];

    assert(job != NULL);
    hush_agent_build_combined(combined, sizeof(combined), job);
    /* Local inference: the model is configured on the Ollama provider overlay
     * (providers.json -> model). runtime_ready() already requires one, so the
     * model field is non-empty here; guard anyway. */
    (void)hush_provider_status(&st, HUSH_ROSTER_PROVIDER_OLLAMA);
    if (st.model[0] == '\0')
        _exit(127);
    argv[0] = (char *)HUSH_AGENT_OLLAMA_BIN;
    argv[1] = (char *)"run";
    argv[2] = st.model;
    argv[3] = combined;
    argv[4] = NULL;
    execvp(argv[0], argv);
    _exit(127);
}

hush_status_t hush_agent_spawn_grok(hush_agent_job_t *job)
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
        hush_agent_run_worker(fds[1], job);
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

