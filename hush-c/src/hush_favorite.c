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

#define HUSH_FAVORITE_SUFFIX ".json"
#define HUSH_FAVORITE_TMP_SUFFIX ".tmp"

/* One scan pass over dir; off/first/valid/dropped live here. */
typedef struct {
    char *out;
    size_t outsz;
    size_t *off;
    const char *dir;
    int *first;
    size_t valid;
    int dropped;
} hush_favorite_scan_t;

/* Writes the validated loadouts dir for robot into out. Fails ARG. */
static hush_status_t hush_favorite_loadouts(char *out, size_t outsz,
                                            const char *robot);

/* Cleans src into dst: copies, trims spaces, enforces the name law. */
static hush_status_t hush_favorite_clean_name(char *dst, size_t dstsz,
                                              const char *src);

/* Trims leading/trailing spaces of text in place. */
static void hush_favorite_trim(char *text);

/* True when name holds 1..47 bytes, no controls/quotes, valid UTF-8. */
static int hush_favorite_name_ok(const char *name);

/* Writes lowercase a-z0-9- slug of name into dst. */
static void hush_favorite_slugify(char *dst, size_t dstsz, const char *name);

/* True when path names dir/<file> with no escape. Pure. */
static int hush_favorite_under_dir(const char *path, const char *dir);

/* Writes dir/<slug>.json for the cleaned name into out. */
static hush_status_t hush_favorite_file_path(char *out, size_t outsz,
                                             const char *dir,
                                             const char *slug);

/* Creates the canonical dir plus parents. Only save calls it. */
static hush_status_t hush_favorite_make_tree(const char *dir);

/* Joins a/b into out. Empty on overflow. */
static void hush_favorite_join(char *out, size_t outsz,
                               const char *a, const char *b);

/* mkdir with the home dir mode. EEXIST is success. */
static hush_status_t hush_favorite_mkdir(const char *path);

/* Reads dir/<slug> plus suffix into out. Skips nothing. */
static hush_status_t hush_favorite_read_entry(hush_favorite_t *out,
                                              const char *dir,
                                              const char *entry);

/* Reads the stored display name for dir/<slug> into out. */
static hush_status_t hush_favorite_stored_name(char *out, size_t outsz,
                                               const char *dir,
                                               const char *slug);

/* Counts parseable favorites directly under dir. */
static hush_status_t hush_favorite_count_dir(size_t *out, const char *dir);

/* Writes the file slug for clean into slug; refuses clashes plus cap. */
static hush_status_t hush_favorite_gate_save(char *slug, size_t slugsz,
                                             const char *dir,
                                             const char *clean);

/* Refuses unknown, cross-slug, or repeated ids for robot. */
static hush_status_t hush_favorite_check_ids(const char *robot,
                                             char ids[][HUSH_SKILL_ID_MAX],
                                             size_t nids);

/* Writes text to path through a temp file plus rename. */
static hush_status_t hush_favorite_write_text(const char *path,
                                              const char *text);

/* Reads path into out (capped). Rejects NUL plus truncation. */
static hush_status_t hush_favorite_read_text(char *out, size_t outsz,
                                             const char *path);

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

/* Handles one scan entry: reads, caps, appends. */
static hush_status_t hush_favorite_scan_entry(hush_favorite_scan_t *scan,
                                              const char *entry);

/* Scans dir into the open list at out+*off. */
static hush_status_t hush_favorite_scan_dir(char *out, size_t outsz,
                                            size_t *off, const char *dir,
                                            int *first);

/* Closes the list JSON array; writes out_len when provided. */
static hush_status_t hush_favorite_list_close(char *out, size_t outsz,
                                              size_t *off, size_t *out_len);

