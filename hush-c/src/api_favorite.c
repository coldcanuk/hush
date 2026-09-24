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
    HUSH_LOADOUT_KEY_MAX = 12
};

/* Copies the decoded string at path into out. Refuses truncation. */
static hush_status_t hush_loadout_take_text(char *out, size_t outsz,
                                            const char *body,
                                            const char *path);

/* Reads skill_0..skill_7 into ids. Refuses a skill_8 overflow. */
static hush_status_t hush_loadout_take_skills(char ids[][HUSH_SKILL_ID_MAX],
                                              size_t *nids, const char *body);

/* Saves the posted doll as a named favorite. */
static hush_status_t hush_loadout_save(int fd, const char *body);

/* Lists favorites for the posted robot. */
static hush_status_t hush_loadout_list(int fd, const char *body);

/* Loads one favorite by name. */
static hush_status_t hush_loadout_load(int fd, const char *body);

/* Replies the loaded favorite as loadout JSON. */
static hush_status_t hush_loadout_reply_favorite(int fd,
                                                 const hush_favorite_t *fav);

/* Deletes one favorite by name; the worn doll is untouched. */
static hush_status_t hush_loadout_delete(int fd, const char *body);

hush_status_t hush_http_serve_loadout(int fd, const char *body)
{
    char action[HUSH_LOADOUT_ACTION_MAX];
    hush_status_t st;

    if (body == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_ARG);
    st = hush_loadout_take_text(action, sizeof action, body, "/action");
    if (st != HUSH_OK)
        return hush_http_reply_session(fd, st);
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

static hush_status_t hush_loadout_take_text(char *out, size_t outsz,
                                            const char *body,
                                            const char *path)
{
    hush_json_value_t value;
    hush_status_t st;

    assert(out != NULL);
    assert(body != NULL);
    assert(path != NULL);
    st = hush_json_lookup(&value, body, path);
    if (st != HUSH_OK)
        return HUSH_ERR_PARSE;
    return hush_json_decode(out, outsz, &value);
}

static hush_status_t hush_loadout_take_skills(char ids[][HUSH_SKILL_ID_MAX],
                                              size_t *nids, const char *body)
{
    size_t n = 0;
    size_t i;

    assert(ids != NULL);
    assert(nids != NULL);
    assert(body != NULL);
    *nids = 0;
    if (hush_http_json_has_key(body, "skill_8"))
        return HUSH_ERR_FULL;
    for (i = 0; i < (size_t)HUSH_SKILL_EQUIP_MAX; i++) {
        char path[HUSH_LOADOUT_KEY_MAX];
        char id[HUSH_SKILL_ID_MAX];
        hush_json_value_t value;
        hush_status_t st;

        if (snprintf(path, sizeof path, "/skill_%zu", i)
            >= (int)sizeof path)
            return HUSH_ERR_FULL;
        st = hush_json_lookup(&value, body, path);
        if (st == HUSH_ERR_NOT_FOUND)
            continue;
        if (st != HUSH_OK)
            return HUSH_ERR_PARSE;
        st = hush_json_decode(id, sizeof id, &value);
        if (st != HUSH_OK)
            return st;
        if (id[0] == '\0')
            continue;
        /* decode already refused truncation, so the copy always fits. */
        memcpy(ids[n], id, strlen(id) + 1);
        n++;
    }
    *nids = n;
    return HUSH_OK;
}

static hush_status_t hush_loadout_save(int fd, const char *body)
{
    char robot[HUSH_SKILL_ROBOT_MAX];
    char name[HUSH_FAVORITE_NAME_MAX];
    char ids[HUSH_SKILL_EQUIP_MAX][HUSH_SKILL_ID_MAX];
    char reply[HUSH_FAVORITE_FILE_MAX];
    char esc[HUSH_FAVORITE_NAME_MAX * 2];
    size_t nids = 0;
    hush_status_t st;
    int n;

    assert(body != NULL);
    st = hush_loadout_take_text(robot, sizeof robot, body, "/robot");
    if (st != HUSH_OK)
        return hush_http_reply_session(fd, st);
    st = hush_loadout_take_text(name, sizeof name, body, "/name");
    if (st != HUSH_OK)
        return hush_http_reply_session(fd, st);
    st = hush_loadout_take_skills(ids, &nids, body);
    if (st != HUSH_OK)
        return hush_http_reply_session(fd, st);
    st = hush_favorite_save(robot, name, ids, nids);
    if (st != HUSH_OK)
        return hush_http_reply_session(fd, st);
    if (hush_favorite_escape(esc, sizeof esc, name) != HUSH_OK)
        return hush_http_reply_session(fd, HUSH_ERR_FULL);
    n = snprintf(reply, sizeof reply,
                 "{\"ok\":true,\"name\":\"%s\",\"nskills\":%zu}\n", esc, nids);
    if (n < 0 || (size_t)n >= sizeof reply)
        return hush_http_reply_session(fd, HUSH_ERR_FULL);
    hush_http_reply(fd, "200 OK", "application/json", reply, (size_t)n);
    return HUSH_OK;
}

static hush_status_t hush_loadout_list(int fd, const char *body)
{
    char robot[HUSH_SKILL_ROBOT_MAX];
    char reply[HUSH_FAVORITE_JSON_MAX];
    size_t n = 0;
    hush_status_t st;

    assert(body != NULL);
    st = hush_loadout_take_text(robot, sizeof robot, body, "/robot");
    if (st != HUSH_OK)
        return hush_http_reply_session(fd, st);
    st = hush_favorite_list_json(robot, reply, sizeof reply, &n);
    if (st != HUSH_OK)
        return hush_http_reply_session(fd, st);
    hush_http_reply(fd, "200 OK", "application/json", reply, n);
    return HUSH_OK;
}

static hush_status_t hush_loadout_load(int fd, const char *body)
{
    char robot[HUSH_SKILL_ROBOT_MAX];
    char name[HUSH_FAVORITE_NAME_MAX];
    hush_favorite_t fav;
    hush_status_t st;

    assert(body != NULL);
    st = hush_loadout_take_text(robot, sizeof robot, body, "/robot");
    if (st != HUSH_OK)
        return hush_http_reply_session(fd, st);
    st = hush_loadout_take_text(name, sizeof name, body, "/name");
    if (st != HUSH_OK)
        return hush_http_reply_session(fd, st);
    st = hush_favorite_load(robot, name, &fav);
    if (st != HUSH_OK)
        return hush_http_reply_session(fd, st);
    return hush_loadout_reply_favorite(fd, &fav);
}

static hush_status_t hush_loadout_reply_favorite(int fd,
                                                 const hush_favorite_t *fav)
{
    char reply[HUSH_FAVORITE_JSON_MAX];
    char esc[HUSH_FAVORITE_NAME_MAX * 2];
    size_t off = 0;
    int n;

    assert(fav != NULL);
    if (hush_favorite_escape(esc, sizeof esc, fav->name) != HUSH_OK)
        return hush_http_reply_session(fd, HUSH_ERR_FULL);
    n = snprintf(reply, sizeof reply,
                 "{\"ok\":true,\"name\":\"%s\",\"skills\":[", esc);
    if (n < 0 || (size_t)n >= sizeof reply)
        return hush_http_reply_session(fd, HUSH_ERR_FULL);
    off = (size_t)n;
    if (hush_favorite_put_ids(reply, sizeof reply, &off, fav->skills,
                              fav->nskills) != HUSH_OK)
        return hush_http_reply_session(fd, HUSH_ERR_FULL);
    n = snprintf(reply + off, sizeof reply - off, "]}\n");
    if (n < 0 || off + (size_t)n >= sizeof reply)
        return hush_http_reply_session(fd, HUSH_ERR_FULL);
    off += (size_t)n;
    hush_http_reply(fd, "200 OK", "application/json", reply, off);
    return HUSH_OK;
}

static hush_status_t hush_loadout_delete(int fd, const char *body)
{
    char robot[HUSH_SKILL_ROBOT_MAX];
    char name[HUSH_FAVORITE_NAME_MAX];
    char reply[HUSH_FAVORITE_FILE_MAX];
    char esc[HUSH_FAVORITE_NAME_MAX * 2];
    hush_status_t st;
    int n;

    assert(body != NULL);
    st = hush_loadout_take_text(robot, sizeof robot, body, "/robot");
    if (st != HUSH_OK)
        return hush_http_reply_session(fd, st);
    st = hush_loadout_take_text(name, sizeof name, body, "/name");
    if (st != HUSH_OK)
        return hush_http_reply_session(fd, st);
    st = hush_favorite_delete(robot, name);
    if (st != HUSH_OK)
        return hush_http_reply_session(fd, st);
    if (hush_favorite_escape(esc, sizeof esc, name) != HUSH_OK)
        return hush_http_reply_session(fd, HUSH_ERR_FULL);
    n = snprintf(reply, sizeof reply, "{\"ok\":true,\"name\":\"%s\"}\n", esc);
    if (n < 0 || (size_t)n >= sizeof reply)
        return hush_http_reply_session(fd, HUSH_ERR_FULL);
    hush_http_reply(fd, "200 OK", "application/json", reply, (size_t)n);
    return HUSH_OK;
}
