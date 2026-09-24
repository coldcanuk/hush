/* tests/test_favorite.c: PE-4 favorites save/list/load/delete. */

#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hush_favorite.h"
#include "hush_home.h"
#include "hush_skill.h"

enum {
    FAV_TEST_PATH_MAX = 192,
    FAV_TEST_SCAN_MAX = 64,
    FAV_TEST_LONG_SLUG = 64,
    FAV_TEST_MAX_NAME = 47
};

typedef struct {
    char base[FAV_TEST_PATH_MAX];
    char home[FAV_TEST_PATH_MAX];
    char probe[HUSH_HOME_PATH_MAX];
    char user_id[HUSH_SKILL_ID_MAX];
    char local_id[HUSH_SKILL_ID_MAX];
    char other_id[HUSH_SKILL_ID_MAX];
    char ids[HUSH_SKILL_EQUIP_MAX][HUSH_SKILL_ID_MAX];
    char nine[HUSH_SKILL_EQUIP_MAX + 1][HUSH_SKILL_ID_MAX];
    char list[HUSH_FAVORITE_JSON_MAX];
    hush_favorite_t fav;
} fav_fixture_t;

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

/* Bounded copy for test fixtures. Fails the test on truncation. */
static void take_str(char *dst, size_t dstsz, const char *src,
                     const char *what)
{
    int n = snprintf(dst, dstsz, "%s", src);

    expect(n > 0 && (size_t)n < dstsz, what);
}

/* Forges one skill; writes its id into out_id. */
static void forge_skill(char *out_id, const char *name, const char *scope,
                        const char *robot)
{
    hush_skill_forge_in_t in = {0};

    take_str(in.name, sizeof in.name, name, "forge name fits");
    take_str(in.summary, sizeof in.summary, "probe skill", "forge sum fits");
    take_str(in.body, sizeof in.body, "probe body", "forge body fits");
    take_str(in.scope, sizeof in.scope, scope, "forge scope fits");
    if (robot[0] != '\0')
        take_str(in.robot, sizeof in.robot, robot, "forge robot fits");
    expect(hush_skill_forge(&in, out_id, HUSH_SKILL_ID_MAX) == HUSH_OK,
           "forge skill");
}

/* True when path is absent from disk. */
static int path_missing(const char *path)
{
    return access(path, F_OK) != 0;
}

/* True when base holds no entry besides want (plus dot files). */
static int base_holds_only(const char *base, const char *want)
{
    DIR *dp = opendir(base);
    size_t found = 0;

    if (dp == NULL)
        return 0;
    for (size_t i = 0; i < (size_t)FAV_TEST_SCAN_MAX; i++) {
        struct dirent *ent = readdir(dp);

        if (ent == NULL)
            break;
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        if (strcmp(ent->d_name, want) != 0) {
            closedir(dp);
            return 0;
        }
        found++;
    }
    closedir(dp);
    return found == 1;
}

/* Counts list entries by their name keys. */
static size_t list_entry_count(const char *list)
{
    size_t n = 0;
    const char *p = list;

    while ((p = strstr(p, "\"name\":\"")) != NULL) {
        n++;
        p++;
    }
    return n;
}

/* Isolates HUSH_HOME, forges probe skills, checks the dir contract. */
static void setup_fixture(fav_fixture_t *fx)
{
    char dir[HUSH_HOME_PATH_MAX] = {0};

    take_str(fx->base, sizeof fx->base, "/tmp/hush-fav-base-XXXXXX",
             "base fits");
    expect(mkdtemp(fx->base) != NULL, "mktemp base");
    take_str(fx->home, sizeof fx->home, fx->base, "home fits");
    expect(strlen(fx->home) + strlen("/hush") + 1 < sizeof fx->home,
           "home fits");
    strcat(fx->home, "/hush");
    unsetenv("HUSH_CONFIG_DIR");
    expect(setenv("HUSH_HOME", fx->home, 1) == 0, "set HUSH_HOME");
    forge_skill(fx->user_id, "Fav Probe", HUSH_SKILL_SCOPE_USER, "");
    expect(strcmp(fx->user_id, "user:fav-probe") == 0, "user probe id");
    forge_skill(fx->local_id, "Fav Local", HUSH_SKILL_SCOPE_ROBOT,
                "sentry");
    forge_skill(fx->other_id, "Other Local", HUSH_SKILL_SCOPE_ROBOT,
                "other");
    expect(hush_home_loadouts_dir(dir, sizeof dir, "sentry") == HUSH_OK,
           "loadouts dir");
    expect(strstr(dir, "robots/sentry/loadouts") != NULL, "loadouts path");
    expect(hush_home_loadouts_dir(dir, sizeof dir, "../evil")
           == HUSH_ERR_ARG, "bad robot slug");
    expect(hush_home_loadouts_dir(dir, sizeof dir, "")
           == HUSH_ERR_ARG, "empty robot slug");
}

