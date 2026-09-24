/* hush_favorite.c: owns per-robot favorite loadout files (PE-4 v1). */

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hush_favorite.h"
#include "hush_home.h"
#include "hush_json.h"
#include "hush_skill.h"

enum {
    HUSH_FAVORITE_SUFFIX_LEN = 5,
    HUSH_FAVORITE_TMP_TAIL_LEN = 4
};

#define HUSH_FAVORITE_SUFFIX ".json"
#define HUSH_FAVORITE_SLUG_CHARS "abcdefghijklmnopqrstuvwxyz0123456789-_"

/* True when name is a non-empty file-safe favorite slug. */
static int hush_favorite_is_slug(const char *slug);

/* Writes lowercase a-z0-9- slug of name into dst. */
static void hush_favorite_slugify(char *dst, size_t dstsz, const char *name);

/* Trims leading/trailing spaces of text in place. */
static void hush_favorite_trim(char *text);

/* True when trimmed name holds 1..47 printable chars, no quotes. */
static int hush_favorite_name_ok(const char *name);

/* mkdir 0700. EEXIST is success. */
static hush_status_t hush_favorite_mkdir(const char *path);

/* Ensures robots/<robot>/loadouts exists; writes the dir into out. */
static hush_status_t hush_favorite_ensure_dir(char *out, size_t outsz,
                                              const char *robot);

/* Writes robots/<robot>/loadouts/<slug>.json into out. */
static hush_status_t hush_favorite_path(char *out, size_t outsz,
                                        const char *robot, const char *name);

/* Copies src into dst. Empty on overflow. */
static void hush_favorite_copy(char *dst, size_t dstsz, const char *src);

/* Joins a/b into out. Empty on overflow. */
static void hush_favorite_join(char *out, size_t outsz,
                               const char *a, const char *b);

/* Writes text to path through a temp file and rename. */
static hush_status_t hush_favorite_write_text(const char *path,
                                              const char *text);

/* Reads path into out (capped). Rejects NUL/truncation. */
static hush_status_t hush_favorite_read_text(char *out, size_t outsz,
                                             const char *path);

/* Refuses unknown or cross-slug ids for robot. */
static hush_status_t hush_favorite_check_ids(const char *robot,
                                             char ids[][HUSH_SKILL_ID_MAX],
                                             size_t nids);

/* Formats one save file body into out. */
static hush_status_t hush_favorite_format_body(char *out, size_t outsz,
                                               const char *robot,
                                               const char *name,
                                               char ids[][HUSH_SKILL_ID_MAX],
                                               size_t nids);

/* Parses a save file body into out. */
static hush_status_t hush_favorite_parse_body(hush_favorite_t *out,
                                              const char *body);

/* Appends one {"name":..,"skills":[..]} object at out+*off. */
static hush_status_t hush_favorite_put_one(char *out, size_t outsz,
                                           size_t *off,
                                           const hush_favorite_t *fav,
                                           int first);

/* True when entry is a bounded <slug>.json favorite file. */
static int hush_favorite_is_list_entry(const char *entry);

/* Reads dir/entry into the list at out+*off; skips corrupt files. */
static hush_status_t hush_favorite_append_entry(char *out, size_t outsz,
                                                size_t *off, const char *dir,
                                                const char *entry,
                                                int *first);

/* Closes the list JSON array; writes out_len when provided. */
static hush_status_t hush_favorite_list_close(char *out, size_t outsz,
                                              size_t *off, size_t *out_len);

hush_status_t hush_favorite_save(const char *robot, const char *name,
                                 char ids[][HUSH_SKILL_ID_MAX], size_t nids)
{
    char clean[HUSH_FAVORITE_NAME_MAX];
    char path[HUSH_HOME_PATH_MAX];
    char body[HUSH_FAVORITE_FILE_MAX];
    hush_status_t st;

    if (robot == NULL || name == NULL || ids == NULL)
        return HUSH_ERR_ARG;
    hush_favorite_copy(clean, sizeof(clean), name);
    hush_favorite_trim(clean);
    if (!hush_favorite_name_ok(clean))
        return HUSH_ERR_PARSE;
    if (nids < (size_t)HUSH_SKILL_EQUIP_LOW ||
        nids > (size_t)HUSH_SKILL_EQUIP_MAX)
        return HUSH_ERR_DENIED;
    st = hush_favorite_check_ids(robot, ids, nids);
    if (st != HUSH_OK)
        return st;
    st = hush_favorite_format_body(body, sizeof(body), robot, clean,
                                   ids, nids);
    if (st != HUSH_OK)
        return st;
    st = hush_favorite_path(path, sizeof(path), robot, clean);
    if (st != HUSH_OK)
        return st;
    return hush_favorite_write_text(path, body);
}

