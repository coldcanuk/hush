/* tests/test_sandbox_write.c: #375 N2, Landlock write scope. A jailed child
 * may write only its cwd, its stub HOME (which holds its private TMPDIR) and
 * the declared device nodes; the rest of the tree, the shared /tmp, the real
 * auth trees and /dev/shm are read-only, and an XDG_*_HOME outside its HOME
 * is dropped. The job's own provider auth directory (~/.grok or ~/.codex)
 * takes a tmp + rename token refresh, never through a symlink, and no other
 * provider's does (B2). bwrap's write scope stays what it was. A stub probe script runs
 * through the real jail and reports each write. All homes and values are
 * fake; the real HOME is never used. #375 H10 pins: an existing file above
 * HOME, a symlink or a cross-directory rename into the own auth dir, every
 * XDG home outside HOME, and a non-provider job sharing a provider prefix. */

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
/* An existing file in the test root, an ancestor of HOME (H10-1). */
static const char WRITE_TEST_ANC[] = "{\"ancestor\":true}\n";
/* Every XDG home the jail filters, XDG_STATE_HOME last: a dropped entry at
 * the end of environ is where a missing NULL terminator shows (H10-3). */
static const char *const WRITE_TEST_XDG[] = {
    "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME",
};

enum {
    WRITE_TEST_NXDG = sizeof WRITE_TEST_XDG / sizeof WRITE_TEST_XDG[0]
};

/* argv: dump outside tree auth pts anc. Prints name=ok or name=fail:<error>.
 * ok runs a command and prints name=ok or name=fail. rn is rename(2) alone,
 * so a refused cross-directory rename cannot fall back to copy (H10-2).
 * env_dups reads the env the jail exec'd with (/proc), which sh dedups. */
static const char WRITE_TEST_PROBE[] =
    "#!/bin/sh\n"
    "try() { if err=$( { : > \"$2\"; } 2>&1 ); then echo \"$1=ok\"; "
    "else echo \"$1=fail:$err\"; fi; }\n"
    "ok() { n=$1; shift; if \"$@\" 2>/dev/null; then echo \"$n=ok\"; "
    "else echo \"$n=fail\"; fi; }\n"
    "rn() { python3 -c 'import os,sys; os.rename(sys.argv[1], sys.argv[2])' \"$1\" \"$2\"; }\n"
    "{\n"
    "try cwd \"$PWD/w.txt\"; try home \"$HOME/w.txt\"\n"
    "try tmpdir \"${TMPDIR:-/nonexistent}/w.txt\"\n"
    "try outside \"$2/w.txt\"; try tree \"$3\"; try auth \"$HOME/.grok/w.txt\"\n"
    "try authreal \"$4/w.txt\"; try devnull /dev/null; try devzero /dev/zero\n"
    "try tty /dev/tty; try pts \"$5\"; try urandom /dev/urandom\n"
    "try shm /dev/shm/hush-write-probe\n"
    "try relcwd ./w-rel.txt; echo \"pwd_real=$(pwd -P)\"\n"
    "try grokfile \"$HOME/.grok/auth.json\"; try codexfile \"$HOME/.codex/auth.json\"\n"
    "if mkdir \"$HOME/.grok/sub\" 2>/dev/null; then echo grokmkdir=ok; "
    "else echo grokmkdir=fail; fi\n"
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
    "try ancfile \"$6\"\n"
    "ok cwdsym ln -s /nonexistent \"$PWD/lnk\"; ok groksym ln -s /nonexistent \"$HOME/.grok/lnk\"\n"
    "rm -f \"$PWD/lnk\" \"$HOME/.grok/lnk\" 2>/dev/null\n"
    "echo r > \"$PWD/mv.txt\"; ok cwdrename rn \"$PWD/mv.txt\" \"$PWD/mv2.txt\"\n"
    "ok intorefer rn \"$PWD/mv2.txt\" \"$HOME/.grok/mv.txt\"\n"
    "rm -f \"$PWD/mv.txt\" \"$PWD/mv2.txt\" \"$HOME/.grok/mv.txt\" 2>/dev/null\n"
    "echo \"XDG_CONFIG_HOME=${XDG_CONFIG_HOME:-unset}\"\n"
    "echo \"XDG_STATE_HOME=${XDG_STATE_HOME:-unset}\"\n"
    "if [ -r /proc/$$/environ ]; then\n"
    "echo \"env_dups=$(tr '\\0' '\\n' < /proc/$$/environ | sort | uniq -d | wc -l)\"\n"
    "else echo env_dups=unread; fi\n"
    "} > \"$1\"\n";

