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
    hush_skill_forge_in_t in;

    memset(&in, 0, sizeof in);
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
    struct dirent *ent;
    size_t found = 0;

    if (dp == NULL)
        return 0;
    while ((ent = readdir(dp)) != NULL) {
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

int main(void)
{
    static char list[HUSH_FAVORITE_JSON_MAX];
    static hush_favorite_t fav;
    char base[192];
    char home[192];
    char dir[HUSH_HOME_PATH_MAX];
    char probe[HUSH_HOME_PATH_MAX];
    char user_id[HUSH_SKILL_ID_MAX];
    char local_id[HUSH_SKILL_ID_MAX];
    char other_id[HUSH_SKILL_ID_MAX];
    char ids[HUSH_SKILL_EQUIP_MAX][HUSH_SKILL_ID_MAX];
    char nine[HUSH_SKILL_EQUIP_MAX + 1][HUSH_SKILL_ID_MAX];
    char capname[HUSH_FAVORITE_NAME_MAX];
    size_t out_len = 0;
    size_t i;

    take_str(base, sizeof base, "/tmp/hush-fav-base-XXXXXX", "base fits");
    expect(mkdtemp(base) != NULL, "mktemp base");
    take_str(home, sizeof home, base, "home fits");
    expect(strlen(home) + strlen("/hush") + 1 < sizeof home, "home fits");
    strcat(home, "/hush");
    unsetenv("HUSH_CONFIG_DIR");
    if (setenv("HUSH_HOME", home, 1) != 0)
        return 1;
    forge_skill(user_id, "Fav Probe", HUSH_SKILL_SCOPE_USER, "");
    expect(strcmp(user_id, "user:fav-probe") == 0, "user probe id");
    forge_skill(local_id, "Fav Local", HUSH_SKILL_SCOPE_ROBOT, "sentry");
    forge_skill(other_id, "Other Local", HUSH_SKILL_SCOPE_ROBOT, "other");
    expect(hush_home_loadouts_dir(dir, sizeof dir, "sentry") == HUSH_OK,
           "loadouts dir");
    expect(strstr(dir, "robots/sentry/loadouts") != NULL, "loadouts path");
    expect(hush_home_loadouts_dir(dir, sizeof dir, "../evil")
           == HUSH_ERR_ARG, "bad robot slug");
    expect(hush_home_loadouts_dir(dir, sizeof dir, "")
           == HUSH_ERR_ARG, "empty robot slug");
    memset(ids, 0, sizeof ids);
    take_str(ids[0], sizeof ids[0], "system:forge-skill", "id fits");
    take_str(ids[1], sizeof ids[1], user_id, "id fits");
    expect(hush_favorite_save("sentry", "Patrol", ids, 2) == HUSH_OK,
           "save patrol");
    expect(hush_favorite_save("sentry", "  ", ids, 2) == HUSH_ERR_PARSE,
           "empty name refused");
    expect(hush_favorite_save("sentry", "Empty", ids, 0) == HUSH_ERR_DENIED,
           "zero skills refused");
    memset(nine, 0, sizeof nine);
    expect(hush_favorite_save("sentry", "Big", nine, 9) == HUSH_ERR_FULL,
           "nine skills refused");
    memset(ids, 0, sizeof ids);
    take_str(ids[0], sizeof ids[0], "system:no-such-skill", "id fits");
    expect(hush_favorite_save("sentry", "Ghost", ids, 1) == HUSH_ERR_DENIED,
           "unknown skill refused");
    memset(ids, 0, sizeof ids);
    take_str(ids[0], sizeof ids[0], other_id, "id fits");
    expect(hush_favorite_save("sentry", "Cross", ids, 1) == HUSH_ERR_DENIED,
           "cross slug refused");
    memset(ids, 0, sizeof ids);
    take_str(ids[0], sizeof ids[0], "system:forge-skill", "id fits");
    take_str(ids[1], sizeof ids[1], "system:forge-skill", "id fits");
    expect(hush_favorite_save("sentry", "Dupes", ids, 2) == HUSH_ERR_DENIED,
           "repeated ids refused");
    expect(hush_favorite_save("", "Patrol", ids, 1) == HUSH_ERR_ARG,
           "empty robot refused");
    memset(ids, 0, sizeof ids);
    take_str(ids[0], sizeof ids[0], local_id, "id fits");
    expect(hush_favorite_save("sentry", "Second", ids, 1) == HUSH_OK,
           "save second");
    expect(hush_favorite_save("sentry", "patrol", ids, 1)
           == HUSH_ERR_DENIED, "slug clash refused");
    expect(hush_favorite_save("sentry", "Patrol!", ids, 1)
           == HUSH_ERR_DENIED, "punct clash refused");
    expect(hush_favorite_save("sentry", "Patrol", ids, 1) == HUSH_OK,
           "exact overwrite allowed");
    expect(hush_favorite_load("sentry", "patrol", &fav) == HUSH_OK,
           "clash alias loads stored");
    expect(strcmp(fav.name, "Patrol") == 0, "stored name survives clash");
    expect(hush_favorite_list_json("sentry", list, sizeof list, &out_len)
           == HUSH_OK, "list");
    expect(out_len > 0, "list length");
    expect(strstr(list, "\"name\":\"Patrol\"") != NULL, "list patrol");
    expect(strstr(list, "\"name\":\"Second\"") != NULL, "list second");
    expect(strstr(list, "robot:sentry:fav-local") != NULL, "list skill id");
    expect(hush_favorite_load("sentry", "Patrol", &fav) == HUSH_OK,
           "load patrol");
    expect(strcmp(fav.name, "Patrol") == 0, "patrol name");
    expect(hush_favorite_delete("sentry", "Patrol") == HUSH_OK,
           "delete patrol");
    expect(hush_favorite_load("sentry", "Patrol", &fav)
           == HUSH_ERR_NOT_FOUND, "deleted gone");
    expect(hush_favorite_load("sentry", "Second", &fav) == HUSH_OK,
           "second survives delete");
    expect(fav.nskills == 1, "second count");
    expect(hush_favorite_delete("sentry", "Missing")
           == HUSH_ERR_NOT_FOUND, "delete missing");
    expect(hush_favorite_load("ghost", "Patrol", &fav)
           == HUSH_ERR_NOT_FOUND, "load creates no dirs");
    take_str(probe, sizeof probe, home, "probe fits");
    expect(strlen(probe) + strlen("/robots/ghost") + 1 < sizeof probe,
           "probe fits");
    strcat(probe, "/robots/ghost");
    expect(path_missing(probe), "ghost tree absent after load");
    expect(hush_favorite_delete("ghost", "Patrol") == HUSH_ERR_NOT_FOUND,
           "delete creates no dirs");
    expect(path_missing(probe), "ghost tree absent after delete");
    expect(hush_favorite_list_json("ghost", list, sizeof list, &out_len)
           == HUSH_OK, "missing tree lists empty");
    expect(strstr(list, "\"favorites\":[]") != NULL, "empty list body");
    expect(path_missing(probe), "ghost tree absent after list");
    memset(ids, 0, sizeof ids);
    take_str(ids[0], sizeof ids[0], "system:forge-skill", "id fits");
    expect(hush_favorite_save("../../.ssh", "Evil", ids, 1)
           == HUSH_ERR_ARG, "traversal robot refused on save");
    expect(hush_favorite_load("../../.ssh", "Evil", &fav) == HUSH_ERR_ARG,
           "traversal robot refused on load");
    {
        hush_status_t evil = hush_favorite_delete("../../.ssh", "Evil");

        expect(evil == HUSH_ERR_ARG, "traversal robot refused on delete");
    }
    expect(hush_favorite_save("sentry", "../../x", ids, 1)
           == HUSH_ERR_PARSE, "traversal name refused on save");
    expect(hush_favorite_load("sentry", "../../x", &fav) == HUSH_ERR_PARSE,
           "traversal name refused on load");
    expect(hush_favorite_delete("sentry", "../../x") == HUSH_ERR_PARSE,
           "traversal name refused on delete");
    expect(base_holds_only(base, "hush"), "nothing escapes HUSH_HOME");
    take_str(probe, sizeof probe, base, "probe fits");
    expect(strlen(probe) + strlen("/.ssh") + 1 < sizeof probe,
           "probe fits");
    strcat(probe, "/.ssh");
    expect(path_missing(probe), "no ssh tree created");
    memset(ids, 0, sizeof ids);
    take_str(ids[0], sizeof ids[0], user_id, "id fits");
    for (i = 0; i < (size_t)HUSH_FAVORITE_COUNT_MAX; i++) {
        int n = snprintf(capname, sizeof capname, "Cap %zu", i);

        expect(n > 0 && (size_t)n < sizeof capname, "cap name fits");
        expect(hush_favorite_save("capper", capname, ids, 1) == HUSH_OK,
               "cap fill");
    }
    expect(hush_favorite_save("capper", "Cap extra", ids, 1)
           == HUSH_ERR_FULL, "33rd favorite refused");
    expect(hush_favorite_delete("capper", "Cap 0") == HUSH_OK,
           "cap delete");
    expect(hush_favorite_save("capper", "Cap extra", ids, 1) == HUSH_OK,
           "cap freed");
    if (g_fail)
        return 1;
    printf("test_favorite ok\n");
    return 0;
}
