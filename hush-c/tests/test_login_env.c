/* tests/test_login_env.c: #375 N1, no implicit key into `grok login --oauth`.
 * The login runs unjailed, so hush_provider_start_login itself must drop
 * XAI_API_KEY and every GROK_* variable for grok, on the direct path (also
 * with the key as the last environ entry), through HUSH_PROVIDER_TERM and
 * through the DISPLAY xterm, and leave another provider's login env alone.
 * Stub grok, codex, terminal and xterm binaries first on PATH dump their env,
 * so no real CLI or terminal starts. All values are fake. The run removes
 * exactly the files and dirs it made. */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "hush_provider.h"
#include "hush_sandbox.h"

extern char **environ;

enum {
    LOGIN_TEST_PATH_MAX = 512,
    LOGIN_TEST_DUMP_MAX = 65536,
    LOGIN_TEST_DIR_MODE = 0700,
    LOGIN_TEST_POLLS = 500,           /* polls for the dump file */
    LOGIN_TEST_POLL_NS = 10000000     /* 10 ms per poll: 5 s in all */
};

static const char LOGIN_TEST_SYS_PATH[] = "/usr/bin:/bin";
static const char LOGIN_TEST_DUMP_ENV[] = "LOGIN_TEST_DUMP";
/* A provider stub: dumps its env, then publishes the dump by rename. */
static const char LOGIN_TEST_STUB[] =
    "#!/bin/sh\nenv > \"$LOGIN_TEST_DUMP.part\" && mv \"$LOGIN_TEST_DUMP.part\" "
    "\"$LOGIN_TEST_DUMP\"\n";
/* A terminal stub: run as `term -e cmd`, it runs cmd. */
static const char LOGIN_TEST_TERM[] = "#!/bin/sh\nexec sh -c \"$2\"\n";
/* An xterm stub: run as `xterm -hold -e cmd` ($3 is cmd), it runs cmd. */
static const char LOGIN_TEST_XTERM[] = "#!/bin/sh\nexec sh -c \"$3\"\n";

/* Every stub this run writes into root/bin, and its body. */
static const char *const LOGIN_TEST_STUB_NAMES[] = {"grok", "codex", "term", "xterm"};
static const char *const LOGIN_TEST_STUB_BODIES[] = {
    LOGIN_TEST_STUB, LOGIN_TEST_STUB, LOGIN_TEST_TERM, LOGIN_TEST_XTERM
};
enum { LOGIN_TEST_NSTUBS = sizeof LOGIN_TEST_STUB_NAMES / sizeof LOGIN_TEST_STUB_NAMES[0] };

/* Fake secrets set in the parent. */
static const char LOGIN_TEST_XAI[] = "XAI_API_KEY=fake-xai-key";
static const char LOGIN_TEST_GROK[] = "GROK_FOO=fake-grok-override";
static const char LOGIN_TEST_OPENAI[] = "OPENAI_API_KEY=fake-openai-key";

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

/* Writes an executable script body to dir/name. */
static void write_script(const char *dir, const char *name, const char *body)
{
    char path[LOGIN_TEST_PATH_MAX * 2];
    FILE *fp = NULL;

    snprintf(path, sizeof path, "%s/%s", dir, name);
    fp = fopen(path, "w");
    expect(fp != NULL && fputs(body, fp) >= 0, "script write");
    if (fp != NULL)
        expect(fclose(fp) == 0, "script close");
    expect(chmod(path, S_IRWXU) == 0, "script mode");
}

