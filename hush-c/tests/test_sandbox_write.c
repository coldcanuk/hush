/* tests/test_sandbox_write.c: #375 N2, Landlock write scope. A jailed child
 * may write only its cwd, its stub HOME (which holds its private TMPDIR) and
 * the declared device nodes; the rest of the tree, the shared /tmp, the real
 * auth trees and /dev/shm are read-only, and an XDG_*_HOME outside its HOME
 * is dropped. The job's own provider token file (grok or codex auth.json)
 * is writable in place, never through a symlink, and no other provider's is
 * (B2). bwrap's write scope stays what it was. A stub probe script runs
 * through the real jail and reports each write. All homes and values are
 * fake; the real HOME is never used. */

#define _XOPEN_SOURCE 700

#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "hush_sandbox.h"

enum {
    WRITE_TEST_PATH_MAX = 512,
    WRITE_TEST_DUMP_MAX = 16384,
    WRITE_TEST_DIR_MODE = 0700,
    WRITE_TEST_EXEC_FAIL = 127, /* child exit when the jail refuses */
    WRITE_TEST_FTW_FDS = 16
};

static const char WRITE_TEST_SYS_PATH[] = "/usr/bin:/bin";
static const char WRITE_TEST_TREE_NAME[] = ".hush-write-probe";
static const char WRITE_TEST_SHM[] = "/dev/shm/hush-write-probe";
static const char WRITE_TEST_DATA_REL[] = ".local/share";
static const char WRITE_TEST_TOKEN[] = "{\"fake\":true}\n";
/* What an own-token run may not reach: a symlink target outside HOME. */
static const char WRITE_TEST_TARGET[] = "{\"target\":true}\n";
/* argv: dump outside tree auth pts. Prints name=ok or name=fail:<error>. */
static const char WRITE_TEST_PROBE[] =
    "#!/bin/sh\n"
    "try() { if err=$( { : > \"$2\"; } 2>&1 ); then echo \"$1=ok\"; "
    "else echo \"$1=fail:$err\"; fi; }\n"
    "{\n"
    "try cwd \"$PWD/w.txt\"; try home \"$HOME/w.txt\"\n"
    "try tmpdir \"${TMPDIR:-/nonexistent}/w.txt\"\n"
    "try outside \"$2/w.txt\"; try tree \"$3\"; try auth \"$HOME/.grok/w.txt\"\n"
    "try authreal \"$4/w.txt\"; try devnull /dev/null; try devzero /dev/zero\n"
    "try tty /dev/tty; try pts \"$5\"; try urandom /dev/urandom\n"
    "try shm /dev/shm/hush-write-probe\n"
    "try relcwd ./w-rel.txt; echo \"pwd_real=$(pwd -P)\"\n"
    "try grokfile \"$HOME/.grok/auth.json\"; try codexfile \"$HOME/.codex/auth.json\"\n"
    "try grokinner \"$HOME/.grok/auth.json/inner\"\n"
    "for p in grok codex; do f=\"$HOME/.$p/auth.json\"\n"
    "if err=$( { : > \"$f.tmp\" && mv -f \"$f.tmp\" \"$f\"; } 2>&1 ); then echo \"${p}rename=ok\"; "
    "else echo \"${p}rename=fail:$err\"; fi; done\n"
    "if stty -g <\"$5\" >/dev/null 2>&1; then echo pts_ioctl=ok; else echo pts_ioctl=fail; fi\n"
    "echo \"urandom_read=$(head -c 1 /dev/urandom | wc -c)\"\n"
    "echo \"TMPDIR=${TMPDIR:-unset}\"\n"
    "case \"${TMPDIR:-}\" in \"$HOME\"/*) echo tmp_under_home=1;; *) echo tmp_under_home=0;; esac\n"
    "echo \"XDG_CACHE_HOME=${XDG_CACHE_HOME:-unset}\"\n"
    "echo \"XDG_DATA_HOME=${XDG_DATA_HOME:-unset}\"\n"
    "if cat \"$HOME/.grok/auth.json\" > /dev/null 2>&1; then echo auth_read=ok; "
    "else echo auth_read=fail; fi\n"
    "} > \"$1\"\n";

/* Paths one run needs, all under the test root except tree. */
typedef struct {
    char root[WRITE_TEST_PATH_MAX];
    char cwd[WRITE_TEST_PATH_MAX];
    char outside[WRITE_TEST_PATH_MAX];
    char tree[WRITE_TEST_PATH_MAX * 2];
    char auth[WRITE_TEST_PATH_MAX];
    char pts[WRITE_TEST_PATH_MAX];
} write_test_ctx_t;

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

