/* tests/test_pass.c: hush_pass save/get/has against a stub helper. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hush_pass.h"

enum {
    TEST_PATH_MAX = 128,
    TEST_DIR_MODE = 0700
};

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

    snprintf(script, sizeof(script), "/tmp/hush-pass-rm-%ld.sh", (long)getpid());
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
 * (/tmp/hush-avail-<pid>), so repeated runs leave nothing behind. */
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
    char base[64];
    char bindir[80];
    char emptydir[80];
    char fake[96];
    char *kept = NULL;
    const char *path = getenv("PATH");

    hush_pass_set_helper(NULL);
    unsetenv(HUSH_PASS_ENV_HELPER);
    if (path != NULL)
        kept = strdup(path);
    snprintf(base, sizeof(base), "/tmp/hush-avail-%ld", (long)getpid());
    snprintf(bindir, sizeof(bindir), "%s/bin", base);
    snprintf(emptydir, sizeof(emptydir), "%s/empty", base);
    mkdir(base, 0700);
    mkdir(bindir, 0700);
    snprintf(fake, sizeof(fake), "%s/pass", bindir);
    test_make_fake(fake);
    snprintf(fake, sizeof(fake), "%s/hush-pass", bindir);
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


/* Removes the fake store at dir (/tmp/hush-unit-pass-<pid>). */
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
    char dir[64];
    const char *helper = "tests/fake-pass.sh";

    if (snprintf(dir, sizeof(dir), "/tmp/hush-unit-pass-%ld", (long)getpid()) < 0)
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