hush_status_t hush_favorite_save(const char *robot, const char *name,
                                 char ids[][HUSH_SKILL_ID_MAX], size_t nids)
{
    char clean[HUSH_FAVORITE_NAME_MAX];
    char dir[HUSH_HOME_PATH_MAX];
    char slug[HUSH_FAVORITE_SLUG_MAX];
    char path[HUSH_HOME_PATH_MAX];
    char body[HUSH_FAVORITE_FILE_MAX];
    hush_status_t st;

    if (robot == NULL || name == NULL || ids == NULL)
        return HUSH_ERR_ARG;
    st = hush_favorite_loadouts(dir, sizeof dir, robot);
    if (st != HUSH_OK)
        return st;
    st = hush_favorite_clean_name(clean, sizeof clean, name);
    if (st != HUSH_OK)
        return st;
    if (nids == 0)
        return HUSH_ERR_DENIED;
    if (nids > (size_t)HUSH_SKILL_EQUIP_MAX)
        return HUSH_ERR_FULL;
    st = hush_favorite_check_ids(robot, ids, nids);
    if (st != HUSH_OK)
        return st;
    st = hush_favorite_make_tree(dir);
    if (st != HUSH_OK)
        return st;
    st = hush_favorite_gate_save(slug, sizeof slug, dir, clean);
    if (st != HUSH_OK)
        return st;
    st = hush_favorite_format_body(body, sizeof body, robot, clean,
                                   ids, nids);
    if (st != HUSH_OK)
        return st;
    st = hush_favorite_file_path(path, sizeof path, dir, slug);
    if (st != HUSH_OK)
        return st;
    return hush_favorite_write_text(path, body);
}

hush_status_t hush_favorite_load(const char *robot, const char *name,
                                 hush_favorite_t *out)
{
    char clean[HUSH_FAVORITE_NAME_MAX];
    char dir[HUSH_HOME_PATH_MAX];
    char slug[HUSH_FAVORITE_SLUG_MAX];
    char path[HUSH_HOME_PATH_MAX];
    char body[HUSH_FAVORITE_FILE_MAX];
    hush_status_t st;

    if (robot == NULL || name == NULL || out == NULL)
        return HUSH_ERR_ARG;
    memset(out, 0, sizeof *out);
    st = hush_favorite_loadouts(dir, sizeof dir, robot);
    if (st != HUSH_OK)
        return st;
    st = hush_favorite_clean_name(clean, sizeof clean, name);
    if (st != HUSH_OK)
        return st;
    hush_favorite_slugify(slug, sizeof slug, clean);
    if (slug[0] == '\0')
        return HUSH_ERR_PARSE;
    st = hush_favorite_file_path(path, sizeof path, dir, slug);
    if (st != HUSH_OK)
        return st;
    st = hush_favorite_read_text(body, sizeof body, path);
    if (st != HUSH_OK)
        return st;
    return hush_favorite_parse_body(out, body);
}

