/* api_favorite.c: owns POST /api/loadout favorite routes (PE-4 v1). */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hush_favorite.h"
#include "hush_home.h"
#include "hush_http_internal.h"
#include "hush_json.h"
#include "hush_skill.h"

/* Favorite refusal reasons: each is the whole 400 body (plus a newline).
 * tests/check_reasons.py pins every string; reword both together. */
#define HUSH_LOADOUT_WHY_ACTION "Loadout action must be save, list, load or delete."
#define HUSH_LOADOUT_WHY_ROBOT \
    "Robot must be a slug: a-z, 0-9, - or _ (1-%d characters)."
#define HUSH_LOADOUT_WHY_NAME \
    "Favorite names use letters, digits, spaces, - or _ " \
    "(1-%d characters, at least one letter or digit)."
#define HUSH_LOADOUT_WHY_IDS "Skill ids must be strings of at most %d characters."
#define HUSH_LOADOUT_WHY_EMPTY "A favorite needs at least one skill."
#define HUSH_LOADOUT_WHY_MANY "A favorite holds at most %d skills."
#define HUSH_LOADOUT_WHY_TWICE "Skill %s is listed twice."
#define HUSH_LOADOUT_WHY_CLASH \
    "That name clashes with a different saved favorite; pick another name."
#define HUSH_LOADOUT_WHY_FULL \
    "This robot already has %d favorites; delete one first."
#define HUSH_LOADOUT_WHY_MISSING "No favorite with that name for this robot."
#define HUSH_LOADOUT_WHY_CORRUPT "That saved favorite file is corrupt."

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

/* Reads /robot into robot and checks the robot slug law. Fails ARG. */
static hush_status_t hush_loadout_take_robot(char *robot, size_t robotsz,
                                             const char *body);

/* Reads /name into name and checks the favorite name law. Fails PARSE. */
static hush_status_t hush_loadout_take_name(char *name, size_t namesz,
                                            const char *body);

/* Replies the robot-slug refusal for st. */
static hush_status_t hush_loadout_refuse_robot(int fd, hush_status_t st);

/* Replies the favorite-name refusal for st. */
static hush_status_t hush_loadout_refuse_name(int fd, hush_status_t st);

/* Replies the refusal for a failed skill_N read. */
static hush_status_t hush_loadout_refuse_ids(int fd, hush_status_t st,
                                             const char *body);

/* Writes why for a refused save of ids by status; generic when unknown. */
static void hush_loadout_save_why(char *why, size_t whysz, hush_status_t st,
                                  const char *robot,
                                  char ids[][HUSH_SKILL_ID_MAX], size_t nids);

/* Writes why for the first unknown, cross-slug, or repeated id. 1 on match. */
static int hush_loadout_ids_why(char *why, size_t whysz, const char *robot,
                                char ids[][HUSH_SKILL_ID_MAX], size_t nids);

/* Writes why for a refused load or delete by status. */
static void hush_loadout_find_why(char *why, size_t whysz, hush_status_t st);

/* True when ids[idx] repeats an earlier id. */
static int hush_loadout_seen(char ids[][HUSH_SKILL_ID_MAX], size_t idx);

/* Writes fmt into why with id echoed through hush_http_safe_id. */
static void hush_loadout_why_id(char *why, size_t whysz, const char *fmt,
                                const char *id);

