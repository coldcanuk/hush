/* tests/test_favorite_reuse.c: delete drops favorites for that robot id. */

#define _XOPEN_SOURCE 700 /* nftw, for the private root cleanup */

#include <assert.h>
#include <dirent.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>

#include "hush_favorite.h"
#include "hush_launch.h"
#include "hush_roster.h"
#include "hush_store.h"

enum { REUSE_SCAN_MAX = 64 };

enum {
    /* Longest private root path; every path a test derives from it fits. */
    ROOT_PATH_MAX = 96,
    /* Open-fd budget for the nftw cleanup walk. */
    ROOT_FTW_FDS = 16
};

/* <TMPDIR or /tmp>/hush-fav-reuse-XXXXXX: this run's private root, 0700 from mkdtemp.
 * Every key, pass store, home and config the test writes lives under it. */
#define ROOT_NAME "hush-fav-reuse-XXXXXX"
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

static void fill_agent(hush_roster_agent_in_t *in, const char *name)
{
    int n = 0;

    memset(in, 0, sizeof(*in));
    n = snprintf(in->name, sizeof in->name, "%s", name);
    expect(n > 0 && (size_t)n < sizeof in->name, "name fits");
    memcpy(in->prompt, "Watch the perimeter.", 21);
    memcpy(in->provider, HUSH_ROSTER_PROVIDER_GROK_BUILD,
           sizeof HUSH_ROSTER_PROVIDER_GROK_BUILD);
}

static int copy_slug(const hush_launch_t *launch, const char *name,
                     char *out, size_t outsz)
{
    size_t i = 0;
    int n = 0;

    for (i = 0; i < launch->roster.nagents &&
                i < (size_t)HUSH_ROSTER_AGENTS_MAX; i++) {
        if (strcmp(launch->roster.agents[i].name, name) != 0)
            continue;
        n = snprintf(out, outsz, "%s", launch->roster.agents[i].slug);
        return n > 0 && (size_t)n < outsz;
    }
    return 0;
}

static void save_named(const char *robot, const char *fav)
{
    char ids[HUSH_SKILL_EQUIP_MAX][HUSH_SKILL_ID_MAX];

    memset(ids, 0, sizeof ids);
    memcpy(ids[0], "system:forge-skill", 19);
    expect(hush_favorite_save(robot, fav, ids, 1) == HUSH_OK, "save favorite");
}

static int list_has(const char *robot, const char *fav)
{
    static char list[HUSH_FAVORITE_JSON_MAX];
    char key[HUSH_FAVORITE_NAME_MAX + 16];
    size_t n = 0;
    int wrote = 0;

    expect(hush_favorite_list_json(robot, list, sizeof list, &n) == HUSH_OK,
           "list favorites");
    wrote = snprintf(key, sizeof key, "\"name\":\"%s\"", fav);
    expect(wrote > 0 && (size_t)wrote < sizeof key, "favorite key fits");
    return strstr(list, key) != NULL;
}

/* True when dir has no entry besides dot and dotdot. */
static int dir_empty(const char *dir)
{
    DIR *dp = opendir(dir);
    size_t i = 0;

    if (dp == NULL)
        return 0;
    for (i = 0; i < (size_t)REUSE_SCAN_MAX; i++) {
        struct dirent *ent = readdir(dp);

        if (ent == NULL)
            break;
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        closedir(dp);
        return 0;
    }
    closedir(dp);
    return 1;
}

/* True when dir's names are only the two robots, so nothing was archived. */
static int robots_are_only(const char *dir)
{
    DIR *dp = opendir(dir);
    size_t i = 0;
    size_t found = 0;

    if (dp == NULL)
        return 0;
    for (i = 0; i < (size_t)REUSE_SCAN_MAX; i++) {
        struct dirent *ent = readdir(dp);

        if (ent == NULL)
            break;
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        if (strcmp(ent->d_name, "sentry") != 0 &&
            strcmp(ent->d_name, "keeper") != 0) {
            closedir(dp);
            return 0;
        }
        found++;
    }
    closedir(dp);
    return found == 2;
}

