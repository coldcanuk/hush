/* hush_favorite.h: per-robot named skill loadout files (PE-4 v1). */

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
    /* nskills always holds 1..HUSH_SKILL_EQUIP_MAX after a successful call. */
    size_t nskills;
} hush_favorite_t;

/* Saves name as the 1..8 skill ids under robots/<robot>/loadouts/.
 * Names allow letters, digits, space, '-' and '_' only (1..47 chars);
 * anything else (% and . included) is refused, never mangled.
 * Overwrites only the exact same display name; a slug clash with a
 * different stored name is refused. Creates the loadouts tree; the only
 * entry that may create directories. Refused saves create nothing.
 * Fails with HUSH_ERR_ARG on bad pointers or a bad robot slug,
 * HUSH_ERR_PARSE on an empty or non-allowlisted favorite name or a
 * corrupt stored file, HUSH_ERR_DENIED on an empty set or unknown,
 * cross-slug, repeated, or clashing skills, HUSH_ERR_FULL over 8
 * skills, over 32 favorites, past the list envelope, on allocation
 * failure, or on overflow, HUSH_ERR_IO on disk errors. */
hush_status_t hush_favorite_save(const char *robot, const char *name,
                                 char ids[][HUSH_SKILL_ID_MAX], size_t nids);

/* Reads name into out, resolving through the file slug: any alias of the
 * stored name addresses it, and out carries the stored name. Creates no
 * directories. Fails with HUSH_ERR_ARG on bad pointers or a bad robot
 * slug, HUSH_ERR_PARSE on a bad name or a corrupt file,
 * HUSH_ERR_NOT_FOUND when missing, HUSH_ERR_FULL on overflow,
 * HUSH_ERR_IO on disk errors. */
hush_status_t hush_favorite_load(const char *robot, const char *name,
                                 hush_favorite_t *out);

/* Removes only the stored file for name, resolving through the file
 * slug like load. Creates no directories. Fails with HUSH_ERR_ARG on
 * bad pointers or a bad robot slug, HUSH_ERR_PARSE on a bad name,
 * HUSH_ERR_NOT_FOUND when missing, HUSH_ERR_FULL on overflow,
 * HUSH_ERR_IO on disk errors. */
hush_status_t hush_favorite_delete(const char *robot, const char *name);

/* Writes {"ok":true,"robot":..,"favorites":[..]} into out, creating no
 * directories; a missing tree lists empty. Skips unreadable files.
 * Fails with HUSH_ERR_ARG on bad pointers or a bad robot slug,
 * HUSH_ERR_FULL on overflow or past HUSH_FAVORITE_COUNT_MAX valid
 * favorites (save refuses the excess, so this stays unreachable),
 * HUSH_ERR_IO when the tree cannot be opened (except missing) or a
 * read fails mid-scan. */
hush_status_t hush_favorite_list_json(const char *robot, char *out,
                                      size_t outsz, size_t *out_len);

/* Escapes src into dst, measuring first so truncation fails instead of
 * cutting. Fails with HUSH_ERR_ARG on bad pointers, HUSH_ERR_FULL when
 * dst cannot hold the escaped form. */
hush_status_t hush_favorite_escape(char *dst, size_t dstsz, const char *src);

/* Appends fav's quoted ids to the open skills array at out+*off.
 * Fails with HUSH_ERR_ARG on bad pointers, HUSH_ERR_FULL on overflow. */
hush_status_t hush_favorite_put_ids(char *out, size_t outsz, size_t *off,
                                    const hush_favorite_t *fav);

#endif /* HUSH_FAVORITE_H */
