/* hush_home.h: ~/.hush layout, config dir, first-run ensure. */

#ifndef HUSH_HOME_H
#define HUSH_HOME_H

#include <stddef.h>
#include "hush_status.h"

enum {
    HUSH_HOME_PATH_MAX = 384
};

#define HUSH_HOME_ENV "HUSH_HOME"
#define HUSH_HOME_ENV_CONFIG "HUSH_CONFIG_DIR"
#define HUSH_HOME_DIR_CONFIG "config"
#define HUSH_HOME_DIR_AGENTS "agents"
#define HUSH_HOME_DIR_SKILLS "skills"
#define HUSH_HOME_DIR_SYSTEM "system"
#define HUSH_HOME_DIR_USER "user"
#define HUSH_HOME_DIR_ROBOTS "robots"

/* Writes the hush home root into out. Empty on overflow. */
void hush_home_root(char *out, size_t outsz);

/* Writes the config directory into out. Honors HUSH_CONFIG_DIR. */
void hush_home_config_dir(char *out, size_t outsz);

/* Writes hush_home_root/agents into out. Empty on overflow. */
void hush_home_agents_dir(char *out, size_t outsz);

/* Writes the legacy ~/.config/hush path into out. Empty on overflow. */
void hush_home_legacy_config_dir(char *out, size_t outsz);

/* Writes skills/<scope>[/<robot>] under the home root. robot may be NULL.
 * Fails with HUSH_ERR_ARG on a bad scope. */
hush_status_t hush_home_skills_dir(char *out, size_t outsz,
                                   const char *scope, const char *robot);

/* mkdir 0700 config/agents/skills trees. Seeds forge-skill when missing.
 * When HUSH_CONFIG_DIR is set and HUSH_HOME is not, skips $HOME/.hush so
 * tests never write the real user home. Always mkdir the config dir. */
hush_status_t hush_home_ensure(void);

#endif /* HUSH_HOME_H */