/* Writes text to path with mode, failing the test on any short write. */
static void write_text(const char *path, const char *text, mode_t mode)
{
    FILE *fp = fopen(path, "w");

    expect(fp != NULL && fputs(text, fp) >= 0, "write text");
    if (fp != NULL)
        expect(fclose(fp) == 0, "close text");
    expect(chmod(path, mode) == 0, "text mode");
}

/* Writes dir/name into out, failing the test when it does not fit. */
static void join(char *out, size_t outsz, const char *dir, const char *name)
{
    size_t d = strlen(dir);
    size_t n = strlen(name);

    out[0] = '\0';
    expect(d + 1 + n < outsz, "path fits");
    if (d + 1 + n >= outsz)
        return;
    memmove(out, dir, d);
    out[d] = '/';
    memcpy(out + d + 1, name, n + 1);
}

/* The line "name=..." from the dump, or "" when absent. */
static const char *field(const char *dump, const char *name, char *out, size_t outsz)
{
    size_t n = strlen(name);

    out[0] = '\0';
    for (const char *at = dump; at != NULL && *at != '\0'; at = strchr(at, '\n')) {
        at += *at == '\n';
        if (strncmp(at, name, n) == 0 && at[n] == '=') {
            const char *end = strchr(at, '\n');
            size_t len = end != NULL ? (size_t)(end - at - (long)n - 1) : strlen(at + n + 1);
            snprintf(out, outsz, "%.*s", (int)len, at + n + 1);
            break;
        }
    }
    return out;
}

/* 1 when the probe reported name=ok. */
static int wrote(const char *dump, const char *name)
{
    char value[WRITE_TEST_PATH_MAX];

    return strcmp(field(dump, name, value, sizeof value), "ok") == 0;
}

/* Runs the probe as file (probe, grok or codex) jailed under backend; its
 * dump lands in out. The child starts in "/", so only the jail can put it in
 * its job cwd. Returns 1 when the jail ran it, 0 when the jail refused. */
static int run_probe(char *out, size_t outsz, const write_test_ctx_t *ctx,
                     const char *backend, const char *file)
{
    char home[WRITE_TEST_PATH_MAX];
    char dump[WRITE_TEST_PATH_MAX];
    char data[WRITE_TEST_PATH_MAX * 2];
    int status = 0;

    out[0] = '\0';
    join(dump, sizeof dump, ctx->cwd, "probe.dump");
    expect(setenv("HUSH_SANDBOX_BACKEND", backend, 1) == 0, "backend env");
    expect(hush_sandbox_home_make(home, sizeof home) == HUSH_OK, "stub home");
    pid_t pid = fork();
    if (pid == 0) {
        hush_sandbox_job_t job = {ctx->cwd, home, 0};
        char *argv[] = {(char *)file, dump, (char *)ctx->outside, (char *)ctx->tree,
                        (char *)ctx->auth, (char *)ctx->pts, NULL};
        join(data, sizeof data, home, WRITE_TEST_DATA_REL);
        if (setenv("XDG_DATA_HOME", data, 1) == 0 && chdir("/") == 0)
            (void)hush_sandbox_exec(&job, file, argv);
        _exit(WRITE_TEST_EXEC_FAIL);
    }
    expect(pid > 0 && waitpid(pid, &status, 0) == pid, "reap");
    hush_sandbox_home_remove(home, sizeof home);
    if (WIFEXITED(status) && WEXITSTATUS(status) == WRITE_TEST_EXEC_FAIL)
        return 0;
    FILE *fp = fopen(dump, "r");
    expect(fp != NULL, "probe dump");
    if (fp == NULL)
        return 1;
    size_t n = fread(out, 1, outsz - 1, fp);
    out[n] = '\0';
    (void)fclose(fp);
    (void)unlink(dump);
    return 1;
}

/* nftw callback: removes path (links themselves, never their targets). */
static int remove_entry(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
    (void)st;
    (void)flag;
    (void)ftw;
    return remove(path);
}

/* Removes anything a leaky jail let the probe create outside its scope. */
static void sweep_leaks(const write_test_ctx_t *ctx)
{
    char path[WRITE_TEST_PATH_MAX * 2];

    (void)unlink(ctx->tree);
    (void)unlink(WRITE_TEST_SHM);
    join(path, sizeof path, ctx->outside, "w.txt");
    (void)unlink(path);
    join(path, sizeof path, ctx->auth, "w.txt");
    (void)unlink(path);
    (void)unlink("/w-rel.txt");
}

