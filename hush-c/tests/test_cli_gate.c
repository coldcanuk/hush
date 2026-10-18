/* tests/test_cli_gate.c: #356. Grok's permission argv never auto-approves
 * and allows Edit only on a gated turn. A Grok or Copilot turn starts only
 * after its readiness probes pass: --version at or above the pinned minimum
 * and help listing every permission flag, each probe jailed and on a clock.
 * Stub CLIs under a scratch TMPDIR answer the probes; no real CLI runs. */

#define _XOPEN_SOURCE 700

#include <ftw.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "hush_cli_gate.h"
#include "hush_sandbox.h"

enum {
    CG_PATH_MAX = 512,
    CG_BODY_MAX = 2048,
    CG_WHY_MAX = 256,
    CG_FTW_FDS = 16,
    /* A hung probe must be refused within the probe clock plus this slack. */
    CG_HANG_SLACK_MS = 3000,
    CG_MS_PER_S = 1000,
    CG_NS_PER_MS = 1000000
};

#define CG_GROK_HELP "echo \"--permission-mode <mode> (default, dontAsk) --allow --deny " \
                     "--disallowed-tools\"; exit 0"
#define CG_COPILOT_HELP "echo \"--deny-tool --excluded-tools --allow-tool " \
                        "--disable-builtin-mcps --disallow-temp-dir\"; exit 0"

/* One run: the failure flag and the scratch tree. Nothing here is static. */
typedef struct {
    int fail;
    char dir[CG_PATH_MAX];
    char home[CG_PATH_MAX];
    char log[CG_PATH_MAX + 64];
} cg_t;

static void expect(cg_t *t, int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        t->fail = 1;
    }
}

/* Writes <dir>/bin/<name>: logs $1, answers --version with version_body and
 * the help probe with help_body, exits 9 for anything else. */
static void stub(cg_t *t, const char *name, const char *version_body, const char *help_body)
{
    char path[CG_PATH_MAX + 64] = {0};
    char body[CG_BODY_MAX] = {0};
    FILE *f = NULL;

    snprintf(path, sizeof(path), "%s/bin/%s", t->dir, name);
    snprintf(body, sizeof(body), "#!/bin/sh\necho \"$1\" >> '%s'\ncase \"$1\" in\n"
             "--version) %s;;\n--help|help) %s;;\nesac\nexit 9\n",
             t->log, version_body, help_body);
    f = fopen(path, "w");
    expect(t, f != NULL && fputs(body, f) >= 0, "write stub");
    if (f != NULL)
        fclose(f);
    expect(t, chmod(path, S_IRWXU) == 0, "stub mode");
}

static hush_status_t ready(cg_t *t, const hush_cli_spec_t *spec)
{
    hush_sandbox_job_t jail = {t->dir, t->home, 0};
    char why[CG_WHY_MAX] = {0};

    return hush_cli_ready(spec, &jail, why, sizeof(why));
}

static long now_ms(void)
{
    struct timespec ts = {0, 0};

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * CG_MS_PER_S + ts.tv_nsec / CG_NS_PER_MS;
}

static void check_versions(cg_t *t)
{
    const hush_cli_version_t grok = {0, 2, 50};
    const hush_cli_version_t copilot = {1, 0, 0};

    expect(t, hush_cli_version_ok("grok 0.2.50\n", &grok) == 1, "pinned grok version passes");
    expect(t, hush_cli_version_ok("grok 0.2.49\n", &grok) == 0, "older grok patch refused");
    expect(t, hush_cli_version_ok("v0.10.0", &grok) == 1, "newer minor passes");
    expect(t, hush_cli_version_ok("1.0.0", &grok) == 1, "newer major passes");
    expect(t, hush_cli_version_ok("0.1.99", &grok) == 0, "older minor refused");
    expect(t, hush_cli_version_ok("build 12.0 then 0.2.51", &grok) == 1, "first triple wins");
    expect(t, hush_cli_version_ok("no version here", &grok) == 0, "no version refused");
    expect(t, hush_cli_version_ok("", &grok) == 0, "empty output refused");
    expect(t, hush_cli_version_ok("0.2", &grok) == 0, "two fields refused");
    expect(t, hush_cli_version_ok("copilot 0.0.370", &copilot) == 0, "pre-GA copilot refused");
    expect(t, hush_cli_version_ok("GitHub Copilot CLI 1.0.3.", &copilot) == 1, "GA copilot");
}