/* Paths one run needs, all under the test root except tree. */
typedef struct {
    char root[WRITE_TEST_PATH_MAX];
    char cwd[WRITE_TEST_PATH_MAX];
    char outside[WRITE_TEST_PATH_MAX];
    char tree[WRITE_TEST_PATH_MAX * 2];
    char auth[WRITE_TEST_PATH_MAX];
    char pts[WRITE_TEST_PATH_MAX];
    char anc[WRITE_TEST_PATH_MAX];
    int xdg_outside; /* 1: every XDG home outside HOME, else data inside */
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

/* Sets the child's XDG homes: XDG_DATA_HOME inside home, or with
 * ctx->xdg_outside all of WRITE_TEST_XDG under the test root, appended in
 * order so XDG_STATE_HOME ends environ. Returns 1 on success. */
static int set_child_xdg(const write_test_ctx_t *ctx, const char *home)
{
    char path[WRITE_TEST_PATH_MAX * 2];

    if (!ctx->xdg_outside) {
        join(path, sizeof path, home, WRITE_TEST_DATA_REL);
        return setenv("XDG_DATA_HOME", path, 1) == 0;
    }
    for (size_t i = 0; i < (size_t)WRITE_TEST_NXDG; ++i) {
        if (unsetenv(WRITE_TEST_XDG[i]) != 0)
            return 0;
    }
    for (size_t i = 0; i < (size_t)WRITE_TEST_NXDG; ++i) {
        join(path, sizeof path, ctx->root, WRITE_TEST_XDG[i]);
        if (setenv(WRITE_TEST_XDG[i], path, 1) != 0)
            return 0;
    }
    return 1;
}

/* Runs the probe as file (probe, grok, codex or copilot) jailed under backend; its
 * dump lands in out. The child starts in "/", so only the jail can put it in
 * its job cwd. Returns 1 when the jail ran it, 0 when the jail refused. */
static int run_probe(char *out, size_t outsz, const write_test_ctx_t *ctx,
                     const char *backend, const char *file)
{
    char home[WRITE_TEST_PATH_MAX];
    char dump[WRITE_TEST_PATH_MAX];
    int status = 0;

    out[0] = '\0';
    join(dump, sizeof dump, ctx->cwd, "probe.dump");
    expect(setenv("HUSH_SANDBOX_BACKEND", backend, 1) == 0, "backend env");
    expect(hush_sandbox_home_make(home, sizeof home) == HUSH_OK, "stub home");
    pid_t pid = fork();
    if (pid == 0) {
        hush_sandbox_job_t job = {ctx->cwd, home, 0};
        char *argv[] = {(char *)file, dump, (char *)ctx->outside, (char *)ctx->tree,
                        (char *)ctx->auth, (char *)ctx->pts, (char *)ctx->anc, NULL};
        if (set_child_xdg(ctx, home) && chdir("/") == 0)
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
    expect(!wrote(dump, "grokrename") && !wrote(dump, "codexrename"),
           "landlock: an auth dir writable by a non-provider binary");
    expect(!wrote(dump, "ancfile"), "H10-1: a file above HOME writable");
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
    expect(strcmp(field(dump, "env_dups", value, sizeof value), "0") == 0,
           "landlock: env holds a duplicate entry");
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

/* Builds the fake real HOME (fake .grok and .codex tokens), cwd, outside dir,
 * the ancestor file and probes (as probe, grok, codex and copilot) under a
 * fresh /tmp root, with TMPDIR unset. Returns 1 on success. */
static int make_ctx(write_test_ctx_t *ctx, const char *start)
{
    char path[WRITE_TEST_PATH_MAX * 2];

    snprintf(ctx->root, sizeof ctx->root, "/tmp/hush-sandbox-write-XXXXXX");
    /* The bwrap pin reads TMPDIR as unset, whatever the caller's (H10-6). */
    if (mkdtemp(ctx->root) == NULL || unsetenv("TMPDIR") != 0)
        return 0;
    ctx->xdg_outside = 0;
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
    write_probe(path, "copilot");
    char search[WRITE_TEST_PATH_MAX * 3];
    snprintf(search, sizeof search, "%s:%s", path, WRITE_TEST_SYS_PATH);
    expect(setenv("PATH", search, 1) == 0, "PATH");
    join(path, sizeof path, ctx->root, "xdg-cache");
    expect(setenv("XDG_CACHE_HOME", path, 1) == 0, "outside XDG");
    join(ctx->anc, sizeof ctx->anc, ctx->root, "anc.txt");
    write_text(ctx->anc, WRITE_TEST_ANC, S_IRUSR | S_IWUSR);
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

/* H10-2: the own auth dir takes no symlink and no file renamed in from the
 * job cwd, while the cwd takes both (so a refusal is the jail's). */
static void check_token_links_made(const char *dump)
{
    expect(wrote(dump, "cwdsym") && wrote(dump, "cwdrename"),
           "H10-2: control symlink or rename in the job cwd failed");
    expect(!wrote(dump, "groksym"), "H10-2: grok auth dir takes a symlink");
    expect(!wrote(dump, "intorefer"), "H10-2: grok auth dir takes a rename from the cwd");
}

/* H10-4: a job whose binary shares a provider's first letters (copilot,
 * codex) gets neither provider's auth dir. */
static void check_other_provider(char *dump, size_t dumpsz, const write_test_ctx_t *ctx)
{
    if (!landlock_as(dump, dumpsz, ctx, "copilot"))
        return;
    expect(wrote(dump, "cwd"), "H10-4: copilot job cwd not writable");
    expect(!wrote(dump, "codexfile") && !wrote(dump, "codexrename"),
           "H10-4: codex auth dir writable by copilot");
    expect(!wrote(dump, "grokfile") && !wrote(dump, "grokrename"),
           "H10-4: grok auth dir writable by copilot");
}

/* Fails the test unless the dump shows name unset. */
static void expect_unset(const char *dump, const char *name)
{
    char value[WRITE_TEST_PATH_MAX];
    char msg[WRITE_TEST_PATH_MAX];

    snprintf(msg, sizeof msg, "H10-3: %s outside HOME kept", name);
    expect(strcmp(field(dump, name, value, sizeof value), "unset") == 0, msg);
}

/* H10-3: every XDG home outside HOME, XDG_STATE_HOME last in environ, is
 * dropped, and the compacted env (NULL-terminated) repeats no entry. */
static void check_xdg_outside(char *dump, size_t dumpsz, const write_test_ctx_t *ctx)
{
    write_test_ctx_t outside = *ctx;
    char value[WRITE_TEST_PATH_MAX];

    outside.xdg_outside = 1;
    if (!landlock_as(dump, dumpsz, &outside, "probe"))
        return;
    for (size_t i = 0; i < (size_t)WRITE_TEST_NXDG; ++i)
        expect_unset(dump, WRITE_TEST_XDG[i]);
    expect(strcmp(field(dump, "env_dups", value, sizeof value), "0") == 0,
           "H10-3: env holds a duplicate entry");
}

/* B2: the own auth dir takes an in-place write and a tmp + rename refresh
 * but no new subdirectory; the other provider's dir takes neither. */
static void check_own_tokens(char *dump, size_t dumpsz, const write_test_ctx_t *ctx)
{
    if (landlock_as(dump, dumpsz, ctx, "grok")) {
        expect(wrote(dump, "grokfile"), "B2: grok's own token not writable");
        expect(!wrote(dump, "codexfile"), "B2: codex token writable by grok");
        expect(wrote(dump, "grokrename"), "B2: grok tmp + rename refresh fails");
        expect(!wrote(dump, "codexrename"), "B2: codex token dir writable by grok");
        expect(!wrote(dump, "grokmkdir"), "B2: grok auth dir takes a subdirectory");
        check_token_links_made(dump);
    }
    if (landlock_as(dump, dumpsz, ctx, "codex")) {
        expect(wrote(dump, "codexfile"), "B2: codex's own token not writable");
        expect(!wrote(dump, "grokfile"), "B2: grok token writable by codex");
        expect(wrote(dump, "codexrename"), "B2: codex tmp + rename refresh fails");
        expect(!wrote(dump, "grokrename"), "B2: grok token dir writable by codex");
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
 * symlinked auth dir) stays read-only. */
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
        expect(!wrote(dump, "codexfile") && !wrote(dump, "codexrename"),
               "B2: auth dir through a symlink writable");
    /* An auth path that is not a directory refuses the jail outright (its
     * read-only grant takes no dir rights), so it is never made writable. */
    join(path, sizeof path, home, ".grok");
    expect(nftw(path, remove_entry, WRITE_TEST_FTW_FDS, FTW_DEPTH | FTW_PHYS) == 0, "rm .grok");
    write_text(path, WRITE_TEST_TOKEN, S_IRUSR | S_IWUSR);
    expect(!run_probe(dump, dumpsz, ctx, "landlock", "grok"), "B2: a non-dir auth path jailed");
    sweep_leaks(ctx);
}

/* The Landlock legs: write scope, devices, XDG homes, other providers, own
 * tokens, token links. */
static void check_landlock(char *dump, size_t dumpsz, const write_test_ctx_t *ctx)
{
    char real[WRITE_TEST_PATH_MAX];

    if (landlock_as(dump, dumpsz, ctx, "probe")) {
        check_landlock_writes(dump);
        check_landlock_devs(dump);
        expect(strcmp(field(dump, "pwd_real", real, sizeof real), ctx->cwd) == 0,
               "landlock: child cwd is not its job cwd");
    }
    expect(holds(ctx->anc, WRITE_TEST_ANC), "H10-1: a file above HOME changed");
    check_xdg_outside(dump, dumpsz, ctx);
    check_other_provider(dump, dumpsz, ctx);
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
        /* bwrap's /tmp is a private tmpfs, so ancfile may "write" there. */
        expect(holds(ctx.anc, WRITE_TEST_ANC), "bwrap: a file above HOME changed");
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
