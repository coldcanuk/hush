/* hush_favorite.h: per-robot named skill loadout favorites (PE-4 v1). */

#ifndef HUSH_FAVORITE_H
#define HUSH_FAVORITE_H

#include <stddef.h>

#include "hush_skill.h"
#include "hush_status.h"

enum {
    HUSH_FAVORITE_NAME_MAX = 48,
    HUSH_FAVORITE_SLUG_MAX = 64,
    HUSH_FAVORITE_COUNT_MAX = 32,
    HUSH_FAVORITE_FILE_MAX = 4096,
    HUSH_FAVORITE_JSON_MAX = 16384
};

typedef struct {
    char name[HUSH_FAVORITE_NAME_MAX];
    char skills[HUSH_SKILL_EQUIP_MAX][HUSH_SKILL_ID_MAX];
    size_t nskills;
} hush_favorite_t;

/* Saves name as the 1..8 skill ids under robots/<robot>/loadouts/.
 * Fails with HUSH_ERR_ARG on bad pointers, HUSH_ERR_PARSE on an empty
 * name or bad robot slug, HUSH_ERR_DENIED on unknown or cross-slug
 * skills, HUSH_ERR_FULL over cap or overflow, HUSH_ERR_IO on disk. */
hush_status_t hush_favorite_save(const char *robot, const char *name,
                                 char ids[][HUSH_SKILL_ID_MAX], size_t nids);

/* Reads name into out. Fails with HUSH_ERR_ARG, HUSH_ERR_NOT_FOUND,
 * HUSH_ERR_IO, or HUSH_ERR_PARSE on a corrupt file. */
hush_status_t hush_favorite_load(const char *robot, const char *name,
                                 hush_favorite_t *out);

/* Removes name from the list only; the worn doll is untouched.
 * Fails with HUSH_ERR_ARG, HUSH_ERR_NOT_FOUND, or HUSH_ERR_IO. */
hush_status_t hush_favorite_delete(const char *robot, const char *name);

/* Writes {"ok":true,"robot":..,"favorites":[..]} into out.
 * Missing robots/<robot>/loadouts/ lists empty. Fails with
 * HUSH_ERR_ARG, HUSH_ERR_FULL, or HUSH_ERR_IO. */
hush_status_t hush_favorite_list_json(const char *robot, char *out,
                                      size_t outsz, size_t *out_len);

#endif /* HUSH_FAVORITE_H */
