/* hush_skillui.c: owns static HTML/CSS token extract for Hush skills. */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "hush_json.h"
#include "hush_skill.h"
#include "hush_skillui.h"

enum {
    HUSH_SKILLUI_HEX_MIN = 3,
    HUSH_SKILLUI_HEX_MAX = 8
};

static void hush_skillui_copy(char *dst, size_t dstsz, const char *src);
static void hush_skillui_push(char list[][HUSH_SKILLUI_TOKEN_LEN], size_t *n,
                              const char *tok);
static void hush_skillui_take_hex(hush_skillui_t *out, const char *p);
static void hush_skillui_take_rgb(hush_skillui_t *out, const char *p);
static void hush_skillui_take_after(char list[][HUSH_SKILLUI_TOKEN_LEN],
                                    size_t *n, const char *p, const char *key);
static hush_status_t hush_skillui_mkdir(const char *path);
static void hush_skillui_slugify(char *dst, size_t dstsz, const char *name);

void hush_skillui_init(hush_skillui_t *out)
{
    if (out == NULL)
        return;
    memset(out, 0, sizeof(*out));
}

hush_status_t hush_skillui_extract(hush_skillui_t *out, const char *html,
                                   size_t n)
{
    size_t i;

    if (out == NULL || html == NULL)
        return HUSH_ERR_ARG;
    hush_skillui_init(out);
    for (i = 0; i < n && html[i] != '\0'; i++) {
        if (html[i] == '#')
            hush_skillui_take_hex(out, html + i);
        if (i + 4 <= n && strncmp(html + i, "rgb(", 4) == 0)
            hush_skillui_take_rgb(out, html + i);
        hush_skillui_take_after(out->fonts, &out->nfonts, html + i,
                                "font-family:");
        hush_skillui_take_after(out->spaces, &out->nspaces, html + i,
                                "font-size:");
        hush_skillui_take_after(out->spaces, &out->nspaces, html + i,
                                "padding:");
        hush_skillui_take_after(out->spaces, &out->nspaces, html + i,
                                "margin:");
    }
    return HUSH_OK;
}

