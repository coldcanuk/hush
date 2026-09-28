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

/* Reads /skill_idx into ids; skips absent or empty ids. */
static hush_status_t hush_loadout_take_skill(char ids[][HUSH_SKILL_ID_MAX],
                                             size_t *nids, const char *body,
                                             size_t idx);

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
    char action[HUSH_LOADOUT_ACTION_MAX] = {0};
    hush_status_t st = HUSH_OK;

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
    hush_json_value_t value = {0};
    hush_status_t st = HUSH_OK;

    assert(out != NULL);
    assert(body != NULL);
    assert(path != NULL);
    st = hush_json_lookup(&value, body, path);
    if (st != HUSH_OK)
        return HUSH_ERR_PARSE;
    return hush_json_decode(out, outsz, &value);
}

static hush_status_t hush_loadout_take_skill(char ids[][HUSH_SKILL_ID_MAX],
                                             size_t *nids, const char *body,
                                             size_t idx)
{
    char path[HUSH_LOADOUT_KEY_MAX] = {0};
    char id[HUSH_SKILL_ID_MAX] = {0};
    hush_json_value_t value = {0};
    hush_status_t st = HUSH_OK;

    assert(ids != NULL);
    assert(nids != NULL);
    assert(body != NULL);
    assert(*nids < (size_t)HUSH_SKILL_EQUIP_MAX);
    if (snprintf(path, sizeof path, "/skill_%zu", idx) >= (int)sizeof path)
        return HUSH_ERR_FULL;
    st = hush_json_lookup(&value, body, path);
    if (st == HUSH_ERR_NOT_FOUND)
        return HUSH_OK;
    if (st != HUSH_OK)
        return HUSH_ERR_PARSE;
    st = hush_json_decode(id, sizeof id, &value);
    if (st != HUSH_OK)
        return st;
    if (id[0] == '\0')
        return HUSH_OK;
    /* Decode already refused truncation, so the copy always fits. */
    memcpy(ids[*nids], id, strlen(id) + 1);
    (*nids)++;
    return HUSH_OK;
}

static hush_status_t hush_loadout_take_skills(char ids[][HUSH_SKILL_ID_MAX],
                                              size_t *nids, const char *body)
{
    hush_status_t st = HUSH_OK;

    assert(ids != NULL);
    assert(nids != NULL);
    assert(body != NULL);
    *nids = 0;
    if (hush_http_json_has_key(body, "skill_8"))
        return HUSH_ERR_FULL;
    for (size_t i = 0; i < (size_t)HUSH_SKILL_EQUIP_MAX; i++) {
        st = hush_loadout_take_skill(ids, nids, body, i);
        if (st != HUSH_OK)
            return st;
    }
    return HUSH_OK;
}

static hush_status_t hush_loadout_save(int fd, const char *body)
{
    char robot[HUSH_SKILL_ROBOT_MAX] = {0};
    char name[HUSH_FAVORITE_NAME_MAX] = {0};
    char ids[HUSH_SKILL_EQUIP_MAX][HUSH_SKILL_ID_MAX] = {0};
    char reply[HUSH_FAVORITE_FILE_MAX] = {0};
    size_t nids = 0;
    hush_status_t st = HUSH_OK;
    int n = 0;

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
    n = snprintf(reply, sizeof reply, "{\"ok\":true,\"nskills\":%zu}\n",
                 nids);
    if (n < 0 || (size_t)n >= sizeof reply)
        return hush_http_reply_session(fd, HUSH_ERR_FULL);
    hush_http_reply(fd, "200 OK", "application/json", reply, (size_t)n);
    return HUSH_OK;
}

static hush_status_t hush_loadout_list(int fd, const char *body)
{
    char robot[HUSH_SKILL_ROBOT_MAX] = {0};
    char reply[HUSH_FAVORITE_JSON_MAX] = {0};
    size_t n = 0;
    hush_status_t st = HUSH_OK;

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
    char robot[HUSH_SKILL_ROBOT_MAX] = {0};
    char name[HUSH_FAVORITE_NAME_MAX] = {0};
    hush_favorite_t fav = {0};
    hush_status_t st = HUSH_OK;

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
    char reply[HUSH_FAVORITE_JSON_MAX] = {0};
    char esc[HUSH_FAVORITE_NAME_MAX * 2] = {0};
    size_t off = 0;
    int n = 0;

    assert(fav != NULL);
    if (hush_favorite_escape(esc, sizeof esc, fav->name) != HUSH_OK)
        return hush_http_reply_session(fd, HUSH_ERR_FULL);
    n = snprintf(reply, sizeof reply,
                 "{\"ok\":true,\"name\":\"%s\",\"skills\":[", esc);
    if (n < 0 || (size_t)n >= sizeof reply)
        return hush_http_reply_session(fd, HUSH_ERR_FULL);
    off = (size_t)n;
    if (hush_favorite_put_ids(reply, sizeof reply, &off, fav) != HUSH_OK)
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
    char robot[HUSH_SKILL_ROBOT_MAX] = {0};
    char name[HUSH_FAVORITE_NAME_MAX] = {0};
    char reply[HUSH_FAVORITE_FILE_MAX] = {0};
    hush_status_t st = HUSH_OK;
    int n = 0;

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
    n = snprintf(reply, sizeof reply, "{\"ok\":true}\n");
    if (n < 0 || (size_t)n >= sizeof reply)
        return hush_http_reply_session(fd, HUSH_ERR_FULL);
    hush_http_reply(fd, "200 OK", "application/json", reply, (size_t)n);
    return HUSH_OK;
}
