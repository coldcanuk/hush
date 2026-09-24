/* api_favorite.c: owns POST /api/loadout favorite routes (PE-4 v1). */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "hush_favorite.h"
#include "hush_http_internal.h"
#include "hush_json.h"
#include "hush_skill.h"

enum {
    HUSH_LOADOUT_ACTION_MAX = 16,
    HUSH_LOADOUT_ROBOT_MAX = 64,
    HUSH_LOADOUT_BODY_MAX = 4096,
    HUSH_LOADOUT_REPLY_MAX = 16384
};

/* Reads skill_0..skill_7 into ids; writes the count into *nids. */
static void hush_loadout_take_skills(char ids[][HUSH_SKILL_ID_MAX],
                                     size_t *nids, const char *body);

/* Saves the posted doll as a named favorite. */
static hush_status_t hush_loadout_save(int fd, const char *body);

/* Lists favorites for the posted robot. */
static hush_status_t hush_loadout_list(int fd, const char *body);

/* Loads one favorite by name. */
static hush_status_t hush_loadout_load(int fd, const char *body);

/* Deletes one favorite by name; the worn doll is untouched. */
static hush_status_t hush_loadout_delete(int fd, const char *body);

hush_status_t hush_http_serve_loadout(int fd, const char *body)
{
    char action[HUSH_LOADOUT_ACTION_MAX];

    if (body == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_ARG);
    if (!hush_http_json_field(body, "action", action, sizeof(action)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    if (strcmp(action, "save") == 0)
        return hush_loadout_save(fd, body);
    if (strcmp(action, "list") == 0)
        return hush_loadout_list(fd, body);
    if (strcmp(action, "load") == 0)
        return hush_loadout_load(fd, body);
    if (strcmp(action, "delete") == 0)
        return hush_loadout_delete(fd, body);
    return hush_http_reply_session(fd, HUSH_ERR_PARSE);
}

static void hush_loadout_take_skills(char ids[][HUSH_SKILL_ID_MAX],
                                     size_t *nids, const char *body)
{
    char key[24];
    size_t i;

    assert(ids != NULL);
    assert(nids != NULL);
    assert(body != NULL);
    *nids = 0;
    for (i = 0; i < (size_t)HUSH_SKILL_EQUIP_MAX; ++i) {
        if (snprintf(key, sizeof(key), "skill_%zu", i) >= (int)sizeof(key))
            return;
        if (!hush_http_json_field(body, key, ids[*nids],
                                  sizeof(ids[0])))
            continue;
        if (ids[*nids][0] == '\0')
            continue;
        (*nids)++;
    }
}

static hush_status_t hush_loadout_save(int fd, const char *body)
{
    char robot[HUSH_LOADOUT_ROBOT_MAX];
    char name[HUSH_FAVORITE_NAME_MAX];
    char ids[HUSH_SKILL_EQUIP_MAX][HUSH_SKILL_ID_MAX];
    size_t nids = 0;
    char reply[HUSH_LOADOUT_BODY_MAX];
    hush_status_t st;
    int n;

    assert(body != NULL);
    if (!hush_http_json_field(body, "robot", robot, sizeof(robot)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    if (!hush_http_json_field(body, "name", name, sizeof(name)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    hush_loadout_take_skills(ids, &nids, body);
    st = hush_favorite_save(robot, name, ids, nids);
    if (st != HUSH_OK)
        return hush_http_reply_session(fd, st);
    {
        char esc[HUSH_FAVORITE_NAME_MAX * 2];

        hush_json_escape(name, esc, sizeof(esc));
        n = snprintf(reply, sizeof(reply),
                     "{\"ok\":true,\"name\":\"%s\",\"nskills\":%zu}\n",
                     esc, nids);
    }
    if (n < 0 || (size_t)n >= sizeof(reply))
        return hush_http_reply_session(fd, HUSH_ERR_FULL);
    hush_http_reply(fd, "200 OK", "application/json", reply, (size_t)n);
    return HUSH_OK;
}

static hush_status_t hush_loadout_list(int fd, const char *body)
{
    char robot[HUSH_LOADOUT_ROBOT_MAX];
    char reply[HUSH_LOADOUT_REPLY_MAX];
    size_t n = 0;
    hush_status_t st;

    assert(body != NULL);
    if (!hush_http_json_field(body, "robot", robot, sizeof(robot)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    st = hush_favorite_list_json(robot, reply, sizeof(reply), &n);
    if (st != HUSH_OK)
        return hush_http_reply_session(fd, st);
    hush_http_reply(fd, "200 OK", "application/json", reply, n);
    return HUSH_OK;
}

static hush_status_t hush_loadout_load(int fd, const char *body)
{
    char robot[HUSH_LOADOUT_ROBOT_MAX];
    char name[HUSH_FAVORITE_NAME_MAX];
    char reply[HUSH_LOADOUT_REPLY_MAX];
    hush_favorite_t fav;
    hush_status_t st;
    size_t off = 0;
    size_t i;
    int n;

    assert(body != NULL);
    if (!hush_http_json_field(body, "robot", robot, sizeof(robot)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    if (!hush_http_json_field(body, "name", name, sizeof(name)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    st = hush_favorite_load(robot, name, &fav);
    if (st != HUSH_OK)
        return hush_http_reply_session(fd, st);
    {
        char esc[HUSH_FAVORITE_NAME_MAX * 2];

        hush_json_escape(fav.name, esc, sizeof(esc));
        n = snprintf(reply, sizeof(reply),
                     "{\"ok\":true,\"name\":\"%s\",\"skills\":[", esc);
        if (n < 0 || (size_t)n >= sizeof(reply))
            return hush_http_reply_session(fd, HUSH_ERR_FULL);
        off = (size_t)n;
    }
    for (i = 0; i < fav.nskills; i++) {
        char esc[HUSH_SKILL_ID_MAX * 2];

        hush_json_escape(fav.skills[i], esc, sizeof(esc));
        n = snprintf(reply + off, sizeof(reply) - off, "%s\"%s\"",
                     (i == 0) ? "" : ",", esc);
        if (n < 0 || off + (size_t)n >= sizeof(reply))
            return hush_http_reply_session(fd, HUSH_ERR_FULL);
        off += (size_t)n;
    }
    n = snprintf(reply + off, sizeof(reply) - off, "]}\n");
    if (n < 0 || off + (size_t)n >= sizeof(reply))
        return hush_http_reply_session(fd, HUSH_ERR_FULL);
    off += (size_t)n;
    hush_http_reply(fd, "200 OK", "application/json", reply, off);
    return HUSH_OK;
}

static hush_status_t hush_loadout_delete(int fd, const char *body)
{
    char robot[HUSH_LOADOUT_ROBOT_MAX];
    char name[HUSH_FAVORITE_NAME_MAX];
    char reply[HUSH_LOADOUT_BODY_MAX];
    hush_status_t st;
    int n;

    assert(body != NULL);
    if (!hush_http_json_field(body, "robot", robot, sizeof(robot)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    if (!hush_http_json_field(body, "name", name, sizeof(name)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    st = hush_favorite_delete(robot, name);
    if (st != HUSH_OK)
        return hush_http_reply_session(fd, st);
    {
        char esc[HUSH_FAVORITE_NAME_MAX * 2];

        hush_json_escape(name, esc, sizeof(esc));
        n = snprintf(reply, sizeof(reply), "{\"ok\":true,\"name\":\"%s\"}\n",
                     esc);
    }
    if (n < 0 || (size_t)n >= sizeof(reply))
        return hush_http_reply_session(fd, HUSH_ERR_FULL);
    hush_http_reply(fd, "200 OK", "application/json", reply, (size_t)n);
    return HUSH_OK;
}
