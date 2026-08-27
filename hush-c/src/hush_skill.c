/* hush_skill.c: owns skill catalog scan, forge writer, and voice ids. */

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "hush_home.h"
#include "hush_json.h"
#include "hush_skill.h"

enum {
    HUSH_SKILL_DIR_MAX = 256,
    HUSH_SKILL_LINE_MAX = 256,
    HUSH_SKILL_HEAD_LINES = 24,
    HUSH_SKILL_HEAD_WEIGHT = 4,
    HUSH_SKILL_FENCE_WEIGHT = 8,
    HUSH_SKILL_CHAR_DIV = 200
};

#define HUSH_SKILL_FORGE_BODY \
    "---\n" \
    "name: forge-skill\n" \
    "description: Create a new Hush skill for a robot, the user, or the hive.\n" \
    "role: any\n" \
    "---\n\n" \
    "# Forge a Hush skill\n\n" \
    "A Hush skill is a SKILL.md the hive equips onto a robot like an inventory " \
    "item. Forging writes a new file. Equipping points a robot at an existing " \
    "one. Pruning unequips; the file stays on disk.\n\n" \
    "## Layout (~/.hush/)\n\n" \
    "system: ~/.hush/skills/system/<slug>/SKILL.md\n" \
    "user: ~/.hush/skills/user/<slug>/SKILL.md\n" \
    "robot: ~/.hush/skills/robots/<robot-slug>/<slug>/SKILL.md\n\n" \
    "System skills ship with Hush. User skills are hive-wide. Robot skills " \
    "belong to one robot.\n\n" \
    "## Equip and prune\n\n" \
    "The robot editor Armory lists skills by scope. Click a chip to equip it " \
    "into the Loadout. Click a Loadout chip to prune it.\n\n" \
    "## Forge (this skill)\n\n" \
    "POST /api/skill {name, summary, body, scope, robot?}. Do not forge into " \
    "system from the UI. Do not write secrets into a skill.\n"

static const char *const hush_skill_voices[HUSH_SKILL_VOICE_COUNT] = {
    "alloy",
    "echo",
    "fable",
    "onyx",
    "nova",
    "shimmer"
};

/* True when scope is system, user, or robot. */
static int hush_skill_is_scope(const char *scope);

/* Writes lowercase a-z0-9- slug of name into dst. */
static void hush_skill_slugify(char *dst, size_t dstsz, const char *name);

/* Builds scope:slug or robot:robot:slug into out. */
static hush_status_t hush_skill_make_id(char *out, size_t outsz,
                                        const char *scope,
                                        const char *robot,
                                        const char *slug);

/* Copies src into dst. Empty on overflow. */
static void hush_skill_copy(char *dst, size_t dstsz, const char *src);

/* Joins a/b into out. Empty on overflow. */
static void hush_skill_join(char *out, size_t outsz, const char *a, const char *b);

/* Loads one scope directory (and robot subdirs when scope is robot). */
static hush_status_t hush_skill_load_scope(hush_skill_catalog_t *cat,
                                           const char *scope,
                                           const char *robot);

/* Scans robot folders under skills/robots. */
static hush_status_t hush_skill_load_robots(hush_skill_catalog_t *cat);

/* Reads one SKILL.md folder into the catalog. */
static hush_status_t hush_skill_read_one(hush_skill_catalog_t *cat,
                                         const char *scope,
                                         const char *robot,
                                         const char *slug,
                                         const char *dir);

/* Fills summary from SKILL.md head. */
static void hush_skill_read_summary(char *dst, size_t dstsz, const char *path);

/* Takes description: or # heading from line into dst when dst is empty. */
static void hush_skill_take_summary_line(char *dst, size_t dstsz, const char *line);

/* mkdir 0700. EEXIST is success. */
static hush_status_t hush_skill_mkdir(const char *path);

/* Writes markdown for a forged skill. */
static hush_status_t hush_skill_write_file(const char *path,
                                           const hush_skill_forge_in_t *in,
                                           const char *slug);

