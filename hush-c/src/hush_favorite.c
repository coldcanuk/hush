/* hush_favorite.c: owns per-robot favorite loadout files (PE-4 v1). */

#include <assert.h>
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
    HUSH_FAVORITE_KEY_MAX = 12,
    HUSH_FAVORITE_SCAN_MAX = 256,
    /* Worst escaped skill id: 95 raw bytes x \uXXXX plus NUL. */
    HUSH_FAVORITE_ESC_ID_MAX = HUSH_SKILL_ID_MAX * HUSH_JSON_U_LEN + 1
};

#define HUSH_FAVORITE_SUFFIX ".json"
#define HUSH_FAVORITE_TMP_SUFFIX ".tmp"
#define HUSH_FAVORITE_LIST_HEAD "{\"ok\":true,\"robot\":\""
#define HUSH_FAVORITE_LIST_MID "\",\"favorites\":["
#define HUSH_FAVORITE_LIST_TAIL "]}\n"
#define HUSH_FAVORITE_ENTRY_HEAD "{\"name\":\""
#define HUSH_FAVORITE_ENTRY_MID "\",\"skills\":["
#define HUSH_FAVORITE_ENTRY_TAIL "]}"
#define HUSH_FAVORITE_SAVE_MID "\",\"robot\":\""
#define HUSH_FAVORITE_SAVE_TAIL "]}\n"

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

/* True when name holds 1..47 allowlisted bytes: letters, digits,
 * space, '-', '_'. Rejects %, dots, and everything else. */
static int hush_favorite_is_name(const char *name);

/* True when path names dir/<file> with no escape. Pure. */
static int hush_favorite_is_under_dir(const char *path, const char *dir);

/* Writes dir/<slug>.json into out. */
static hush_status_t hush_favorite_file_path(char *out, size_t outsz,
                                             const char *dir,
                                             const char *slug);

/* Writes <slug>.json into out. */
static hush_status_t hush_favorite_slug_entry(char *out, size_t outsz,
                                              const char *slug);

/* Reads dir/<entry> into out. Skips nothing. */
static hush_status_t hush_favorite_read_entry(hush_favorite_t *out,
                                              const char *dir,
                                              const char *entry);

/* Adds one entry to the audit totals; skips non-favorites. */
static hush_status_t hush_favorite_audit_entry(size_t *count, size_t *bytes,
                                               const char *dir,
                                               const char *entry);

/* Counts valid favorites plus their list bytes under dir. */
static hush_status_t hush_favorite_audit_dir(size_t *count, size_t *bytes,
                                             const char *dir);

/* True when count entries of bytes content plus draft still fit. Pure. */
static int hush_favorite_fits_list(const char *robot, size_t count,
                                   size_t bytes,
                                   const hush_favorite_t *draft);

/* Packs validated save inputs into a draft favorite. */
static void hush_favorite_make_draft(hush_favorite_t *out, const char *clean,
                                     char ids[][HUSH_SKILL_ID_MAX],
                                     size_t nids);

/* Refuses clashes, cap overfill, plus lists that could not emit. */
static hush_status_t hush_favorite_gate_save(const char *dir,
                                             const char *slug,
                                             const char *robot,
                                             const hush_favorite_t *draft);

/* Gates, creates, formats, then atomically stores one favorite. */
static hush_status_t hush_favorite_commit(const char *dir, const char *slug,
                                          const char *robot,
                                          const hush_favorite_t *draft);

/* Checks ids[idx]: known, robot-scoped, first occurrence. */
static hush_status_t hush_favorite_check_one(const hush_skill_catalog_t *cat,
                                             const char *robot,
                                             char ids[][HUSH_SKILL_ID_MAX],
                                             size_t nids, size_t idx);

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

/* Escaped width of one byte under the JSON string rule. Pure. */
static size_t hush_favorite_escaped_byte(unsigned char c);

