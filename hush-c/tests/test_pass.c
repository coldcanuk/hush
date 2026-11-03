/* tests/test_pass.c: hush_pass save/get/has against a stub helper. */

#define _XOPEN_SOURCE 700 /* nftw, for the private root cleanup */

#include <assert.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hush_pass.h"

enum {
    /* A scratch base: the private root plus one "/hush-...-<pid>" name. */
    TEST_BASE_MAX = 136,
    /* A base plus its longest suffix ("/agents_sgt-major-payne_nsec"). */
    TEST_PATH_MAX = 168,
    TEST_DIR_MODE = 0700
};

enum {
    /* Longest private root path; every path a test derives from it fits. */
    ROOT_PATH_MAX = 96,
    /* Open-fd budget for the nftw cleanup walk. */
    ROOT_FTW_FDS = 16,
    /* Exit status when the root cannot be removed; atexit cannot return one. */
    ROOT_CLEANUP_FAIL_RC = 1
};

/* <TMPDIR or /tmp>/hush-pass-XXXXXX: this run's private root, 0700 from mkdtemp.
 * Every key, pass store, home and config the test writes lives under it. */
#define ROOT_NAME "hush-pass-XXXXXX"
#define ROOT_TMP_DEFAULT "/tmp"

/* The private root and the pid that made it. File scope because an atexit
 * handler takes no argument. Empty until root_make succeeds and again after
 * root_remove. */
static char g_root[ROOT_PATH_MAX];
static pid_t g_root_pid;

/* Makes g_root under root_tmpdir() (/tmp when that is NULL); root_remove,
 * registered with atexit, removes it on every return from main and every
 * exit(). Returns 0, after a FAIL line, when root_self_check
 * fails, the path does not fit, mkdtemp fails, atexit
 * refuses, or the root is not under an absolute TMPDIR. */
static int root_make(void);

/* atexit handler: removes g_root and everything under it, in the making
 * process only, so a forked child that exits cannot take the root early.
 * When the walk fails it prints FAIL and ends the process with
 * ROOT_CLEANUP_FAIL_RC, so a leftover root fails the test. */
static void root_remove(void);

/* nftw callback: removes one entry under g_root; FTW_DEPTH visits children
 * first. */
static int root_remove_entry(const char *path, const struct stat *st, int flag,
                             struct FTW *ftw);

/* True when path is root or lies below it: root must match whole path
 * components, so "/r" holds "/r" and "/r/k" but not the sibling "/rX/k".
 * A root that ends in '/' already ends a component: "/r/" holds "/r/k". */
static int root_holds(const char *root, const char *path);

/* Self-test of the two path checks, run before the root is made: returns
 * 0, after a FAIL line, when root_holds or root_is_under_tmpdir accepts a
 * prefix sibling ("/tmp/xprivate" for "/tmp/x" or "/tmp/x/"), or when the
 * pin rejects "/tmp/x/k" under a trailing-slash TMPDIR "/tmp/x/". A real
 * root never sits in such a sibling, so nothing else exercises that
 * boundary. */
static int root_self_check(void);

/* True when tmpdir is NULL, empty or relative (root_make then uses /tmp), or
 * when root lies under tmpdir by whole path components. A root made anywhere
 * else (a fixed /tmp, say) escapes the run's own cleanup and leak scan.
 * root_make passes getenv("TMPDIR") here, never root_tmpdir(), so a fault in
 * the leaf that placed the root cannot also blind this check. */
static int root_is_under_tmpdir(const char *tmpdir, const char *root);

/* TMPDIR when it is set and absolute; NULL when it is unset, empty, or
 * relative. Borrowed from the environment, never owned or freed. */
static const char *root_tmpdir(void);

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

/* Rejects saves through a missing helper and reports error text. */
/* rm that prints on stdout must still remove the entry. */
static void test_pass_delete_prints(void)
{
    char script[TEST_PATH_MAX];
    FILE *fp;

    snprintf(script, sizeof(script), "%s/hush-pass-rm-%ld.sh", g_root, (long)getpid());
    fp = fopen(script, "w");
    expect(fp != NULL, "rm script");
    if (fp == NULL)
        return;
    fputs("#!/bin/sh\n", fp);
    fputs("echo removed\n", fp);
    fputs("dir=\"${HUSH_FAKE_PASS_DIR:-/tmp/hush-fake-pass}\"\n", fp);
    fputs("file=\"$dir/$(printf '%s' \"$2\" | tr '/' '_')\"\n", fp);
    fputs("rm -f \"$file\"\n", fp);
    expect(fclose(fp) == 0, "rm script close");
    expect(chmod(script, 0700) == 0, "rm script mode");
    hush_pass_set_helper(script);
    expect(hush_pass_delete(HUSH_PASS_PAYNE_NSEC) == HUSH_OK,
           "delete while printing");
    hush_pass_set_helper("tests/fake-pass.sh");
    if (access("tests/fake-pass.sh", X_OK) != 0)
        hush_pass_set_helper("./tests/fake-pass.sh");
    expect(!hush_pass_has(HUSH_PASS_PAYNE_NSEC), "payne pass gone");
    unlink(script);
}

