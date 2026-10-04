/* tests/test_keystore.c: pass, op, and secret-tool restore the same public ids. */

#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hush_keystore.h"
#include "hush_launch.h"
#include "hush_pass.h"

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

static void wipe_helpers(void)
{
    hush_pass_set_helper(NULL);
    unsetenv("HUSH_PASS_HELPER");
    unsetenv("HUSH_OP_HELPER");
    unsetenv("HUSH_SECRET_TOOL_HELPER");
    unsetenv("HUSH_OP_VAULT");
    unsetenv("HUSH_FAKE_PASS_DIR");
    unsetenv("HUSH_FAKE_OP_DIR");
    unsetenv("HUSH_FAKE_SECRET_DIR");
}

/* Counts agents/<slug>/nsec files. Bounded directory walk. */
static int count_nsec_files(const char *home)
{
    char agents[256];
    DIR *dir;
    int count = 0;
    int seen = 0;

    snprintf(agents, sizeof(agents), "%s/agents", home);
    dir = opendir(agents);
    if (dir == NULL)
        return 0;
    for (seen = 0; seen < 64; seen++) {
        struct dirent *ent = readdir(dir);
        char path[640];
        struct stat st;

        if (ent == NULL)
            break;
        if (ent->d_name[0] == '.')
            continue;
        snprintf(path, sizeof(path), "%s/%s/nsec", agents, ent->d_name);
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
            count++;
    }
    closedir(dir);
    return count;
}

static void drop_nsec_files(const char *home)
{
    char agents[256];
    DIR *dir;
    int seen;

    snprintf(agents, sizeof(agents), "%s/agents", home);
    dir = opendir(agents);
    if (dir == NULL)
        return;
    for (seen = 0; seen < 64; seen++) {
        struct dirent *ent = readdir(dir);
        char path[640];

        if (ent == NULL)
            break;
        if (ent->d_name[0] == '.')
            continue;
        snprintf(path, sizeof(path), "%s/%s/nsec", agents, ent->d_name);
        unlink(path);
    }
    closedir(dir);
}

static int arm_store(hush_keystore_kind kind, const char *tag)
{
    char dir[128];

    snprintf(dir, sizeof(dir), "/tmp/hush-ku-%s-%d", tag, (int)getpid());
    if (mkdir(dir, 0700) != 0)
        return 0;
    if (kind == HUSH_KEYSTORE_PASS) {
        if (setenv("HUSH_FAKE_PASS_DIR", dir, 1) != 0)
            return 0;
        hush_pass_set_helper("tests/fake-pass.sh");
        return 1;
    }
    hush_pass_set_helper("/nonexistent/hush-pass-helper");
    if (kind == HUSH_KEYSTORE_OP) {
        if (setenv("HUSH_FAKE_OP_DIR", dir, 1) != 0)
            return 0;
        if (setenv("HUSH_OP_HELPER", "tests/fake-op.py", 1) != 0)
            return 0;
        return 1;
    }
    if (setenv("HUSH_FAKE_SECRET_DIR", dir, 1) != 0)
        return 0;
    if (setenv("HUSH_SECRET_TOOL_HELPER", "tests/fake-secret-tool.py", 1) != 0)
        return 0;
    return 1;
}

static hush_status_t save_open_ids(hush_keystore_kind kind,
                                  const hush_launch_t *launch)
{
    size_t i;
    hush_status_t st;

    st = hush_keystore_save(kind, HUSH_PASS_IDENTITY_NSEC, launch->human.nsec);
    if (st != HUSH_OK)
        return st;
    st = hush_keystore_save(kind, HUSH_PASS_PAYNE_NSEC, launch->payne.nsec);
    if (st != HUSH_OK)
        return st;
    for (i = 0; i < launch->roster.nagents && i < 32; i++) {
        char path[HUSH_PASS_PATH_MAX];
        int wrote;
        const hush_roster_agent_t *agent = &launch->roster.agents[i];

        wrote = snprintf(path, sizeof(path), "agents/%s/nsec", agent->slug);
        if (wrote < 0 || (size_t)wrote >= sizeof(path))
            return HUSH_ERR_ARG;
        st = hush_keystore_save(kind, path, agent->id.nsec);
        if (st != HUSH_OK)
            return st;
    }
    return HUSH_OK;
}

static int pubs_match(const hush_launch_t *left, const hush_launch_t *right)
{
    size_t i;

    if (strcmp(left->human.pubkey_hex, right->human.pubkey_hex) != 0)
        return 0;
    if (strcmp(left->payne.pubkey_hex, right->payne.pubkey_hex) != 0)
        return 0;
    if (left->roster.nagents != right->roster.nagents)
        return 0;
    for (i = 0; i < left->roster.nagents && i < 32; i++) {
        const char *a = left->roster.agents[i].id.pubkey_hex;
        const char *b = right->roster.agents[i].id.pubkey_hex;

        if (strcmp(a, b) != 0)
            return 0;
    }
    return 1;
}