hush_status_t hush_favorite_load(const char *robot, const char *name,
                                 hush_favorite_t *out)
{
    char path[HUSH_HOME_PATH_MAX];
    char body[HUSH_FAVORITE_FILE_MAX];
    hush_status_t st;

    if (robot == NULL || name == NULL || out == NULL)
        return HUSH_ERR_ARG;
    memset(out, 0, sizeof(*out));
    st = hush_favorite_path(path, sizeof(path), robot, name);
    if (st != HUSH_OK)
        return st;
    st = hush_favorite_read_text(body, sizeof(body), path);
    if (st != HUSH_OK)
        return st;
    return hush_favorite_parse_body(out, body);
}

hush_status_t hush_favorite_delete(const char *robot, const char *name)
{
    char path[HUSH_HOME_PATH_MAX];
    hush_status_t st;

    if (robot == NULL || name == NULL)
        return HUSH_ERR_ARG;
    st = hush_favorite_path(path, sizeof(path), robot, name);
    if (st != HUSH_OK)
        return st;
    if (unlink(path) != 0)
        return errno == ENOENT ? HUSH_ERR_NOT_FOUND : HUSH_ERR_IO;
    return HUSH_OK;
}

hush_status_t hush_favorite_list_json(const char *robot, char *out,
                                      size_t outsz, size_t *out_len)
{
    char dir[HUSH_HOME_PATH_MAX];
    char esc[HUSH_FAVORITE_NAME_MAX * 2];
    DIR *dp;
    struct dirent *ent;
    size_t n = 0;
    size_t off = 0;
    int nn = 0;
    int first = 1;

    if (robot == NULL || out == NULL || outsz == 0)
        return HUSH_ERR_ARG;
    if (hush_home_loadouts_dir(dir, sizeof(dir), robot) != HUSH_OK)
        return HUSH_ERR_ARG;
    hush_json_escape(robot, esc, sizeof(esc));
    nn = snprintf(out, outsz, "{\"ok\":true,\"robot\":\"%s\",\"favorites\":[",
                  esc);
    if (nn < 0 || (size_t)nn >= outsz)
        return HUSH_ERR_FULL;
    off = (size_t)nn;
    dp = opendir(dir);
    if (dp == NULL)
        return hush_favorite_list_close(out, outsz, &off, out_len);
    while ((ent = readdir(dp)) != NULL &&
           n < (size_t)HUSH_FAVORITE_COUNT_MAX) {
        if (!hush_favorite_is_list_entry(ent->d_name))
            continue;
        n++;
        if (hush_favorite_append_entry(out, outsz, &off, dir,
                                       ent->d_name, &first) != HUSH_OK) {
            closedir(dp);
            return HUSH_ERR_FULL;
        }
    }
    closedir(dp);
    return hush_favorite_list_close(out, outsz, &off, out_len);
}

static int hush_favorite_is_slug(const char *slug)
{
    assert(slug != NULL);
    if (slug[0] == '\0' || strlen(slug) >= (size_t)HUSH_FAVORITE_SLUG_MAX)
        return 0;
    return strspn(slug, HUSH_FAVORITE_SLUG_CHARS) == strlen(slug);
}

static void hush_favorite_slugify(char *dst, size_t dstsz, const char *name)
{
    size_t i = 0;
    size_t o = 0;
    unsigned char c;
    int dash = 0;

    assert(dst != NULL);
    assert(dstsz > 0);
    dst[0] = '\0';
    if (name == NULL)
        return;
    while (name[i] != '\0' && o + 1 < dstsz) {
        c = (unsigned char)name[i++];
        if (isalnum(c)) {
            dst[o++] = (char)tolower(c);
            dash = 0;
            continue;
        }
        if ((c == ' ' || c == '-' || c == '_') && o > 0 && !dash) {
            dst[o++] = '-';
            dash = 1;
        }
    }
    if (o > 0 && dst[o - 1] == '-')
        o--;
    dst[o] = '\0';
}

static void hush_favorite_trim(char *text)
{
    size_t n;
    size_t start = 0;

    assert(text != NULL);
    n = strlen(text);
    while (start < n && text[start] == ' ')
        start++;
    while (n > start && text[n - 1] == ' ')
        n--;
    if (start > 0)
        memmove(text, text + start, n - start);
    text[n - start] = '\0';
}

