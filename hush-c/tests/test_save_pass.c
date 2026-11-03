/* tests/test_save_pass.c: #235 create/update must not OK when pass save fails.
 * Pins missing|fail|path at the roster write, and update honouring save_pass. */

#define _XOPEN_SOURCE 700 /* nftw, for the private root cleanup */

#include <assert.h>
#include <errno.h>
#include <ftw.h>
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

enum {
    /* Longest private root path; every path a test derives from it fits. */
    ROOT_PATH_MAX = 96,
    /* Open-fd budget for the nftw cleanup walk. */
    ROOT_FTW_FDS = 16,
    /* Exit status when the root cannot be removed; atexit cannot return one. */
    ROOT_CLEANUP_FAIL_RC = 1
};

/* <TMPDIR or /tmp>/hush-sp-XXXXXX: this run's private root, 0700 from mkdtemp.
 * Every key, pass store, home and config the test writes lives under it. */
#define ROOT_NAME "hush-sp-XXXXXX"
#define ROOT_TMP_DEFAULT "/tmp"

/* The private root and the pid that made it. File scope because an atexit
 * handler takes no argument. Empty until root_make succeeds and again after
 * root_remove. */
static char g_root[ROOT_PATH_MAX];
static pid_t g_root_pid;

/* Makes g_root under root_tmpdir() (/tmp when that is NULL); root_remove,
 * registered with atexit, removes it on every return from main and every
 * exit(). Returns 0, after a FAIL line, when root_holds
 * fails its own check, the path does not fit, mkdtemp fails, atexit
 * refuses, or the root is not under an absolute TMPDIR. */
static int root_make(void);

/* atexit handler: removes g_root and everything under it, in the making
 * process only, so a forked child that exits cannot take the root early.
 * When the walk fails it prints FAIL and ends the process with
 * ROOT_CLEANUP_FAIL_RC, so a leftover root fails the test. */
static void root_remove(void);

/* nftw callback: removes one entry under g_root; FTW_DEPTH visits children
 * first. */
static int root_remove_entry(const char *path, const struct stat *st, int flag,
                             struct FTW *ftw);

/* True when path is root or lies below it: root must match whole path
 * components, so "/r" holds "/r" and "/r/k" but not the sibling "/rX/k". */
static int root_holds(const char *root, const char *path);

/* True when root_tmpdir() is NULL (root_make then uses /tmp), or when
 * g_root lies under it with one trailing slash dropped. A root made anywhere
 * else (a fixed /tmp, say) escapes the run's own cleanup and leak scan.
 * Requires a made root (g_root non-empty). */
static int root_is_under_tmpdir(void);

/* TMPDIR when it is set and absolute; NULL when it is unset, empty, or
 * relative. Borrowed from the environment, never owned or freed. */
