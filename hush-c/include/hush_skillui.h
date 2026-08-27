/* hush_skillui.h: static HTML/CSS design-token extract (no npm). */

#ifndef HUSH_SKILLUI_H
#define HUSH_SKILLUI_H

#include <stddef.h>
#include "hush_status.h"

enum {
    HUSH_SKILLUI_TOKEN_MAX = 32,
    HUSH_SKILLUI_TOKEN_LEN = 80,
    HUSH_SKILLUI_JSON_MAX = 8192,
    HUSH_SKILLUI_NAME_MAX = 64
};

typedef struct {
    char colors[HUSH_SKILLUI_TOKEN_MAX][HUSH_SKILLUI_TOKEN_LEN];
    size_t ncolors;
    char fonts[HUSH_SKILLUI_TOKEN_MAX][HUSH_SKILLUI_TOKEN_LEN];
    size_t nfonts;
    char spaces[HUSH_SKILLUI_TOKEN_MAX][HUSH_SKILLUI_TOKEN_LEN];
    size_t nspaces;
} hush_skillui_t;

/* Zeros tokens. Safe on NULL. */
void hush_skillui_init(hush_skillui_t *out);

/* Walks html for colors, font-family, and spacing tokens. */
hush_status_t hush_skillui_extract(hush_skillui_t *out, const char *html,
                                   size_t n);

/* Writes {"ok":true,"colors":[...],"fonts":[...],"spaces":[...]}. */
hush_status_t hush_skillui_format_json(const hush_skillui_t *tok,
                                       char *out, size_t outsz, size_t *out_len);

/* Writes dir/<slug>/SKILL.md as a reverse-engineering skill package. */
hush_status_t hush_skillui_write_skill(const char *dir, const char *name,
                                       const hush_skillui_t *tok);

#endif /* HUSH_SKILLUI_H */