static int hush_favorite_name_ok(const char *name)
{
    size_t i;
    size_t n;

    assert(name != NULL);
    n = strlen(name);
    if (n == 0 || n >= (size_t)HUSH_FAVORITE_NAME_MAX)
        return 0;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)name[i];

        if (c < (unsigned char)' ' || c == (unsigned char)'"'
            || c == (unsigned char)'\\')
            return 0;
    }
    return 1;
}

static hush_status_t hush_favorite_mkdir(const char *path)
{
    assert(path != NULL);
    if (path[0] == '\0')
        return HUSH_ERR_IO;
    if (mkdir(path, 0700) != 0 && errno != EEXIST)
        return HUSH_ERR_IO;
    return HUSH_OK;
}

static hush_status_t hush_favorite_ensure_dir(char *out, size_t outsz,
                                              const char *robot)
{
    char root[HUSH_HOME_PATH_MAX];
    char robots[HUSH_HOME_PATH_MAX];
    char scoped[HUSH_HOME_PATH_MAX];
    hush_status_t st;

    assert(out != NULL);
    assert(robot != NULL);
    out[0] = '\0';
    hush_home_root(root, sizeof(root));
    if (root[0] == '\0')
        return HUSH_ERR_IO;
    hush_favorite_join(robots, sizeof(robots), root, HUSH_HOME_DIR_ROBOTS);
    hush_favorite_join(scoped, sizeof(scoped), robots, robot);
    if (scoped[0] == '\0')
        return HUSH_ERR_FULL;
    st = hush_favorite_mkdir(robots);
    if (st != HUSH_OK)
        return st;
    st = hush_favorite_mkdir(scoped);
    if (st != HUSH_OK)
        return st;
    hush_favorite_join(out, outsz, scoped, HUSH_HOME_DIR_LOADOUTS);
    if (out[0] == '\0')
        return HUSH_ERR_FULL;
    return hush_favorite_mkdir(out);
}

static hush_status_t hush_favorite_path(char *out, size_t outsz,
                                        const char *robot, const char *name)
{
    char dir[HUSH_HOME_PATH_MAX];
    char slug[HUSH_FAVORITE_SLUG_MAX];
    char trimmed[HUSH_FAVORITE_NAME_MAX];
    hush_status_t st;

    assert(out != NULL);
    assert(robot != NULL);
    assert(name != NULL);
    out[0] = '\0';
    hush_favorite_copy(trimmed, sizeof(trimmed), name);
    hush_favorite_trim(trimmed);
    if (!hush_favorite_name_ok(trimmed))
        return HUSH_ERR_PARSE;
    hush_favorite_slugify(slug, sizeof(slug), trimmed);
    if (slug[0] == '\0')
        return HUSH_ERR_PARSE;
    st = hush_favorite_ensure_dir(dir, sizeof(dir), robot);
    if (st != HUSH_OK)
        return st;
    hush_favorite_join(out, outsz, dir, slug);
    if (out[0] == '\0')
        return HUSH_ERR_FULL;
    if (strlen(out) + (size_t)HUSH_FAVORITE_SUFFIX_LEN >= outsz)
        return HUSH_ERR_FULL;
    strcat(out, HUSH_FAVORITE_SUFFIX);
    return HUSH_OK;
}

static void hush_favorite_copy(char *dst, size_t dstsz, const char *src)
{
    size_t n;

    assert(dst != NULL);
    assert(dstsz > 0);
    dst[0] = '\0';
    if (src == NULL)
        return;
    n = strlen(src);
    if (n + 1 > dstsz)
        return;
    memcpy(dst, src, n + 1);
}

static void hush_favorite_join(char *out, size_t outsz,
                               const char *a, const char *b)
{
    int n;

    assert(out != NULL);
    assert(outsz > 0);
    out[0] = '\0';
    if (a == NULL || b == NULL || a[0] == '\0')
        return;
    n = snprintf(out, outsz, "%s/%s", a, b);
    if (n <= 0 || (size_t)n >= outsz)
        out[0] = '\0';
}