/* Appends one skill object to JSON. */
static hush_status_t hush_skill_put_one(char *out, size_t outsz, size_t *off,
                                        const hush_skill_t *skill, int first);

/* Appends the voices array. */
static hush_status_t hush_skill_put_voices(char *out, size_t outsz, size_t *off,
                                           int with_voices);

/* Reads SKILL.md into body (capped). */
static void hush_skill_read_body(char *dst, size_t dstsz, const char *path);

/* Takes role: or category: from a frontmatter line. */
static void hush_skill_take_meta_line(char *role, size_t rolesz,
                                      char *category, size_t catsz,
                                      const char *line);

/* Copies src file to dst. */
static hush_status_t hush_skill_copy_file(const char *src, const char *dst);

void hush_skill_init_catalog(hush_skill_catalog_t *cat)
{
    if (cat == NULL)
        return;
    memset(cat, 0, sizeof(*cat));
}

hush_status_t hush_skill_load_catalog(hush_skill_catalog_t *cat)
{
    if (cat == NULL)
        return HUSH_ERR_ARG;
    hush_skill_init_catalog(cat);
    if (hush_home_ensure() != HUSH_OK)
        return HUSH_ERR_IO;
    (void)hush_skill_seed_pack("../skills/system");
    (void)hush_skill_seed_pack("skills/system");
    if (hush_skill_load_scope(cat, HUSH_SKILL_SCOPE_SYSTEM, NULL) != HUSH_OK)
        return HUSH_ERR_IO;
    if (hush_skill_load_scope(cat, HUSH_SKILL_SCOPE_USER, NULL) != HUSH_OK)
        return HUSH_ERR_IO;
    return hush_skill_load_robots(cat);
}

hush_status_t hush_skill_forge(const hush_skill_forge_in_t *in,
                               char *out_id, size_t idsz)
{
    char slug[HUSH_SKILL_NAME_MAX];
    char scoped[HUSH_HOME_PATH_MAX];
    char skilldir[HUSH_HOME_PATH_MAX];
    char path[HUSH_HOME_PATH_MAX];
    const char *robot;

    if (in == NULL || out_id == NULL || idsz == 0)
        return HUSH_ERR_ARG;
    out_id[0] = '\0';
    if (strcmp(in->scope, HUSH_SKILL_SCOPE_USER) != 0
        && strcmp(in->scope, HUSH_SKILL_SCOPE_ROBOT) != 0)
        return HUSH_ERR_DENIED;
    robot = (strcmp(in->scope, HUSH_SKILL_SCOPE_ROBOT) == 0) ? in->robot : NULL;
    if (strcmp(in->scope, HUSH_SKILL_SCOPE_ROBOT) == 0
        && (robot == NULL || robot[0] == '\0'))
        return HUSH_ERR_PARSE;
    hush_skill_slugify(slug, sizeof(slug), in->name);
    if (slug[0] == '\0')
        return HUSH_ERR_PARSE;
    if (hush_home_ensure() != HUSH_OK)
        return HUSH_ERR_IO;
    if (hush_home_skills_dir(scoped, sizeof(scoped), in->scope, robot) != HUSH_OK)
        return HUSH_ERR_IO;
    if (hush_skill_mkdir(scoped) != HUSH_OK)
        return HUSH_ERR_IO;
    hush_skill_join(skilldir, sizeof(skilldir), scoped, slug);
    if (skilldir[0] == '\0' || hush_skill_mkdir(skilldir) != HUSH_OK)
        return HUSH_ERR_IO;
    hush_skill_join(path, sizeof(path), skilldir, HUSH_SKILL_FILE_NAME);
    if (path[0] == '\0')
        return HUSH_ERR_FULL;
    if (hush_skill_write_file(path, in, slug) != HUSH_OK)
        return HUSH_ERR_IO;
    return hush_skill_make_id(out_id, idsz, in->scope, robot, slug);
}