static const char *root_tmpdir(void);

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
    fputs("#!/bin/sh\ncat >/dev/null\nexit 1\n", fp);
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

    wrote = snprintf(home, homesz, "%s/hush-sp-home-%d", g_root, (int)getpid());
    if (wrote < 0 || (size_t)wrote >= homesz)
        return 0;
    snprintf(cfg, sizeof(cfg), "%s/hush-sp-cfg-%d", g_root, (int)getpid());
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
    char pass_dir[128];
    size_t i;

    wipe_helpers();
    snprintf(pass_dir, sizeof(pass_dir), "%s/hush-sp-path-pass", g_root);
    expect(arm_pass(pass_dir), "path arm pass");
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
    expect(strcmp(launch.robot_pass_error, "pass is not available") == 0,
           "missing surfaces robot_pass_error");
    expect(launch.pass_error[0] == '\0',
           "missing leaves identity pass_error alone");
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
    snprintf(helper_dir, sizeof(helper_dir), "%s/hush-sp-fail-%d", g_root, (int)getpid());
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
    expect(launch.robot_pass_error[0] != '\0', "fail surfaces robot_pass_error");
    expect(strstr(launch.robot_pass_error, "pass helper failed") != NULL ||
               strstr(launch.robot_pass_error, "save failed") != NULL,
           "fail robot_pass_error names the helper");
    expect(launch.pass_error[0] == '\0',
           "fail leaves identity pass_error alone");
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
    snprintf(pass_dir, sizeof(pass_dir), "%s/hush-sp-upd-%d", g_root, (int)getpid());
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
    /* B3e_update: preset identity pass_error; robot success must not clear it. */
    snprintf(launch.pass_error, sizeof(launch.pass_error), "%s",
             "pass helper failed");
    expect(launch.pass_saved == 0, "upd start pass_saved off");
    expect(hush_launch_update_agent(&launch, slug, &in, 1) == HUSH_OK,
           "upd save_pass=1");
    expect(hush_keystore_load_kind(HUSH_KEYSTORE_PASS, loaded, sizeof(loaded),
                                   path) == HUSH_OK,
           "upd wrote pass");
    expect(strcmp(loaded, a->id.nsec) == 0, "upd pass holds Bravo nsec");
    /* B3d: update success must not set identity pass_saved. */
    expect(launch.pass_saved == 0, "b3d upd success leaves pass_saved");
    expect(strcmp(launch.pass_error, "pass helper failed") == 0,
           "b3e_upd leaves identity pass_error");
    hush_store_destroy(store);
}

/* Update save_pass:true must refuse when pass cannot store (M8/M9). */
static void test_update_refuses_pass_fail(void)
{
    static hush_launch_t launch;
    hush_store_t *store = NULL;
    hush_roster_agent_in_t in;
    const hush_roster_agent_t *a;
    char home[128];
    char helper_dir[128];
    char slug[HUSH_ROSTER_NAME_MAX];
    size_t i;

    wipe_helpers();
    snprintf(helper_dir, sizeof(helper_dir), "%s/hush-sp-updfail-%d", g_root,
             (int)getpid());
    expect(arm_fail_pass(helper_dir), "updfail arm");
    if (!raise_hive(&launch, home, sizeof(home))) {
        expect(0, "updfail hive");
        return;
    }
    expect(hush_store_create(&store) == HUSH_OK, "updfail store");
    fill_agent(&in, "Echo");
    expect(hush_launch_add_agent(&launch, store, &in, 0) == HUSH_OK,
           "updfail create save_pass=0");
    a = NULL;
    for (i = 0; i < launch.roster.nagents; i++) {
        if (strcmp(launch.roster.agents[i].name, "Echo") == 0)
            a = &launch.roster.agents[i];
    }
    expect(a != NULL, "updfail Echo present");
    if (a == NULL)
        return;
    snprintf(slug, sizeof(slug), "%s", a->slug);
    memset(&in, 0, sizeof(in));
    snprintf(in.prompt, sizeof(in.prompt), "Updated.");
    expect(hush_launch_update_agent(&launch, slug, &in, 1) == HUSH_ERR_IO,
           "updfail update returns IO");
    expect(launch.robot_pass_error[0] != '\0',
           "updfail surfaces robot_pass_error");
    expect(launch.pass_error[0] == '\0',
           "updfail leaves identity pass_error alone");
    /* N1: roster must stay byte-identical after pass 400. */
    a = hush_roster_agent_by_slug(&launch.roster, slug);
    expect(a != NULL, "updfail Echo still present");
    if (a != NULL)
        expect(strcmp(a->prompt, "Walk the floor.") == 0,
               "updfail prompt unchanged after 400");
    hush_store_destroy(store);
}