static hush_status_t hush_favorite_write_text(const char *path,
                                              const char *text)
{
    char tmp[HUSH_HOME_PATH_MAX];
    FILE *fp;
    size_t n;

    assert(path != NULL);
    assert(text != NULL);
    if (strlen(path) + (size_t)HUSH_FAVORITE_TMP_TAIL_LEN >= sizeof(tmp))
        return HUSH_ERR_FULL;
    strcpy(tmp, path);
    strcat(tmp, ".tmp");
    fp = fopen(tmp, "w");
    if (fp == NULL)
        return HUSH_ERR_IO;
    n = strlen(text);
    if (n > 0 && fwrite(text, 1, n, fp) != n) {
        fclose(fp);
        return HUSH_ERR_IO;
    }
    if (fclose(fp) != 0)
        return HUSH_ERR_IO;
    if (rename(tmp, path) != 0)
        return HUSH_ERR_IO;
    return HUSH_OK;
}

static hush_status_t hush_favorite_read_text(char *out, size_t outsz,
                                             const char *path)
{
    FILE *fp;
    size_t n;

    assert(out != NULL);
    assert(outsz > 0);
    assert(path != NULL);
    fp = fopen(path, "rb");
    if (fp == NULL)
        return errno == ENOENT ? HUSH_ERR_NOT_FOUND : HUSH_ERR_IO;
    n = fread(out, 1, outsz - 1, fp);
    out[n] = '\0';
    {
        int extra = fgetc(fp);
        int failed = ferror(fp);
        int closed = fclose(fp);

        if (failed || closed != 0)
            return HUSH_ERR_IO;
        if (extra != EOF)
            return HUSH_ERR_FULL;
    }
    if (memchr(out, '\0', n) != NULL)
        return HUSH_ERR_PARSE;
    return HUSH_OK;
}

static hush_status_t hush_favorite_check_ids(const char *robot,
                                             char ids[][HUSH_SKILL_ID_MAX],
                                             size_t nids)
{
    hush_skill_catalog_t cat;
    size_t i;
    const hush_skill_t *skill;

    assert(robot != NULL);
    assert(ids != NULL);
    hush_skill_init_catalog(&cat);
    if (hush_skill_load_catalog(&cat) != HUSH_OK)
        return HUSH_ERR_IO;
    for (i = 0; i < nids; i++) {
        skill = hush_skill_find(&cat, ids[i]);
        if (skill == NULL)
            return HUSH_ERR_DENIED;
        if (!hush_skill_robot_ok(skill, robot))
            return HUSH_ERR_DENIED;
    }
    return HUSH_OK;
}

