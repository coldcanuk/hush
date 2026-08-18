/* tests/test_pass.c: hush_pass save/get/has against a stub helper. */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

int main(void)
{
    char secret[HUSH_PASS_SECRET_MAX];
    char err[HUSH_PASS_ERR_MAX];
    const char *dir = "/tmp/hush-test-pass-store";
    const char *helper = "tests/fake-pass.sh";

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

    hush_pass_set_helper("/no/such/hush-pass-helper");
    expect(hush_pass_save(HUSH_PASS_IDENTITY_NSEC, "nsec1x") == HUSH_ERR_IO,
           "missing helper");
    hush_pass_last_error(err, sizeof(err));
    expect(err[0] != '\0', "error text");

    hush_pass_set_helper(NULL);
    if (g_fail)
        return 1;
    printf("test_pass ok\n");
    return 0;
}