hush_status_t hush_skill_format_json(const hush_skill_catalog_t *cat,
                                     int with_voices,
                                     char *out, size_t outsz,
                                     size_t *out_len)
{
    size_t off = 0;
    size_t i;
    int n;

    if (cat == NULL || out == NULL || outsz == 0)
        return HUSH_ERR_ARG;
    n = snprintf(out, outsz,
                 "{\"ok\":true,\"scopes\":[\"system\",\"user\",\"robot\"],"
                 "\"watermarks\":{\"slots_low\":%d,\"slots_high\":%d,"
                 "\"chars_low\":%d,\"chars_high\":%d,"
                 "\"complex_low\":%d,\"complex_high\":%d},\"skills\":[",
                 HUSH_SKILL_EQUIP_LOW, HUSH_SKILL_EQUIP_MAX,
                 HUSH_SKILL_CHAR_LOW, HUSH_SKILL_CHAR_HIGH,
                 HUSH_SKILL_COMPLEX_LOW, HUSH_SKILL_COMPLEX_HIGH);
    if (n < 0 || (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    off = (size_t)n;
    for (i = 0; i < cat->nskills; ++i) {
        if (hush_skill_put_one(out, outsz, &off, &cat->skills[i], i == 0)
            != HUSH_OK)
            return HUSH_ERR_FULL;
    }
    if (hush_skill_put_voices(out, outsz, &off, with_voices) != HUSH_OK)
        return HUSH_ERR_FULL;
    if (out_len != NULL)
        *out_len = off;
    return HUSH_OK;
}

int hush_skill_is_voice(const char *id)
{
    size_t i;

    if (id == NULL || id[0] == '\0')
        return 0;
    for (i = 0; i < (size_t)HUSH_SKILL_VOICE_COUNT; ++i) {
        if (strcmp(id, hush_skill_voices[i]) == 0)
            return 1;
    }
    return 0;
}

const char *hush_skill_voice_id(size_t idx)
{
    if (idx >= (size_t)HUSH_SKILL_VOICE_COUNT)
        return "";
    return hush_skill_voices[idx];
}

const char *hush_skill_forge_body(void)
{
    return HUSH_SKILL_FORGE_BODY;
}

void hush_skill_score_body(const char *body, size_t *out_chars, int *out_complex)
{
    size_t i;
    size_t n;
    int heads = 0;
    int fences = 0;
    int lists = 0;
    int at_line = 1;

    n = (body != NULL) ? strlen(body) : 0;
    if (out_chars != NULL)
        *out_chars = n;
    for (i = 0; i < n; i++) {
        if (at_line && body[i] == '#')
            heads++;
        if (at_line && (body[i] == '-' || body[i] == '*') &&
            i + 1 < n && body[i + 1] == ' ')
            lists++;
        if (at_line && body[i] == '`') {
            if (i + 2 < n && body[i + 1] == '`' && body[i + 2] == '`')
                fences++;
        }
        at_line = (body[i] == '\n');
    }
    if (out_complex != NULL)
        *out_complex = heads * HUSH_SKILL_HEAD_WEIGHT +
            fences * HUSH_SKILL_FENCE_WEIGHT + lists +
            (int)(n / (size_t)HUSH_SKILL_CHAR_DIV);
}

const hush_skill_t *hush_skill_find(const hush_skill_catalog_t *cat,
                                    const char *id)
{
    size_t i;

    if (cat == NULL || id == NULL || id[0] == '\0')
        return NULL;
    for (i = 0; i < cat->nskills; i++) {
        if (strcmp(cat->skills[i].id, id) == 0)
            return &cat->skills[i];
    }
    return NULL;
}

int hush_skill_role_ok(const hush_skill_t *skill, const char *robot_role)
{
    const char *need;
    const char *who;

    if (skill == NULL)
        return 0;
    need = skill->role[0] ? skill->role : HUSH_SKILL_ROLE_ANY;
    if (strcmp(need, HUSH_SKILL_ROLE_ANY) == 0)
        return 1;
    who = (robot_role != NULL && robot_role[0] != '\0')
        ? robot_role : HUSH_SKILL_ROLE_WORKER;
    return strcmp(need, who) == 0;
}

hush_status_t hush_skill_try_equip(const hush_skill_catalog_t *cat,
                                   char ids[][HUSH_SKILL_ID_MAX],
                                   size_t *nids,
                                   const char *skill_id,
                                   const char *robot_role)
{
    const hush_skill_t *skill;
    const hush_skill_t *worn;
    size_t i;
    size_t chars = 0;
    int complex = 0;

    if (cat == NULL || ids == NULL || nids == NULL || skill_id == NULL)
        return HUSH_ERR_ARG;
    skill = hush_skill_find(cat, skill_id);
    if (skill == NULL)
        return HUSH_ERR_NOT_FOUND;
    if (!hush_skill_role_ok(skill, robot_role))
        return HUSH_ERR_DENIED;
    for (i = 0; i < *nids; i++) {
        if (strcmp(ids[i], skill_id) == 0)
            return HUSH_OK;
        worn = hush_skill_find(cat, ids[i]);
        if (worn == NULL)
            continue;
        chars += worn->chars;
        complex += worn->complexity;
    }
    if (*nids >= (size_t)HUSH_SKILL_EQUIP_MAX)
        return HUSH_ERR_FULL;
    if (chars + skill->chars > (size_t)HUSH_SKILL_CHAR_HIGH)
        return HUSH_ERR_FULL;
    if (complex + skill->complexity > HUSH_SKILL_COMPLEX_HIGH)
        return HUSH_ERR_FULL;
    hush_skill_copy(ids[*nids], sizeof(ids[0]), skill_id);
    (*nids)++;
    return HUSH_OK;
}

hush_status_t hush_skill_seed_pack(const char *pack_dir)
{
    DIR *dp;
    struct dirent *ent;
    char child[HUSH_HOME_PATH_MAX];
    char src[HUSH_HOME_PATH_MAX];
    char dest_dir[HUSH_HOME_PATH_MAX];
    char dest[HUSH_HOME_PATH_MAX];
    char sysdir[HUSH_HOME_PATH_MAX];
    struct stat st;
    size_t n = 0;

    if (pack_dir == NULL || pack_dir[0] == '\0')
        return HUSH_ERR_ARG;
    if (hush_home_ensure() != HUSH_OK)
        return HUSH_ERR_IO;
    if (hush_home_skills_dir(sysdir, sizeof(sysdir),
                             HUSH_SKILL_SCOPE_SYSTEM, NULL) != HUSH_OK)
        return HUSH_ERR_IO;
    dp = opendir(pack_dir);
    if (dp == NULL)
        return HUSH_ERR_NOT_FOUND;
    while ((ent = readdir(dp)) != NULL && n < (size_t)HUSH_SKILL_DIR_MAX) {
        if (ent->d_name[0] == '.')
            continue;
        n++;
        hush_skill_join(child, sizeof(child), pack_dir, ent->d_name);
        hush_skill_join(src, sizeof(src), child, HUSH_SKILL_FILE_NAME);
        if (src[0] == '\0' || stat(src, &st) != 0)
            continue;
        hush_skill_join(dest_dir, sizeof(dest_dir), sysdir, ent->d_name);
        if (dest_dir[0] == '\0' || hush_skill_mkdir(dest_dir) != HUSH_OK)
            continue;
        hush_skill_join(dest, sizeof(dest), dest_dir, HUSH_SKILL_FILE_NAME);
        if (dest[0] == '\0' || stat(dest, &st) == 0)
            continue;
        (void)hush_skill_copy_file(src, dest);
    }
    closedir(dp);
    return HUSH_OK;
}

static int hush_skill_is_scope(const char *scope)
{
    assert(scope != NULL);
    if (strcmp(scope, HUSH_SKILL_SCOPE_SYSTEM) == 0)
        return 1;
    if (strcmp(scope, HUSH_SKILL_SCOPE_USER) == 0)
        return 1;
    return strcmp(scope, HUSH_SKILL_SCOPE_ROBOT) == 0;
}

static void hush_skill_slugify(char *dst, size_t dstsz, const char *name)
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

static hush_status_t hush_skill_make_id(char *out, size_t outsz,
                                        const char *scope,
                                        const char *robot,
                                        const char *slug)
{
    int n;

    assert(out != NULL);
    assert(scope != NULL);
    assert(slug != NULL);
    if (strcmp(scope, HUSH_SKILL_SCOPE_ROBOT) == 0 && robot != NULL
        && robot[0] != '\0')
        n = snprintf(out, outsz, "robot:%s:%s", robot, slug);
    else
        n = snprintf(out, outsz, "%s:%s", scope, slug);
    if (n <= 0 || (size_t)n >= outsz) {
        out[0] = '\0';
        return HUSH_ERR_FULL;
    }
    return HUSH_OK;
}

static void hush_skill_copy(char *dst, size_t dstsz, const char *src)
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

static void hush_skill_join(char *out, size_t outsz, const char *a, const char *b)
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

static hush_status_t hush_skill_load_scope(hush_skill_catalog_t *cat,
                                           const char *scope,
                                           const char *robot)
{
    char dir[HUSH_HOME_PATH_MAX];
    char child[HUSH_HOME_PATH_MAX];
    DIR *dp;
    struct dirent *ent;
    size_t n = 0;

    assert(cat != NULL);
    assert(scope != NULL);
    assert(hush_skill_is_scope(scope));
    if (hush_home_skills_dir(dir, sizeof(dir), scope, robot) != HUSH_OK)
        return HUSH_ERR_IO;
    dp = opendir(dir);
    if (dp == NULL)
        return HUSH_OK;
    while ((ent = readdir(dp)) != NULL && n < (size_t)HUSH_SKILL_DIR_MAX) {
        if (ent->d_name[0] == '.')
            continue;
        n++;
        hush_skill_join(child, sizeof(child), dir, ent->d_name);
        if (child[0] == '\0')
            continue;
        (void)hush_skill_read_one(cat, scope, robot, ent->d_name, child);
    }
    closedir(dp);
    return HUSH_OK;
}

static hush_status_t hush_skill_load_robots(hush_skill_catalog_t *cat)
{
    char dir[HUSH_HOME_PATH_MAX];
    DIR *dp;
    struct dirent *ent;
    size_t n = 0;

    assert(cat != NULL);
    if (hush_home_skills_dir(dir, sizeof(dir), HUSH_SKILL_SCOPE_ROBOT, NULL)
        != HUSH_OK)
        return HUSH_ERR_IO;
    dp = opendir(dir);
    if (dp == NULL)
        return HUSH_OK;
    while ((ent = readdir(dp)) != NULL && n < (size_t)HUSH_SKILL_DIR_MAX) {
        if (ent->d_name[0] == '.')
            continue;
        n++;
        (void)hush_skill_load_scope(cat, HUSH_SKILL_SCOPE_ROBOT, ent->d_name);
    }
    closedir(dp);
    return HUSH_OK;
}

static hush_status_t hush_skill_read_one(hush_skill_catalog_t *cat,
                                         const char *scope,
                                         const char *robot,
                                         const char *slug,
                                         const char *dir)
{
    char path[HUSH_HOME_PATH_MAX];
    hush_skill_t *slot;
    struct stat st;

    assert(cat != NULL);
    assert(scope != NULL);
    assert(slug != NULL);
    assert(dir != NULL);
    if (cat->nskills >= (size_t)HUSH_SKILL_CATALOG_MAX)
        return HUSH_ERR_FULL;
    hush_skill_join(path, sizeof(path), dir, HUSH_SKILL_FILE_NAME);
    if (path[0] == '\0' || stat(path, &st) != 0 || !S_ISREG(st.st_mode))
        return HUSH_ERR_NOT_FOUND;
    slot = &cat->skills[cat->nskills];
    memset(slot, 0, sizeof(*slot));
    hush_skill_copy(slot->name, sizeof(slot->name), slug);
    hush_skill_copy(slot->scope, sizeof(slot->scope), scope);
    if (robot != NULL)
        hush_skill_copy(slot->robot, sizeof(slot->robot), robot);
    if (hush_skill_make_id(slot->id, sizeof(slot->id), scope, robot, slug)
        != HUSH_OK)
        return HUSH_ERR_FULL;
    hush_skill_copy(slot->role, sizeof(slot->role), HUSH_SKILL_ROLE_ANY);
    hush_skill_read_summary(slot->summary, sizeof(slot->summary), path);
    {
        FILE *fp;
        char line[HUSH_SKILL_LINE_MAX];
        size_t i;
        char body[HUSH_SKILL_BODY_MAX];

        fp = fopen(path, "r");
        if (fp != NULL) {
            for (i = 0; i < (size_t)HUSH_SKILL_HEAD_LINES; i++) {
                if (fgets(line, (int)sizeof(line), fp) == NULL)
                    break;
                hush_skill_take_meta_line(slot->role, sizeof(slot->role),
                                          slot->category, sizeof(slot->category),
                                          line);
            }
            fclose(fp);
        }
        hush_skill_read_body(body, sizeof(body), path);
        hush_skill_score_body(body, &slot->chars, &slot->complexity);
    }
    cat->nskills++;
    return HUSH_OK;
}

static void hush_skill_read_summary(char *dst, size_t dstsz, const char *path)
{
    FILE *fp;
    char line[HUSH_SKILL_LINE_MAX];
    size_t i;

    assert(dst != NULL);
    assert(dstsz > 0);
    assert(path != NULL);
    dst[0] = '\0';
    fp = fopen(path, "r");
    if (fp == NULL)
        return;
    for (i = 0; i < (size_t)HUSH_SKILL_HEAD_LINES; ++i) {
        if (fgets(line, (int)sizeof(line), fp) == NULL)
            break;
        hush_skill_take_summary_line(dst, dstsz, line);
        if (dst[0] != '\0')
            break;
    }
    fclose(fp);
}

static void hush_skill_take_summary_line(char *dst, size_t dstsz, const char *line)
{
    const char *p;

    assert(dst != NULL);
    assert(line != NULL);
    if (dst[0] != '\0')
        return;
    if (strncmp(line, "description:", 12) == 0) {
        p = line + 12;
        while (*p == ' ')
            p++;
        hush_skill_copy(dst, dstsz, p);
        dst[strcspn(dst, "\r\n")] = '\0';
        return;
    }
    if (line[0] == '#' && line[1] == ' ') {
        hush_skill_copy(dst, dstsz, line + 2);
        dst[strcspn(dst, "\r\n")] = '\0';
    }
}

static hush_status_t hush_skill_mkdir(const char *path)
{
    assert(path != NULL);
    if (path[0] == '\0')
        return HUSH_ERR_IO;
    if (mkdir(path, 0700) != 0 && errno != EEXIST)
        return HUSH_ERR_IO;
    return HUSH_OK;
}

static hush_status_t hush_skill_write_file(const char *path,
                                           const hush_skill_forge_in_t *in,
                                           const char *slug)
{
    FILE *fp;
    const char *summary;
    const char *body;

    assert(path != NULL);
    assert(in != NULL);
    assert(slug != NULL);
    summary = (in->summary[0] != '\0') ? in->summary : slug;
    body = (in->body[0] != '\0') ? in->body : summary;
    fp = fopen(path, "w");
    if (fp == NULL)
        return HUSH_ERR_IO;
    fprintf(fp, "---\nname: %s\ndescription: %s\n---\n\n# %s\n\n%s\n",
            slug, summary, slug, body);
    fclose(fp);
    return HUSH_OK;
}

static hush_status_t hush_skill_put_one(char *out, size_t outsz, size_t *off,
                                        const hush_skill_t *skill, int first)
{
    char name[HUSH_SKILL_NAME_MAX * 2];
    char sum[HUSH_SKILL_SUMMARY_MAX * 2];
    char robot[HUSH_SKILL_ROBOT_MAX * 2];
    int n;

    assert(out != NULL);
    assert(off != NULL);
    assert(skill != NULL);
    hush_json_escape(skill->name, name, sizeof(name));
    hush_json_escape(skill->summary, sum, sizeof(sum));
    hush_json_escape(skill->robot, robot, sizeof(robot));
    n = snprintf(out + *off, outsz - *off,
                 "%s{\"id\":\"%s\",\"name\":\"%s\",\"scope\":\"%s\","
                 "\"robot\":\"%s\",\"summary\":\"%s\",\"role\":\"%s\","
                 "\"category\":\"%s\",\"chars\":%zu,\"complexity\":%d}",
                 first ? "" : ",",
                 skill->id, name, skill->scope, robot, sum,
                 skill->role[0] ? skill->role : HUSH_SKILL_ROLE_ANY,
                 skill->category, skill->chars, skill->complexity);
    if (n < 0 || *off + (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    *off += (size_t)n;
    return HUSH_OK;
}

static hush_status_t hush_skill_put_voices(char *out, size_t outsz, size_t *off,
                                           int with_voices)
{
    size_t i;
    int n;

    assert(out != NULL);
    assert(off != NULL);
    n = snprintf(out + *off, outsz - *off, "],\"voices\":[");
    if (n < 0 || *off + (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    *off += (size_t)n;
    if (with_voices) {
        for (i = 0; i < (size_t)HUSH_SKILL_VOICE_COUNT; ++i) {
            n = snprintf(out + *off, outsz - *off, "%s\"%s\"",
                         (i == 0) ? "" : ",", hush_skill_voices[i]);
            if (n < 0 || *off + (size_t)n >= outsz)
                return HUSH_ERR_FULL;
            *off += (size_t)n;
        }
    }
    n = snprintf(out + *off, outsz - *off, "]}\n");
    if (n < 0 || *off + (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    *off += (size_t)n;
    return HUSH_OK;
}

static void hush_skill_read_body(char *dst, size_t dstsz, const char *path)
{
    FILE *fp;
    size_t n;

    assert(dst != NULL);
    assert(dstsz > 0);
    dst[0] = '\0';
    if (path == NULL)
        return;
    fp = fopen(path, "r");
    if (fp == NULL)
        return;
    n = fread(dst, 1, dstsz - 1, fp);
    dst[n] = '\0';
    fclose(fp);
}

static void hush_skill_take_meta_line(char *role, size_t rolesz,
                                      char *category, size_t catsz,
                                      const char *line)
{
    const char *p;

    assert(role != NULL);
    assert(category != NULL);
    assert(line != NULL);
    if (strncmp(line, "role:", 5) == 0) {
        p = line + 5;
        while (*p == ' ')
            p++;
        hush_skill_copy(role, rolesz, p);
        role[strcspn(role, "\r\n")] = '\0';
        return;
    }
    if (strncmp(line, "category:", 9) == 0) {
        p = line + 9;
        while (*p == ' ')
            p++;
        hush_skill_copy(category, catsz, p);
        category[strcspn(category, "\r\n")] = '\0';
    }
}

static hush_status_t hush_skill_copy_file(const char *src, const char *dst)
{
    FILE *in;
    FILE *out;
    char buf[512];
    size_t n;

    assert(src != NULL);
    assert(dst != NULL);
    in = fopen(src, "r");
    if (in == NULL)
        return HUSH_ERR_IO;
    out = fopen(dst, "w");
    if (out == NULL) {
        fclose(in);
        return HUSH_ERR_IO;
    }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            return HUSH_ERR_IO;
        }
    }
    fclose(in);
    fclose(out);
    return HUSH_OK;
}
