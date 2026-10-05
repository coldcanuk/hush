/* tests/test_save_pass.c: #235 create/update must not OK when pass save fails.
 * Pins missing|fail|path at the roster write, and update honouring save_pass. */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hush_identity.h"
#include "hush_keystore.h"
#include "hush_launch.h"
#include "hush_pass.h"
#include "hush_roster.h"

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

static int mkdir_ok(const char *dir)
{
    return mkdir(dir, 0700) == 0 || errno == EEXIST;
}

static void wipe_helpers(void)
{
    hush_pass_set_helper(NULL);
    unsetenv("HUSH_PASS_HELPER");
    unsetenv("HUSH_FAKE_PASS_DIR");
    unsetenv("HUSH_OP_HELPER");
    unsetenv("HUSH_SECRET_TOOL_HELPER");
}

static int arm_pass(const char *dir)
{
    if (!mkdir_ok(dir))
        return 0;
    if (setenv("HUSH_FAKE_PASS_DIR", dir, 1) != 0)
        return 0;
    hush_pass_set_helper("tests/fake-pass.sh");
    return 1;
}

static int arm_fail_pass(const char *dir)
{
    char path[192];
    FILE *fp;

    if (!mkdir_ok(dir))
        return 0;
    snprintf(path, sizeof(path), "%s/failpass.sh", dir);
    fp = fopen(path, "w");
    if (fp == NULL)
        return 0;
    fputs("#!/bin/sh\nexit 1\n", fp);
    fclose(fp);
    if (chmod(path, 0755) != 0)
        return 0;
    hush_pass_set_helper(path);
    return 1;
}

static int raise_hive(hush_launch_t *keys, char *home, size_t homesz)
{
    char cfg[128];
    hush_store_t *store = NULL;
    int wrote;

    wrote = snprintf(home, homesz, "/tmp/hush-sp-home-%d", (int)getpid());
    if (wrote < 0 || (size_t)wrote >= homesz)
        return 0;
    snprintf(cfg, sizeof(cfg), "/tmp/hush-sp-cfg-%d", (int)getpid());
    if (!mkdir_ok(home) || !mkdir_ok(cfg))
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
    if (hush_launch_create_vibe(keys, store, "HQ", "save-pass") != HUSH_OK)
        return 0;
    hush_store_destroy(store);
    return keys->human.pubkey_hex[0] != '\0';
}

static void fill_agent(hush_roster_agent_in_t *in, const char *name)
{
    memset(in, 0, sizeof(*in));
    snprintf(in->name, sizeof(in->name), "%s", name);
    snprintf(in->prompt, sizeof(in->prompt), "Walk the floor.");
    memcpy(in->provider, HUSH_ROSTER_PROVIDER_GROK_BUILD,
           sizeof(HUSH_ROSTER_PROVIDER_GROK_BUILD));
}

/* Path: overlong slug must return ARG (not silent OK). */
static void test_path_too_long(void)
{
    char slug[256];
    char secret[HUSH_IDENTITY_NSEC_MAX];
    hush_identity_t id;
    size_t i;

    wipe_helpers();
    expect(arm_pass("/tmp/hush-sp-path-pass"), "path arm pass");
    expect(hush_identity_generate(&id) == HUSH_OK, "path gen");
    snprintf(secret, sizeof(secret), "%s", id.nsec);
    for (i = 0; i + 1 < sizeof(slug); i++)
        slug[i] = 'a';
    slug[sizeof(slug) - 1] = '\0';
    expect(hush_roster_write_agent_pass(slug, secret) == HUSH_ERR_ARG,
           "path overlong returns ARG");
}

/* Missing: save_pass create must not leave a robot when pass is gone. */
static void test_missing_refuses_create(void)
{
    static hush_launch_t launch;
    hush_store_t *store = NULL;
    hush_roster_agent_in_t in;
    char home[128];
    size_t before;

    wipe_helpers();
    setenv("HUSH_PASS_HELPER", "/nonexistent/pass", 1);
    hush_pass_set_helper(NULL);
    if (!raise_hive(&launch, home, sizeof(home))) {
        expect(0, "missing hive");
        return;
    }
    expect(hush_store_create(&store) == HUSH_OK, "missing store");
    before = launch.roster.nagents;
    fill_agent(&in, "Delta");
    expect(hush_launch_add_agent(&launch, store, &in, 1) == HUSH_ERR_DENIED,
           "missing create returns DENIED");
    expect(launch.roster.nagents == before, "missing create added no robot");
    expect(strcmp(launch.pass_error, "pass is not available") == 0,
           "missing surfaces pass_error");
    hush_store_destroy(store);
}