/* Sets one NAME=value entry from a constant. */
static void put_fake(const char *entry)
{
    char name[LOGIN_TEST_PATH_MAX];
    const char *eq = strchr(entry, '=');
    size_t n = (size_t)(eq - entry);

    memcpy(name, entry, n);
    name[n] = '\0';
    expect(setenv(name, eq + 1, 1) == 0, name);
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

/* Starts provider id's login with its dump at root/tag.env and reads the
 * dump into out once the stub publishes it. Returns 1 when it arrived. */
static int run_login(char *out, size_t outsz, const char *root, const char *id,
                     const char *tag)
{
    char dump[LOGIN_TEST_PATH_MAX * 2];
    struct timespec step = {.tv_sec = 0, .tv_nsec = LOGIN_TEST_POLL_NS};
    FILE *fp = NULL;

    out[0] = '\0';
    snprintf(dump, sizeof dump, "%s/%s.env", root, tag);
    expect(setenv(LOGIN_TEST_DUMP_ENV, dump, 1) == 0, "dump env");
    expect(hush_provider_start_login(id) == HUSH_OK, tag);
    for (int i = 0; i < LOGIN_TEST_POLLS && fp == NULL; ++i) {
        fp = fopen(dump, "r");
        if (fp == NULL)
            (void)nanosleep(&step, NULL);
    }
    while (waitpid(-1, NULL, WNOHANG) > 0)
        continue;
    expect(fp != NULL, "login dump never arrived");
    if (fp == NULL)
        return 0;
    size_t n = fread(out, 1, outsz - 1, fp);
    out[n] = '\0';
    (void)fclose(fp);
    expect(unlink(dump) == 0, "dump cleanup");
    return 1;
}

/* Grok's login drops both fakes and keeps the rest; codex keeps all. */
static void check_logins(const char *root, const char *path_tag)
{
    static char dump[LOGIN_TEST_DUMP_MAX];
    char tag[LOGIN_TEST_PATH_MAX];

    snprintf(tag, sizeof tag, "grok-%s", path_tag);
    if (run_login(dump, sizeof dump, root, "grok-build", tag)) {
        expect(!has_line(dump, LOGIN_TEST_XAI), "grok login got XAI_API_KEY");
        expect(!has_line(dump, LOGIN_TEST_GROK), "grok login got GROK_FOO");
        expect(has_line(dump, LOGIN_TEST_OPENAI), "grok login lost OPENAI_API_KEY");
    }
    snprintf(tag, sizeof tag, "codex-%s", path_tag);
    if (run_login(dump, sizeof dump, root, "codex", tag)) {
        expect(has_line(dump, LOGIN_TEST_XAI), "codex login lost XAI_API_KEY");
        expect(has_line(dump, LOGIN_TEST_GROK), "codex login lost GROK_FOO");
    }
}

/* 1 when env holds the exact entry. */
static int env_has(char **env, const char *entry)
{
    for (char **e = env; *e != NULL; ++e) {
        if (strcmp(*e, entry) == 0)
            return 1;
    }
    return 0;
}

/* #375 Gauge HC-1 and HC-2: hush_sandbox_drop_grok_env_for on fixed
 * environs. A key that is the last entry is dropped (the compacted environ
 * is NULL-terminated again), and an entry with no '=' is kept. */
static void check_drop_units(void)
{
    static char keep[] = "KEEP=1";
    static char noeq[] = "NOEQ";
    static char xai[] = "XAI_API_KEY=fake-xai-key";
    static char grok[] = "GROK_FOO=fake-grok-override";
    char *tail[] = {keep, noeq, grok, xai, NULL};
    char *bare[] = {noeq, NULL};
    char **was = environ;

    environ = tail;
    hush_sandbox_drop_grok_env_for("grok");
    expect(!env_has(environ, xai), "drop kept XAI_API_KEY as the last entry");
    expect(!env_has(environ, grok), "drop kept GROK_FOO");
    expect(env_has(environ, keep) && env_has(environ, noeq), "drop lost a plain entry");
    environ = bare;
    hush_sandbox_drop_grok_env_for("grok");
    expect(environ[0] == noeq && environ[1] == NULL, "drop lost an entry with no '='");
    environ = was;
}

/* 1 when the last environ entry is the exact entry. */
static int env_last_is(const char *entry)
{
    char **e = environ;

    while (e[0] != NULL && e[1] != NULL)
        ++e;
    return e[0] != NULL && strcmp(e[0], entry) == 0;
}

/* Writes every stub into bin; returns how many were written. */
static int write_stubs(const char *bin)
{
    for (int i = 0; i < LOGIN_TEST_NSTUBS; ++i)
        write_script(bin, LOGIN_TEST_STUB_NAMES[i], LOGIN_TEST_STUB_BODIES[i]);
    return LOGIN_TEST_NSTUBS;
}

/* Removes the first nstubs stubs from bin, then bin and root: only the
 * paths this run made. Returns 1 when all are gone. */
static int remove_own(const char *root, const char *bin, int nstubs)
{
    char path[LOGIN_TEST_PATH_MAX * 2];
    int ok = 1;

    for (int i = 0; i < nstubs; ++i) {
        snprintf(path, sizeof path, "%s/%s", bin, LOGIN_TEST_STUB_NAMES[i]);
        ok = unlink(path) == 0 && ok;
    }
    ok = rmdir(bin) == 0 && ok;
    return rmdir(root) == 0 && ok;
}

/* Runs the four login legs: direct, direct with XAI_API_KEY last in
 * environ, HUSH_PROVIDER_TERM, and the DISPLAY xterm. */
static void check_legs(const char *root, const char *bin)
{
    char term[LOGIN_TEST_PATH_MAX * 2];

    check_logins(root, "direct");
    /* The dump var exists now, so the re-set key lands after it, last. */
    expect(unsetenv("XAI_API_KEY") == 0, "unset key");
    put_fake(LOGIN_TEST_XAI);
    expect(env_last_is(LOGIN_TEST_XAI), "key is not the last environ entry");
    check_logins(root, "tail");
    snprintf(term, sizeof term, "%s/term", bin);
    expect(setenv("HUSH_PROVIDER_TERM", term, 1) == 0, "term env");
    check_logins(root, "term");
    expect(unsetenv("HUSH_PROVIDER_TERM") == 0 && setenv("DISPLAY", ":0", 1) == 0,
           "xterm env");
    check_logins(root, "xterm");
}

int main(void)
{
    char root[] = "/tmp/hush-login-env-XXXXXX";
    char bin[LOGIN_TEST_PATH_MAX];
    char path[LOGIN_TEST_PATH_MAX * 2];

    check_drop_units();
    if (mkdtemp(root) == NULL)
        return 1;
    printf("test_login_env: scratch root %s\n", root); /* removed below */
    (void)fflush(stdout);
    snprintf(bin, sizeof bin, "%s/bin", root);
    expect(mkdir(bin, LOGIN_TEST_DIR_MODE) == 0, "bin dir");
    int nstubs = write_stubs(bin);
    snprintf(path, sizeof path, "%s:%s", bin, LOGIN_TEST_SYS_PATH);
    expect(setenv("PATH", path, 1) == 0, "PATH");
    expect(unsetenv("DISPLAY") == 0 && unsetenv("HUSH_PROVIDER_TERM") == 0, "no terminal");
    put_fake(LOGIN_TEST_XAI);
    put_fake(LOGIN_TEST_GROK);
    put_fake(LOGIN_TEST_OPENAI);
    check_legs(root, bin);
    expect(remove_own(root, bin, nstubs), "scratch root not removed");
    if (g_fail)
        return 1;
    printf("login env ok\n");
    return 0;
}