static hush_status_t hush_favorite_format_body(char *out, size_t outsz,
                                               const char *robot,
                                               const char *name,
                                               char ids[][HUSH_SKILL_ID_MAX],
                                               size_t nids)
{
    char esc_name[HUSH_FAVORITE_NAME_MAX * 2];
    char esc_robot[HUSH_HOME_PATH_MAX];
    char esc_id[HUSH_SKILL_ID_MAX * 2];
    size_t off = 0;
    size_t i;
    int n;

    assert(out != NULL);
    assert(robot != NULL);
    assert(name != NULL);
    assert(ids != NULL);
    hush_json_escape(name, esc_name, sizeof(esc_name));
    hush_json_escape(robot, esc_robot, sizeof(esc_robot));
    n = snprintf(out, outsz, "{\"name\":\"%s\",\"robot\":\"%s\",\"skills\":[",
                 esc_name, esc_robot);
    if (n < 0 || (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    off = (size_t)n;
    for (i = 0; i < nids; i++) {
        hush_json_escape(ids[i], esc_id, sizeof(esc_id));
        n = snprintf(out + off, outsz - off, "%s\"%s\"",
                     (i == 0) ? "" : ",", esc_id);
        if (n < 0 || off + (size_t)n >= outsz)
            return HUSH_ERR_FULL;
        off += (size_t)n;
    }
    n = snprintf(out + off, outsz - off, "]}\n");
    if (n < 0 || off + (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    off += (size_t)n;
    (void)off;
    return HUSH_OK;
}

static hush_status_t hush_favorite_parse_body(hush_favorite_t *out,
                                              const char *body)
{
    hush_json_value_t value;
    size_t n = 0;
    size_t i;
    char key[24];
    char id[HUSH_SKILL_ID_MAX];

    assert(out != NULL);
    assert(body != NULL);
    memset(out, 0, sizeof(*out));
    if (hush_json_lookup(&value, body, "/name") != HUSH_OK)
        return HUSH_ERR_PARSE;
    if (hush_json_decode(out->name, sizeof(out->name), &value) != HUSH_OK)
        return HUSH_ERR_PARSE;
    hush_favorite_trim(out->name);
    if (!hush_favorite_name_ok(out->name))
        return HUSH_ERR_PARSE;
    for (i = 0; i < (size_t)HUSH_SKILL_EQUIP_MAX; i++) {
        if (snprintf(key, sizeof(key), "/skills/%zu", i) >= (int)sizeof(key))
            return HUSH_ERR_FULL;
        if (hush_json_lookup(&value, body, key) != HUSH_OK)
            break;
        if (hush_json_decode(id, sizeof(id), &value) != HUSH_OK)
            return HUSH_ERR_PARSE;
        if (id[0] == '\0')
            return HUSH_ERR_PARSE;
        hush_favorite_copy(out->skills[n], sizeof(out->skills[n]), id);
        if (out->skills[n][0] == '\0')
            return HUSH_ERR_FULL;
        n++;
    }
    out->nskills = n;
    if (n < (size_t)HUSH_SKILL_EQUIP_LOW || n > (size_t)HUSH_SKILL_EQUIP_MAX)
        return HUSH_ERR_PARSE;
    return HUSH_OK;
}

static hush_status_t hush_favorite_put_one(char *out, size_t outsz,
                                           size_t *off,
                                           const hush_favorite_t *fav,
                                           int first)
{
    char esc[HUSH_FAVORITE_NAME_MAX * 2];
    char esc_id[HUSH_SKILL_ID_MAX * 2];
    size_t i;
    int n;

    assert(out != NULL);
    assert(off != NULL);
    assert(fav != NULL);
    hush_json_escape(fav->name, esc, sizeof(esc));
    n = snprintf(out + *off, outsz - *off,
                 "%s{\"name\":\"%s\",\"skills\":[", first ? "" : ",", esc);
    if (n < 0 || *off + (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    *off += (size_t)n;
    for (i = 0; i < fav->nskills; i++) {
        hush_json_escape(fav->skills[i], esc_id, sizeof(esc_id));
        n = snprintf(out + *off, outsz - *off, "%s\"%s\"",
                     (i == 0) ? "" : ",", esc_id);
        if (n < 0 || *off + (size_t)n >= outsz)
            return HUSH_ERR_FULL;
        *off += (size_t)n;
    }
    n = snprintf(out + *off, outsz - *off, "]}");
    if (n < 0 || *off + (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    *off += (size_t)n;
    return HUSH_OK;
}

static int hush_favorite_is_list_entry(const char *entry)
{
    size_t n;
    size_t base;

    assert(entry != NULL);
    n = strlen(entry);
    if (n <= (size_t)HUSH_FAVORITE_SUFFIX_LEN ||
        n >= (size_t)HUSH_FAVORITE_SLUG_MAX + (size_t)HUSH_FAVORITE_SUFFIX_LEN)
        return 0;
    if (strcmp(entry + n - (size_t)HUSH_FAVORITE_SUFFIX_LEN,
               HUSH_FAVORITE_SUFFIX) != 0)
        return 0;
    base = n - (size_t)HUSH_FAVORITE_SUFFIX_LEN;
    {
        char slug[HUSH_FAVORITE_SLUG_MAX];

        if (base >= sizeof(slug))
            return 0;
        memcpy(slug, entry, base);
        slug[base] = '\0';
        return hush_favorite_is_slug(slug);
    }
}

static hush_status_t hush_favorite_append_entry(char *out, size_t outsz,
                                                size_t *off, const char *dir,
                                                const char *entry,
                                                int *first)
{
    char path[HUSH_HOME_PATH_MAX];
    char body[HUSH_FAVORITE_FILE_MAX];
    hush_favorite_t fav;

    assert(out != NULL);
    assert(off != NULL);
    assert(dir != NULL);
    assert(entry != NULL);
    assert(first != NULL);
    hush_favorite_join(path, sizeof(path), dir, entry);
    if (path[0] == '\0')
        return HUSH_ERR_FULL;
    if (hush_favorite_read_text(body, sizeof(body), path) != HUSH_OK)
        return HUSH_OK;
    if (hush_favorite_parse_body(&fav, body) != HUSH_OK)
        return HUSH_OK;
    if (hush_favorite_put_one(out, outsz, off, &fav, *first) != HUSH_OK)
        return HUSH_ERR_FULL;
    *first = 0;
    return HUSH_OK;
}

static hush_status_t hush_favorite_list_close(char *out, size_t outsz,
                                              size_t *off, size_t *out_len)
{
    int n;

    assert(out != NULL);
    assert(off != NULL);
    n = snprintf(out + *off, outsz - *off, "]}\n");
    if (n < 0 || *off + (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    *off += (size_t)n;
    if (out_len != NULL)
        *out_len = *off;
    return HUSH_OK;
}
