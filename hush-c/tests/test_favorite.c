/* tests/test_favorite.c: PE-4 favorites save/list/load/delete. */

#define _POSIX_C_SOURCE 200809L

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

int main(void)
{
    static char list[HUSH_FAVORITE_JSON_MAX];
    static hush_favorite_t fav;
    hush_skill_forge_in_t in;
    char home[192];
    char dir[HUSH_HOME_PATH_MAX];
    char id[HUSH_SKILL_ID_MAX];
    char ids[HUSH_SKILL_EQUIP_MAX][HUSH_SKILL_ID_MAX];
    char nine[HUSH_SKILL_EQUIP_MAX + 1][HUSH_SKILL_ID_MAX];
    size_t out_len = 0;

    snprintf(home, sizeof(home), "/tmp/hush-fav-test-%d", (int)getpid());
    unsetenv("HUSH_CONFIG_DIR");
    if (setenv("HUSH_HOME", home, 1) != 0)
        return 1;
    memset(&in, 0, sizeof(in));
    memcpy(in.name, "Fav Probe", 10);
    memcpy(in.summary, "Probe skill.", 13);
    memcpy(in.body, "Probe body.", 12);
    memcpy(in.scope, HUSH_SKILL_SCOPE_USER, sizeof(HUSH_SKILL_SCOPE_USER));
    expect(hush_skill_forge(&in, id, sizeof(id)) == HUSH_OK, "forge user");
    expect(strcmp(id, "user:fav-probe") == 0, "user probe id");
    memset(&in, 0, sizeof(in));
    memcpy(in.name, "Fav Local", 10);
    memcpy(in.summary, "Local skill.", 13);
    memcpy(in.body, "Local body.", 12);
    memcpy(in.scope, HUSH_SKILL_SCOPE_ROBOT, sizeof(HUSH_SKILL_SCOPE_ROBOT));
    memcpy(in.robot, "sentry", 7);
    expect(hush_skill_forge(&in, id, sizeof(id)) == HUSH_OK, "forge robot");
    expect(hush_home_loadouts_dir(dir, sizeof(dir), "sentry") == HUSH_OK,
           "loadouts dir");
    expect(strstr(dir, "robots/sentry/loadouts") != NULL, "loadouts path");
    expect(hush_home_loadouts_dir(dir, sizeof(dir), "../evil")
           == HUSH_ERR_ARG, "bad robot slug");
    expect(hush_home_loadouts_dir(dir, sizeof(dir), "")
           == HUSH_ERR_ARG, "empty robot slug");
    memset(ids, 0, sizeof(ids));
    memcpy(ids[0], "system:forge-skill", 19);
    memcpy(ids[1], "user:fav-probe", 15);
    expect(hush_favorite_save("sentry", "Patrol", ids, 2) == HUSH_OK,
           "save patrol");
    expect(hush_favorite_save("sentry", "  ", ids, 2) == HUSH_ERR_PARSE,
           "empty name refused");
    expect(hush_favorite_save("sentry", "Empty", ids, 0) == HUSH_ERR_DENIED,
           "zero skills refused");
    memset(nine, 0, sizeof(nine));
    expect(hush_favorite_save("sentry", "Big", nine, 9) == HUSH_ERR_DENIED,
           "nine skills refused");
    memset(ids, 0, sizeof(ids));
    memcpy(ids[0], "system:no-such-skill", 21);
    expect(hush_favorite_save("sentry", "Ghost", ids, 1) == HUSH_ERR_DENIED,
           "unknown skill refused");
    memset(&in, 0, sizeof(in));
    memcpy(in.name, "Other Local", 12);
    memcpy(in.summary, "Other.", 7);
    memcpy(in.body, "Other body.", 12);
    memcpy(in.scope, HUSH_SKILL_SCOPE_ROBOT, sizeof(HUSH_SKILL_SCOPE_ROBOT));
    memcpy(in.robot, "other", 6);
    expect(hush_skill_forge(&in, id, sizeof(id)) == HUSH_OK, "forge other");
    memset(ids, 0, sizeof(ids));
    memcpy(ids[0], "robot:other:other-local", 24);
    expect(hush_favorite_save("sentry", "Cross", ids, 1) == HUSH_ERR_DENIED,
           "cross slug refused");
    memset(ids, 0, sizeof(ids));
    memcpy(ids[0], "robot:sentry:fav-local", 23);
    expect(hush_favorite_save("sentry", "Second", ids, 1) == HUSH_OK,
           "save second");
    expect(hush_favorite_list_json("sentry", list, sizeof(list), &out_len)
           == HUSH_OK, "list");
    expect(out_len > 0, "list length");
    expect(strstr(list, "\"name\":\"Patrol\"") != NULL, "list patrol");
    expect(strstr(list, "\"name\":\"Second\"") != NULL, "list second");
    expect(strstr(list, "system:forge-skill") != NULL, "list skill id");
    expect(hush_favorite_load("sentry", "Patrol", &fav) == HUSH_OK,
           "load patrol");
    expect(strcmp(fav.name, "Patrol") == 0, "patrol name");
    expect(fav.nskills == 2, "patrol count");
    expect(hush_favorite_delete("sentry", "Patrol") == HUSH_OK,
           "delete patrol");
    expect(hush_favorite_load("sentry", "Patrol", &fav)
           == HUSH_ERR_NOT_FOUND, "deleted gone");
    expect(hush_favorite_load("sentry", "Second", &fav) == HUSH_OK,
           "second survives delete");
    expect(fav.nskills == 1, "second count");
    expect(hush_favorite_delete("sentry", "Missing")
           == HUSH_ERR_NOT_FOUND, "delete missing");
    if (g_fail)
        return 1;
    printf("test_favorite ok\n");
    return 0;
}