hush_status_t hush_favorite_delete(const char *robot, const char *name)
{
    char clean[HUSH_FAVORITE_NAME_MAX];
    char dir[HUSH_HOME_PATH_MAX];
    char slug[HUSH_FAVORITE_SLUG_MAX];
    char path[HUSH_HOME_PATH_MAX];
    hush_status_t st;

    if (robot == NULL || name == NULL)
        return HUSH_ERR_ARG;
    st = hush_favorite_loadouts(dir, sizeof dir, robot);
    if (st != HUSH_OK)
        return st;
    st = hush_favorite_clean_name(clean, sizeof clean, name);
    if (st != HUSH_OK)
        return st;
    hush_favorite_slugify(slug, sizeof slug, clean);
    if (slug[0] == '\0')
        return HUSH_ERR_PARSE;
    st = hush_favorite_file_path(path, sizeof path, dir, slug);
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
    char esc[HUSH_SKILL_ROBOT_MAX * 2];
    size_t off = 0;
    int first = 1;
    int n;
    hush_status_t st;

    if (robot == NULL || out == NULL || outsz == 0)
        return HUSH_ERR_ARG;
    st = hush_favorite_loadouts(dir, sizeof dir, robot);
    if (st != HUSH_OK)
        return st;
    if (hush_favorite_escape(esc, sizeof esc, robot) != HUSH_OK)
        return HUSH_ERR_FULL;
    n = snprintf(out, outsz, "{\"ok\":true,\"robot\":\"%s\",\"favorites\":[",
                 esc);
    if (n < 0 || (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    off = (size_t)n;
    st = hush_favorite_scan_dir(out, outsz, &off, dir, &first);
    if (st != HUSH_OK)
        return st;
    return hush_favorite_list_close(out, outsz, &off, out_len);
}

hush_status_t hush_favorite_escape(char *dst, size_t dstsz, const char *src)
{
    size_t need = 1;
    const unsigned char *p;

    if (dst == NULL || dstsz == 0 || src == NULL)
        return HUSH_ERR_ARG;
    for (p = (const unsigned char *)src; *p != '\0'; p++) {
        if (*p == (unsigned char)'"' || *p == (unsigned char)'\\' ||
            *p == (unsigned char)'\n' || *p == (unsigned char)'\r' ||
            *p == (unsigned char)'\t')
            need += sizeof "\\\"" - 1;
        else if (*p < (unsigned char)' ')
            need += sizeof "\\u0000" - 1;
        else
            need += 1;
    }
    if (need > dstsz)
        return HUSH_ERR_FULL;
    hush_json_escape(src, dst, dstsz);
    return HUSH_OK;
}

hush_status_t hush_favorite_put_ids(char *out, size_t outsz, size_t *off,
                                    const char ids[][HUSH_SKILL_ID_MAX],
                                    size_t nids)
{
    size_t i;

    if (out == NULL || off == NULL || ids == NULL)
        return HUSH_ERR_ARG;
    for (i = 0; i < nids; i++) {
        char esc[HUSH_SKILL_ID_MAX * 2];
        int n;

        if (hush_favorite_escape(esc, sizeof esc, ids[i]) != HUSH_OK)
            return HUSH_ERR_FULL;
        n = snprintf(out + *off, outsz - *off, "%s\"%s\"",
                     (i == 0) ? "" : ",", esc);
        if (n < 0 || *off + (size_t)n >= outsz)
            return HUSH_ERR_FULL;
        *off += (size_t)n;
    }
    return HUSH_OK;
}

static hush_status_t hush_favorite_loadouts(char *out, size_t outsz,
                                            const char *robot)
{
    assert(out != NULL);
    assert(robot != NULL);
    out[0] = '\0';
    if (!hush_home_is_robot_slug(robot))
        return HUSH_ERR_ARG;
    if (hush_home_loadouts_dir(out, outsz, robot) != HUSH_OK)
        return HUSH_ERR_FULL;
    return HUSH_OK;
}

static hush_status_t hush_favorite_clean_name(char *dst, size_t dstsz,
                                              const char *src)
{
    size_t n;

    assert(dst != NULL);
    assert(src != NULL);
    n = strlen(src);
    if (n >= dstsz)
        return HUSH_ERR_FULL;
    memcpy(dst, src, n + 1);
    hush_favorite_trim(dst);
    if (!hush_favorite_name_ok(dst))
        return HUSH_ERR_PARSE;
    return HUSH_OK;
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
    const unsigned char *p;
    size_t chars = 0;

    assert(name != NULL);
    if (name[0] == '\0' || strlen(name) >= (size_t)HUSH_FAVORITE_NAME_MAX)
        return 0;
    for (p = (const unsigned char *)name; *p != '\0'; p++) {
        if (*p < (unsigned char)' ' || *p == 0x7f ||
            *p == (unsigned char)'"' || *p == (unsigned char)'\\' ||
            *p == (unsigned char)'/')
            return 0;
    }
    if (hush_json_count_chars(&chars, name, (size_t)HUSH_FAVORITE_NAME_MAX)
        != HUSH_OK)
        return 0;
    assert(chars > 0);
    return 1;
}

static void hush_favorite_slugify(char *dst, size_t dstsz, const char *name)
{
    size_t i = 0;
    size_t o = 0;
    int dash = 0;

    assert(dst != NULL);
    assert(dstsz > 0);
    assert(name != NULL);
    dst[0] = '\0';
    while (name[i] != '\0' && o + 1 < dstsz) {
        unsigned char c = (unsigned char)name[i++];

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

static int hush_favorite_under_dir(const char *path, const char *dir)
{
    size_t n;

    assert(path != NULL);
    assert(dir != NULL);
    n = strlen(dir);
    if (n == 0 || strlen(path) <= n)
        return 0;
    if (memcmp(path, dir, n) != 0)
        return 0;
    return path[n] == '/';
}

static hush_status_t hush_favorite_file_path(char *out, size_t outsz,
                                             const char *dir,
                                             const char *slug)
{
    assert(out != NULL);
    assert(dir != NULL);
    assert(slug != NULL);
    out[0] = '\0';
    hush_favorite_join(out, outsz, dir, slug);
    if (out[0] == '\0')
        return HUSH_ERR_FULL;
    if (strlen(out) + sizeof HUSH_FAVORITE_SUFFIX >= outsz)
        return HUSH_ERR_FULL;
    strcat(out, HUSH_FAVORITE_SUFFIX);
    if (!hush_favorite_under_dir(out, dir))
        return HUSH_ERR_FULL;
    return HUSH_OK;
}

static hush_status_t hush_favorite_make_tree(const char *dir)
{
    char scoped[HUSH_HOME_PATH_MAX];
    char robots[HUSH_HOME_PATH_MAX];
    char *cut;
    hush_status_t st;

    assert(dir != NULL);
    if (strlen(dir) + 1 > sizeof scoped)
        return HUSH_ERR_FULL;
    memcpy(scoped, dir, strlen(dir) + 1);
    cut = strrchr(scoped, '/');
    if (cut == NULL)
        return HUSH_ERR_FULL;
    *cut = '\0';
    if (strlen(scoped) + 1 > sizeof robots)
        return HUSH_ERR_FULL;
    memcpy(robots, scoped, strlen(scoped) + 1);
    cut = strrchr(robots, '/');
    if (cut == NULL)
        return HUSH_ERR_FULL;
    *cut = '\0';
    st = hush_favorite_mkdir(robots);
    if (st != HUSH_OK)
        return st;
    st = hush_favorite_mkdir(scoped);
    if (st != HUSH_OK)
        return st;
    return hush_favorite_mkdir(dir);
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

static hush_status_t hush_favorite_mkdir(const char *path)
{
    assert(path != NULL);
    if (path[0] == '\0')
        return HUSH_ERR_IO;
    if (mkdir(path, HUSH_HOME_DIR_MODE) != 0 && errno != EEXIST)
        return HUSH_ERR_IO;
    return HUSH_OK;
}

static hush_status_t hush_favorite_read_entry(hush_favorite_t *out,
                                              const char *dir,
                                              const char *entry)
{
    char path[HUSH_HOME_PATH_MAX];
    char body[HUSH_FAVORITE_FILE_MAX];
    hush_status_t st;

    assert(out != NULL);
    assert(dir != NULL);
    assert(entry != NULL);
    hush_favorite_join(path, sizeof path, dir, entry);
    if (path[0] == '\0')
        return HUSH_ERR_FULL;
    if (!hush_favorite_under_dir(path, dir))
        return HUSH_ERR_FULL;
    st = hush_favorite_read_text(body, sizeof body, path);
    if (st != HUSH_OK)
        return st;
    return hush_favorite_parse_body(out, body);
}

static hush_status_t hush_favorite_stored_name(char *out, size_t outsz,
                                               const char *dir,
                                               const char *slug)
{
    char entry[HUSH_FAVORITE_SLUG_MAX + sizeof HUSH_FAVORITE_SUFFIX];
    hush_favorite_t fav;
    hush_status_t st;

    assert(out != NULL);
    assert(dir != NULL);
    assert(slug != NULL);
    if (strlen(slug) + sizeof HUSH_FAVORITE_SUFFIX > sizeof entry)
        return HUSH_ERR_FULL;
    memcpy(entry, slug, strlen(slug));
    memcpy(entry + strlen(slug), HUSH_FAVORITE_SUFFIX,
           sizeof HUSH_FAVORITE_SUFFIX);
    st = hush_favorite_read_entry(&fav, dir, entry);
    if (st != HUSH_OK)
        return st;
    if (strlen(fav.name) >= outsz)
        return HUSH_ERR_FULL;
    memcpy(out, fav.name, strlen(fav.name) + 1);
    return HUSH_OK;
}

static hush_status_t hush_favorite_count_dir(size_t *out, const char *dir)
{
    DIR *dp;
    size_t total = 0;
    size_t valid = 0;

    assert(out != NULL);
    assert(dir != NULL);
    *out = 0;
    dp = opendir(dir);
    if (dp == NULL)
        return errno == ENOENT ? HUSH_OK : HUSH_ERR_IO;
    while (total < (size_t)HUSH_FAVORITE_SCAN_MAX) {
        struct dirent *ent = readdir(dp);
        hush_favorite_t fav;

        if (ent == NULL)
            break;
        total++;
        if (!hush_favorite_is_list_entry(ent->d_name))
            continue;
        if (hush_favorite_read_entry(&fav, dir, ent->d_name) != HUSH_OK)
            continue;
        valid++;
    }
    closedir(dp);
    *out = valid;
    return HUSH_OK;
}

static hush_status_t hush_favorite_gate_save(char *slug, size_t slugsz,
                                             const char *dir,
                                             const char *clean)
{
    char stored[HUSH_FAVORITE_NAME_MAX];
    size_t count = 0;
    hush_status_t st;

    assert(slug != NULL);
    assert(dir != NULL);
    assert(clean != NULL);
    hush_favorite_slugify(slug, slugsz, clean);
    if (slug[0] == '\0')
        return HUSH_ERR_PARSE;
    st = hush_favorite_stored_name(stored, sizeof stored, dir, slug);
    if (st == HUSH_OK) {
        if (strcmp(stored, clean) != 0)
            return HUSH_ERR_DENIED;
        return HUSH_OK;
    }
    if (st != HUSH_ERR_NOT_FOUND)
        return st;
    st = hush_favorite_count_dir(&count, dir);
    if (st != HUSH_OK)
        return st;
    if (count >= (size_t)HUSH_FAVORITE_COUNT_MAX)
        return HUSH_ERR_FULL;
    return HUSH_OK;
}

static hush_status_t hush_favorite_check_ids(const char *robot,
                                             char ids[][HUSH_SKILL_ID_MAX],
                                             size_t nids)
{
    hush_skill_catalog_t *cat;
    size_t i;
    size_t j;

    assert(robot != NULL);
    assert(ids != NULL);
    /* Heap frame: the catalog is ~120 KB, too wide for a request stack.
     * Allocation failure reports FULL: the check cannot run. */
    cat = malloc(sizeof *cat);
    if (cat == NULL)
        return HUSH_ERR_FULL;
    hush_skill_init_catalog(cat);
    if (hush_skill_load_catalog(cat) != HUSH_OK) {
        free(cat);
        return HUSH_ERR_IO;
    }
    for (i = 0; i < nids; i++) {
        const hush_skill_t *skill = hush_skill_find(cat, ids[i]);

        if (skill == NULL) {
            free(cat);
            return HUSH_ERR_DENIED;
        }
        if (!hush_skill_robot_ok(skill, robot)) {
            free(cat);
            return HUSH_ERR_DENIED;
        }
        for (j = 0; j < i; j++) {
            if (strcmp(ids[i], ids[j]) == 0) {
                free(cat);
                return HUSH_ERR_DENIED;
            }
        }
    }
    free(cat);
    return HUSH_OK;
}

static hush_status_t hush_favorite_write_text(const char *path,
                                              const char *text)
{
    char tmp[HUSH_HOME_PATH_MAX];
    FILE *fp;
    size_t n;

    assert(path != NULL);
    assert(text != NULL);
    if (strlen(path) + sizeof HUSH_FAVORITE_TMP_SUFFIX >= sizeof tmp)
        return HUSH_ERR_FULL;
    memcpy(tmp, path, strlen(path) + 1);
    memcpy(tmp + strlen(path), HUSH_FAVORITE_TMP_SUFFIX,
           sizeof HUSH_FAVORITE_TMP_SUFFIX);
    fp = fopen(tmp, "w");
    if (fp == NULL)
        return HUSH_ERR_IO;
    n = strlen(text);
    if (n > 0 && fwrite(text, 1, n, fp) != n) {
        fclose(fp);
        unlink(tmp);
        return HUSH_ERR_IO;
    }
    if (fclose(fp) != 0) {
        unlink(tmp);
        return HUSH_ERR_IO;
    }
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return HUSH_ERR_IO;
    }
    return HUSH_OK;
}

static hush_status_t hush_favorite_read_text(char *out, size_t outsz,
                                             const char *path)
{
    FILE *fp;
    size_t n;
    int extra;
    int failed;
    int closed;

    assert(out != NULL);
    assert(outsz > 0);
    assert(path != NULL);
    fp = fopen(path, "rb");
    if (fp == NULL)
        return errno == ENOENT ? HUSH_ERR_NOT_FOUND : HUSH_ERR_IO;
    n = fread(out, 1, outsz - 1, fp);
    out[n] = '\0';
    extra = fgetc(fp);
    failed = ferror(fp);
    closed = fclose(fp);
    if (failed || closed != 0)
        return HUSH_ERR_IO;
    if (extra != EOF)
        return HUSH_ERR_FULL;
    if (memchr(out, '\0', n) != NULL)
        return HUSH_ERR_PARSE;
    return HUSH_OK;
}

static hush_status_t hush_favorite_format_body(char *out, size_t outsz,
                                               const char *robot,
                                               const char *name,
                                               char ids[][HUSH_SKILL_ID_MAX],
                                               size_t nids)
{
    char esc_name[HUSH_FAVORITE_NAME_MAX * 2];
    char esc_robot[HUSH_SKILL_ROBOT_MAX * 2];
    size_t off = 0;
    int n;

    assert(out != NULL);
    assert(robot != NULL);
    assert(name != NULL);
    assert(ids != NULL);
    if (hush_favorite_escape(esc_name, sizeof esc_name, name) != HUSH_OK)
        return HUSH_ERR_FULL;
    if (hush_favorite_escape(esc_robot, sizeof esc_robot, robot) != HUSH_OK)
        return HUSH_ERR_FULL;
    n = snprintf(out, outsz, "{\"name\":\"%s\",\"robot\":\"%s\",\"skills\":[",
                 esc_name, esc_robot);
    if (n < 0 || (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    off = (size_t)n;
    if (hush_favorite_put_ids(out, outsz, &off, ids, nids) != HUSH_OK)
        return HUSH_ERR_FULL;
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

    assert(out != NULL);
    assert(body != NULL);
    memset(out, 0, sizeof *out);
    if (hush_json_lookup(&value, body, "/name") != HUSH_OK)
        return HUSH_ERR_PARSE;
    if (hush_json_decode(out->name, sizeof out->name, &value) != HUSH_OK)
        return HUSH_ERR_PARSE;
    hush_favorite_trim(out->name);
    if (!hush_favorite_name_ok(out->name))
        return HUSH_ERR_PARSE;
    for (i = 0; i < (size_t)HUSH_SKILL_EQUIP_MAX; i++) {
        char key[HUSH_FAVORITE_KEY_MAX];
        char id[HUSH_SKILL_ID_MAX];

        if (snprintf(key, sizeof key, "/skills/%zu", i) >= (int)sizeof key)
            return HUSH_ERR_FULL;
        if (hush_json_lookup(&value, body, key) != HUSH_OK)
            break;
        if (hush_json_decode(id, sizeof id, &value) != HUSH_OK)
            return HUSH_ERR_PARSE;
        if (id[0] == '\0')
            return HUSH_ERR_PARSE;
        if (strlen(id) + 1 > sizeof out->skills[n])
            return HUSH_ERR_FULL;
        memcpy(out->skills[n], id, strlen(id) + 1);
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
    int n;

    assert(out != NULL);
    assert(off != NULL);
    assert(fav != NULL);
    if (hush_favorite_escape(esc, sizeof esc, fav->name) != HUSH_OK)
        return HUSH_ERR_FULL;
    n = snprintf(out + *off, outsz - *off,
                 "%s{\"name\":\"%s\",\"skills\":[", first ? "" : ",", esc);
    if (n < 0 || *off + (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    *off += (size_t)n;
    if (hush_favorite_put_ids(out, outsz, off, fav->skills, fav->nskills)
        != HUSH_OK)
        return HUSH_ERR_FULL;
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
    char slug[HUSH_FAVORITE_SLUG_MAX];

    assert(entry != NULL);
    n = strlen(entry);
    if (n <= sizeof HUSH_FAVORITE_SUFFIX - 1 ||
        n >= (size_t)HUSH_FAVORITE_SLUG_MAX + sizeof HUSH_FAVORITE_SUFFIX - 1)
        return 0;
    base = n - (sizeof HUSH_FAVORITE_SUFFIX - 1);
    if (strcmp(entry + base, HUSH_FAVORITE_SUFFIX) != 0)
        return 0;
    if (base >= sizeof slug)
        return 0;
    memcpy(slug, entry, base);
    slug[base] = '\0';
    return hush_home_is_robot_slug(slug);
}

static hush_status_t hush_favorite_scan_entry(hush_favorite_scan_t *scan,
                                              const char *entry)
{
    hush_favorite_t fav;

    assert(scan != NULL);
    assert(entry != NULL);
    if (!hush_favorite_is_list_entry(entry))
        return HUSH_OK;
    if (hush_favorite_read_entry(&fav, scan->dir, entry) != HUSH_OK)
        return HUSH_OK;
    if (scan->valid >= (size_t)HUSH_FAVORITE_COUNT_MAX) {
        scan->dropped = 1;
        return HUSH_OK;
    }
    if (hush_favorite_put_one(scan->out, scan->outsz, scan->off, &fav,
                              *scan->first) != HUSH_OK)
        return HUSH_ERR_FULL;
    *scan->first = 0;
    scan->valid++;
    return HUSH_OK;
}

static hush_status_t hush_favorite_scan_dir(char *out, size_t outsz,
                                            size_t *off, const char *dir,
                                            int *first)
{
    hush_favorite_scan_t scan = {
        .out = out,
        .outsz = outsz,
        .off = off,
        .dir = dir,
        .first = first,
        .valid = 0,
        .dropped = 0
    };
    DIR *dp;
    size_t total = 0;

    assert(out != NULL);
    assert(off != NULL);
    assert(dir != NULL);
    assert(first != NULL);
    dp = opendir(dir);
    if (dp == NULL)
        return HUSH_OK;
    while (total < (size_t)HUSH_FAVORITE_SCAN_MAX) {
        struct dirent *ent = readdir(dp);

        if (ent == NULL)
            break;
        total++;
        if (hush_favorite_scan_entry(&scan, ent->d_name) != HUSH_OK) {
            closedir(dp);
            return HUSH_ERR_FULL;
        }
    }
    closedir(dp);
    if (scan.dropped)
        return HUSH_ERR_FULL;
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