static void test_pass_missing_helper(void)
{
    char err[HUSH_PASS_ERR_MAX];

    hush_pass_set_helper("/no/such/hush-pass-helper");
    expect(hush_pass_save(HUSH_PASS_IDENTITY_NSEC, "nsec1x") == HUSH_ERR_IO,
           "missing helper");
    hush_pass_last_error(err, sizeof(err));
    expect(err[0] != '\0', "error text");
    hush_pass_set_helper(NULL);
}

/* Writes an always-zero executable stub. */
static void test_make_fake(const char *path)
{
    FILE *fp = fopen(path, "w");

    if (fp == NULL)
        return;
    fputs("#!/bin/sh\nexit 0\n", fp);
    fclose(fp);
    chmod(path, 0700);
}

/* Expects hush_pass_available() false when PATH holds a stub hush-pass but
 * no pass: the virgin-VM case (helper shipped, pass not installed). base is
 * an existing scratch directory; PATH is left pointing at the new dir. */
static void test_pass_helper_only(const char *base)
{
    char dir[TEST_PATH_MAX] = {0};
    char fake[TEST_PATH_MAX] = {0};

    assert(base != NULL);
    const int dir_len = snprintf(dir, sizeof(dir), "%s/helper-only", base);
    const int fake_len = snprintf(fake, sizeof(fake), "%s/hush-pass", dir);
    if (dir_len < 0 || (size_t)dir_len >= sizeof(dir) || fake_len < 0
        || (size_t)fake_len >= sizeof(fake)) {
        expect(0, "helper-only path fits");
        return;
    }
    if (mkdir(dir, TEST_DIR_MODE) != 0) {
        expect(0, "helper-only mkdir");
        return;
    }
    test_make_fake(fake);
    expect(access(fake, X_OK) == 0, "helper-only stub");
    if (setenv("PATH", dir, 1) != 0) {
        expect(0, "helper-only PATH");
        return;
    }
    expect(!hush_pass_available(), "helper present, pass absent");
}

/* Repo helper ../scripts/hush-pass counts with no hush-pass on PATH.
 * pass present is available. pass absent is not (kills a stuck-true). */
static void test_pass_repo_helper(const char *base)
{
    char rel[TEST_PATH_MAX];

    assert(base != NULL);
    snprintf(rel, sizeof(rel), "%s/scripts", base);
    expect(mkdir(rel, TEST_DIR_MODE) == 0, "repo scripts mkdir");
    snprintf(rel, sizeof(rel), "%s/scripts/hush-pass", base);
    test_make_fake(rel);
    expect(access(rel, X_OK) == 0, "repo helper stub");
    snprintf(rel, sizeof(rel), "%s/repo-pass", base);
    expect(mkdir(rel, TEST_DIR_MODE) == 0, "repo pass mkdir");
    snprintf(rel, sizeof(rel), "%s/repo-pass/pass", base);
    test_make_fake(rel);
    snprintf(rel, sizeof(rel), "%s/repo-empty", base);
    expect(mkdir(rel, TEST_DIR_MODE) == 0, "repo empty mkdir");
    snprintf(rel, sizeof(rel), "%s/repo-cwd", base);
    expect(mkdir(rel, TEST_DIR_MODE) == 0 && chdir(rel) == 0, "repo cwd");
    hush_pass_set_helper(NULL);
    unsetenv(HUSH_PASS_ENV_HELPER);
    snprintf(rel, sizeof(rel), "%s/repo-pass", base);
    expect(setenv("PATH", rel, 1) == 0, "repo-helper PATH");
    expect(hush_pass_available(), "repo helper with pass");
    snprintf(rel, sizeof(rel), "%s/repo-empty", base);
    expect(setenv("PATH", rel, 1) == 0, "repo-helper empty PATH");
    expect(!hush_pass_available(), "repo helper, pass absent");
}

/* Removes the scratch tree that test_pass_available builds under base
 * (<root>/hush-avail-<pid>), so repeated runs leave nothing behind. */
static void test_pass_cleanup(const char *base)
{
    static const char *const files[] = {
        "bin/pass", "bin/hush-pass", "helper-only/hush-pass",
        "scripts/hush-pass", "repo-pass/pass"
    };
    static const char *const dirs[] = {
        "bin", "helper-only", "scripts", "repo-pass", "repo-empty",
        "repo-cwd"
    };
    char p[TEST_PATH_MAX];

    assert(base != NULL);
    if (chdir("/") != 0)
        expect(0, "cleanup chdir");
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        snprintf(p, sizeof(p), "%s/%s", base, files[i]);
        unlink(p);
    }
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        snprintf(p, sizeof(p), "%s/%s", base, dirs[i]);
        rmdir(p);
    }
    rmdir(base);
    expect(access(base, F_OK) != 0, "scratch dir removed");
}

