/* hush_home.c: owns ~/.hush paths, mkdir ensure, and forge-skill seed. */

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "hush_home.h"
#include "hush_skill.h"

enum {
    HUSH_HOME_MODE = 0700
};

#define HUSH_HOME_LEGACY_TAIL ".config/hush"

/* Copies src into dst. Empty on overflow or NULL src. */
static void hush_home_copy(char *dst, size_t dstsz, const char *src);

/* Joins a/b into out. Empty on overflow. */
static void hush_home_join(char *out, size_t outsz, const char *a, const char *b);

/* True when HUSH_HOME is set or HUSH_CONFIG_DIR is unset. */
static int hush_home_should_make_tree(void);

/* mkdir 0700 path. EEXIST is success. */
static hush_status_t hush_home_mkdir(const char *path);

/* Joins root/name and mkdir. */
static hush_status_t hush_home_mkdir_child(char *out, size_t outsz,
                                           const char *root, const char *name);

/* mkdir config, agents, skills/{system,user,robots} under root. */
static hush_status_t hush_home_make_tree(const char *root);

/* Writes SKILL.md for forge-skill when the file is missing. */
static hush_status_t hush_home_seed_forge(const char *root);

void hush_home_root(char *out, size_t outsz)
{
    const char *env;
    const char *home;
    int n;

    if (out == NULL || outsz == 0)
        return;
    out[0] = '\0';
    env = getenv(HUSH_HOME_ENV);
    if (env != NULL && env[0] != '\0') {
        hush_home_copy(out, outsz, env);
        return;
    }
    home = getenv("HOME");
    if (home == NULL || home[0] == '\0')
        return;
    n = snprintf(out, outsz, "%s/.hush", home);
    if (n <= 0 || (size_t)n >= outsz)
        out[0] = '\0';
}

void hush_home_config_dir(char *out, size_t outsz)
{
    const char *env;
    char root[HUSH_HOME_PATH_MAX];

    if (out == NULL || outsz == 0)
        return;
    out[0] = '\0';
    env = getenv(HUSH_HOME_ENV_CONFIG);
    if (env != NULL && env[0] != '\0') {
        hush_home_copy(out, outsz, env);
        return;
    }
    hush_home_root(root, sizeof(root));
    hush_home_join(out, outsz, root, HUSH_HOME_DIR_CONFIG);
}

void hush_home_agents_dir(char *out, size_t outsz)
{
    char root[HUSH_HOME_PATH_MAX];

    if (out == NULL || outsz == 0)
        return;
    hush_home_root(root, sizeof(root));
    hush_home_join(out, outsz, root, HUSH_HOME_DIR_AGENTS);
}

void hush_home_legacy_config_dir(char *out, size_t outsz)
{
    const char *xdg;
    const char *home;
    int n;

    if (out == NULL || outsz == 0)
        return;
    out[0] = '\0';
    xdg = getenv("XDG_CONFIG_HOME");
    if (xdg != NULL && xdg[0] != '\0') {
        n = snprintf(out, outsz, "%s/hush", xdg);
        if (n <= 0 || (size_t)n >= outsz)
            out[0] = '\0';
        return;
    }
    home = getenv("HOME");
    if (home == NULL || home[0] == '\0')
        return;
    n = snprintf(out, outsz, "%s/%s", home, HUSH_HOME_LEGACY_TAIL);
    if (n <= 0 || (size_t)n >= outsz)
        out[0] = '\0';
}

hush_status_t hush_home_skills_dir(char *out, size_t outsz,
                                   const char *scope, const char *robot)
{
    char root[HUSH_HOME_PATH_MAX];
    char skills[HUSH_HOME_PATH_MAX];
    char scoped[HUSH_HOME_PATH_MAX];

    if (out == NULL || outsz == 0 || scope == NULL)
        return HUSH_ERR_ARG;
    out[0] = '\0';
    if (strcmp(scope, HUSH_SKILL_SCOPE_SYSTEM) != 0
        && strcmp(scope, HUSH_SKILL_SCOPE_USER) != 0
        && strcmp(scope, HUSH_SKILL_SCOPE_ROBOT) != 0)
        return HUSH_ERR_ARG;
    hush_home_root(root, sizeof(root));
    hush_home_join(skills, sizeof(skills), root, HUSH_HOME_DIR_SKILLS);
    if (strcmp(scope, HUSH_SKILL_SCOPE_ROBOT) == 0)
        hush_home_join(scoped, sizeof(scoped), skills, HUSH_HOME_DIR_ROBOTS);
    else
        hush_home_join(scoped, sizeof(scoped), skills, scope);
    if (strcmp(scope, HUSH_SKILL_SCOPE_ROBOT) == 0
        && robot != NULL && robot[0] != '\0') {
        hush_home_join(out, outsz, scoped, robot);
        if (out[0] == '\0')
            return HUSH_ERR_FULL;
        return HUSH_OK;
    }
    hush_home_copy(out, outsz, scoped);
    if (out[0] == '\0')
        return HUSH_ERR_FULL;
    return HUSH_OK;
}