hush_status_t hush_http_serve_loadout(int fd, const char *body)
{
    char action[HUSH_LOADOUT_ACTION_MAX] = {0};
    hush_status_t st = HUSH_OK;

    if (body == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_ARG);
    st = hush_loadout_take_text(action, sizeof action, body, "/action");
    if (st != HUSH_OK)
        return hush_http_reply_refused(fd, st, HUSH_LOADOUT_WHY_ACTION);
    if (strcmp(action, "save") == 0)
        return hush_loadout_save(fd, body);
    if (strcmp(action, "list") == 0)
        return hush_loadout_list(fd, body);
    if (strcmp(action, "load") == 0)
        return hush_loadout_load(fd, body);
    if (strcmp(action, "delete") == 0)
        return hush_loadout_delete(fd, body);
    return hush_http_reply_refused(fd, HUSH_ERR_PARSE, HUSH_LOADOUT_WHY_ACTION);
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
    char why[HUSH_HTTP_WHY_MAX] = {0};
    size_t nids = 0;
    hush_status_t st = HUSH_OK;
    int n = 0;

    assert(body != NULL);
    st = hush_loadout_take_robot(robot, sizeof robot, body);
    if (st != HUSH_OK)
        return hush_loadout_refuse_robot(fd, st);
    st = hush_loadout_take_name(name, sizeof name, body);
    if (st != HUSH_OK)
        return hush_loadout_refuse_name(fd, st);
    st = hush_loadout_take_skills(ids, &nids, body);
    if (st != HUSH_OK)
        return hush_loadout_refuse_ids(fd, st, body);
    st = hush_favorite_save(robot, name, ids, nids);
    if (st != HUSH_OK) {
        hush_loadout_save_why(why, sizeof why, st, robot, ids, nids);
        return hush_http_reply_refused(fd, st, why);
    }
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
    st = hush_loadout_take_robot(robot, sizeof robot, body);
    if (st != HUSH_OK)
        return hush_loadout_refuse_robot(fd, st);
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
    char why[HUSH_HTTP_WHY_MAX] = {0};
    hush_favorite_t fav = {0};
    hush_status_t st = HUSH_OK;

    assert(body != NULL);
    st = hush_loadout_take_robot(robot, sizeof robot, body);
    if (st != HUSH_OK)
        return hush_loadout_refuse_robot(fd, st);
    st = hush_loadout_take_name(name, sizeof name, body);
    if (st != HUSH_OK)
        return hush_loadout_refuse_name(fd, st);
    st = hush_favorite_load(robot, name, &fav);
    if (st != HUSH_OK) {
        hush_loadout_find_why(why, sizeof why, st);
        return hush_http_reply_refused(fd, st, why);
    }
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
    char why[HUSH_HTTP_WHY_MAX] = {0};
    hush_status_t st = HUSH_OK;
    int n = 0;

    assert(body != NULL);
    st = hush_loadout_take_robot(robot, sizeof robot, body);
    if (st != HUSH_OK)
        return hush_loadout_refuse_robot(fd, st);
    st = hush_loadout_take_name(name, sizeof name, body);
    if (st != HUSH_OK)
        return hush_loadout_refuse_name(fd, st);
    st = hush_favorite_delete(robot, name);
    if (st != HUSH_OK) {
        hush_loadout_find_why(why, sizeof why, st);
        return hush_http_reply_refused(fd, st, why);
    }
    n = snprintf(reply, sizeof reply, "{\"ok\":true}\n");
    if (n < 0 || (size_t)n >= sizeof reply)
        return hush_http_reply_session(fd, HUSH_ERR_FULL);
    hush_http_reply(fd, "200 OK", "application/json", reply, (size_t)n);
    return HUSH_OK;
}

static hush_status_t hush_loadout_take_robot(char *robot, size_t robotsz,
                                             const char *body)
{
    hush_status_t st = HUSH_OK;

    assert(robot != NULL && robotsz > 0 && body != NULL);
    st = hush_loadout_take_text(robot, robotsz, body, "/robot");
    if (st != HUSH_OK)
        return st;
    return hush_home_is_robot_slug(robot) ? HUSH_OK : HUSH_ERR_ARG;
}

static hush_status_t hush_loadout_take_name(char *name, size_t namesz,
                                            const char *body)
{
    hush_status_t st = HUSH_OK;

    assert(name != NULL && namesz > 0 && body != NULL);
    st = hush_loadout_take_text(name, namesz, body, "/name");
    if (st != HUSH_OK)
        return st;
    return hush_favorite_name_ok(name) ? HUSH_OK : HUSH_ERR_PARSE;
}

static hush_status_t hush_loadout_refuse_robot(int fd, hush_status_t st)
{
    char why[HUSH_HTTP_WHY_MAX] = {0};

    (void)snprintf(why, sizeof why, HUSH_LOADOUT_WHY_ROBOT,
                   (int)HUSH_FAVORITE_ROBOT_LEN);
    return hush_http_reply_refused(fd, st, why);
}

static hush_status_t hush_loadout_refuse_name(int fd, hush_status_t st)
{
    char why[HUSH_HTTP_WHY_MAX] = {0};

    (void)snprintf(why, sizeof why, HUSH_LOADOUT_WHY_NAME,
                   (int)HUSH_FAVORITE_NAME_LEN);
    return hush_http_reply_refused(fd, st, why);
}