/* Detects the real helper path with no override and a controlled PATH.
 * A stuck-true or stuck-false hush_pass_available fails here. */
static void test_pass_available(void)
{
    char base[TEST_BASE_MAX];
    char bindir[TEST_PATH_MAX];
    char emptydir[TEST_PATH_MAX];
    char fake[TEST_PATH_MAX];
    char *kept = NULL;
    const char *path = getenv("PATH");

    hush_pass_set_helper(NULL);
    unsetenv(HUSH_PASS_ENV_HELPER);
    if (path != NULL)
        kept = strdup(path);
    snprintf(base, sizeof(base), "%s/hush-avail-%ld", g_root, (long)getpid());
    snprintf(bindir, sizeof(bindir), "%s/bin", base);
    snprintf(emptydir, sizeof(emptydir), "%s/empty", base);
    mkdir(base, 0700);
    mkdir(bindir, 0700);
    snprintf(fake, sizeof(fake), "%s/bin/pass", base);
    test_make_fake(fake);
    snprintf(fake, sizeof(fake), "%s/bin/hush-pass", base);
    test_make_fake(fake);
    if (chdir(base) != 0)
        expect(0, "chdir tmp");
    setenv("PATH", bindir, 1);
    expect(hush_pass_available(), "pass+helper present");
    setenv("PATH", emptydir, 1);
    expect(!hush_pass_available(), "helper and pass absent");
    test_pass_helper_only(base);
    test_pass_repo_helper(base);
    if (kept != NULL) {
        setenv("PATH", kept, 1);
        free(kept);
    }
    test_pass_cleanup(base);
}


/* Removes the fake store at dir (<root>/hush-unit-pass-<pid>). */
static void test_pass_store_cleanup(const char *dir)
{
    static const char *const names[] = {
        "identity_nsec", "agents_sgt-major-payne_nsec"
    };
    char path[TEST_PATH_MAX];
    size_t i;

    assert(dir != NULL);
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        unlink(path);
    }
    rmdir(dir);
    expect(access(dir, F_OK) != 0, "unit pass store removed");
}

int main(void)
{
    char secret[HUSH_PASS_SECRET_MAX];
    char dir[TEST_BASE_MAX];
    const char *helper = "tests/fake-pass.sh";

    if (!root_make())
        return 1;
    if (snprintf(dir, sizeof(dir), "%s/hush-unit-pass-%ld", g_root, (long)getpid()) < 0)
        return 1;
    if (setenv("HUSH_FAKE_PASS_DIR", dir, 1) != 0)
        return 1;
    if (access(helper, X_OK) != 0)
        helper = "./tests/fake-pass.sh";
    hush_pass_set_helper(helper);

    expect(hush_pass_save("", "nsec1abc") == HUSH_ERR_ARG, "empty path");
    expect(hush_pass_save("../x", "nsec1abc") == HUSH_ERR_ARG, "dotdot");
    expect(hush_pass_save(HUSH_PASS_IDENTITY_NSEC, "") == HUSH_ERR_ARG,
           "empty secret");
    expect(!hush_pass_has(HUSH_PASS_IDENTITY_NSEC), "missing before save");
    expect(hush_pass_save(HUSH_PASS_IDENTITY_NSEC, "nsec1testvalue") == HUSH_OK,
           "save");
    expect(hush_pass_has(HUSH_PASS_IDENTITY_NSEC), "has after save");
    expect(hush_pass_get(secret, sizeof(secret), HUSH_PASS_IDENTITY_NSEC) ==
               HUSH_OK,
           "get");
    expect(strcmp(secret, "nsec1testvalue") == 0, "roundtrip");
    expect(hush_pass_save(HUSH_PASS_PAYNE_NSEC, "nsec1payne") == HUSH_OK,
           "payne");
    expect(hush_pass_get(secret, sizeof(secret), HUSH_PASS_PAYNE_NSEC) ==
               HUSH_OK,
           "get payne");
    expect(strcmp(secret, "nsec1payne") == 0, "payne value");

    test_pass_delete_prints();
    test_pass_missing_helper();
    test_pass_available();
    test_pass_store_cleanup(dir);
    if (g_fail)
        return 1;
    printf("test_pass ok\n");
    return 0;
}