/* B1a/B1b: context DENIED must not name a robot pass outcome. */
static void test_context_denied_keeps_pass_st(void)
{
    static hush_launch_t launch;
    hush_store_t *store = NULL;
    hush_roster_agent_in_t in;
    char home[128];
    char pass_dir[128];
    size_t before;

    wipe_helpers();
    snprintf(pass_dir, sizeof(pass_dir), "%s/hush-sp-b1-%d", g_root, (int)getpid());
    expect(arm_pass(pass_dir), "b1 arm");
    if (!raise_hive(&launch, home, sizeof(home))) {
        expect(0, "b1 hive");
        return;
    }
    expect(hush_store_create(&store) == HUSH_OK, "b1 store");
    before = launch.roster.nagents;
    fill_agent(&in, "Golf");
    memcpy(in.provider, HUSH_ROSTER_PROVIDER_OLLAMA,
           sizeof(HUSH_ROSTER_PROVIDER_OLLAMA));
    memcpy(in.context[0].name, "brief.md", 9);
    memcpy(in.context[0].mime, HUSH_ROSTER_MIME_PLAIN,
           sizeof(HUSH_ROSTER_MIME_PLAIN));
    in.context[0].text = "hi";
    in.context[0].bytes = 2;
    in.ncontext = 1;
    expect(hush_launch_add_agent(&launch, store, &in, 1) == HUSH_ERR_DENIED,
           "b1 context DENIED");
    expect(launch.roster.nagents == before, "b1 added no robot");
    expect(launch.robot_pass_error[0] == '\0',
           "b1 leaves robot_pass_error empty");
    expect(launch.pass_error[0] == '\0',
           "b1 leaves identity pass_error empty");
    hush_store_destroy(store);
}

/* B3b: robot create success must not set identity pass_saved. */
static void test_robot_success_leaves_pass_saved(void)
{
    static hush_launch_t launch;
    hush_store_t *store = NULL;
    hush_roster_agent_in_t in;
    char home[128];
    char pass_dir[128];

    wipe_helpers();
    snprintf(pass_dir, sizeof(pass_dir), "%s/hush-sp-b3b-%d", g_root, (int)getpid());
    expect(arm_pass(pass_dir), "b3b arm");
    if (!raise_hive(&launch, home, sizeof(home))) {
        expect(0, "b3b hive");
        return;
    }
    expect(launch.pass_saved == 0, "b3b start pass_saved off");
    expect(hush_store_create(&store) == HUSH_OK, "b3b store");
    /* B3e_create: preset identity pass_error; robot success must not clear it. */
    snprintf(launch.pass_error, sizeof(launch.pass_error), "%s",
             "pass helper failed");
    fill_agent(&in, "Hotel");
    expect(hush_launch_add_agent(&launch, store, &in, 1) == HUSH_OK,
           "b3b create save_pass=1");
    expect(launch.pass_saved == 0, "b3b robot success leaves pass_saved");
    expect(strcmp(launch.pass_error, "pass helper failed") == 0,
           "b3e_create leaves identity pass_error");
    hush_store_destroy(store);
}

/* B3c: robot pass fail must not clear identity pass_saved. */
static void test_robot_fail_leaves_pass_saved(void)
{
    static hush_launch_t launch;
    hush_store_t *store = NULL;
    hush_roster_agent_in_t in;
    char home[128];
    char helper_dir[128];

    wipe_helpers();
    snprintf(helper_dir, sizeof(helper_dir), "%s/hush-sp-b3c-%d", g_root,
             (int)getpid());
    expect(arm_fail_pass(helper_dir), "b3c arm");
    if (!raise_hive(&launch, home, sizeof(home))) {
        expect(0, "b3c hive");
        return;
    }
    launch.pass_saved = 1;
    expect(hush_store_create(&store) == HUSH_OK, "b3c store");
    fill_agent(&in, "India");
    expect(hush_launch_add_agent(&launch, store, &in, 1) == HUSH_ERR_IO,
           "b3c create fails");
    expect(launch.pass_saved == 1, "b3c robot fail leaves pass_saved");
    hush_store_destroy(store);
}

int main(void)
{
    if (!root_make())
        return 1;
    test_path_too_long();
    test_missing_refuses_create();
    test_fail_refuses_create();
    test_update_honours_save_pass();
    test_update_refuses_pass_fail();
    test_context_denied_keeps_pass_st();
    test_robot_success_leaves_pass_saved();
    test_robot_fail_leaves_pass_saved();
    if (g_fail)
        return 1;
    printf("test_save_pass ok\n");
    return 0;
}

