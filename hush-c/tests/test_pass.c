/* tests/test_pass.c: hush_pass save/get/has against a stub helper. */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hush_pass.h"

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

/* Rejects saves through a missing helper and reports error text. */
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
    expect(!hush_pass_available(), "pass absent");
    if (kept != NULL) {
        setenv("PATH", kept, 1);
        free(kept);
    }
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

    test_pass_missing_helper();
    test_pass_available();
    if (g_fail)
        return 1;
    printf("test_pass ok\n");
    return 0;
}
