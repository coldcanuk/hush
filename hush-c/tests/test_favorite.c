/* tests/test_favorite.c: PE-4 favorites save/list/load/delete. */

#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hush_favorite.h"
#include "hush_home.h"
#include "hush_skill.h"

enum {
    FAV_TEST_PATH_MAX = 192,
    FAV_TEST_SCAN_MAX = 64,
    FAV_TEST_LONG_SLUG = 64,
    FAV_TEST_MAX_NAME = 47,
    FAV_TEST_LONG_SLUG_LEN = 90,
    FAV_TEST_HOSTILE_BYTE = 0x01,
    /* Hostile entries measure 4375 B: 9 + 9 + 12 + 8 x (2 + 540) + 7
     * + 2. Envelope for robot "hostile" is 45, so saves total
     * 44 + k x 4376: six fit (26300), the seventh is refused. */
    FAV_TEST_HOSTILE_FITS = 6,
    FAV_TEST_HOSTILE_TRIES = 8,
    /* Boundary robot: a 63-char slug with 32 maximum (853 B) entries
     * plus an unchanged re-save totals exactly 27428 content bytes,
     * which lists fine. The one-comma over-count refuses it. */
    FAV_TEST_EDGE_SLUG = 63
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
    expect(hush_favorite_save("sentry", "PATROL", fx->ids, 1)
           == HUSH_ERR_DENIED, "case clash refused");
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

/* Allowlist names: letters, digits, space, '-', '_' only. Percent,
 * dots, and punctuation never store; separator variants still clash. */
static void check_names(fav_fixture_t *fx)
{
    memset(fx->ids, 0, sizeof fx->ids);
    take_str(fx->ids[0], sizeof fx->ids[0], "system:forge-skill", "id fits");
    expect(hush_favorite_save("sentry", "%2e%2e", fx->ids, 1)
           == HUSH_ERR_PARSE, "percent name refused on save");
    expect(hush_favorite_load("sentry", "%2e%2e", &fx->fav)
           == HUSH_ERR_PARSE, "percent name refused on load");
    expect(hush_favorite_delete("sentry", "%2e%2e") == HUSH_ERR_PARSE,
           "percent name refused on delete");
    expect(hush_favorite_save("sentry", "..", fx->ids, 1) == HUSH_ERR_PARSE,
           "dotdot refused on save");
    expect(hush_favorite_load("sentry", "..", &fx->fav) == HUSH_ERR_PARSE,
           "dotdot refused on load");
    expect(hush_favorite_delete("sentry", "..") == HUSH_ERR_PARSE,
           "dotdot refused on delete");
    expect(hush_favorite_save("sentry", ".", fx->ids, 1) == HUSH_ERR_PARSE,
           "dot refused");
    expect(hush_favorite_save("sentry", "!!!", fx->ids, 1)
           == HUSH_ERR_PARSE, "punct refused");
    expect(hush_favorite_save("sentry", "-", fx->ids, 1)
           == HUSH_ERR_PARSE, "slug-empty refused");
    expect(hush_favorite_save("sentry", "100%", fx->ids, 1)
           == HUSH_ERR_PARSE, "percent refused");
    expect(hush_favorite_save("sentry", "a b", fx->ids, 1) == HUSH_OK,
           "separator name saves");
    expect(hush_favorite_save("sentry", "a-b", fx->ids, 1)
           == HUSH_ERR_DENIED, "separator clash refused");
    expect(hush_favorite_save("sentry", "A_B", fx->ids, 1)
           == HUSH_ERR_DENIED, "case clash refused");
    expect(hush_favorite_delete("sentry", "a b") == HUSH_OK,
           "separator cleanup");
}

/* The 33rd favorite is refused. Filling the cap with short names
 * exercises the count gate; the envelope fit is proven by check_giant
 * and check_hostile below. */
static void check_cap(fav_fixture_t *fx)
{
    char capname[HUSH_FAVORITE_NAME_MAX] = {0};

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
}

/* A full cap of maximum-length names still lists completely. */
static void check_wide(fav_fixture_t *fx)
{
    char wide[HUSH_FAVORITE_NAME_MAX] = {0};
    size_t out_len = 0;

    memset(fx->ids, 0, sizeof fx->ids);
    take_str(fx->ids[0], sizeof fx->ids[0], fx->user_id, "id fits");
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

/* Plants a user skill with the exact dir slug; writes its id out. */
static void plant_skill(char *out_id, const char *slug)
{
    char root[HUSH_HOME_PATH_MAX] = {0};
    char dir[HUSH_HOME_PATH_MAX] = {0};
    char file[HUSH_HOME_PATH_MAX] = {0};
    FILE *fp = NULL;
    int n = 0;

    hush_home_root(root, sizeof root);
    expect(root[0] != '\0', "home root set");
    n = snprintf(dir, sizeof dir, "%s/skills/user/%s", root, slug);
    expect(n > 0 && (size_t)n < sizeof dir, "skill dir fits");
    expect(mkdir(dir, HUSH_HOME_DIR_MODE) == 0, "plant skill dir");
    n = snprintf(file, sizeof file, "%s/SKILL.md", dir);
    expect(n > 0 && (size_t)n < sizeof file, "skill file fits");
    fp = fopen(file, "w");
    expect(fp != NULL, "plant skill file");
    if (fp != NULL) {
        fputs("---\nname: plant\n---\n\n# Plant\n", fp);
        fclose(fp);
    }
    n = snprintf(out_id, HUSH_SKILL_ID_MAX, "user:%s", slug);
    expect(n > 0 && (size_t)n < (size_t)HUSH_SKILL_ID_MAX, "plant id fits");
}

/* A planted 33rd valid file makes list report FULL, never truncate. */
static void check_overfill(fav_fixture_t *fx)
{
    char dir[HUSH_HOME_PATH_MAX] = {0};
    char file[HUSH_HOME_PATH_MAX] = {0};
    FILE *fp = NULL;
    size_t out_len = 0;
    int n = 0;

    expect(hush_home_loadouts_dir(dir, sizeof dir, "capper") == HUSH_OK,
           "capper dir");
    n = snprintf(file, sizeof file, "%s/zzz-extra.json", dir);
    expect(n > 0 && (size_t)n < sizeof file, "plant path fits");
    fp = fopen(file, "w");
    expect(fp != NULL, "plant 33rd file");
    if (fp != NULL) {
        fputs("{\"name\":\"Zzz Extra\",\"robot\":\"capper\","
              "\"skills\":[\"system:forge-skill\"]}\n", fp);
        fclose(fp);
    }
    expect(hush_favorite_list_json("capper", fx->list, sizeof fx->list,
                                   &out_len) == HUSH_ERR_FULL,
           "33rd file lists FULL");
    expect(hush_favorite_delete("capper", "Zzz Extra") == HUSH_OK,
           "planted cleanup");
    expect(hush_favorite_list_json("capper", fx->list, sizeof fx->list,
                                   &out_len) == HUSH_OK,
           "capper lists again");
    expect(list_entry_count(fx->list) == (size_t)HUSH_FAVORITE_COUNT_MAX,
           "capper back to 32");
}

/* Long catalog ids on the new-name path plus overwrite growth: a full
 * cap of 95-char ids still lists completely. */
static void check_giant(fav_fixture_t *fx)
{
    char slugs[HUSH_SKILL_EQUIP_MAX][HUSH_SKILL_ID_MAX] = {{0}};
    char ids[HUSH_SKILL_EQUIP_MAX][HUSH_SKILL_ID_MAX] = {{0}};
    char all[HUSH_SKILL_EQUIP_MAX][HUSH_SKILL_ID_MAX] = {{0}};
    size_t out_len = 0;

    for (size_t i = 0; i < (size_t)HUSH_SKILL_EQUIP_MAX; i++) {
        memset(slugs[i], 'g', (size_t)FAV_TEST_LONG_SLUG_LEN);
        slugs[i][FAV_TEST_LONG_SLUG_LEN - 1] = (char)('0' + i);
        plant_skill(ids[i], slugs[i]);
    }
    for (size_t k = 0; k < (size_t)HUSH_FAVORITE_COUNT_MAX; k++) {
        char name[HUSH_FAVORITE_NAME_MAX] = {0};
        int n = snprintf(name, sizeof name, "Giant %02zu", k);

        expect(n > 0 && (size_t)n < sizeof name, "giant name fits");
        for (size_t i = 0; i < (size_t)HUSH_SKILL_EQUIP_MAX; i++)
            take_str(all[i], sizeof all[i], ids[i], "giant id fits");
        expect(hush_favorite_save("giant", name, all,
                                 (size_t)HUSH_SKILL_EQUIP_MAX) == HUSH_OK,
               "giant fill");
    }
    expect(hush_favorite_list_json("giant", fx->list, sizeof fx->list,
                                   &out_len) == HUSH_OK,
           "giant lists");
    expect(list_entry_count(fx->list) == (size_t)HUSH_FAVORITE_COUNT_MAX,
           "giant lists all 32");
    memset(all, 0, sizeof all);
    take_str(all[0], sizeof all[0], fx->user_id, "id fits");
    expect(hush_favorite_save("sprout", "Seed", all, 1) == HUSH_OK,
           "sprout seed");
    for (size_t k = 1; k < (size_t)HUSH_FAVORITE_COUNT_MAX; k++) {
        char name[HUSH_FAVORITE_NAME_MAX] = {0};
        int n = snprintf(name, sizeof name, "Sprout %02zu", k);

        expect(n > 0 && (size_t)n < sizeof name, "sprout name fits");
        expect(hush_favorite_save("sprout", name, all, 1) == HUSH_OK,
               "sprout fill");
    }
    for (size_t i = 0; i < (size_t)HUSH_SKILL_EQUIP_MAX; i++)
        take_str(all[i], sizeof all[i], ids[i], "giant id fits");
    expect(hush_favorite_save("sprout", "Seed", all, 8) == HUSH_OK,
           "sprout overwrite grows");
    expect(hush_favorite_list_json("sprout", fx->list, sizeof fx->list,
                                   &out_len) == HUSH_OK,
           "sprout lists");
    expect(list_entry_count(fx->list) == (size_t)HUSH_FAVORITE_COUNT_MAX,
           "sprout lists all 32");
}

/* Boundary overwrite: a 63-char robot holding 32 maximum (853 B)
 * entries re-saves one unchanged for exactly 27428 content bytes,
 * which lists fine. The one-comma over-count refuses it with FULL. */
static void check_edge(fav_fixture_t *fx)
{
    char edgeslug[FAV_TEST_EDGE_SLUG + 1] = {0};
    char slugs[HUSH_SKILL_EQUIP_MAX][HUSH_SKILL_ID_MAX] = {{0}};
    char ids[HUSH_SKILL_EQUIP_MAX][HUSH_SKILL_ID_MAX] = {{0}};
    char all[HUSH_SKILL_EQUIP_MAX][HUSH_SKILL_ID_MAX] = {{0}};
    char first[HUSH_FAVORITE_NAME_MAX] = {0};
    size_t out_len = 0;

    memset(edgeslug, 'e', (size_t)FAV_TEST_EDGE_SLUG);
    for (size_t i = 0; i < (size_t)HUSH_SKILL_EQUIP_MAX; i++) {
        memset(slugs[i], 'x', (size_t)FAV_TEST_LONG_SLUG_LEN);
        slugs[i][FAV_TEST_LONG_SLUG_LEN - 1] = (char)('0' + i);
        plant_skill(ids[i], slugs[i]);
    }
    for (size_t i = 0; i < (size_t)HUSH_SKILL_EQUIP_MAX; i++)
        take_str(all[i], sizeof all[i], ids[i], "edge id fits");
    for (size_t k = 0; k < (size_t)HUSH_FAVORITE_COUNT_MAX; k++) {
        char name[HUSH_FAVORITE_NAME_MAX] = {0};
        int n = snprintf(name, sizeof name, "%.*s%02zu",
                         FAV_TEST_MAX_NAME - 2, all[0] + 5, k);

        expect(n > 0 && (size_t)n < sizeof name, "edge name fits");
        expect(strlen(name) == (size_t)FAV_TEST_MAX_NAME, "edge is max");
        if (k == 0)
            take_str(first, sizeof first, name, "edge first fits");
        expect(hush_favorite_save(edgeslug, name, all, 8) == HUSH_OK,
               "edge fill");
    }
    expect(hush_favorite_save(edgeslug, first, all, 8) == HUSH_OK,
           "edge overwrite fits");
    expect(hush_favorite_list_json(edgeslug, fx->list, sizeof fx->list,
                                   &out_len) == HUSH_OK,
           "edge lists");
    expect(list_entry_count(fx->list) == (size_t)HUSH_FAVORITE_COUNT_MAX,
           "edge lists all 32");
}

/* Escape-hostile ids trip the envelope gate: 4375 B entries fit six
 * to a robot, and the seventh save is refused. */
static void check_hostile(fav_fixture_t *fx)
{
    char slugs[HUSH_SKILL_EQUIP_MAX][HUSH_SKILL_ID_MAX] = {{0}};
    char ids[HUSH_SKILL_EQUIP_MAX][HUSH_SKILL_ID_MAX] = {{0}};
    char all[HUSH_SKILL_EQUIP_MAX][HUSH_SKILL_ID_MAX] = {{0}};
    size_t out_len = 0;

    for (size_t i = 0; i < (size_t)HUSH_SKILL_EQUIP_MAX; i++) {
        memset(slugs[i], FAV_TEST_HOSTILE_BYTE,
               (size_t)FAV_TEST_LONG_SLUG_LEN);
        slugs[i][FAV_TEST_LONG_SLUG_LEN - 1] = (char)('0' + i);
        plant_skill(ids[i], slugs[i]);
    }
    for (size_t i = 0; i < (size_t)HUSH_SKILL_EQUIP_MAX; i++)
        take_str(all[i], sizeof all[i], ids[i], "hostile id fits");
    for (size_t k = 0; k < (size_t)FAV_TEST_HOSTILE_TRIES; k++) {
        char name[HUSH_FAVORITE_NAME_MAX] = {0};
        int n = snprintf(name, sizeof name, "Hostile %zu", k);
        hush_status_t st = HUSH_OK;

        expect(n > 0 && (size_t)n < sizeof name, "hostile name fits");
        st = hush_favorite_save("hostile", name, all,
                                  (size_t)HUSH_SKILL_EQUIP_MAX);
        if (k < (size_t)FAV_TEST_HOSTILE_FITS)
            expect(st == HUSH_OK, "hostile fill fits");
        else
            expect(st == HUSH_ERR_FULL, "hostile overfill refused");
    }
    expect(hush_favorite_list_json("hostile", fx->list, sizeof fx->list,
                                   &out_len) == HUSH_OK,
           "hostile lists");
    expect(list_entry_count(fx->list) == (size_t)FAV_TEST_HOSTILE_FITS,
           "hostile lists six");
    expect(out_len == 26300, "hostile list measures 26300");
    memset(all, 0, sizeof all);
    take_str(all[0], sizeof all[0], "system:forge-skill", "id fits");
    expect(hush_favorite_save("hostile", "Seed", all, 1) == HUSH_OK,
           "seed fits beside six");
    for (size_t i = 0; i < (size_t)HUSH_SKILL_EQUIP_MAX; i++)
        take_str(all[i], sizeof all[i], ids[i], "hostile id fits");
    expect(hush_favorite_save("hostile", "Seed", all,
                              (size_t)HUSH_SKILL_EQUIP_MAX)
           == HUSH_ERR_FULL, "seed growth refused");
    expect(hush_favorite_load("hostile", "Seed", &fx->fav) == HUSH_OK,
           "seed reads back");
    expect(fx->fav.nskills == 1, "seed still short");
    expect(hush_favorite_list_json("hostile", fx->list, sizeof fx->list,
                                   &out_len) == HUSH_OK,
           "hostile lists after refusal");
    expect(list_entry_count(fx->list)
               == (size_t)FAV_TEST_HOSTILE_FITS + 1,
           "hostile lists seven");
}

/* Unreadable trees report IO on list and save; access restored after. */
static void check_io(fav_fixture_t *fx)
{
    char dir[HUSH_HOME_PATH_MAX] = {0};
    size_t out_len = 0;

    memset(fx->ids, 0, sizeof fx->ids);
    take_str(fx->ids[0], sizeof fx->ids[0], fx->user_id, "id fits");
    expect(hush_favorite_save("locked", "Key", fx->ids, 1) == HUSH_OK,
           "lock seed");
    expect(hush_home_loadouts_dir(dir, sizeof dir, "locked") == HUSH_OK,
           "locked dir");
    expect(chmod(dir, 0) == 0, "lock tree");
    expect(hush_favorite_list_json("locked", fx->list, sizeof fx->list,
                                   &out_len) == HUSH_ERR_IO,
           "locked list is IO");
    expect(hush_favorite_save("locked", "Key2", fx->ids, 1) == HUSH_ERR_IO,
           "locked save is IO");
    expect(chmod(dir, HUSH_HOME_DIR_MODE) == 0, "unlock tree");
    expect(hush_favorite_list_json("locked", fx->list, sizeof fx->list,
                                   &out_len) == HUSH_OK,
           "unlocked lists");
}

int main(void)
{
    fav_fixture_t fx = {0};

    setup_fixture(&fx);
    check_save_refusals(&fx);
    check_clash(&fx);
    check_list_delete(&fx);
    check_no_create(&fx);
    check_traversal(&fx);
    check_names(&fx);
    check_cap(&fx);
    check_wide(&fx);
    check_overfill(&fx);
    check_giant(&fx);
    check_edge(&fx);
    check_hostile(&fx);
    check_io(&fx);
    if (g_fail)
        return 1;
    printf("test_favorite ok\n");
    return 0;
}