/* Escaped width of text without materializing it. Pure. */
static size_t hush_favorite_escaped_len(const char *text);

/* List bytes of one favorite, commas excluded. Pure. */
static size_t hush_favorite_entry_len(const hush_favorite_t *fav);

/* Formats one save file body into out. */
static hush_status_t hush_favorite_format_body(char *out, size_t outsz,
                                               const char *robot,
                                               const hush_favorite_t *draft);

/* Parses a save file body into out. */
static hush_status_t hush_favorite_parse_body(hush_favorite_t *out,
                                              const char *body);

/* Reads /skills/idx into out->skills[out->nskills]. NOT_FOUND at end. */
static hush_status_t hush_favorite_parse_skill(hush_favorite_t *out,
                                               const char *body, size_t idx);

/* Appends one entry object at out+*off. */
static hush_status_t hush_favorite_put_one(char *out, size_t outsz,
                                           size_t *off,
                                           const hush_favorite_t *fav,
                                           int first);

/* True when entry is a bounded <slug>.json favorite file. */
static int hush_favorite_is_list_entry(const char *entry);

/* Handles one scan entry: reads, caps, appends. */
static hush_status_t hush_favorite_scan_entry(hush_favorite_scan_t *scan,
                                              const char *entry);

/* Opens dir for scanning. Missing trees report NOT_FOUND. */
static hush_status_t hush_favorite_open_dir(DIR **out, const char *dir);

/* Reads one dirent: NULL at end, IO on readdir failure. */
static hush_status_t hush_favorite_next_dirent(DIR *dp, struct dirent **out);

/* Scans scan->dir into the open list. */
static hush_status_t hush_favorite_scan_dir(hush_favorite_scan_t *scan);

/* Closes dp, reporting clean end, truncation, or a read error. */
static hush_status_t hush_favorite_end_scan(DIR *dp, size_t total);

/* Closes the list JSON array; writes out_len when provided. */
static hush_status_t hush_favorite_list_close(char *out, size_t outsz,
                                              size_t *off, size_t *out_len);

hush_status_t hush_favorite_save(const char *robot, const char *name,
                                 char ids[][HUSH_SKILL_ID_MAX], size_t nids)
{
    char clean[HUSH_FAVORITE_NAME_MAX] = {0};
    char dir[HUSH_HOME_PATH_MAX] = {0};
    char slug[HUSH_FAVORITE_SLUG_MAX] = {0};
    hush_favorite_t draft = {0};
    hush_status_t st = HUSH_OK;

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
    hush_skill_slugify(slug, sizeof slug, clean);
    if (slug[0] == '\0')
        return HUSH_ERR_PARSE;
    hush_favorite_make_draft(&draft, clean, ids, nids);
    return hush_favorite_commit(dir, slug, robot, &draft);
}