static void check_help(cg_t *t)
{
    const char *const grok[] = {"--allow", "--deny", "dontAsk", NULL};

    expect(t, hush_cli_help_ok("--allow x --deny y (dontAsk)", grok) == 1, "all flags listed");
    expect(t, hush_cli_help_ok("--allow-tool --deny dontAsk", grok) == 0,
           "--allow-tool is not --allow");
    expect(t, hush_cli_help_ok("--allow --deny-tool dontAsk", grok) == 0,
           "--deny-tool is not --deny");
    expect(t, hush_cli_help_ok("--allow --deny dontAskMe", grok) == 0, "whole words only");
    expect(t, hush_cli_help_ok("", grok) == 0, "empty help refused");
}

/* 1 when argv holds flag immediately followed by value. */
static int has_pair(char **argv, const char *flag, const char *value)
{
    for (size_t i = 0; i + 1 < (size_t)HUSH_CLI_GROK_POLICY_SLOTS; i++) {
        if (strcmp(argv[i], flag) == 0 && strcmp(argv[i + 1], value) == 0)
            return 1;
    }
    return 0;
}

static void check_policy(cg_t *t)
{
    char *argv[HUSH_CLI_GROK_POLICY_SLOTS] = {0};
    const char *denied[] = {"Bash", "MCPTool", "WebFetch", "WebSearch"};

    expect(t, hush_cli_grok_policy(argv, HUSH_CLI_GROK_POLICY_SLOTS - 1, 1) == 0,
           "policy refuses a short buffer");
    for (int gated = 0; gated <= 1; gated++) {
        expect(t, hush_cli_grok_policy(argv, HUSH_CLI_GROK_POLICY_SLOTS, gated) ==
                  (size_t)HUSH_CLI_GROK_POLICY_SLOTS, "policy fills its slots");
        expect(t, has_pair(argv, "--permission-mode", "dontAsk"), "dontAsk mode");
        expect(t, has_pair(argv, "--allow", "Read") && has_pair(argv, "--allow", "Grep"),
               "read tools allowed");
        for (size_t i = 0; i < sizeof(denied) / sizeof(denied[0]); i++)
            expect(t, has_pair(argv, "--deny", denied[i]), denied[i]);
        expect(t, has_pair(argv, gated ? "--allow" : "--deny", "Edit"), "Edit follows the gate");
        expect(t, !has_pair(argv, gated ? "--deny" : "--allow", "Edit"), "Edit only one way");
    }
}

/* Each readiness case: a stub, then HUSH_OK only for the good ones. */
static void check_ready(cg_t *t)
{
    /* The relay ignores SIGCHLD; readiness must still reap its probes. */
    expect(t, signal(SIGCHLD, SIG_IGN) != SIG_ERR, "ignore SIGCHLD like the relay");
    stub(t, "grok", "echo \"grok 0.2.50\"; exit 0", CG_GROK_HELP);
    expect(t, ready(t, &hush_cli_spec_grok) == HUSH_OK, "good grok is ready");
    stub(t, "grok", "echo \"grok 0.2.49\"; exit 0", CG_GROK_HELP);
    expect(t, ready(t, &hush_cli_spec_grok) == HUSH_ERR_DENIED, "old grok refused");
    stub(t, "grok", "echo \"grok 0.2.50\"; exit 0",
         "echo \"--permission-mode dontAsk --allow --deny-tool --disallowed-tools\"; exit 0");
    expect(t, ready(t, &hush_cli_spec_grok) == HUSH_ERR_DENIED, "grok help without --deny");
    stub(t, "grok", "echo \"grok 0.2.50\"; exit 0",
         "echo \"--permission-mode --allow --deny --disallowed-tools\"; exit 0");
    expect(t, ready(t, &hush_cli_spec_grok) == HUSH_ERR_DENIED, "grok help without dontAsk");
    stub(t, "grok", "echo \"grok 0.2.50\"; exit 3", CG_GROK_HELP);
    expect(t, ready(t, &hush_cli_spec_grok) == HUSH_ERR_DENIED, "failing --version refused");
    stub(t, "grok", "echo \"grok 0.2.50\"; exit 0", "echo \"--permission-mode dontAsk "
         "--allow --deny --disallowed-tools\"; exit 1");
    expect(t, ready(t, &hush_cli_spec_grok) == HUSH_ERR_DENIED, "failing help refused");
    stub(t, "copilot", "echo \"1.0.0\"; exit 0", CG_COPILOT_HELP);
    expect(t, ready(t, &hush_cli_spec_copilot) == HUSH_OK, "good copilot is ready");
    stub(t, "copilot", "echo \"0.0.370\"; exit 0", CG_COPILOT_HELP);
    expect(t, ready(t, &hush_cli_spec_copilot) == HUSH_ERR_DENIED, "pre-GA copilot refused");
    stub(t, "copilot", "echo \"1.0.0\"; exit 0", "echo \"--deny-tool --excluded-tools "
         "--allow-tool --disable-builtin-mcps\"; exit 0");
    expect(t, ready(t, &hush_cli_spec_copilot) == HUSH_ERR_DENIED,
           "copilot help without --disallow-temp-dir");
}

