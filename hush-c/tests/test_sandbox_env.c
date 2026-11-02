/* tests/test_sandbox_env.c: #375 PA1, no implicit key leak. A jailed grok
 * child of an OAuth job (the default) never sees XAI_API_KEY or any GROK_*
 * variable; one whose job chose an API key keeps them; another provider's
 * child gets its allowlisted keys either way. A stub grok and a stub codex
 * first on PATH dump their env into the job cwd, so no real CLI ever starts.
 * All values are fake. */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "hush_sandbox.h"

enum {
    ENV_TEST_PATH_MAX = 512,
    ENV_TEST_DUMP_MAX = 65536,
    ENV_TEST_DIR_MODE = 0700,
    ENV_TEST_RELAY_PORT = 9,  /* any port: the jail only blocks it */
    ENV_TEST_EXEC_FAIL = 127  /* child exit when the jail refuses */
};

static const char ENV_TEST_SYS_PATH[] = "/usr/bin:/bin";
static const char ENV_TEST_STUB[] = "#!/bin/sh\nenv > \"$1\"\n";

/* Fake secrets set in the parent. Each names who may receive it. */
static const char ENV_TEST_XAI[] = "XAI_API_KEY=fake-xai-key";
static const char ENV_TEST_GROK[] = "GROK_FOO=fake-grok-override";
static const char ENV_TEST_OPENAI[] = "OPENAI_API_KEY=fake-openai-key";
static const char ENV_TEST_CODEX[] = "CODEX_FOO=fake-codex-option";

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

/* Writes an executable stub named name into bin. */
static void write_stub(const char *bin, const char *name)
{
    char path[ENV_TEST_PATH_MAX];
    FILE *fp = NULL;

    snprintf(path, sizeof path, "%s/%s", bin, name);
    fp = fopen(path, "w");
    expect(fp != NULL && fputs(ENV_TEST_STUB, fp) >= 0, "stub write");
    if (fp != NULL)
        expect(fclose(fp) == 0, "stub close");
    expect(chmod(path, S_IRWXU) == 0, "stub mode");
}

/* Sets one NAME=value entry from a constant. */
static void put_fake(const char *entry)
{
    char name[ENV_TEST_PATH_MAX];
    const char *eq = strchr(entry, '=');
    size_t n = (size_t)(eq - entry);

    memcpy(name, entry, n);
    name[n] = '\0';
    expect(setenv(name, eq + 1, 1) == 0, name);
}

/* Runs name jailed in cwd; it dumps its env to cwd/name.env, read into out.
 * Returns 1 when the jail ran the stub, 0 when the jail refused it. */
static int run_jailed(char *out, size_t outsz, const char *cwd, const char *name,
                      hush_sandbox_cred_t cred)
{
    char home[ENV_TEST_PATH_MAX];
    char dump[ENV_TEST_PATH_MAX * 2];
    int status = 0;

    out[0] = '\0';
    snprintf(dump, sizeof dump, "%s/%s.env", cwd, name);
    expect(hush_sandbox_home_make(home, sizeof home) == HUSH_OK, "stub home");
    pid_t pid = fork();
    if (pid == 0) {
        hush_sandbox_job_t job = {cwd, home, ENV_TEST_RELAY_PORT, cred};
        char *argv[] = {(char *)name, dump, NULL};

        if (chdir(cwd) == 0)
            (void)hush_sandbox_exec(&job, name, argv);
        _exit(ENV_TEST_EXEC_FAIL);
    }
    expect(pid > 0 && waitpid(pid, &status, 0) == pid, "reap");
    hush_sandbox_home_remove(home, sizeof home);
    if (WIFEXITED(status) && WEXITSTATUS(status) == ENV_TEST_EXEC_FAIL)
        return 0;
    expect(WIFEXITED(status) && WEXITSTATUS(status) == 0, name);
    FILE *fp = fopen(dump, "r");
    expect(fp != NULL, "env dump");
    if (fp == NULL)
        return 1;
    size_t n = fread(out, 1, outsz - 1, fp);
    out[n] = '\0';
    (void)fclose(fp);
    expect(unlink(dump) == 0, "dump cleanup");
    return 1;
}

