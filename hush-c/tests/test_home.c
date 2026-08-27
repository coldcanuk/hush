/* tests/test_home.c: ~/.hush tree, env overrides, forge-skill seed. */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hush_home.h"
#include "hush_skill.h"

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

static int is_dir(const char *path)
{
    struct stat st;

    return path != NULL && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int is_file(const char *path)
{
    struct stat st;

    return path != NULL && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int main(void)
{
    char root[HUSH_HOME_PATH_MAX];
    char cfg[HUSH_HOME_PATH_MAX];
    char agents[HUSH_HOME_PATH_MAX];
    char forge[HUSH_HOME_PATH_MAX];
    char isolated[192];
    char only_cfg[192];
    char home_only[192];

    snprintf(isolated, sizeof(isolated), "/tmp/hush-home-test-%d", (int)getpid());
    snprintf(only_cfg, sizeof(only_cfg), "/tmp/hush-home-cfg-%d", (int)getpid());
    snprintf(home_only, sizeof(home_only), "/tmp/hush-home-user-%d", (int)getpid());
    unsetenv("HUSH_CONFIG_DIR");
    if (setenv("HUSH_HOME", isolated, 1) != 0)
        return 1;
    expect(hush_home_ensure() == HUSH_OK, "ensure HUSH_HOME");
    hush_home_root(root, sizeof(root));
    hush_home_config_dir(cfg, sizeof(cfg));
    hush_home_agents_dir(agents, sizeof(agents));
    expect(strstr(root, isolated) != NULL, "root is HUSH_HOME");
    expect(is_dir(cfg), "config dir");
    expect(is_dir(agents), "agents dir");
    snprintf(forge, sizeof(forge),
             "%s/skills/system/forge-skill/SKILL.md", isolated);
    expect(is_file(forge), "forge-skill seeded");
    unsetenv("HUSH_HOME");
    if (setenv("HUSH_CONFIG_DIR", only_cfg, 1) != 0)
        return 1;
    expect(hush_home_ensure() == HUSH_OK, "ensure config override");
    hush_home_config_dir(cfg, sizeof(cfg));
    expect(strcmp(cfg, only_cfg) == 0, "config dir override");
    expect(is_dir(only_cfg), "override mkdir");
    unsetenv("HUSH_CONFIG_DIR");
    if (setenv("HOME", home_only, 1) != 0)
        return 1;
    expect(mkdir(home_only, 0700) == 0 || errno == EEXIST, "mkdir HOME");
    expect(hush_home_ensure() == HUSH_OK, "ensure HOME/.hush");
    snprintf(root, sizeof(root), "%s/.hush/config", home_only);
    snprintf(agents, sizeof(agents), "%s/.hush/agents", home_only);
    expect(is_dir(root), "HOME/.hush/config");
    expect(is_dir(agents), "HOME/.hush/agents");
    if (g_fail)
        return 1;
    printf("test_home ok\n");
    return 0;
}