/* Refusals: empty names, empty sets, over-cap sets, bad skills, slugs. */
static void check_save_refusals(fav_fixture_t *fx)
{
    char long_robot[FAV_TEST_LONG_SLUG + 1] = {0};
    char long_name[HUSH_FAVORITE_NAME_MAX + 1] = {0};

    memset(fx->ids, 0, sizeof fx->ids);
    take_str(fx->ids[0], sizeof fx->ids[0], "system:forge-skill", "id fits");
    take_str(fx->ids[1], sizeof fx->ids[1], fx->user_id, "id fits");
    expect(hush_favorite_save("sentry", "Patrol", fx->ids, 2) == HUSH_OK,
           "save patrol");
    expect(hush_favorite_save("sentry", "  ", fx->ids, 2) == HUSH_ERR_PARSE,
           "empty name refused");
    expect(hush_favorite_save("sentry", "Empty", fx->ids, 0)
           == HUSH_ERR_DENIED, "zero skills refused");
    memset(fx->nine, 0, sizeof fx->nine);
    expect(hush_favorite_save("sentry", "Big", fx->nine, 9) == HUSH_ERR_FULL,
           "nine skills refused");
    memset(fx->ids, 0, sizeof fx->ids);
    take_str(fx->ids[0], sizeof fx->ids[0], "system:no-such-skill",
             "id fits");
    expect(hush_favorite_save("sentry", "Ghost", fx->ids, 1)
           == HUSH_ERR_DENIED, "unknown skill refused");
    memset(fx->ids, 0, sizeof fx->ids);
    take_str(fx->ids[0], sizeof fx->ids[0], fx->other_id, "id fits");
    expect(hush_favorite_save("sentry", "Cross", fx->ids, 1)
           == HUSH_ERR_DENIED, "cross slug refused");
    memset(fx->ids, 0, sizeof fx->ids);
    take_str(fx->ids[0], sizeof fx->ids[0], "system:forge-skill", "id fits");
    take_str(fx->ids[1], sizeof fx->ids[1], "system:forge-skill", "id fits");
    expect(hush_favorite_save("sentry", "Dupes", fx->ids, 2)
           == HUSH_ERR_DENIED, "repeated ids refused");
    expect(hush_favorite_save("", "Patrol", fx->ids, 1) == HUSH_ERR_ARG,
           "empty robot refused");
    expect(hush_favorite_load("", "Patrol", &fx->fav) == HUSH_ERR_ARG,
           "empty robot refused on load");
    expect(hush_favorite_delete("", "Patrol") == HUSH_ERR_ARG,
           "empty robot refused on delete");
    memset(long_robot, 'r', sizeof long_robot - 1);
    expect(hush_favorite_save(long_robot, "Patrol", fx->ids, 1)
           == HUSH_ERR_ARG, "long robot refused");
    memset(long_name, 'n', sizeof long_name - 1);
    expect(hush_favorite_save("sentry", long_name, fx->ids, 1)
           == HUSH_ERR_FULL, "long name refused");
}