static int root_make(void)
{
    const char *base_dir = root_tmpdir();
    int wrote = 0;

    assert(g_root[0] == '\0');
    if (!root_self_check())
        return 0;
    /* A relative TMPDIR would leave g_root relative, and a test that
     * chdirs would then walk the wrong dir at exit. */
    if (base_dir == NULL)
        base_dir = ROOT_TMP_DEFAULT;
    wrote = snprintf(g_root, sizeof(g_root), "%s/%s", base_dir, ROOT_NAME);
    if (wrote < 0 || (size_t)wrote >= sizeof(g_root) || mkdtemp(g_root) == NULL) {
        g_root[0] = '\0';
        fprintf(stderr, "FAIL private root under %s\n", base_dir);
        return 0;
    }
    g_root_pid = getpid();
    /* Deviation (section 7): atexit takes a bare callback; no table fits. */
    if (atexit(root_remove) != 0) {
        root_remove();
        fprintf(stderr, "FAIL atexit for the private root\n");
        return 0;
    }
    /* atexit is already set, so the root is still removed on this FAIL. */
    if (!root_is_under_tmpdir(getenv("TMPDIR"), g_root)) {
        fprintf(stderr, "FAIL private root %s is not under TMPDIR\n", g_root);
        return 0;
    }
    return 1;
}

static void root_remove(void)
{
    if (g_root[0] == '\0' || getpid() != g_root_pid)
        return;
    /* FTW_PHYS never follows a symlink out of the root; FTW_MOUNT never
     * walks into another filesystem mounted under it. Deviation (section 7):
     * nftw takes a bare callback; no table fits. */
    if (nftw(g_root, root_remove_entry, ROOT_FTW_FDS,
             FTW_DEPTH | FTW_PHYS | FTW_MOUNT) != 0) {
        fprintf(stderr, "FAIL private root cleanup: %s\n", g_root);
        (void)fflush(NULL);
        /* exit() may not run inside an atexit handler; _exit may. */
        _exit(ROOT_CLEANUP_FAIL_RC);
    }
    g_root[0] = '\0';
}

static int root_remove_entry(const char *path, const struct stat *st, int flag,
                             struct FTW *ftw)
{
    (void)st;
    (void)flag;
    (void)ftw;
    /* Defense in depth only: FTW_PHYS and FTW_MOUNT keep the walk inside
     * g_root, so this assert cannot fire through nftw. root_self_check
     * checks root_holds directly instead. */
    assert(root_holds(g_root, path));
    return remove(path);
}

static int root_holds(const char *root, const char *path)
{
    size_t n = 0;

    assert(root != NULL && path != NULL);
    n = strlen(root);
    return n > 0 && strncmp(path, root, n) == 0 &&
           (path[n] == '\0' || path[n] == '/' || root[n - 1] == '/');
}

static int root_self_check(void)
{
    /* nftw never hands root_remove_entry a path outside the root, so the
     * prefix-sibling case is exercised only here: directly just below, and
     * through root_is_under_tmpdir in the /tmp/x and /tmp/x/ cases. */
    if (!root_holds("/r", "/r") || !root_holds("/r", "/r/k") ||
        root_holds("/r", "/rX/k") || root_holds("/r", "/rX")) {
        fprintf(stderr, "FAIL root_holds accepts a prefix sibling\n");
        return 0;
    }
    if (!root_is_under_tmpdir("/tmp/x", "/tmp/x/hush-k") ||
        root_is_under_tmpdir("/tmp/x", "/tmp/xprivate") ||
        root_is_under_tmpdir("/tmp/x", "/tmp/xprivate/hush-k")) {
        fprintf(stderr, "FAIL root_is_under_tmpdir accepts a prefix sibling\n");
        return 0;
    }
    /* A TMPDIR may end in '/'; its boundary is that slash. */
    if (!root_is_under_tmpdir("/tmp/x/", "/tmp/x/k") ||
        root_is_under_tmpdir("/tmp/x/", "/tmp/xprivate") ||
        root_is_under_tmpdir("/tmp/x/", "/tmp/xprivate/k")) {
        fprintf(stderr, "FAIL root_is_under_tmpdir mishandles a TMPDIR ending in '/'\n");
        return 0;
    }
    return 1;
}

static int root_is_under_tmpdir(const char *tmpdir, const char *root)
{
    assert(root != NULL && root[0] != '\0');
    /* The same absolute test as root_tmpdir, repeated on purpose: the check
     * must not share the leaf it checks. No trailing-slash strip: root_holds
     * treats a tmpdir ending in '/' as ending a component, so "X/" holds both
     * "X/<name>" and the "X//<name>" root_make builds. */
    if (tmpdir == NULL || tmpdir[0] != '/')
        return 1;
    return root_holds(tmpdir, root);
}

static const char *root_tmpdir(void)
{
    const char *env = getenv("TMPDIR");
    if (env == NULL || env[0] != '/')
        return NULL;
    return env;
}
