/* hush_skill.h: three-scope skill catalog, forge writer, robot voices. */

#ifndef HUSH_SKILL_H
#define HUSH_SKILL_H

#include <stddef.h>
#include "hush_status.h"

enum {
    HUSH_SKILL_NAME_MAX = 64,
    HUSH_SKILL_ID_MAX = 96,
    HUSH_SKILL_SUMMARY_MAX = 160,
    HUSH_SKILL_BODY_MAX = 4096,
    HUSH_SKILL_SCOPE_MAX = 16,
    HUSH_SKILL_ROBOT_MAX = 64,
    HUSH_SKILL_CATALOG_MAX = 256,
    HUSH_SKILL_EQUIP_MAX = 8,
    HUSH_SKILL_EQUIP_LOW = 1,
    HUSH_SKILL_CHAR_LOW = 200,
    HUSH_SKILL_CHAR_HIGH = 8000,
    HUSH_SKILL_COMPLEX_LOW = 2,
    HUSH_SKILL_COMPLEX_HIGH = 64,
    HUSH_SKILL_JSON_MAX = 65536,
    HUSH_SKILL_VOICE_MAX = 32,
    HUSH_SKILL_VOICE_COUNT = 6,
    HUSH_SKILL_ROLE_MAX = 16,
    HUSH_SKILL_CATEGORY_MAX = 32
};

#define HUSH_SKILL_SCOPE_SYSTEM "system"
#define HUSH_SKILL_SCOPE_USER "user"
#define HUSH_SKILL_SCOPE_ROBOT "robot"
#define HUSH_SKILL_ROLE_WORKER "worker"
#define HUSH_SKILL_ROLE_CHAPERON "chaperon"
#define HUSH_SKILL_ROLE_ANY "any"
#define HUSH_SKILL_FORGE_SLUG "forge-skill"
#define HUSH_SKILL_FORGE_ID "system:forge-skill"
#define HUSH_SKILL_FILE_NAME "SKILL.md"

typedef struct {
    char id[HUSH_SKILL_ID_MAX];
    char name[HUSH_SKILL_NAME_MAX];
    char scope[HUSH_SKILL_SCOPE_MAX];
    char robot[HUSH_SKILL_ROBOT_MAX];
    char summary[HUSH_SKILL_SUMMARY_MAX];
    char role[HUSH_SKILL_ROLE_MAX];
    char category[HUSH_SKILL_CATEGORY_MAX];
    size_t chars;
    int complexity;
} hush_skill_t;

typedef struct {
    hush_skill_t skills[HUSH_SKILL_CATALOG_MAX];
    size_t nskills;
} hush_skill_catalog_t;

typedef struct {
    char name[HUSH_SKILL_NAME_MAX];
    char summary[HUSH_SKILL_SUMMARY_MAX];
    char body[HUSH_SKILL_BODY_MAX];
    char scope[HUSH_SKILL_SCOPE_MAX];
    char robot[HUSH_SKILL_ROBOT_MAX];
} hush_skill_forge_in_t;

/* Zeros the catalog. Safe on NULL. */
void hush_skill_init_catalog(hush_skill_catalog_t *cat);

/* Scans ~/.hush/skills into cat. Seeds forge-skill when missing. */
hush_status_t hush_skill_load_catalog(hush_skill_catalog_t *cat);

/* Writes a new SKILL.md under user or robot scope. Fills out_id. */
hush_status_t hush_skill_forge(const hush_skill_forge_in_t *in,
                               char *out_id, size_t idsz);

/* Writes catalog JSON including scopes and optional voices. */
hush_status_t hush_skill_format_json(const hush_skill_catalog_t *cat,
                                     int with_voices,
                                     char *out, size_t outsz,
                                     size_t *out_len);

/* True when id is a known robot voice. */
int hush_skill_is_voice(const char *id);

/* Voice id at idx, or empty string when out of range. */
const char *hush_skill_voice_id(size_t idx);

/* Canonical forge-skill markdown. Used to seed ~/.hush/skills/system. */
const char *hush_skill_forge_body(void);

/* Character and complexity scores for a SKILL.md body. */
void hush_skill_score_body(const char *body, size_t *out_chars, int *out_complex);

/* Finds a catalog skill by id. NULL when missing. */
const hush_skill_t *hush_skill_find(const hush_skill_catalog_t *cat,
                                    const char *id);

/* True when skill.role may be worn by robot_role (worker/chaperon). */
int hush_skill_role_ok(const hush_skill_t *skill, const char *robot_role);

/* Equips skill_id onto ids[*nids] when role and watermarks allow.
 * Fails HUSH_ERR_DENIED on a role wall, HUSH_ERR_FULL over cap,
 * HUSH_ERR_NOT_FOUND when the id is unknown. */
hush_status_t hush_skill_try_equip(const hush_skill_catalog_t *cat,
                                   char ids[][HUSH_SKILL_ID_MAX],
                                   size_t *nids,
                                   const char *skill_id,
                                   const char *robot_role);

/* Copies pack_dir/<slug>/SKILL.md into ~/.hush/skills/system when missing. */
hush_status_t hush_skill_seed_pack(const char *pack_dir);

#endif /* HUSH_SKILL_H */