hush_status_t hush_skillui_format_json(const hush_skillui_t *tok,
                                       char *out, size_t outsz, size_t *out_len)
{
    size_t off = 0;
    size_t i;
    int n;

    if (tok == NULL || out == NULL || outsz == 0)
        return HUSH_ERR_ARG;
    n = snprintf(out, outsz, "{\"ok\":true,\"colors\":[");
    if (n < 0 || (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    off = (size_t)n;
    for (i = 0; i < tok->ncolors; i++) {
        char esc[HUSH_SKILLUI_TOKEN_LEN * 2];

        hush_json_escape(tok->colors[i], esc, sizeof(esc));
        n = snprintf(out + off, outsz - off, "%s\"%s\"",
                     (i == 0) ? "" : ",", esc);
        if (n < 0 || off + (size_t)n >= outsz)
            return HUSH_ERR_FULL;
        off += (size_t)n;
    }
    n = snprintf(out + off, outsz - off, "],\"fonts\":[");
    if (n < 0 || off + (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    off += (size_t)n;
    for (i = 0; i < tok->nfonts; i++) {
        char esc[HUSH_SKILLUI_TOKEN_LEN * 2];

        hush_json_escape(tok->fonts[i], esc, sizeof(esc));
        n = snprintf(out + off, outsz - off, "%s\"%s\"",
                     (i == 0) ? "" : ",", esc);
        if (n < 0 || off + (size_t)n >= outsz)
            return HUSH_ERR_FULL;
        off += (size_t)n;
    }
    n = snprintf(out + off, outsz - off, "],\"spaces\":[");
    if (n < 0 || off + (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    off += (size_t)n;
    for (i = 0; i < tok->nspaces; i++) {
        char esc[HUSH_SKILLUI_TOKEN_LEN * 2];

        hush_json_escape(tok->spaces[i], esc, sizeof(esc));
        n = snprintf(out + off, outsz - off, "%s\"%s\"",
                     (i == 0) ? "" : ",", esc);
        if (n < 0 || off + (size_t)n >= outsz)
            return HUSH_ERR_FULL;
        off += (size_t)n;
    }
    n = snprintf(out + off, outsz - off, "]}\n");
    if (n < 0 || off + (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    off += (size_t)n;
    if (out_len != NULL)
        *out_len = off;
    return HUSH_OK;
}

hush_status_t hush_skillui_write_skill(const char *dir, const char *name,
                                       const hush_skillui_t *tok)
{
    char slug[HUSH_SKILLUI_NAME_MAX];
    char path[384];
    FILE *fp;
    size_t i;
    int n;

    if (dir == NULL || name == NULL || tok == NULL)
        return HUSH_ERR_ARG;
    hush_skillui_slugify(slug, sizeof(slug), name);
    if (slug[0] == '\0')
        return HUSH_ERR_PARSE;
    n = snprintf(path, sizeof(path), "%s/%s", dir, slug);
    if (n < 0 || (size_t)n >= sizeof(path))
        return HUSH_ERR_FULL;
    if (hush_skillui_mkdir(path) != HUSH_OK)
        return HUSH_ERR_IO;
    n = snprintf(path, sizeof(path), "%s/%s/%s", dir, slug, HUSH_SKILL_FILE_NAME);
    if (n < 0 || (size_t)n >= sizeof(path))
        return HUSH_ERR_FULL;
    fp = fopen(path, "w");
    if (fp == NULL)
        return HUSH_ERR_IO;
    fprintf(fp,
            "---\nname: %s\ndescription: Hush-adapted SkillUI extract of %s.\n"
            "role: worker\ncategory: reverse-engineering\n---\n\n"
            "# %s\n\nHush-adapted reverse-engineering skill. Not npm SkillUI.\n\n"
            "## Colors\n",
            slug, slug, slug);
    for (i = 0; i < tok->ncolors; i++)
        fprintf(fp, "- `%s`\n", tok->colors[i]);
    fprintf(fp, "\n## Fonts\n");
    for (i = 0; i < tok->nfonts; i++)
        fprintf(fp, "- `%s`\n", tok->fonts[i]);
    fprintf(fp, "\n## Spacing\n");
    for (i = 0; i < tok->nspaces; i++)
        fprintf(fp, "- `%s`\n", tok->spaces[i]);
    fclose(fp);
    return HUSH_OK;
}

static void hush_skillui_copy(char *dst, size_t dstsz, const char *src)
{
    size_t n;

    assert(dst != NULL);
    assert(dstsz > 0);
    dst[0] = '\0';
    if (src == NULL)
        return;
    n = strlen(src);
    if (n + 1 > dstsz)
        n = dstsz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void hush_skillui_push(char list[][HUSH_SKILLUI_TOKEN_LEN], size_t *n,
                              const char *tok)
{
    size_t i;

    assert(n != NULL);
    if (tok == NULL || tok[0] == '\0' || *n >= (size_t)HUSH_SKILLUI_TOKEN_MAX)
        return;
    for (i = 0; i < *n; i++) {
        if (strcmp(list[i], tok) == 0)
            return;
    }
    hush_skillui_copy(list[*n], sizeof(list[0]), tok);
    (*n)++;
}

static void hush_skillui_take_hex(hush_skillui_t *out, const char *p)
{
    char tok[HUSH_SKILLUI_TOKEN_LEN];
    size_t n = 0;

    assert(out != NULL);
    assert(p != NULL);
    if (p[0] != '#')
        return;
    tok[n++] = '#';
    while (n + 1 < sizeof(tok) && isxdigit((unsigned char)p[n]))
        tok[n] = p[n], n++;
    tok[n] = '\0';
    if (n - 1 < (size_t)HUSH_SKILLUI_HEX_MIN ||
        n - 1 > (size_t)HUSH_SKILLUI_HEX_MAX)
        return;
    hush_skillui_push(out->colors, &out->ncolors, tok);
}

static void hush_skillui_take_rgb(hush_skillui_t *out, const char *p)
{
    char tok[HUSH_SKILLUI_TOKEN_LEN];
    size_t n = 0;

    assert(out != NULL);
    assert(p != NULL);
    while (p[n] != '\0' && p[n] != ')' && n + 2 < sizeof(tok)) {
        tok[n] = p[n];
        n++;
    }
    if (p[n] == ')') {
        tok[n++] = ')';
        tok[n] = '\0';
        hush_skillui_push(out->colors, &out->ncolors, tok);
    }
}

static void hush_skillui_take_after(char list[][HUSH_SKILLUI_TOKEN_LEN],
                                    size_t *n, const char *p, const char *key)
{
    char tok[HUSH_SKILLUI_TOKEN_LEN];
    size_t k;
    size_t i = 0;

    assert(p != NULL);
    assert(key != NULL);
    k = strlen(key);
    if (strncmp(p, key, k) != 0)
        return;
    p += k;
    while (*p == ' ' || *p == '\t')
        p++;
    while (*p != '\0' && *p != ';' && *p != '\n' && i + 1 < sizeof(tok)) {
        tok[i++] = *p++;
    }
    tok[i] = '\0';
    while (i > 0 && (tok[i - 1] == ' ' || tok[i - 1] == '"' ||
                     tok[i - 1] == '\'')) {
        i--;
        tok[i] = '\0';
    }
    while (tok[0] == '"' || tok[0] == '\'')
        memmove(tok, tok + 1, strlen(tok));
    hush_skillui_push(list, n, tok);
}

static hush_status_t hush_skillui_mkdir(const char *path)
{
    if (path == NULL || path[0] == '\0')
        return HUSH_ERR_IO;
    if (mkdir(path, 0700) != 0 && errno != EEXIST)
        return HUSH_ERR_IO;
    return HUSH_OK;
}

static void hush_skillui_slugify(char *dst, size_t dstsz, const char *name)
{
    size_t i = 0;
    size_t o = 0;
    unsigned char c;
    int dash = 0;

    assert(dst != NULL);
    dst[0] = '\0';
    if (name == NULL)
        return;
    while (name[i] != '\0' && o + 1 < dstsz) {
        c = (unsigned char)name[i++];
        if (isalnum(c)) {
            dst[o++] = (char)tolower(c);
            dash = 0;
        } else if (!dash && o > 0) {
            dst[o++] = '-';
            dash = 1;
        }
    }
    dst[o] = '\0';
}