hush_status_t hush_favorite_load(const char *robot, const char *name,
                                 hush_favorite_t *out)
{
    char clean[HUSH_FAVORITE_NAME_MAX] = {0};
    char dir[HUSH_HOME_PATH_MAX] = {0};
    char slug[HUSH_FAVORITE_SLUG_MAX] = {0};
    char path[HUSH_HOME_PATH_MAX] = {0};
    char body[HUSH_FAVORITE_FILE_MAX] = {0};
    hush_status_t st = HUSH_OK;

    if (robot == NULL || name == NULL || out == NULL)
        return HUSH_ERR_ARG;
    memset(out, 0, sizeof *out);
    st = hush_favorite_loadouts(dir, sizeof dir, robot);
    if (st != HUSH_OK)
        return st;
    st = hush_favorite_clean_name(clean, sizeof clean, name);
    if (st != HUSH_OK)
        return st;
    hush_skill_slugify(slug, sizeof slug, clean);
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
    char clean[HUSH_FAVORITE_NAME_MAX] = {0};
    char dir[HUSH_HOME_PATH_MAX] = {0};
    char slug[HUSH_FAVORITE_SLUG_MAX] = {0};
    char path[HUSH_HOME_PATH_MAX] = {0};
    hush_status_t st = HUSH_OK;

    if (robot == NULL || name == NULL)
        return HUSH_ERR_ARG;
    st = hush_favorite_loadouts(dir, sizeof dir, robot);
    if (st != HUSH_OK)
        return st;
    st = hush_favorite_clean_name(clean, sizeof clean, name);
    if (st != HUSH_OK)
        return st;
    hush_skill_slugify(slug, sizeof slug, clean);
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
    char dir[HUSH_HOME_PATH_MAX] = {0};
    char esc[HUSH_SKILL_ROBOT_MAX * 2] = {0};
    size_t off = 0;
    int first = 1;
    int n = 0;
    hush_status_t st = HUSH_OK;
    hush_favorite_scan_t scan = {
        .out = out,
        .outsz = outsz,
        .off = &off,
        .dir = dir,
        .first = &first,
        .valid = 0,
        .dropped = 0
    };

    if (robot == NULL || out == NULL || outsz == 0)
        return HUSH_ERR_ARG;
    st = hush_favorite_loadouts(dir, sizeof dir, robot);
    if (st != HUSH_OK)
        return st;
    if (hush_favorite_escape(esc, sizeof esc, robot) != HUSH_OK)
        return HUSH_ERR_FULL;
    n = snprintf(out, outsz, "%s%s%s", HUSH_FAVORITE_LIST_HEAD, esc,
                 HUSH_FAVORITE_LIST_MID);
    if (n < 0 || (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    off = (size_t)n;
    st = hush_favorite_scan_dir(&scan);
    if (st != HUSH_OK)
        return st;
    if (scan.dropped)
        return HUSH_ERR_FULL;
    return hush_favorite_list_close(out, outsz, &off, out_len);
}

hush_status_t hush_favorite_escape(char *dst, size_t dstsz, const char *src)
{
    size_t need = 1;

    if (dst == NULL || dstsz == 0 || src == NULL)
        return HUSH_ERR_ARG;
    need += hush_favorite_escaped_len(src);
    if (need > dstsz)
        return HUSH_ERR_FULL;
    hush_json_escape(src, dst, dstsz);
    return HUSH_OK;
}

hush_status_t hush_favorite_put_ids(char *out, size_t outsz, size_t *off,
                                    const hush_favorite_t *fav)
{
    if (out == NULL || off == NULL || fav == NULL)
        return HUSH_ERR_ARG;
    for (size_t i = 0; i < fav->nskills; i++) {
        char esc[HUSH_FAVORITE_ESC_ID_MAX] = {0};
        int n = 0;

        if (hush_favorite_escape(esc, sizeof esc, fav->skills[i])
            != HUSH_OK)
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
    return hush_home_loadouts_dir(out, outsz, robot);
}

static hush_status_t hush_favorite_clean_name(char *dst, size_t dstsz,
                                              const char *src)
{
    size_t n = 0;

    assert(dst != NULL);
    assert(src != NULL);
    n = strlen(src);
    if (n >= dstsz)
        return HUSH_ERR_FULL;
    memcpy(dst, src, n + 1);
    hush_favorite_trim(dst);
    if (!hush_favorite_is_name(dst))
        return HUSH_ERR_PARSE;
    return HUSH_OK;
}

static void hush_favorite_trim(char *text)
{
    size_t n = 0;
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

static int hush_favorite_is_name(const char *name)
{
    const unsigned char *p = NULL;

    assert(name != NULL);
    if (name[0] == '\0' || strlen(name) >= (size_t)HUSH_FAVORITE_NAME_MAX)
        return 0;
    /* Explicit ranges, never isalnum: locale-independent by design. */
    for (p = (const unsigned char *)name; *p != '\0'; p++) {
        unsigned char c = *p;

        if ((c < (unsigned char)'a' || c > (unsigned char)'z') &&
            (c < (unsigned char)'A' || c > (unsigned char)'Z') &&
            (c < (unsigned char)'0' || c > (unsigned char)'9') &&
            c != (unsigned char)' ' && c != (unsigned char)'-' &&
            c != (unsigned char)'_')
            return 0;
    }
    return 1;
}

static int hush_favorite_is_under_dir(const char *path, const char *dir)
{
    size_t n = 0;

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
    hush_home_join(out, outsz, dir, slug);
    if (out[0] == '\0')
        return HUSH_ERR_FULL;
    if (strlen(out) + sizeof HUSH_FAVORITE_SUFFIX >= outsz)
        return HUSH_ERR_FULL;
    strcat(out, HUSH_FAVORITE_SUFFIX);
    if (!hush_favorite_is_under_dir(out, dir))
        return HUSH_ERR_FULL;
    return HUSH_OK;
}

static hush_status_t hush_favorite_slug_entry(char *out, size_t outsz,
                                              const char *slug)
{
    assert(out != NULL);
    assert(slug != NULL);
    out[0] = '\0';
    if (strlen(slug) + sizeof HUSH_FAVORITE_SUFFIX > outsz)
        return HUSH_ERR_FULL;
    memcpy(out, slug, strlen(slug));
    memcpy(out + strlen(slug), HUSH_FAVORITE_SUFFIX,
           sizeof HUSH_FAVORITE_SUFFIX);
    return HUSH_OK;
}

static hush_status_t hush_favorite_read_entry(hush_favorite_t *out,
                                              const char *dir,
                                              const char *entry)
{
    char path[HUSH_HOME_PATH_MAX] = {0};
    char body[HUSH_FAVORITE_FILE_MAX] = {0};
    hush_status_t st = HUSH_OK;

    assert(out != NULL);
    assert(dir != NULL);
    assert(entry != NULL);
    hush_home_join(path, sizeof path, dir, entry);
    if (path[0] == '\0')
        return HUSH_ERR_FULL;
    if (!hush_favorite_is_under_dir(path, dir))
        return HUSH_ERR_FULL;
    st = hush_favorite_read_text(body, sizeof body, path);
    if (st != HUSH_OK)
        return st;
    return hush_favorite_parse_body(out, body);
}

static int hush_favorite_fits_list(const char *robot, size_t count,
                                   size_t bytes,
                                   const hush_favorite_t *draft)
{
    size_t total = 0;

    assert(robot != NULL);
    assert(draft != NULL);
    total = (sizeof HUSH_FAVORITE_LIST_HEAD - 1)
        + hush_favorite_escaped_len(robot)
        + (sizeof HUSH_FAVORITE_LIST_MID - 1) + bytes;
    if (count > 0)
        total += count;
    total += hush_favorite_entry_len(draft)
        + (sizeof HUSH_FAVORITE_LIST_TAIL - 1);
    return total < (size_t)HUSH_FAVORITE_JSON_MAX;
}

static hush_status_t hush_favorite_audit_entry(size_t *count, size_t *bytes,
                                               const char *dir,
                                               const char *entry)
{
    hush_favorite_t fav = {0};

    assert(count != NULL);
    assert(bytes != NULL);
    assert(dir != NULL);
    assert(entry != NULL);
    if (!hush_favorite_is_list_entry(entry))
        return HUSH_OK;
    if (hush_favorite_read_entry(&fav, dir, entry) != HUSH_OK)
        return HUSH_OK;
    *bytes += hush_favorite_entry_len(&fav);
    (*count)++;
    return HUSH_OK;
}

static hush_status_t hush_favorite_audit_dir(size_t *count, size_t *bytes,
                                             const char *dir)
{
    DIR *dp = NULL;
    size_t total = 0;
    hush_status_t st = HUSH_OK;

    assert(count != NULL);
    assert(bytes != NULL);
    assert(dir != NULL);
    *count = 0;
    *bytes = 0;
    st = hush_favorite_open_dir(&dp, dir);
    if (st == HUSH_ERR_NOT_FOUND)
        return HUSH_OK;
    if (st != HUSH_OK)
        return st;
    while (total < (size_t)HUSH_FAVORITE_SCAN_MAX) {
        struct dirent *ent = NULL;

        st = hush_favorite_next_dirent(dp, &ent);
        if (st == HUSH_ERR_NOT_FOUND)
            break;
        if (st != HUSH_OK) {
            closedir(dp);
            return st;
        }
        total++;
        st = hush_favorite_audit_entry(count, bytes, dir, ent->d_name);
        if (st != HUSH_OK) {
            closedir(dp);
            return st;
        }
    }
    return hush_favorite_end_scan(dp, total);
}

static void hush_favorite_make_draft(hush_favorite_t *out, const char *clean,
                                     char ids[][HUSH_SKILL_ID_MAX],
                                     size_t nids)
{
    assert(out != NULL);
    assert(clean != NULL);
    assert(ids != NULL);
    assert(strlen(clean) < sizeof out->name);
    assert(nids >= (size_t)HUSH_SKILL_EQUIP_LOW);
    assert(nids <= (size_t)HUSH_SKILL_EQUIP_MAX);
    memset(out, 0, sizeof *out);
    memcpy(out->name, clean, strlen(clean) + 1);
    for (size_t i = 0; i < nids; i++) {
        assert(strlen(ids[i]) < (size_t)HUSH_SKILL_ID_MAX);
        memcpy(out->skills[i], ids[i], strlen(ids[i]) + 1);
    }
    out->nskills = nids;
}

static hush_status_t hush_favorite_gate_save(const char *dir,
                                             const char *slug,
                                             const char *robot,
                                             const hush_favorite_t *draft)
{
    char entry[HUSH_FAVORITE_SLUG_MAX + sizeof HUSH_FAVORITE_SUFFIX] = {0};
    hush_favorite_t stored = {0};
    size_t count = 0;
    size_t bytes = 0;
    size_t replace = 0;
    hush_status_t st = HUSH_OK;

    assert(dir != NULL);
    assert(slug != NULL);
    assert(robot != NULL);
    assert(draft != NULL);
    st = hush_favorite_slug_entry(entry, sizeof entry, slug);
    if (st != HUSH_OK)
        return st;
    st = hush_favorite_read_entry(&stored, dir, entry);
    if (st == HUSH_OK) {
        if (strcmp(stored.name, draft->name) != 0)
            return HUSH_ERR_DENIED;
        replace = hush_favorite_entry_len(&stored);
    } else if (st != HUSH_ERR_NOT_FOUND) {
        return st;
    }
    st = hush_favorite_audit_dir(&count, &bytes, dir);
    if (st != HUSH_OK)
        return st;
    if (replace == 0 && count >= (size_t)HUSH_FAVORITE_COUNT_MAX)
        return HUSH_ERR_FULL;
    if (bytes < replace)
        return HUSH_ERR_IO;
    if (!hush_favorite_fits_list(robot, count, bytes - replace, draft))
        return HUSH_ERR_FULL;
    return HUSH_OK;
}

static hush_status_t hush_favorite_commit(const char *dir, const char *slug,
                                          const char *robot,
                                          const hush_favorite_t *draft)
{
    char body[HUSH_FAVORITE_FILE_MAX] = {0};
    char path[HUSH_HOME_PATH_MAX] = {0};
    hush_status_t st = HUSH_OK;

    assert(dir != NULL);
    assert(slug != NULL);
    assert(robot != NULL);
    assert(draft != NULL);
    st = hush_favorite_gate_save(dir, slug, robot, draft);
    if (st != HUSH_OK)
        return st;
    st = hush_home_ensure_loadouts(robot);
    if (st != HUSH_OK)
        return st;
    st = hush_favorite_format_body(body, sizeof body, robot, draft);
    if (st != HUSH_OK)
        return st;
    st = hush_favorite_file_path(path, sizeof path, dir, slug);
    if (st != HUSH_OK)
        return st;
    return hush_favorite_write_text(path, body);
}

static hush_status_t hush_favorite_check_one(const hush_skill_catalog_t *cat,
                                             const char *robot,
                                             char ids[][HUSH_SKILL_ID_MAX],
                                             size_t nids, size_t idx)
{
    const hush_skill_t *skill = NULL;

    assert(cat != NULL);
    assert(robot != NULL);
    assert(ids != NULL);
    assert(idx < nids);
    skill = hush_skill_find(cat, ids[idx]);
    if (skill == NULL)
        return HUSH_ERR_DENIED;
    if (!hush_skill_robot_ok(skill, robot))
        return HUSH_ERR_DENIED;
    for (size_t j = 0; j < idx; j++) {
        if (strcmp(ids[idx], ids[j]) == 0)
            return HUSH_ERR_DENIED;
    }
    return HUSH_OK;
}

static hush_status_t hush_favorite_check_ids(const char *robot,
                                             char ids[][HUSH_SKILL_ID_MAX],
                                             size_t nids)
{
    /* Heap frame: the catalog is ~120 KB, too wide for a request stack.
     * Allocation failure reports FULL: the check cannot run. */
    hush_skill_catalog_t *cat = malloc(sizeof *cat);
    hush_status_t st = HUSH_OK;

    assert(robot != NULL);
    assert(ids != NULL);
    if (cat == NULL)
        return HUSH_ERR_FULL;
    hush_skill_init_catalog(cat);
    st = hush_skill_load_catalog(cat) == HUSH_OK ? HUSH_OK : HUSH_ERR_IO;
    for (size_t i = 0; st == HUSH_OK && i < nids; i++)
        st = hush_favorite_check_one(cat, robot, ids, nids, i);
    free(cat);
    return st;
}

static hush_status_t hush_favorite_write_text(const char *path,
                                              const char *text)
{
    char tmp[HUSH_HOME_PATH_MAX] = {0};
    FILE *fp = NULL;
    size_t n = 0;

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
    FILE *fp = NULL;
    size_t n = 0;
    int extra = 0;
    int failed = 0;
    int closed = 0;

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

/* Must match hush_json_put_byte (hush_json.c): pair, \u, else raw. */
static size_t hush_favorite_escaped_byte(unsigned char c)
{
    if (c == (unsigned char)'"' || c == (unsigned char)'\\' ||
        c == (unsigned char)'\n' || c == (unsigned char)'\r' ||
        c == (unsigned char)'\t')
        return sizeof "\\\"" - 1;
    if (c < (unsigned char)' ')
        return sizeof "\\u0000" - 1;
    return 1;
}

static size_t hush_favorite_escaped_len(const char *text)
{
    size_t need = 0;
    const unsigned char *p = (const unsigned char *)text;

    assert(text != NULL);
    for (; *p != '\0'; p++)
        need += hush_favorite_escaped_byte(*p);
    return need;
}

static size_t hush_favorite_entry_len(const hush_favorite_t *fav)
{
    size_t n = 0;

    assert(fav != NULL);
    n = (sizeof HUSH_FAVORITE_ENTRY_HEAD - 1)
        + hush_favorite_escaped_len(fav->name)
        + (sizeof HUSH_FAVORITE_ENTRY_MID - 1);
    for (size_t i = 0; i < fav->nskills; i++)
        n += 2 + hush_favorite_escaped_len(fav->skills[i]);
    if (fav->nskills > 0)
        n += fav->nskills - 1;
    n += sizeof HUSH_FAVORITE_ENTRY_TAIL - 1;
    return n;
}

static hush_status_t hush_favorite_format_body(char *out, size_t outsz,
                                               const char *robot,
                                               const hush_favorite_t *draft)
{
    char esc_name[HUSH_FAVORITE_NAME_MAX * 2] = {0};
    char esc_robot[HUSH_SKILL_ROBOT_MAX * 2] = {0};
    size_t off = 0;
    int n = 0;

    assert(out != NULL);
    assert(robot != NULL);
    assert(draft != NULL);
    if (hush_favorite_escape(esc_name, sizeof esc_name, draft->name)
        != HUSH_OK)
        return HUSH_ERR_FULL;
    if (hush_favorite_escape(esc_robot, sizeof esc_robot, robot) != HUSH_OK)
        return HUSH_ERR_FULL;
    n = snprintf(out, outsz, "%s%s%s%s%s", HUSH_FAVORITE_ENTRY_HEAD,
                 esc_name, HUSH_FAVORITE_SAVE_MID, esc_robot,
                 HUSH_FAVORITE_ENTRY_MID);
    if (n < 0 || (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    off = (size_t)n;
    if (hush_favorite_put_ids(out, outsz, &off, draft) != HUSH_OK)
        return HUSH_ERR_FULL;
    n = snprintf(out + off, outsz - off, "%s", HUSH_FAVORITE_SAVE_TAIL);
    if (n < 0 || off + (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    return HUSH_OK;
}

static hush_status_t hush_favorite_parse_body(hush_favorite_t *out,
                                              const char *body)
{
    hush_json_value_t value = {0};

    assert(out != NULL);
    assert(body != NULL);
    memset(out, 0, sizeof *out);
    if (hush_json_lookup(&value, body, "/name") != HUSH_OK)
        return HUSH_ERR_PARSE;
    if (hush_json_decode(out->name, sizeof out->name, &value) != HUSH_OK)
        return HUSH_ERR_PARSE;
    hush_favorite_trim(out->name);
    if (!hush_favorite_is_name(out->name))
        return HUSH_ERR_PARSE;
    for (size_t i = 0; i < (size_t)HUSH_SKILL_EQUIP_MAX; i++) {
        hush_status_t st = hush_favorite_parse_skill(out, body, i);

        if (st == HUSH_ERR_NOT_FOUND)
            break;
        if (st != HUSH_OK)
            return st;
    }
    if (out->nskills < (size_t)HUSH_SKILL_EQUIP_LOW ||
        out->nskills > (size_t)HUSH_SKILL_EQUIP_MAX)
        return HUSH_ERR_PARSE;
    return HUSH_OK;
}

static hush_status_t hush_favorite_parse_skill(hush_favorite_t *out,
                                               const char *body, size_t idx)
{
    char key[HUSH_FAVORITE_KEY_MAX] = {0};
    char id[HUSH_SKILL_ID_MAX] = {0};
    hush_json_value_t value = {0};

    assert(out != NULL);
    assert(body != NULL);
    assert(out->nskills < (size_t)HUSH_SKILL_EQUIP_MAX);
    if (snprintf(key, sizeof key, "/skills/%zu", idx) >= (int)sizeof key)
        return HUSH_ERR_FULL;
    if (hush_json_lookup(&value, body, key) != HUSH_OK)
        return HUSH_ERR_NOT_FOUND;
    if (hush_json_decode(id, sizeof id, &value) != HUSH_OK)
        return HUSH_ERR_PARSE;
    if (id[0] == '\0')
        return HUSH_ERR_PARSE;
    if (strlen(id) + 1 > sizeof out->skills[out->nskills])
        return HUSH_ERR_FULL;
    memcpy(out->skills[out->nskills], id, strlen(id) + 1);
    out->nskills++;
    return HUSH_OK;
}

static hush_status_t hush_favorite_put_one(char *out, size_t outsz,
                                           size_t *off,
                                           const hush_favorite_t *fav,
                                           int first)
{
    char esc[HUSH_FAVORITE_NAME_MAX * 2] = {0};
    int n = 0;

    assert(out != NULL);
    assert(off != NULL);
    assert(fav != NULL);
    if (hush_favorite_escape(esc, sizeof esc, fav->name) != HUSH_OK)
        return HUSH_ERR_FULL;
    n = snprintf(out + *off, outsz - *off, "%s%s%s%s", first ? "" : ",",
                 HUSH_FAVORITE_ENTRY_HEAD, esc, HUSH_FAVORITE_ENTRY_MID);
    if (n < 0 || *off + (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    *off += (size_t)n;
    if (hush_favorite_put_ids(out, outsz, off, fav) != HUSH_OK)
        return HUSH_ERR_FULL;
    n = snprintf(out + *off, outsz - *off, "%s", HUSH_FAVORITE_ENTRY_TAIL);
    if (n < 0 || *off + (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    *off += (size_t)n;
    return HUSH_OK;
}

static int hush_favorite_is_list_entry(const char *entry)
{
    size_t n = 0;
    size_t base = 0;
    char slug[HUSH_FAVORITE_SLUG_MAX] = {0};

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
    hush_favorite_t fav = {0};

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

static hush_status_t hush_favorite_open_dir(DIR **out, const char *dir)
{
    assert(out != NULL);
    assert(dir != NULL);
    *out = opendir(dir);
    if (*out != NULL)
        return HUSH_OK;
    return errno == ENOENT ? HUSH_ERR_NOT_FOUND : HUSH_ERR_IO;
}

static hush_status_t hush_favorite_next_dirent(DIR *dp, struct dirent **out)
{
    assert(dp != NULL);
    assert(out != NULL);
    errno = 0;
    *out = readdir(dp);
    if (*out != NULL)
        return HUSH_OK;
    return errno == 0 ? HUSH_ERR_NOT_FOUND : HUSH_ERR_IO;
}

static hush_status_t hush_favorite_scan_dir(hush_favorite_scan_t *scan)
{
    DIR *dp = NULL;
    size_t total = 0;
    hush_status_t st = HUSH_OK;

    assert(scan != NULL);
    assert(scan->out != NULL);
    assert(scan->off != NULL);
    assert(scan->dir != NULL);
    assert(scan->first != NULL);
    st = hush_favorite_open_dir(&dp, scan->dir);
    if (st == HUSH_ERR_NOT_FOUND)
        return HUSH_OK;
    if (st != HUSH_OK)
        return st;
    while (total < (size_t)HUSH_FAVORITE_SCAN_MAX) {
        struct dirent *ent = NULL;

        st = hush_favorite_next_dirent(dp, &ent);
        if (st == HUSH_ERR_NOT_FOUND)
            break;
        if (st != HUSH_OK) {
            closedir(dp);
            return st;
        }
        total++;
        st = hush_favorite_scan_entry(scan, ent->d_name);
        if (st != HUSH_OK) {
            closedir(dp);
            return st;
        }
    }
    return hush_favorite_end_scan(dp, total);
}

static hush_status_t hush_favorite_end_scan(DIR *dp, size_t total)
{
    struct dirent *ent = NULL;
    hush_status_t st = HUSH_OK;

    assert(dp != NULL);
    if (total < (size_t)HUSH_FAVORITE_SCAN_MAX) {
        closedir(dp);
        return HUSH_OK;
    }
    st = hush_favorite_next_dirent(dp, &ent);
    closedir(dp);
    if (st == HUSH_ERR_NOT_FOUND)
        return HUSH_OK;
    if (st != HUSH_OK)
        return st;
    return HUSH_ERR_FULL;
}

static hush_status_t hush_favorite_list_close(char *out, size_t outsz,
                                              size_t *off, size_t *out_len)
{
    int n = 0;

    assert(out != NULL);
    assert(off != NULL);
    n = snprintf(out + *off, outsz - *off, "%s", HUSH_FAVORITE_LIST_TAIL);
    if (n < 0 || *off + (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    *off += (size_t)n;
    if (out_len != NULL)
        *out_len = *off;
    return HUSH_OK;
}