static void isolate(char *home, size_t homesz, char *cfg, size_t cfgsz)
{
    char base[ROOT_PATH_MAX + sizeof("/hush-fav-reuse-XXXXXX")] = {0};
    int n = 0;

    snprintf(base, sizeof(base), "%s/hush-fav-reuse-XXXXXX", g_root);
    expect(mkdtemp(base) != NULL, "mkdtemp");
    n = snprintf(home, homesz, "%s/home", base);
    expect(n > 0 && (size_t)n < homesz, "home fits");
    n = snprintf(cfg, cfgsz, "%s/config", base);
    expect(n > 0 && (size_t)n < cfgsz, "config fits");
    expect(mkdir(home, 0700) == 0, "mkdir home");
    expect(mkdir(cfg, 0700) == 0, "mkdir config");
    expect(setenv("HUSH_HOME", home, 1) == 0, "set HUSH_HOME");
    expect(setenv("HUSH_CONFIG_DIR", cfg, 1) == 0, "set HUSH_CONFIG_DIR");
}

static void raise(hush_launch_t *launch, hush_store_t **store)
{
    expect(hush_store_create(store) == HUSH_OK, "store");
    hush_launch_init(launch);
    expect(hush_launch_create_identity(launch) == HUSH_OK, "identity");
    expect(hush_launch_ack_backup(launch, 0) == HUSH_OK, "ack");
    expect(hush_launch_create_vibe(launch, *store, "HQ", "reuse") == HUSH_OK,
           "vibe");
}

static void add_named(hush_launch_t *launch, hush_store_t *store,
                      const char *name)
{
    hush_roster_agent_in_t in;

    fill_agent(&in, name);
    expect(hush_launch_add_agent(launch, store, &in, 0) == HUSH_OK, "add robot");
}

int main(void)
{
    static hush_launch_t launch;
    hush_store_t *store = NULL;
    hush_roster_agent_in_t in;
    char home[192] = {0};
    char cfg[192] = {0};
    char first[HUSH_ROSTER_NAME_MAX] = {0};
    char again[HUSH_ROSTER_NAME_MAX] = {0};
    char loadouts[256] = {0};
    char robots[256] = {0};
    char sentry[256] = {0};
    int n = 0;

    if (!root_make())
        return 1;
    isolate(home, sizeof home, cfg, sizeof cfg);
    if (g_fail)
        return 1;
    raise(&launch, &store);
    add_named(&launch, store, "Sentry");
    add_named(&launch, store, "Keeper");
    add_named(&launch, store, "Plain");
    expect(hush_launch_remove_agent(&launch, "plain") == HUSH_OK,
           "delete a robot that has no favorites");
    expect(copy_slug(&launch, "Sentry", first, sizeof first), "sentry id");
    expect(strcmp(first, "sentry") == 0, "first id is sentry");
    save_named(first, "Patrol");
    save_named("keeper", "Keep");
    expect(list_has(first, "Patrol"), "patrol present before delete");
    expect(list_has("keeper", "Keep"), "keeper favorite present");
    n = snprintf(loadouts, sizeof loadouts, "%s/robots/sentry/loadouts", home);
    expect(n > 0 && (size_t)n < sizeof loadouts, "loadouts path fits");
    expect(access(loadouts, F_OK) == 0, "loadouts dir exists");
    expect(hush_launch_remove_agent(&launch, first) == HUSH_OK, "delete sentry");
    fill_agent(&in, "Sentry");
    expect(hush_launch_add_agent(&launch, store, &in, 0) == HUSH_OK,
           "recreate sentry");
    expect(copy_slug(&launch, "Sentry", again, sizeof again), "reused id");
    expect(strcmp(again, first) == 0, "same id after recreate");
    expect(!list_has(again, "Patrol"), "reused id has no patrol");
    expect(list_has("keeper", "Keep"), "other robot keeps its favorite");
    expect(access(loadouts, F_OK) != 0, "loadouts dir deleted");
    n = snprintf(sentry, sizeof sentry, "%s/robots/sentry", home);
    expect(n > 0 && (size_t)n < sizeof sentry, "sentry path fits");
    expect(dir_empty(sentry), "sentry dir holds no archived favorites");
    n = snprintf(robots, sizeof robots, "%s/robots", home);
    expect(n > 0 && (size_t)n < sizeof robots, "robots path fits");
    expect(robots_are_only(robots), "no archive directory beside the robots");
    if (g_fail)
        return 1;
    printf("test_favorite_reuse ok\n");
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