static hush_status_t hush_loadout_refuse_ids(int fd, hush_status_t st,
                                             const char *body)
{
    char why[HUSH_HTTP_WHY_MAX] = {0};

    assert(body != NULL);
    if (st == HUSH_ERR_FULL && hush_http_json_has_key(body, "skill_8"))
        (void)snprintf(why, sizeof why, HUSH_LOADOUT_WHY_MANY,
                       (int)HUSH_SKILL_EQUIP_MAX);
    else
        (void)snprintf(why, sizeof why, HUSH_LOADOUT_WHY_IDS,
                       (int)HUSH_FAVORITE_ID_LEN);
    return hush_http_reply_refused(fd, st, why);
}

static void hush_loadout_save_why(char *why, size_t whysz, hush_status_t st,
                                  const char *robot,
                                  char ids[][HUSH_SKILL_ID_MAX], size_t nids)
{
    assert(why != NULL && whysz > 0);
    assert(robot != NULL && ids != NULL);
    why[0] = '\0';
    /* Robot, name, and skill_8 were refused before save; what is left
     * maps one status to one save rule (see hush_favorite_save). */
    if (st == HUSH_ERR_DENIED && nids == 0)
        (void)snprintf(why, whysz, "%s", HUSH_LOADOUT_WHY_EMPTY);
    else if (st == HUSH_ERR_DENIED && !hush_loadout_ids_why(why, whysz, robot,
                                                             ids, nids))
        (void)snprintf(why, whysz, "%s", HUSH_LOADOUT_WHY_CLASH);
    else if (st == HUSH_ERR_FULL)
        (void)snprintf(why, whysz, HUSH_LOADOUT_WHY_FULL,
                       (int)HUSH_FAVORITE_COUNT_MAX);
    else if (st == HUSH_ERR_PARSE)
        (void)snprintf(why, whysz, "%s", HUSH_LOADOUT_WHY_CORRUPT);
}

static int hush_loadout_ids_why(char *why, size_t whysz, const char *robot,
                                char ids[][HUSH_SKILL_ID_MAX], size_t nids)
{
    /* Heap frame like hush_favorite_check_ids: the catalog is ~120 KB. */
    hush_skill_catalog_t *cat = malloc(sizeof *cat);
    const hush_skill_t *skill = NULL;
    int hit = 0;

    assert(why != NULL && robot != NULL && ids != NULL);
    if (cat == NULL)
        return 0;
    hush_skill_init_catalog(cat);
    if (hush_skill_load_catalog(cat) != HUSH_OK)
        nids = 0;
    for (size_t i = 0; !hit && i < nids; i++) {
        skill = hush_skill_find(cat, ids[i]);
        hit = 1;
        if (skill == NULL)
            hush_loadout_why_id(why, whysz, HUSH_HTTP_WHY_SKILL, ids[i]);
        else if (!hush_skill_robot_ok(skill, robot))
            hush_loadout_why_id(why, whysz, HUSH_HTTP_WHY_OWNED, ids[i]);
        else if (i > 0 && hush_loadout_seen(ids, i))
            hush_loadout_why_id(why, whysz, HUSH_LOADOUT_WHY_TWICE, ids[i]);
        else
            hit = 0;
    }
    free(cat);
    return hit;
}

static int hush_loadout_seen(char ids[][HUSH_SKILL_ID_MAX], size_t idx)
{
    assert(ids != NULL);
    for (size_t j = 0; j < idx; j++) {
        if (strcmp(ids[idx], ids[j]) == 0)
            return 1;
    }
    return 0;
}

static void hush_loadout_find_why(char *why, size_t whysz, hush_status_t st)
{
    assert(why != NULL && whysz > 0);
    why[0] = '\0';
    if (st == HUSH_ERR_NOT_FOUND)
        (void)snprintf(why, whysz, "%s", HUSH_LOADOUT_WHY_MISSING);
    else if (st == HUSH_ERR_PARSE)
        (void)snprintf(why, whysz, "%s", HUSH_LOADOUT_WHY_CORRUPT);
}

static void hush_loadout_why_id(char *why, size_t whysz, const char *fmt,
                                const char *id)
{
    char safe[HUSH_HTTP_WHY_ID_MAX];

    assert(why != NULL && whysz > 0 && fmt != NULL);
    hush_http_safe_id(safe, sizeof safe, id);
    (void)snprintf(why, whysz, fmt, safe);
}