static int root_make(void)
{
    const char *base_dir = root_tmpdir();
    int wrote = 0;

    assert(g_root[0] == '\0');
    /* nftw never hands root_remove_entry a path outside the root, so this
     * direct check is the only place the prefix-sibling case is exercised. */
    if (!root_holds("/r", "/r") || !root_holds("/r", "/r/k") ||
        root_holds("/r", "/rX/k") || root_holds("/r", "/rX")) {
        fprintf(stderr, "FAIL root_holds accepts a prefix sibling\n");
        return 0;
    }
    /* A relative TMPDIR would leave g_root relative, and a test that
     * chdirs would then walk the wrong dir at exit. */
    if (base_dir == NULL)
        base_dir = ROOT_TMP_DEFAULT;
    wrote = snprintf(g_root, sizeof(g_root), "%s/%s", base_dir, ROOT_NAME);
    if (wrote < 0 || (size_t)wrote >= sizeof(g_root) || mkdtemp(g_root) == NULL) {
        g_root[0] = '\0';
        fprintf(stderr, "FAIL private root under %s\n", base_dir);
        return 0;
    }
    g_root_pid = getpid();
    /* Deviation (section 7): atexit takes a bare callback; no table fits. */
    if (atexit(root_remove) != 0) {
        root_remove();
        fprintf(stderr, "FAIL atexit for the private root\n");
        return 0;
    }
    /* atexit is already set, so the root is still removed on this FAIL. */
    if (!root_is_under_tmpdir()) {
        fprintf(stderr, "FAIL private root %s is not under TMPDIR\n", g_root);
        return 0;
    }
    return 1;
}

static void root_remove(void)
{
    if (g_root[0] == '\0' || getpid() != g_root_pid)
        return;
    /* FTW_PHYS never follows a symlink out of the root; FTW_MOUNT never
     * walks into another filesystem mounted under it. Deviation (section 7):
     * nftw takes a bare callback; no table fits. */
    if (nftw(g_root, root_remove_entry, ROOT_FTW_FDS,
             FTW_DEPTH | FTW_PHYS | FTW_MOUNT) != 0) {
        fprintf(stderr, "FAIL private root cleanup: %s\n", g_root);
        (void)fflush(NULL);
        /* exit() may not run inside an atexit handler; _exit may. */
        _exit(ROOT_CLEANUP_FAIL_RC);
    }
    g_root[0] = '\0';
}

static int root_remove_entry(const char *path, const struct stat *st, int flag,
                             struct FTW *ftw)
{
    (void)st;
    (void)flag;
    (void)ftw;
    /* Defense in depth only: FTW_PHYS and FTW_MOUNT keep the walk inside
     * g_root, so this assert cannot fire through nftw. root_make checks
     * root_holds directly instead. */
    assert(root_holds(g_root, path));
    return remove(path);
}

static int root_holds(const char *root, const char *path)
{
    size_t n = 0;

    assert(root != NULL && path != NULL);
    n = strlen(root);
    return n > 0 && strncmp(path, root, n) == 0 &&
           (path[n] == '\0' || path[n] == '/');
}

static int root_is_under_tmpdir(void)
{
    assert(g_root[0] != '\0');
    const char *env = root_tmpdir();
    if (env == NULL)
        return 1;

    size_t n = strlen(env);
    if (n > 1 && env[n - 1] == '/')
        n--;
    /* g_root is shorter than ROOT_PATH_MAX, so no longer TMPDIR can hold it:
     * "not under TMPDIR" is the true answer here, not a second error. */
    if (n >= (size_t)ROOT_PATH_MAX)
        return 0;
    char base[ROOT_PATH_MAX] = "";
    memcpy(base, env, n);
    return root_holds(base, g_root);
}

static const char *root_tmpdir(void)
{
    const char *env = getenv("TMPDIR");
    if (env == NULL || env[0] != '/')
        return NULL;
    return env;
}