hush_status_t hush_home_ensure(void)
{
    char root[HUSH_HOME_PATH_MAX];
    char cfg[HUSH_HOME_PATH_MAX];

    if (hush_home_should_make_tree()) {
        hush_home_root(root, sizeof(root));
        if (root[0] == '\0')
            return HUSH_ERR_IO;
        if (hush_home_make_tree(root) != HUSH_OK)
            return HUSH_ERR_IO;
        if (hush_home_seed_forge(root) != HUSH_OK)
            return HUSH_ERR_IO;
    }
    hush_home_config_dir(cfg, sizeof(cfg));
    if (cfg[0] == '\0')
        return HUSH_ERR_IO;
    return hush_home_mkdir(cfg);
}

static void hush_home_copy(char *dst, size_t dstsz, const char *src)
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

static void hush_home_join(char *out, size_t outsz, const char *a, const char *b)
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

static int hush_home_should_make_tree(void)
{
    const char *home_env;
    const char *cfg_env;

    home_env = getenv(HUSH_HOME_ENV);
    if (home_env != NULL && home_env[0] != '\0')
        return 1;
    cfg_env = getenv(HUSH_HOME_ENV_CONFIG);
    if (cfg_env != NULL && cfg_env[0] != '\0')
        return 0;
    return 1;
}

static hush_status_t hush_home_mkdir(const char *path)
{
    assert(path != NULL);
    if (path[0] == '\0')
        return HUSH_ERR_IO;
    if (mkdir(path, HUSH_HOME_MODE) != 0 && errno != EEXIST)
        return HUSH_ERR_IO;
    return HUSH_OK;
}

static hush_status_t hush_home_mkdir_child(char *out, size_t outsz,
                                           const char *root, const char *name)
{
    assert(out != NULL);
    assert(root != NULL);
    assert(name != NULL);
    hush_home_join(out, outsz, root, name);
    if (out[0] == '\0')
        return HUSH_ERR_FULL;
    return hush_home_mkdir(out);
}

static hush_status_t hush_home_make_tree(const char *root)
{
    char path[HUSH_HOME_PATH_MAX];
    char skills[HUSH_HOME_PATH_MAX];

    assert(root != NULL);
    if (hush_home_mkdir(root) != HUSH_OK)
        return HUSH_ERR_IO;
    if (hush_home_mkdir_child(path, sizeof(path), root, HUSH_HOME_DIR_CONFIG)
        != HUSH_OK)
        return HUSH_ERR_IO;
    if (hush_home_mkdir_child(path, sizeof(path), root, HUSH_HOME_DIR_AGENTS)
        != HUSH_OK)
        return HUSH_ERR_IO;
    if (hush_home_mkdir_child(skills, sizeof(skills), root, HUSH_HOME_DIR_SKILLS)
        != HUSH_OK)
        return HUSH_ERR_IO;
    if (hush_home_mkdir_child(path, sizeof(path), skills, HUSH_HOME_DIR_SYSTEM)
        != HUSH_OK)
        return HUSH_ERR_IO;
    if (hush_home_mkdir_child(path, sizeof(path), skills, HUSH_HOME_DIR_USER)
        != HUSH_OK)
        return HUSH_ERR_IO;
    return hush_home_mkdir_child(path, sizeof(path), skills, HUSH_HOME_DIR_ROBOTS);
}

static hush_status_t hush_home_seed_forge(const char *root)
{
    char sysdir[HUSH_HOME_PATH_MAX];
    char skilldir[HUSH_HOME_PATH_MAX];
    char path[HUSH_HOME_PATH_MAX];
    FILE *fp;

    assert(root != NULL);
    if (hush_home_skills_dir(sysdir, sizeof(sysdir),
                             HUSH_SKILL_SCOPE_SYSTEM, NULL) != HUSH_OK)
        return HUSH_ERR_IO;
    hush_home_join(skilldir, sizeof(skilldir), sysdir, HUSH_SKILL_FORGE_SLUG);
    if (skilldir[0] == '\0')
        return HUSH_ERR_FULL;
    if (hush_home_mkdir(skilldir) != HUSH_OK)
        return HUSH_ERR_IO;
    hush_home_join(path, sizeof(path), skilldir, HUSH_SKILL_FILE_NAME);
    if (path[0] == '\0')
        return HUSH_ERR_FULL;
    fp = fopen(path, "r");
    if (fp != NULL) {
        fclose(fp);
        return HUSH_OK;
    }
    fp = fopen(path, "w");
    if (fp == NULL)
        return HUSH_ERR_IO;
    fputs(hush_skill_forge_body(), fp);
    fclose(fp);
    return HUSH_OK;
}