/* Clashes refuse; exact overwrites land; aliases read stored state. */
static void check_clash(fav_fixture_t *fx)
{
    size_t out_len = 0;

    memset(fx->ids, 0, sizeof fx->ids);
    take_str(fx->ids[0], sizeof fx->ids[0], fx->local_id, "id fits");
    expect(hush_favorite_save("sentry", "Second", fx->ids, 1) == HUSH_OK,
           "save second");
    expect(hush_favorite_save("sentry", "patrol", fx->ids, 1)
           == HUSH_ERR_DENIED, "slug clash refused");
    expect(hush_favorite_save("sentry", "Patrol!", fx->ids, 1)
           == HUSH_ERR_DENIED, "punct clash refused");
    expect(hush_favorite_save("sentry", "Patrol", fx->ids, 1) == HUSH_OK,
           "exact overwrite allowed");
    expect(hush_favorite_load("sentry", "patrol", &fx->fav) == HUSH_OK,
           "clash alias loads stored");
    expect(strcmp(fx->fav.name, "Patrol") == 0,
           "stored name survives clash");
    expect(hush_favorite_list_json("sentry", fx->list, sizeof fx->list,
                                   &out_len) == HUSH_OK, "list");
    expect(out_len > 0, "list length");
    expect(strstr(fx->list, "\"name\":\"Patrol\"") != NULL, "list patrol");
    expect(strstr(fx->list, "\"name\":\"Second\"") != NULL, "list second");
    expect(strstr(fx->list, "robot:sentry:fav-local") != NULL,
           "list skill id");
}

/* Load reads stored state; delete removes entries only. */
static void check_list_delete(fav_fixture_t *fx)
{
    expect(hush_favorite_load("sentry", "Patrol", &fx->fav) == HUSH_OK,
           "load patrol");
    expect(strcmp(fx->fav.name, "Patrol") == 0, "patrol name");
    expect(hush_favorite_delete("sentry", "Patrol") == HUSH_OK,
           "delete patrol");
    expect(hush_favorite_load("sentry", "Patrol", &fx->fav)
           == HUSH_ERR_NOT_FOUND, "deleted gone");
    expect(hush_favorite_load("sentry", "Second", &fx->fav) == HUSH_OK,
           "second survives delete");
    expect(fx->fav.nskills == 1, "second count");
    expect(hush_favorite_delete("sentry", "Missing") == HUSH_ERR_NOT_FOUND,
           "delete missing");
}

/* Load, delete, and list create no directories for fresh robots. */
static void check_no_create(fav_fixture_t *fx)
{
    size_t out_len = 0;

    expect(hush_favorite_load("ghost", "Patrol", &fx->fav)
           == HUSH_ERR_NOT_FOUND, "load creates no dirs");
    take_str(fx->probe, sizeof fx->probe, fx->home, "probe fits");
    expect(strlen(fx->probe) + strlen("/robots/ghost") + 1
           < sizeof fx->probe, "probe fits");
    strcat(fx->probe, "/robots/ghost");
    expect(path_missing(fx->probe), "ghost tree absent after load");
    expect(hush_favorite_delete("ghost", "Patrol") == HUSH_ERR_NOT_FOUND,
           "delete creates no dirs");
    expect(path_missing(fx->probe), "ghost tree absent after delete");
    expect(hush_favorite_list_json("ghost", fx->list, sizeof fx->list,
                                   &out_len) == HUSH_OK,
           "missing tree lists empty");
    expect(strstr(fx->list, "\"favorites\":[]") != NULL, "empty list body");
    expect(path_missing(fx->probe), "ghost tree absent after list");
}

/* A seeded victim file proves delete cannot unlink outside HUSH_HOME. */
static void check_victim(fav_fixture_t *fx)
{
    char victim[FAV_TEST_PATH_MAX] = {0};
    char kept[16] = {0};
    FILE *fp = NULL;
    hush_status_t evil = HUSH_OK;

    take_str(victim, sizeof victim, fx->base, "victim fits");
    expect(strlen(victim) + strlen("/victim.txt") + 1 < sizeof victim,
           "victim fits");
    strcat(victim, "/victim.txt");
    fp = fopen(victim, "w");
    expect(fp != NULL, "seed victim");
    if (fp != NULL) {
        fputs("precious\n", fp);
        fclose(fp);
    }
    evil = hush_favorite_delete("..", "victim.txt");
    expect(evil == HUSH_ERR_ARG, "dotdot robot refused");
    fp = fopen(victim, "r");
    expect(fp != NULL, "victim survives");
    if (fp != NULL) {
        expect(fgets(kept, sizeof kept, fp) != NULL, "victim reads");
        fclose(fp);
    }
    expect(strcmp(kept, "precious\n") == 0, "victim intact");
    expect(unlink(victim) == 0, "victim cleaned");
    expect(path_missing(victim), "victim gone");
}

