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
    HUSH_SKILL_CATALOG_MAX = 64,
    HUSH_SKILL_EQUIP_MAX = 8,
    HUSH_SKILL_JSON_MAX = 16384,
    HUSH_SKILL_VOICE_MAX = 32,
    HUSH_SKILL_VOICE_COUNT = 6
};

#define HUSH_SKILL_SCOPE_SYSTEM "system"
#define HUSH_SKILL_SCOPE_USER "user"
#define HUSH_SKILL_SCOPE_ROBOT "robot"
#define HUSH_SKILL_FORGE_SLUG "forge-skill"
#define HUSH_SKILL_FORGE_ID "system:forge-skill"
#define HUSH_SKILL_FILE_NAME "SKILL.md"

typedef struct {
    char id[HUSH_SKILL_ID_MAX];
    char name[HUSH_SKILL_NAME_MAX];
    char scope[HUSH_SKILL_SCOPE_MAX];
    char robot[HUSH_SKILL_ROBOT_MAX];
    char summary[HUSH_SKILL_SUMMARY_MAX];
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

#endif /* HUSH_SKILL_H */