/* #375 N2 rulings 1-4 under Landlock: what may and may not be written. */
static void check_landlock_writes(const char *dump)
{
    char value[WRITE_TEST_PATH_MAX];

    expect(wrote(dump, "cwd"), "landlock: cwd not writable");
    expect(wrote(dump, "home"), "landlock: stub HOME not writable");
    expect(wrote(dump, "tmpdir"), "landlock: private TMPDIR not writable");
    expect(strcmp(field(dump, "tmp_under_home", value, sizeof value), "1") == 0,
           "landlock: TMPDIR is not under the stub HOME");
    expect(!wrote(dump, "outside"), "landlock: shared /tmp outside cwd writable");
    expect(!wrote(dump, "tree"), "landlock: / tree writable");
    expect(!wrote(dump, "auth"), "landlock: auth tree writable via stub HOME");
    expect(!wrote(dump, "authreal"), "landlock: real auth tree writable");
    expect(strcmp(field(dump, "auth_read", value, sizeof value), "ok") == 0,
           "landlock: auth tree not readable");
    expect(wrote(dump, "relcwd"), "landlock: child not started in its job cwd");
    expect(!wrote(dump, "grokfile") && !wrote(dump, "codexfile"),
           "landlock: a token file writable by a non-provider binary");
}

/* #375 N2 rulings 2-3 under Landlock: device nodes and XDG homes. */
static void check_landlock_devs(const char *dump)
{
    char value[WRITE_TEST_PATH_MAX];

    expect(wrote(dump, "devnull"), "landlock: /dev/null not writable");
    expect(wrote(dump, "devzero"), "landlock: /dev/zero not writable");
    expect(strstr(field(dump, "tty", value, sizeof value), "Permission denied") == NULL,
           "landlock: /dev/tty denied by the jail");
    expect(wrote(dump, "pts"), "landlock: /dev/pts/N not writable");
    expect(wrote(dump, "pts_ioctl"), "landlock: /dev/pts/N ioctl denied");
    expect(!wrote(dump, "urandom"), "landlock: /dev/urandom writable");
    expect(strcmp(field(dump, "urandom_read", value, sizeof value), "1") == 0,
           "landlock: /dev/urandom not readable");
    expect(!wrote(dump, "shm"), "landlock: /dev/shm writable");
    expect(strcmp(field(dump, "XDG_CACHE_HOME", value, sizeof value), "unset") == 0,
           "landlock: XDG_CACHE_HOME outside HOME kept");
    expect(strstr(field(dump, "XDG_DATA_HOME", value, sizeof value),
                  WRITE_TEST_DATA_REL) != NULL,
           "landlock: XDG_DATA_HOME inside HOME dropped");
}

/* #375 N2 rule 7: bwrap's scope as it was at 230b43ef (ev12/before.log). */
static void check_bwrap(const char *dump)
{
    char value[WRITE_TEST_PATH_MAX];

    expect(wrote(dump, "cwd") && wrote(dump, "home"), "bwrap: cwd or HOME lost");
    expect(!wrote(dump, "outside") && !wrote(dump, "tree"), "bwrap: wider than before");
    expect(!wrote(dump, "auth") && !wrote(dump, "authreal"), "bwrap: auth writable");
    expect(!wrote(dump, "tmpdir"), "bwrap: gained a TMPDIR");
    /* OBS at 230b43ef: bwrap's --ro-bind of /dev/null is unusable for
     * writes (Permission denied), so this pins that it is still so. */
    expect(!wrote(dump, "devnull"), "bwrap: /dev/null scope changed");
    expect(strcmp(field(dump, "TMPDIR", value, sizeof value), "unset") == 0,
           "bwrap: TMPDIR now set");
    expect(strcmp(field(dump, "XDG_CACHE_HOME", value, sizeof value), "unset") != 0,
           "bwrap: XDG_CACHE_HOME now dropped");
}

/* Prints dump when WRITE_TEST_SHOW is set, for the evidence log. */
static void show(const char *dump, const char *label)
{
    if (getenv("WRITE_TEST_SHOW") != NULL)
        printf("--- %s probe ---\n%s", label, dump);
}

/* Writes the probe as bin/<name> under the test root. */
static void write_probe(const char *bin, const char *name)
{
    char probe[WRITE_TEST_PATH_MAX * 3];

    join(probe, sizeof probe, bin, name);
    write_text(probe, WRITE_TEST_PROBE, S_IRWXU);
}