/* Traversal robots and names fail on every entry. */
static void check_traversal(fav_fixture_t *fx)
{
    hush_status_t evil = HUSH_OK;

    memset(fx->ids, 0, sizeof fx->ids);
    take_str(fx->ids[0], sizeof fx->ids[0], "system:forge-skill", "id fits");
    expect(hush_favorite_save("../../.ssh", "Evil", fx->ids, 1)
           == HUSH_ERR_ARG, "traversal robot refused on save");
    expect(hush_favorite_load("../../.ssh", "Evil", &fx->fav)
           == HUSH_ERR_ARG, "traversal robot refused on load");
    evil = hush_favorite_delete("../../.ssh", "Evil");
    expect(evil == HUSH_ERR_ARG, "traversal robot refused on delete");
    expect(hush_favorite_save("sentry", "../../x", fx->ids, 1)
           == HUSH_ERR_PARSE, "traversal name refused on save");
    expect(hush_favorite_load("sentry", "../../x", &fx->fav)
           == HUSH_ERR_PARSE, "traversal name refused on load");
    expect(hush_favorite_delete("sentry", "../../x") == HUSH_ERR_PARSE,
           "traversal name refused on delete");
    check_victim(fx);
    expect(base_holds_only(fx->base, "hush"), "nothing escapes HUSH_HOME");
    take_str(fx->probe, sizeof fx->probe, fx->base, "probe fits");
    expect(strlen(fx->probe) + strlen("/.ssh") + 1 < sizeof fx->probe,
           "probe fits");
    strcat(fx->probe, "/.ssh");
    expect(path_missing(fx->probe), "no ssh tree created");
}

/* The 33rd favorite is refused; a full cap of maximum-length names
 * still lists completely, proving save never accepts what list
 * cannot emit. */
static void check_cap(fav_fixture_t *fx)
{
    char capname[HUSH_FAVORITE_NAME_MAX] = {0};
    char wide[HUSH_FAVORITE_NAME_MAX] = {0};
    size_t out_len = 0;

    memset(fx->ids, 0, sizeof fx->ids);
    take_str(fx->ids[0], sizeof fx->ids[0], fx->user_id, "id fits");
    for (size_t i = 0; i < (size_t)HUSH_FAVORITE_COUNT_MAX; i++) {
        int n = snprintf(capname, sizeof capname, "Cap %zu", i);

        expect(n > 0 && (size_t)n < sizeof capname, "cap name fits");
        expect(hush_favorite_save("capper", capname, fx->ids, 1) == HUSH_OK,
               "cap fill");
    }
    expect(hush_favorite_save("capper", "Cap extra", fx->ids, 1)
           == HUSH_ERR_FULL, "33rd favorite refused");
    expect(hush_favorite_delete("capper", "Cap 0") == HUSH_OK,
           "cap delete");
    expect(hush_favorite_save("capper", "Cap extra", fx->ids, 1) == HUSH_OK,
           "cap freed");
    memset(wide, 'w', sizeof wide - 1);
    for (size_t i = 0; i < (size_t)HUSH_FAVORITE_COUNT_MAX; i++) {
        char name[HUSH_FAVORITE_NAME_MAX] = {0};
        int n = snprintf(name, sizeof name, "%.*s%02zu",
                         (int)(sizeof wide - 3), wide, i);

        expect(n > 0 && (size_t)n < sizeof name, "wide name fits");
        expect(strlen(name) == (size_t)FAV_TEST_MAX_NAME, "wide is max");
        expect(hush_favorite_save("wide", name, fx->ids, 1) == HUSH_OK,
               "wide fill");
    }
    expect(hush_favorite_list_json("wide", fx->list, sizeof fx->list,
                                   &out_len) == HUSH_OK,
           "wide lists");
    expect(list_entry_count(fx->list) == (size_t)HUSH_FAVORITE_COUNT_MAX,
           "wide lists all 32");
}

int main(void)
{
    fav_fixture_t fx;

    memset(&fx, 0, sizeof fx);
    setup_fixture(&fx);
    check_save_refusals(&fx);
    check_clash(&fx);
    check_list_delete(&fx);
    check_no_create(&fx);
    check_traversal(&fx);
    check_cap(&fx);
    if (g_fail)
        return 1;
    printf("test_favorite ok\n");
    return 0;
}