/* Fail: helper exit 1 → IO + pass_error from last_error. */
static void test_fail_refuses_create(void)
{
    static hush_launch_t launch;
    hush_store_t *store = NULL;
    hush_roster_agent_in_t in;
    char home[128];
    char helper_dir[128];
    size_t before;

    wipe_helpers();
    snprintf(helper_dir, sizeof(helper_dir), "/tmp/hush-sp-fail-%d", (int)getpid());
    expect(arm_fail_pass(helper_dir), "fail arm");
    if (!raise_hive(&launch, home, sizeof(home))) {
        expect(0, "fail hive");
        return;
    }
    expect(hush_store_create(&store) == HUSH_OK, "fail store");
    before = launch.roster.nagents;
    fill_agent(&in, "Charlie");
    expect(hush_launch_add_agent(&launch, store, &in, 1) == HUSH_ERR_IO,
           "fail create returns IO");
    expect(launch.roster.nagents == before, "fail create added no robot");
    expect(launch.pass_error[0] != '\0', "fail surfaces pass_error");
    expect(strstr(launch.pass_error, "pass helper failed") != NULL ||
               strstr(launch.pass_error, "save failed") != NULL,
           "fail pass_error names the helper");
    hush_store_destroy(store);
}

/* Update must honour save_pass:true and store agents/<slug>/nsec. */
static void test_update_honours_save_pass(void)
{
    static hush_launch_t launch;
    hush_store_t *store = NULL;
    hush_roster_agent_in_t in;
    const hush_roster_agent_t *a;
    char home[128];
    char pass_dir[128];
    char path[HUSH_PASS_PATH_MAX];
    char loaded[HUSH_PASS_SECRET_MAX];
    char slug[HUSH_ROSTER_NAME_MAX];
    size_t i;

    wipe_helpers();
    snprintf(pass_dir, sizeof(pass_dir), "/tmp/hush-sp-upd-%d", (int)getpid());
    expect(arm_pass(pass_dir), "upd arm");
    if (!raise_hive(&launch, home, sizeof(home))) {
        expect(0, "upd hive");
        return;
    }
    expect(hush_store_create(&store) == HUSH_OK, "upd store");
    fill_agent(&in, "Bravo");
    expect(hush_launch_add_agent(&launch, store, &in, 0) == HUSH_OK,
           "upd create save_pass=0");
    a = NULL;
    for (i = 0; i < launch.roster.nagents; i++) {
        if (strcmp(launch.roster.agents[i].name, "Bravo") == 0)
            a = &launch.roster.agents[i];
    }
    expect(a != NULL, "upd Bravo present");
    if (a == NULL)
        return;
    snprintf(slug, sizeof(slug), "%s", a->slug);
    snprintf(path, sizeof(path), "agents/%s/nsec", slug);
    expect(hush_keystore_load_kind(HUSH_KEYSTORE_PASS, loaded, sizeof(loaded),
                                   path) == HUSH_ERR_NOT_FOUND,
           "upd create left pass empty");
    memset(&in, 0, sizeof(in));
    snprintf(in.prompt, sizeof(in.prompt), "Updated prompt.");
    expect(hush_launch_update_agent(&launch, slug, &in, 1) == HUSH_OK,
           "upd save_pass=1");
    expect(hush_keystore_load_kind(HUSH_KEYSTORE_PASS, loaded, sizeof(loaded),
                                   path) == HUSH_OK,
           "upd wrote pass");
    expect(strcmp(loaded, a->id.nsec) == 0, "upd pass holds Bravo nsec");
    hush_store_destroy(store);
}

int main(void)
{
    test_path_too_long();
    test_missing_refuses_create();
    test_fail_refuses_create();
    test_update_honours_save_pass();
    if (g_fail)
        return 1;
    printf("test_save_pass ok\n");
    return 0;
}
