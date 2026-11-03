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
    ROOT_PATH_MAX = 72,
    /* Open-fd budget for the nftw cleanup walk. */
    ROOT_FTW_FDS = 16
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

/* Makes g_root under TMPDIR (/tmp when TMPDIR is unset or empty) and
 * registers root_remove with atexit, so every return from main and every
 * exit() removes it. Returns 0, after a FAIL line, when the path does not
 * fit, mkdtemp fails, or atexit refuses. */
static int root_make(void);

/* atexit handler: removes g_root and everything under it, in the making
 * process only, so a forked child that exits cannot take the root early. */
static void root_remove(void);

/* nftw callback: removes one entry under g_root; FTW_DEPTH visits children
 * first. */
static int root_remove_entry(const char *path, const struct stat *st, int flag,
                             struct FTW *ftw);

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
    const char *tmp = getenv("TMPDIR");
    int wrote = 0;

    assert(g_root[0] == '\0');
    if (tmp == NULL || tmp[0] == '\0')
        tmp = ROOT_TMP_DEFAULT;
    wrote = snprintf(g_root, sizeof(g_root), "%s/%s", tmp, ROOT_NAME);
    if (wrote < 0 || (size_t)wrote >= sizeof(g_root) || mkdtemp(g_root) == NULL) {
        g_root[0] = '\0';
        fprintf(stderr, "FAIL private root under %s\n", tmp);
        return 0;
    }
    g_root_pid = getpid();
    /* Deviation (section 7): atexit takes a bare callback; no table fits. */
    if (atexit(root_remove) != 0) {
        root_remove();
        fprintf(stderr, "FAIL atexit for the private root\n");
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
             FTW_DEPTH | FTW_PHYS | FTW_MOUNT) != 0)
        fprintf(stderr, "FAIL private root cleanup: %s\n", g_root);
    g_root[0] = '\0';
}

static int root_remove_entry(const char *path, const struct stat *st, int flag,
                             struct FTW *ftw)
{
    (void)st;
    (void)flag;
    (void)ftw;
    assert(strncmp(path, g_root, strlen(g_root)) == 0);
    return remove(path);
}