static int raise_hive(const char *tag, hush_launch_t *keys, char *home,
                      size_t homesz)
{
    char cfg[128];
    hush_store_t *store = NULL;
    hush_roster_agent_in_t in;
    int wrote;

    wrote = snprintf(home, homesz, "/tmp/hush-ku-home-%s-%d", tag, (int)getpid());
    if (wrote < 0 || (size_t)wrote >= homesz)
        return 0;
    snprintf(cfg, sizeof(cfg), "/tmp/hush-ku-cfg-%s-%d", tag, (int)getpid());
    if (mkdir(home, 0700) != 0 || mkdir(cfg, 0700) != 0)
        return 0;
    if (setenv("HUSH_HOME", home, 1) != 0)
        return 0;
    if (setenv("HUSH_CONFIG_DIR", cfg, 1) != 0)
        return 0;
    hush_launch_init(keys);
    if (hush_store_create(&store) != HUSH_OK)
        return 0;
    if (hush_launch_create_identity(keys) != HUSH_OK)
        return 0;
    if (hush_launch_ack_backup(keys, 0) != HUSH_OK)
        return 0;
    if (hush_launch_create_vibe(keys, store, "HQ", "keystore") != HUSH_OK)
        return 0;
    memset(&in, 0, sizeof(in));
    memcpy(in.name, "Walkbot One", 12);
    memcpy(in.prompt, "Walk the floor.", 16);
    memcpy(in.provider, HUSH_ROSTER_PROVIDER_GROK_BUILD,
           sizeof(HUSH_ROSTER_PROVIDER_GROK_BUILD));
    if (hush_launch_add_agent(keys, store, &in, 0) != HUSH_OK)
        return 0;
    hush_store_destroy(store);
    return keys->human.pubkey_hex[0] != '\0' && keys->payne.pubkey_hex[0] != '\0';
}

static void test_store_restores(hush_keystore_kind kind, const char *tag)
{
    static hush_launch_t keys;
    static hush_launch_t again;
    char home[128];

    wipe_helpers();
    expect(arm_store(kind, tag), tag);
    if (!raise_hive(tag, &keys, home, sizeof(home))) {
        expect(0, tag);
        return;
    }
    expect(count_nsec_files(home) > 0, "seed still writes the old robot file");
    expect(save_open_ids(kind, &keys) == HUSH_OK, "save into one store");
    drop_nsec_files(home);
    expect(count_nsec_files(home) == 0, "plaintext keys removed before restore");
    hush_launch_init(&again);
    expect(hush_launch_restore_identity(&again) == HUSH_OK, "restore human");
    expect(again.logged_in, "human logged in from the store");
    expect(hush_launch_restore_vibe(&again) == HUSH_OK, "restore vibe");
    expect(pubs_match(&keys, &again), "same public ids after restart");
    expect(count_nsec_files(home) == 0, "unlock wrote no plaintext private key");
}

static void test_missing_refuses_lookalike(void)
{
    static hush_launch_t keys;
    static hush_launch_t again;
    hush_identity_t imported;
    char home[128];

    wipe_helpers();
    hush_pass_set_helper("/nonexistent/hush-pass-helper");
    if (setenv("HUSH_OP_HELPER", "/nonexistent/op", 1) != 0)
        expect(0, "missing op helper");
    if (setenv("HUSH_SECRET_TOOL_HELPER", "/nonexistent/secret-tool", 1) != 0)
        expect(0, "missing secret-tool helper");
    if (!raise_hive("miss", &keys, home, sizeof(home))) {
        expect(0, "missing hive");
        return;
    }
    drop_nsec_files(home);
    expect(hush_keystore_import(&imported, HUSH_PASS_IDENTITY_NSEC) ==
               HUSH_ERR_NOT_FOUND,
           "missing store does not invent a key");
    expect(imported.pubkey_hex[0] == '\0', "missing import stays empty");
    expect(strcmp(imported.pubkey_hex, keys.human.pubkey_hex) != 0,
           "missing import is not the old public id");
    expect(count_nsec_files(home) == 0, "missing import wrote no key file");
    expect(hush_keystore_save(HUSH_KEYSTORE_OP, HUSH_PASS_IDENTITY_NSEC,
                              keys.human.nsec) == HUSH_ERR_IO,
           "missing op save fails");
    expect(count_nsec_files(home) == 0, "failed save wrote no key file");
    hush_launch_init(&again);
    expect(hush_launch_restore_identity(&again) == HUSH_OK, "miss human");
    expect(!again.logged_in, "miss stays logged out");
    expect(hush_launch_restore_vibe(&again) == HUSH_OK, "miss vibe");
    hush_launch_mark_restart(&again);
    expect(again.restart_lost_login, "honest restart-loss flag");
    expect(strcmp(again.payne.pubkey_hex, keys.payne.pubkey_hex) != 0,
           "minted Payne is not the old public id");
    expect(strcmp(again.human.pubkey_hex, keys.human.pubkey_hex) != 0,
           "missing human is not the old public id");
}

int main(void)
{
    test_store_restores(HUSH_KEYSTORE_PASS, "pass");
    test_store_restores(HUSH_KEYSTORE_OP, "op");
    test_store_restores(HUSH_KEYSTORE_SECRET, "secret");
    test_missing_refuses_lookalike();
    if (g_fail)
        return 1;
    printf("test_keystore ok\n");
    return 0;
}
