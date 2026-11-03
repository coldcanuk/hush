/* tests/test_profile_patch.c: a partial profile post keeps the other fields. */

#define _XOPEN_SOURCE 700 /* nftw, for the private root cleanup */

#include <assert.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hush_launch.h"
#include "hush_store.h"

enum {
    /* Longest private root path; every path a test derives from it fits. */
    ROOT_PATH_MAX = 96,
    /* Open-fd budget for the nftw cleanup walk. */
    ROOT_FTW_FDS = 16,
    /* Exit status when the root cannot be removed; atexit cannot return one. */
    ROOT_CLEANUP_FAIL_RC = 1
};

/* <TMPDIR or /tmp>/hush-profile-XXXXXX: this run's private root, 0700 from mkdtemp.
 * Every key, pass store, home and config the test writes lives under it. */
#define ROOT_NAME "hush-profile-XXXXXX"
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

static void fill(hush_roster_profile_t *profile)
{
    memset(profile, 0, sizeof(*profile));
    snprintf(profile->first_name, sizeof(profile->first_name), "Ada");
    snprintf(profile->last_name, sizeof(profile->last_name), "Lovelace");
    snprintf(profile->email, sizeof(profile->email), "ada@example");
    snprintf(profile->organization, sizeof(profile->organization), "Analytical");
    snprintf(profile->theme, sizeof(profile->theme), "dark");
}

static unsigned all_names(void)
{
    return HUSH_PROFILE_FIRST | HUSH_PROFILE_LAST | HUSH_PROFILE_EMAIL |
        HUSH_PROFILE_ORG | HUSH_PROFILE_THEME;
}

int main(void)
{
    char home[ROOT_PATH_MAX + sizeof("/hush-profile-XXXXXX")] = {0};
    hush_launch_t launch;
    hush_launch_t again;
    hush_store_t *store = NULL;
    hush_roster_profile_t profile;
    const hush_roster_profile_t *saved;

    if (!root_make())
        return 1;
    snprintf(home, sizeof(home), "%s/hush-profile-XXXXXX", g_root);
    if (mkdtemp(home) == NULL)
        return 1;
    if (setenv("HUSH_HOME", home, 1) != 0)
        return 1;
    hush_launch_init(&launch);
    expect(hush_store_create(&store) == HUSH_OK, "store");
    expect(hush_launch_create_identity(&launch) == HUSH_OK, "identity");
    expect(hush_launch_ack_backup(&launch, 0) == HUSH_OK, "ack");
    expect(hush_launch_create_vibe(&launch, store, "HQ", "x") == HUSH_OK, "vibe");
    fill(&profile);
    expect(hush_launch_patch_profile(&launch, &profile, all_names(), 0) == HUSH_OK,
           "full save");
    memset(&profile, 0, sizeof(profile));
    snprintf(profile.theme, sizeof(profile.theme), "field-office");
    expect(hush_launch_patch_profile(&launch, &profile, HUSH_PROFILE_THEME, 0) ==
               HUSH_OK, "theme only");
    saved = &launch.roster.profile;
    expect(strcmp(saved->first_name, "Ada") == 0, "first name kept");
    expect(strcmp(saved->last_name, "Lovelace") == 0, "last name kept");
    expect(strcmp(saved->email, "ada@example") == 0, "email kept");
    expect(strcmp(saved->organization, "Analytical") == 0, "org kept");
    expect(strcmp(saved->theme, "field-office") == 0, "theme updated");
    expect(hush_launch_patch_profile(&launch, &profile, HUSH_PROFILE_DEVLOG, 1) ==
               HUSH_OK, "dev log on");
    expect(launch.dev_log_enabled == 1, "dev log set");
    expect(strcmp(saved->first_name, "Ada") == 0, "dev log keeps the name");
    expect(hush_launch_patch_profile(&launch, &profile, HUSH_PROFILE_DEVLOG, 0) ==
               HUSH_OK, "dev log off");
    expect(launch.dev_log_enabled == 0, "dev log cleared");
    memset(&profile, 0, sizeof(profile));
    expect(hush_launch_patch_profile(&launch, &profile, HUSH_PROFILE_FIRST, 0) ==
               HUSH_OK, "clear first");
    expect(saved->first_name[0] == '\0', "first name cleared");
    expect(strcmp(saved->last_name, "Lovelace") == 0, "last name still kept");
    snprintf(profile.first_name, sizeof(profile.first_name), "Ada");
    expect(hush_launch_patch_profile(&launch, &profile, HUSH_PROFILE_FIRST, 0) ==
               HUSH_OK, "restore first");
    snprintf(profile.theme, sizeof(profile.theme), "nope");
    expect(hush_launch_patch_profile(&launch, &profile, HUSH_PROFILE_THEME, 0) ==
               HUSH_ERR_PARSE, "bad theme");
    expect(strcmp(saved->theme, "field-office") == 0, "bad theme changes nothing");
    expect(strcmp(saved->first_name, "Ada") == 0, "bad theme keeps the name");
    expect(hush_launch_patch_profile(&launch, &profile, 0, 0) == HUSH_ERR_PARSE,
           "empty mask");
    hush_launch_init(&again);
    expect(hush_launch_restore_vibe(&again) == HUSH_OK, "reload");
    expect(strcmp(again.roster.profile.first_name, "Ada") == 0, "restart first");
    expect(strcmp(again.roster.profile.last_name, "Lovelace") == 0, "restart last");
    expect(strcmp(again.roster.profile.email, "ada@example") == 0, "restart email");
    expect(strcmp(again.roster.profile.organization, "Analytical") == 0,
           "restart org");
    expect(strcmp(again.roster.profile.theme, "field-office") == 0, "restart theme");
    expect(again.dev_log_enabled == 0, "restart dev log");
    hush_store_destroy(store);
    if (g_fail)
        return 1;
    printf("test_profile_patch ok\n");
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
