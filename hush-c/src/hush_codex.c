/* hush_codex.c: exposes Hush's complete C skill to isolated Codex jobs. */

#define _XOPEN_SOURCE 700

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hush_codex.h"

enum {
    HUSH_CODEX_DIRECTORY_MODE = 0700
};

#define HUSH_CODEX_SKILL_ENV "HUSH_CODEX_SKILL_DIR"
#define HUSH_CODEX_SKILL_NAME "write-legible-c"
#define HUSH_CODEX_ENTRYPOINT "SKILL.md"
#define HUSH_CODEX_REFERENCE "references/c-standard.md"

/* Formats a child path into borrowed output; all pointers non-NULL.
 * Returns HUSH_ERR_IO on overflow. */
static hush_status_t hush_codex_join_path(char *out, size_t len,
                                         const char *parent, const char *child);
/* True when the borrowed non-NULL directory has the complete skill. */
static int hush_codex_has_skill(const char *directory);
/* Resolves a complete skill into borrowed PATH_MAX output. Fails HUSH_ERR_IO. */
static hush_status_t hush_codex_resolve_skill(char *out);
/* Creates the non-NULL directory, accepting an existing directory only.
 * Filesystem failures return HUSH_ERR_IO. */
static hush_status_t hush_codex_make_directory(const char *path);
/* Creates a link between non-NULL paths; an existing link must resolve to target.
 * Returns HUSH_ERR_IO on conflict or filesystem failure. */
static hush_status_t hush_codex_link_skill(const char *target, const char *link);

hush_status_t hush_codex_prepare_skills(const char *cwd)
{
    if (cwd == NULL || cwd[0] == '\0')
        return HUSH_ERR_ARG;
    char target[PATH_MAX] = {0};
    if (hush_codex_resolve_skill(target) != HUSH_OK)
        return HUSH_ERR_IO;
    char agents[PATH_MAX] = {0};
    if (hush_codex_join_path(agents, sizeof(agents), cwd, ".agents") != HUSH_OK)
        return HUSH_ERR_IO;
    if (hush_codex_make_directory(agents) != HUSH_OK)
        return HUSH_ERR_IO;
    char skills[PATH_MAX] = {0};
    if (hush_codex_join_path(skills, sizeof(skills), agents, "skills") != HUSH_OK)
        return HUSH_ERR_IO;
    if (hush_codex_make_directory(skills) != HUSH_OK)
        return HUSH_ERR_IO;
    char link[PATH_MAX] = {0};
    if (hush_codex_join_path(link, sizeof(link), skills, HUSH_CODEX_SKILL_NAME) != HUSH_OK)
        return HUSH_ERR_IO;
    return hush_codex_link_skill(target, link);
}

static hush_status_t hush_codex_join_path(char *out, size_t len,
                                         const char *parent, const char *child)
{
    assert(out != NULL);
    assert(len > 0);
    assert(parent != NULL);
    assert(child != NULL);
    int written = snprintf(out, len, "%s/%s", parent, child);
    return written < 0 || (size_t)written >= len ? HUSH_ERR_IO : HUSH_OK;
}

static int hush_codex_has_skill(const char *directory)
{
    assert(directory != NULL);
    const char *files[] = {HUSH_CODEX_ENTRYPOINT, HUSH_CODEX_REFERENCE};
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        char path[PATH_MAX] = {0};
        if (hush_codex_join_path(path, sizeof(path), directory, files[i]) != HUSH_OK)
            return 0;
        if (access(path, R_OK) != 0)
            return 0;
    }
    return 1;
}

static hush_status_t hush_codex_resolve_skill(char *out)
{
    assert(out != NULL);
    const char *override = getenv(HUSH_CODEX_SKILL_ENV);
    if (override != NULL && override[0] != '\0') {
        if (realpath(override, out) == NULL || !hush_codex_has_skill(out))
            return HUSH_ERR_IO;
        return HUSH_OK;
    }
    const char *candidates[] = {
#ifdef HUSH_CODEX_SKILL_SOURCE
        HUSH_CODEX_SKILL_SOURCE,
#endif
#ifdef HUSH_CODEX_SKILL_SHARE
        HUSH_CODEX_SKILL_SHARE,
#endif
        ".agents/skills/" HUSH_CODEX_SKILL_NAME
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        if (realpath(candidates[i], out) != NULL && hush_codex_has_skill(out))
            return HUSH_OK;
    }
    return HUSH_ERR_IO;
}

static hush_status_t hush_codex_make_directory(const char *path)
{
    assert(path != NULL);
    if (mkdir(path, (mode_t)HUSH_CODEX_DIRECTORY_MODE) == 0)
        return HUSH_OK;
    if (errno != EEXIST)
        return HUSH_ERR_IO;
    struct stat info = {0};
    if (lstat(path, &info) != 0 || !S_ISDIR(info.st_mode))
        return HUSH_ERR_IO;
    return HUSH_OK;
}

static hush_status_t hush_codex_link_skill(const char *target, const char *link)
{
    assert(target != NULL);
    assert(link != NULL);
    if (symlink(target, link) == 0)
        return HUSH_OK;
    if (errno != EEXIST)
        return HUSH_ERR_IO;
    char resolved[PATH_MAX] = {0};
    if (realpath(link, resolved) == NULL || strcmp(target, resolved) != 0)
        return HUSH_ERR_IO;
    return HUSH_OK;
}