/* Makes HOME/<dir> with a fake token file auth.json in it. */
static void make_token(const char *home, const char *dir)
{
    char path[WRITE_TEST_PATH_MAX * 2];
    char file[WRITE_TEST_PATH_MAX * 3];

    join(path, sizeof path, home, dir);
    expect(mkdir(path, WRITE_TEST_DIR_MODE) == 0, "fake auth dir");
    join(file, sizeof file, path, "auth.json");
    write_text(file, WRITE_TEST_TOKEN, S_IRUSR | S_IWUSR);
}

/* Builds the fake real HOME (fake .grok and .codex tokens), cwd, outside dir
 * and probes (as probe, grok and codex) under a fresh /tmp root. Returns 1
 * on success. */
static int make_ctx(write_test_ctx_t *ctx, const char *start)
{
    char path[WRITE_TEST_PATH_MAX * 2];

    snprintf(ctx->root, sizeof ctx->root, "/tmp/hush-sandbox-write-XXXXXX");
    if (mkdtemp(ctx->root) == NULL)
        return 0;
    join(ctx->cwd, sizeof ctx->cwd, ctx->root, "cwd");
    join(ctx->outside, sizeof ctx->outside, ctx->root, "outside");
    join(path, sizeof path, ctx->root, "home");
    expect(mkdir(ctx->cwd, WRITE_TEST_DIR_MODE) == 0 &&
           mkdir(ctx->outside, WRITE_TEST_DIR_MODE) == 0
           && mkdir(path, WRITE_TEST_DIR_MODE) == 0, "ctx dirs");
    expect(setenv("HOME", path, 1) == 0, "fake HOME");
    join(ctx->auth, sizeof ctx->auth, path, ".grok");
    make_token(path, ".grok");
    make_token(path, ".codex");
    join(ctx->tree, sizeof ctx->tree, start, WRITE_TEST_TREE_NAME);
    join(path, sizeof path, ctx->root, "bin");
    expect(mkdir(path, WRITE_TEST_DIR_MODE) == 0, "bin dir");
    write_probe(path, "probe");
    write_probe(path, "grok");
    write_probe(path, "codex");
    char search[WRITE_TEST_PATH_MAX * 3];
    snprintf(search, sizeof search, "%s:%s", path, WRITE_TEST_SYS_PATH);
    expect(setenv("PATH", search, 1) == 0, "PATH");
    join(path, sizeof path, ctx->root, "xdg-cache");
    expect(setenv("XDG_CACHE_HOME", path, 1) == 0, "outside XDG");
    return 1;
}

/* Opens a pty pair and writes its slave path into ctx->pts. Returns the
 * master fd, or -1. */
static int open_pts(write_test_ctx_t *ctx)
{
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    const char *name = NULL;

    if (master < 0 || grantpt(master) != 0 || unlockpt(master) != 0 ||
        (name = ptsname(master)) == NULL) {
        expect(0, "pty setup");
        return master;
    }
    snprintf(ctx->pts, sizeof ctx->pts, "%s", name);
    return master;
}

/* Runs the probe as file under Landlock; 1 when it ran (dump filled). */
static int landlock_as(char *dump, size_t dumpsz, const write_test_ctx_t *ctx,
                       const char *file)
{
    int ran = run_probe(dump, dumpsz, ctx, "landlock", file);

    if (!ran)
        expect(0, "landlock jail refused the probe");
    else
        show(dump, file);
    sweep_leaks(ctx);
    return ran;
}

/* B2: own token writable in place, the other provider's not, and no
 * tmp+rename refresh (that needs directory rights, OBS). */
static void check_own_tokens(char *dump, size_t dumpsz, const write_test_ctx_t *ctx)
{
    if (landlock_as(dump, dumpsz, ctx, "grok")) {
        expect(wrote(dump, "grokfile"), "B2: grok's own token not writable");
        expect(!wrote(dump, "codexfile"), "B2: codex token writable by grok");
        expect(!wrote(dump, "grokrename"), "B2: grok token dir writable (rename)");
        expect(!wrote(dump, "codexrename"), "B2: codex token dir writable by grok");
    }
    if (landlock_as(dump, dumpsz, ctx, "codex")) {
        expect(wrote(dump, "codexfile"), "B2: codex's own token not writable");
        expect(!wrote(dump, "grokfile"), "B2: grok token writable by codex");
        expect(!wrote(dump, "codexrename"), "B2: codex token dir writable (rename)");
    }
}