/* A hung --version is killed and refused on the probe clock; a missing
 * binary is refused; no probe ever passes a prompt. */
static void check_hang_and_missing(cg_t *t)
{
    char path[CG_PATH_MAX + 64] = {0};
    long start = 0;

    stub(t, "grok", "exec sleep 30", CG_GROK_HELP);
    start = now_ms();
    expect(t, ready(t, &hush_cli_spec_grok) == HUSH_ERR_DENIED, "hung grok refused");
    expect(t, now_ms() - start < HUSH_CLI_PROBE_MS + CG_HANG_SLACK_MS,
           "hung probe ends on the probe clock");
    snprintf(path, sizeof(path), "%s/bin/copilot", t->dir);
    expect(t, unlink(path) == 0, "remove copilot stub");
    expect(t, ready(t, &hush_cli_spec_copilot) != HUSH_OK, "missing copilot refused");
}

/* 1 when the probe log names only probe arguments. */
static int log_has_only_probes(cg_t *t)
{
    char line[CG_PATH_MAX] = {0};
    FILE *f = fopen(t->log, "r");
    int ok = f != NULL;

    while (f != NULL && fgets(line, sizeof(line), f) != NULL) {
        if (strcmp(line, "--version\n") != 0 && strcmp(line, "--help\n") != 0 &&
            strcmp(line, "help\n") != 0)
            ok = 0;
    }
    if (f != NULL)
        fclose(f);
    return ok;
}

static int remove_entry(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
    (void)st;
    (void)flag;
    (void)ftw;
    return remove(path);
}

/* <TMPDIR or /tmp>/hush-cli-gate-XXXXXX/{bin,probes.log}, PATH first, and a
 * job HOME made the way a turn makes one. */
static int open_scratch(cg_t *t)
{
    const char *tmp = getenv("TMPDIR");
    char path[CG_PATH_MAX + 64] = {0};

    snprintf(t->dir, sizeof(t->dir), "%s/hush-cli-gate-XXXXXX",
             tmp != NULL && tmp[0] != '\0' ? tmp : "/tmp");
    if (mkdtemp(t->dir) == NULL)
        return 0;
    snprintf(t->log, sizeof(t->log), "%s/probes.log", t->dir);
    snprintf(path, sizeof(path), "%s/bin", t->dir);
    expect(t, mkdir(path, S_IRWXU) == 0, "bin dir");
    snprintf(path, sizeof(path), "%s/bin:/usr/bin:/bin", t->dir);
    expect(t, setenv("PATH", path, 1) == 0, "PATH");
    expect(t, hush_sandbox_home_make(t->home, sizeof(t->home)) == HUSH_OK, "job HOME");
    return 1;
}

int main(void)
{
    cg_t t = {0};

    check_versions(&t);
    check_help(&t);
    check_policy(&t);
    if (!open_scratch(&t)) {
        fprintf(stderr, "FAIL scratch\n");
        return 1;
    }
    check_ready(&t);
    check_hang_and_missing(&t);
    expect(&t, log_has_only_probes(&t), "probes never ran a prompt");
    hush_sandbox_home_remove(t.home, sizeof(t.home));
    expect(&t, nftw(t.dir, remove_entry, CG_FTW_FDS, FTW_DEPTH | FTW_PHYS) == 0, "cleanup");
    if (t.fail)
        return 1;
    printf("cli gate ok\n");
    return 0;
}
