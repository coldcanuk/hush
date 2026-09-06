/* test_codex.c: checks isolated skill discovery and filesystem conflicts. */

#define _XOPEN_SOURCE 700

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hush_codex.h"

#define HUSH_CODEX_TEST_TEMPLATE "/tmp/hush-codex-XXXXXX"
#define HUSH_CODEX_TEST_ENV "HUSH_CODEX_SKILL_DIR"
#define HUSH_CODEX_TEST_MISSING "/missing-hush-codex-skill"
#define HUSH_CODEX_TEST_CHILD ".agents/skills/write-legible-c"

typedef struct {
    char root[sizeof(HUSH_CODEX_TEST_TEMPLATE)];
    char agents[PATH_MAX];
    char skills[PATH_MAX];
    char link[PATH_MAX];
} hush_codex_test_t;

/* Initializes caller-owned paths; asserts fixture setup succeeds. */
static void hush_codex_test_init(hush_codex_test_t *test);
/* Checks link creation for a borrowed fixture; repeated setup is idempotent. */
static void hush_codex_test_link(const hush_codex_test_t *test);
/* Checks missing source and conflicting content on a borrowed fixture. */
static void hush_codex_test_conflicts(const hush_codex_test_t *test);
/* Removes only the borrowed fixture's temporary directories. */
static void hush_codex_test_deinit(const hush_codex_test_t *test);

int main(void)
{
    hush_codex_test_t test = {.root = HUSH_CODEX_TEST_TEMPLATE};
    hush_codex_test_init(&test);
    assert(hush_codex_prepare_skills(NULL) == HUSH_ERR_ARG);
    assert(hush_codex_prepare_skills("") == HUSH_ERR_ARG);
    assert(setenv(HUSH_CODEX_TEST_ENV, HUSH_CODEX_SKILL_SOURCE, 1) == 0);
    hush_codex_test_link(&test);
    hush_codex_test_conflicts(&test);
    hush_codex_test_deinit(&test);
    puts("test_codex ok");
    return 0;
}

static void hush_codex_test_init(hush_codex_test_t *test)
{
    assert(test != NULL);
    assert(mkdtemp(test->root) != NULL);
    int written = snprintf(test->agents, sizeof(test->agents), "%s/.agents", test->root);
    assert(written > 0 && (size_t)written < sizeof(test->agents));
    written = snprintf(test->skills, sizeof(test->skills), "%s/.agents/skills", test->root);
    assert(written > 0 && (size_t)written < sizeof(test->skills));
    written = snprintf(test->link, sizeof(test->link), "%s/%s", test->root,
                       HUSH_CODEX_TEST_CHILD);
    assert(written > 0 && (size_t)written < sizeof(test->link));
}

static void hush_codex_test_link(const hush_codex_test_t *test)
{
    assert(test != NULL);
    assert(hush_codex_prepare_skills(test->root) == HUSH_OK);
    assert(hush_codex_prepare_skills(test->root) == HUSH_OK);
    char actual[PATH_MAX] = {0};
    char expected[PATH_MAX] = {0};
    assert(realpath(test->link, actual) != NULL);
    assert(realpath(HUSH_CODEX_SKILL_SOURCE, expected) != NULL);
    assert(strcmp(actual, expected) == 0);
}

static void hush_codex_test_conflicts(const hush_codex_test_t *test)
{
    assert(test != NULL);
    assert(setenv(HUSH_CODEX_TEST_ENV, HUSH_CODEX_TEST_MISSING, 1) == 0);
    assert(hush_codex_prepare_skills(test->root) == HUSH_ERR_IO);
    assert(setenv(HUSH_CODEX_TEST_ENV, HUSH_CODEX_SKILL_SOURCE, 1) == 0);
    assert(unlink(test->link) == 0);
    FILE *file = fopen(test->link, "w");
    assert(file != NULL);
    assert(fputs("preserve", file) >= 0);
    assert(fclose(file) == 0);
    assert(hush_codex_prepare_skills(test->root) == HUSH_ERR_IO);
    file = fopen(test->link, "r");
    assert(file != NULL);
    assert(fgetc(file) == 'p');
    assert(fclose(file) == 0);
    assert(unlink(test->link) == 0);
}

static void hush_codex_test_deinit(const hush_codex_test_t *test)
{
    assert(test != NULL);
    assert(rmdir(test->skills) == 0);
    assert(rmdir(test->agents) == 0);
    assert(rmdir(test->root) == 0);
}
