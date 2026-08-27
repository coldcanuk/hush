/* tests/test_skill.c: three-scope catalog, forge writer, voices. */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static int catalog_has(const hush_skill_catalog_t *cat, const char *id)
{
    size_t i;

    for (i = 0; i < cat->nskills; ++i) {
        if (strcmp(cat->skills[i].id, id) == 0)
            return 1;
    }
    return 0;
}

int main(void)
{
    static hush_skill_catalog_t cat;
    static char json[HUSH_SKILL_JSON_MAX];
    hush_skill_forge_in_t in;
    char home[192];
    char id[HUSH_SKILL_ID_MAX];
    size_t n = 0;

    snprintf(home, sizeof(home), "/tmp/hush-skill-test-%d", (int)getpid());
    unsetenv("HUSH_CONFIG_DIR");
    if (setenv("HUSH_HOME", home, 1) != 0)
        return 1;
    expect(hush_skill_load_catalog(&cat) == HUSH_OK, "load");
    expect(catalog_has(&cat, HUSH_SKILL_FORGE_ID), "system forge-skill");
    expect(hush_skill_is_voice("alloy"), "alloy voice");
    expect(!hush_skill_is_voice("nope"), "bad voice");
    memset(&in, 0, sizeof(in));
    memcpy(in.name, "Joke Book", 10);
    memcpy(in.summary, "Tell short jokes.", 18);
    memcpy(in.body, "Reply with one joke.", 21);
    memcpy(in.scope, HUSH_SKILL_SCOPE_USER, sizeof(HUSH_SKILL_SCOPE_USER));
    expect(hush_skill_forge(&in, id, sizeof(id)) == HUSH_OK, "forge user");
    expect(strcmp(id, "user:joke-book") == 0, "user id");
    memset(&in, 0, sizeof(in));
    memcpy(in.name, "Perimeter", 10);
    memcpy(in.summary, "Watch the wire.", 16);
    memcpy(in.body, "Challenge strangers.", 21);
    memcpy(in.scope, HUSH_SKILL_SCOPE_ROBOT, sizeof(HUSH_SKILL_SCOPE_ROBOT));
    memcpy(in.robot, "happy", 6);
    expect(hush_skill_forge(&in, id, sizeof(id)) == HUSH_OK, "forge robot");
    expect(strcmp(id, "robot:happy:perimeter") == 0, "robot id");
    memcpy(in.scope, HUSH_SKILL_SCOPE_SYSTEM, sizeof(HUSH_SKILL_SCOPE_SYSTEM));
    expect(hush_skill_forge(&in, id, sizeof(id)) == HUSH_ERR_DENIED,
           "no system forge");
    expect(hush_skill_load_catalog(&cat) == HUSH_OK, "reload");
    expect(catalog_has(&cat, "user:joke-book"), "user in catalog");
    expect(catalog_has(&cat, "robot:happy:perimeter"), "robot in catalog");
    expect(hush_skill_format_json(&cat, 1, json, sizeof(json), &n) == HUSH_OK,
           "json");
    expect(strstr(json, "\"scopes\":[\"system\",\"user\",\"robot\"]") != NULL,
           "three scopes");
    expect(strstr(json, HUSH_SKILL_FORGE_ID) != NULL, "forge id json");
    expect(strstr(json, "\"scope\":\"system\"") != NULL, "system scope json");
    expect(strstr(json, "\"scope\":\"user\"") != NULL, "user scope json");
    expect(strstr(json, "\"scope\":\"robot\"") != NULL, "robot scope json");
    expect(strstr(json, "\"alloy\"") != NULL, "voice listed");
    if (g_fail)
        return 1;
    printf("test_skill ok\n");
    return 0;
}