/* Replaces HOME/.codex/auth.json with a symlink to a file outside HOME. */
static void codex_token_to_symlink(const write_test_ctx_t *ctx, char *target,
                                   size_t targetsz)
{
    char token[WRITE_TEST_PATH_MAX * 2];

    join(target, targetsz, ctx->outside, "codex-target.json");
    write_text(target, WRITE_TEST_TARGET, S_IRUSR | S_IWUSR);
    join(token, sizeof token, getenv("HOME"), ".codex/auth.json");
    expect(unlink(token) == 0 && symlink(target, token) == 0, "codex token symlink");
}

/* 1 when path still holds exactly text. */
static int holds(const char *path, const char *text)
{
    char buf[WRITE_TEST_PATH_MAX] = {0};
    FILE *fp = fopen(path, "r");
    size_t n = 0;

    if (fp == NULL)
        return 0;
    n = fread(buf, 1, sizeof buf - 1, fp);
    (void)fclose(fp);
    return n == strlen(text) && memcmp(buf, text, n) == 0;
}

/* B2: an own token reached through a symlink (to a file outside HOME, or a
 * symlinked token dir), or one that is not a regular file, stays read-only. */
static void check_token_links(char *dump, size_t dumpsz, const write_test_ctx_t *ctx)
{
    char target[WRITE_TEST_PATH_MAX * 2];
    char path[WRITE_TEST_PATH_MAX * 2];
    const char *home = getenv("HOME");

    codex_token_to_symlink(ctx, target, sizeof target);
    if (landlock_as(dump, dumpsz, ctx, "codex"))
        expect(!wrote(dump, "codexfile"), "B2: codex token symlink followed");
    expect(holds(target, WRITE_TEST_TARGET), "B2: symlink target changed");
    join(path, sizeof path, home, ".codex");
    expect(nftw(path, remove_entry, WRITE_TEST_FTW_FDS, FTW_DEPTH | FTW_PHYS) == 0 &&
           symlink(".grok", path) == 0, "codex dir symlink");
    if (landlock_as(dump, dumpsz, ctx, "codex"))
        expect(!wrote(dump, "codexfile"), "B2: token through a symlinked dir writable");
    join(path, sizeof path, home, ".grok/auth.json");
    expect(unlink(path) == 0 && mkdir(path, WRITE_TEST_DIR_MODE) == 0, "token as dir");
    join(target, sizeof target, path, "inner");
    write_text(target, WRITE_TEST_TOKEN, S_IRUSR | S_IWUSR);
    if (landlock_as(dump, dumpsz, ctx, "grok"))
        expect(!wrote(dump, "grokinner"), "B2: a token path that is a dir granted");
}

/* The Landlock legs: write scope, devices, own tokens, token links. */
static void check_landlock(char *dump, size_t dumpsz, const write_test_ctx_t *ctx)
{
    char real[WRITE_TEST_PATH_MAX];

    if (landlock_as(dump, dumpsz, ctx, "probe")) {
        check_landlock_writes(dump);
        check_landlock_devs(dump);
        expect(strcmp(field(dump, "pwd_real", real, sizeof real), ctx->cwd) == 0,
               "landlock: child cwd is not its job cwd");
    }
    check_own_tokens(dump, dumpsz, ctx);
    check_token_links(dump, dumpsz, ctx);
}

int main(void)
{
    static char dump[WRITE_TEST_DUMP_MAX];
    write_test_ctx_t ctx;
    char start[WRITE_TEST_PATH_MAX];

    if (hush_sandbox_abi() < HUSH_SANDBOX_ABI_NET) {
        /* The harness counts rc 0 as a pass; SKIP marks it as not run. */
        printf("sandbox write: SKIP (UNKNOWN: landlock unavailable; not exercised)\n");
        return 0;
    }
    if (getcwd(start, sizeof start) == NULL || !make_ctx(&ctx, start))
        return 1;
    int master = open_pts(&ctx);
    check_landlock(dump, sizeof dump, &ctx);
    if (run_probe(dump, sizeof dump, &ctx, "bwrap", "probe")) {
        show(dump, "bwrap");
        check_bwrap(dump);
    }
    else
        printf("sandbox write: bwrap SKIP (UNKNOWN: jail refused; leg not exercised)\n");
    sweep_leaks(&ctx);
    if (master >= 0)
        (void)close(master);
    expect(nftw(ctx.root, remove_entry, WRITE_TEST_FTW_FDS, FTW_DEPTH | FTW_PHYS) == 0,
           "scratch cleanup");
    if (g_fail)
        return 1;
    printf("sandbox write ok\n");
    return 0;
}