/* 1 when the dump holds the exact line entry. */
static int has_line(const char *dump, const char *entry)
{
    size_t n = strlen(entry);

    for (const char *at = strstr(dump, entry); at != NULL; at = strstr(at + 1, entry)) {
        if ((at == dump || at[-1] == '\n') && (at[n] == '\n' || at[n] == '\0'))
            return 1;
    }
    return 0;
}

/* A grok child that chose an API key keeps XAI_API_KEY and GROK_FOO, and a
 * codex child keeps all four fakes, whatever cred its job carries. */
static void check_keyed(char *dump, size_t dumpsz, const char *root)
{
    expect(run_jailed(dump, dumpsz, root, "grok", HUSH_SANDBOX_CRED_API_KEY),
           "jail refused the API-key grok stub");
    expect(has_line(dump, ENV_TEST_XAI), "API-key grok job lost XAI_API_KEY");
    expect(has_line(dump, ENV_TEST_GROK), "API-key grok job lost GROK_FOO");
    for (int cred = HUSH_SANDBOX_CRED_OAUTH; cred <= HUSH_SANDBOX_CRED_API_KEY; ++cred) {
        expect(run_jailed(dump, dumpsz, root, "codex", (hush_sandbox_cred_t)cred),
               "jail refused the codex stub");
        expect(has_line(dump, ENV_TEST_XAI), "codex child lost XAI_API_KEY");
        expect(has_line(dump, ENV_TEST_OPENAI), "codex child lost OPENAI_API_KEY");
        expect(has_line(dump, ENV_TEST_CODEX), "codex child lost CODEX_FOO");
        expect(has_line(dump, ENV_TEST_GROK), "codex child lost GROK_FOO");
    }
}

/* Forces one jail backend, then runs the stubs under it. A refused jail
 * fails only for the detected backend; a forced bwrap may be absent. */
static void check_backend(const char *root, const char *backend, int required)
{
    static char dump[ENV_TEST_DUMP_MAX];

    expect(setenv("HUSH_SANDBOX_BACKEND", backend, 1) == 0, "backend env");
    if (!run_jailed(dump, sizeof dump, root, "grok", HUSH_SANDBOX_CRED_OAUTH)) {
        expect(!required, "detected jail refused the grok stub");
        printf("sandbox env: %s SKIP (jail refused)\n", backend);
        return;
    }
    expect(!has_line(dump, ENV_TEST_XAI), "OAuth grok job got XAI_API_KEY");
    expect(!has_line(dump, ENV_TEST_GROK), "OAuth grok job got GROK_FOO");
    expect(has_line(dump, ENV_TEST_OPENAI), "grok child lost the shared allowlist");
    check_keyed(dump, sizeof dump, root);
}

int main(void)
{
    char root[] = "/tmp/hush-sandbox-env-XXXXXX";
    char bin[ENV_TEST_PATH_MAX];
    char path[ENV_TEST_PATH_MAX * 2];

    if (hush_sandbox_backend() != HUSH_SANDBOX_BACKEND_LANDLOCK) {
        printf("sandbox env: SKIP (no Landlock ABI>=4 here)\n");
        return 0;
    }
    if (mkdtemp(root) == NULL)
        return 1;
    snprintf(bin, sizeof bin, "%s/bin", root);
    expect(mkdir(bin, ENV_TEST_DIR_MODE) == 0, "bin dir");
    write_stub(bin, "grok");
    write_stub(bin, "codex");
    snprintf(path, sizeof path, "%s:%s", bin, ENV_TEST_SYS_PATH);
    expect(setenv("PATH", path, 1) == 0, "PATH");
    put_fake(ENV_TEST_XAI);
    put_fake(ENV_TEST_GROK);
    put_fake(ENV_TEST_OPENAI);
    put_fake(ENV_TEST_CODEX);
    check_backend(root, "landlock", 1);
    check_backend(root, "bwrap", 0);
    if (g_fail)
        return 1;
    printf("sandbox env ok\n");
    return 0;
}
