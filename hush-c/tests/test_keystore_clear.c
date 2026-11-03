/* tests/test_keystore_clear.c: delete clears keys so reuse is a fresh signer.
 * Pins issue #264 Shape 1 and Shape 2 against the fake stores. */

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

enum {
    /* Longest private root path; every path a test derives from it fits. */
    ROOT_PATH_MAX = 96,
    /* Open-fd budget for the nftw cleanup walk. */
    ROOT_FTW_FDS = 16,
    /* Exit status when the root cannot be removed; atexit cannot return one. */
    ROOT_CLEANUP_FAIL_RC = 1
};

/* <TMPDIR or /tmp>/hush-kc-XXXXXX: this run's private root, 0700 from mkdtemp.
 * Every key, pass store, home and config the test writes lives under it. */
#define ROOT_NAME "hush-kc-XXXXXX"
#define ROOT_TMP_DEFAULT "/tmp"

/* The private root and the pid that made it. File scope because an atexit
 * handler takes no argument. Empty until root_make succeeds and again after
 * root_remove. */
static char g_root[ROOT_PATH_MAX];
static pid_t g_root_pid;

/* Makes g_root under root_tmpdir() (/tmp when that is NULL); root_remove,
 * registered with atexit, removes it on every return from main and every
 * exit(). Returns 0, after a FAIL line, when root_self_check
 * fails, the path does not fit, mkdtemp fails, atexit
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

/* Self-test of the two path checks, run before the root is made: returns
 * 0, after a FAIL line, when root_holds or root_is_under_tmpdir accepts a
 * prefix sibling ("/tmp/xprivate" for "/tmp/x"). A real root never sits in
 * such a sibling, so nothing else exercises that boundary. */
static int root_self_check(void);

/* True when tmpdir is NULL, empty or relative (root_make then uses /tmp), or
 * when root lies under tmpdir by whole path components. A root made anywhere
 * else (a fixed /tmp, say) escapes the run's own cleanup and leak scan.
 * root_make passes getenv("TMPDIR") here, never root_tmpdir(), so a fault in
 * the leaf that placed the root cannot also blind this check. */
static int root_is_under_tmpdir(const char *tmpdir, const char *root);

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
    unsetenv("HUSH_FAKE_PASS_WIPE_LOG");
    unsetenv("HUSH_FAKE_OP_DELETE_FAIL");
}

static int mkdir_ok(const char *dir)
{
    if (mkdir(dir, 0700) == 0 || errno == EEXIST)
        return 1;
    return 0;
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

static int arm_op(const char *dir)
{
    if (!mkdir_ok(dir))
        return 0;
    if (setenv("HUSH_FAKE_OP_DIR", dir, 1) != 0)
        return 0;
    if (setenv("HUSH_OP_HELPER", "tests/fake-op.py", 1) != 0)
        return 0;
    return 1;
}

static int raise_hive(const char *tag, hush_launch_t *keys, char *home,
                      size_t homesz)
{
    char cfg[128];
    hush_store_t *store = NULL;
    int wrote;

    wrote = snprintf(home, homesz, "%s/hush-kc-home-%s-%d", g_root, tag, (int)getpid());
    if (wrote < 0 || (size_t)wrote >= homesz)
        return 0;
    snprintf(cfg, sizeof(cfg), "%s/hush-kc-cfg-%s-%d", g_root, tag, (int)getpid());
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
    if (hush_launch_create_vibe(keys, store, "HQ", "clear") != HUSH_OK)
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

static const hush_roster_agent_t *find_named(const hush_launch_t *launch,
                                             const char *name)
{
    size_t i;

    for (i = 0; i < launch->roster.nagents; i++) {
        if (strcmp(launch->roster.agents[i].name, name) == 0)
            return &launch->roster.agents[i];
    }
    return NULL;
}

/* Shape 1: A→pass, delete, B→op; restore must not reload A's pass key. */
static void test_shape1_pass_outranks_op(void)
{
    static hush_launch_t launch;
    static hush_launch_t again;
    hush_store_t *store = NULL;
    hush_roster_agent_in_t in;
    const hush_roster_agent_t *a;
    const hush_roster_agent_t *b;
    char home[128];
    char pass_dir[128];
    char op_dir[128];
    char path[HUSH_PASS_PATH_MAX];
    char loaded[HUSH_PASS_SECRET_MAX];
    hush_keystore_kind found = HUSH_KEYSTORE_NONE;
    char a_nsec[HUSH_IDENTITY_NSEC_MAX];
    char a_pub[HUSH_IDENTITY_HEX_LEN + 1];
    char slug[HUSH_ROSTER_NAME_MAX];

    wipe_helpers();
    snprintf(pass_dir, sizeof(pass_dir), "%s/hush-kc-s1-pass-%d", g_root, (int)getpid());
    snprintf(op_dir, sizeof(op_dir), "%s/hush-kc-s1-op-%d", g_root, (int)getpid());
    expect(arm_pass(pass_dir), "shape1 arm pass");
    expect(arm_op(op_dir), "shape1 arm op");
    if (!raise_hive("s1", &launch, home, sizeof(home))) {
        expect(0, "shape1 hive");
        return;
    }
    expect(hush_store_create(&store) == HUSH_OK, "shape1 store");
    fill_agent(&in, "Sentry");
    expect(hush_launch_add_agent(&launch, store, &in, 1) == HUSH_OK,
           "shape1 add A save_pass=1");
    a = find_named(&launch, "Sentry");
    expect(a != NULL, "shape1 A present");
    if (a == NULL)
        return;
    snprintf(a_nsec, sizeof(a_nsec), "%s", a->id.nsec);
    snprintf(a_pub, sizeof(a_pub), "%s", a->id.pubkey_hex);
    snprintf(slug, sizeof(slug), "%s", a->slug);
    snprintf(path, sizeof(path), "agents/%s/nsec", slug);
    expect(hush_keystore_load_kind(HUSH_KEYSTORE_PASS, loaded, sizeof(loaded),
                                   path) == HUSH_OK,
           "shape1 A landed in pass");
    expect(strcmp(loaded, a_nsec) == 0, "shape1 pass holds A");

    expect(hush_launch_remove_agent(&launch, slug) == HUSH_OK, "shape1 delete A");
    expect(hush_keystore_load_kind(HUSH_KEYSTORE_PASS, loaded, sizeof(loaded),
                                   path) == HUSH_ERR_NOT_FOUND,
           "shape1 delete cleared pass");

    fill_agent(&in, "Sentry");
    expect(hush_launch_add_agent(&launch, store, &in, 0) == HUSH_OK,
           "shape1 add B save_pass=0");
    b = find_named(&launch, "Sentry");
    expect(b != NULL, "shape1 B present");
    if (b == NULL)
        return;
    expect(strcmp(b->slug, slug) == 0, "shape1 reused slug");
    expect(strcmp(b->id.pubkey_hex, a_pub) != 0, "shape1 B is fresh pubkey");
    expect(hush_keystore_load_kind(HUSH_KEYSTORE_OP, loaded, sizeof(loaded),
                                   path) == HUSH_OK,
           "shape1 B landed in op");
    expect(strcmp(loaded, b->id.nsec) == 0, "shape1 op holds B");

    found = HUSH_KEYSTORE_NONE;
    expect(hush_keystore_load_from(&found, loaded, sizeof(loaded), path) ==
               HUSH_OK,
           "shape1 load_from hits");
    expect(found == HUSH_KEYSTORE_OP, "shape1 first hit is op not stale pass");
    expect(strcmp(loaded, a_nsec) != 0, "shape1 load is not A's key");
    expect(strcmp(loaded, b->id.nsec) == 0, "shape1 load is B's key");

    /* Restart restore must also see B, not A. */
    hush_launch_init(&again);
    expect(hush_launch_restore_identity(&again) == HUSH_OK, "shape1 restore id");
    expect(hush_launch_restore_vibe(&again) == HUSH_OK, "shape1 restore vibe");
    b = find_named(&again, "Sentry");
    expect(b != NULL, "shape1 restored Sentry");
    if (b != NULL)
        expect(strcmp(b->id.pubkey_hex, a_pub) != 0,
               "shape1 restore is not deleted A");
    hush_store_destroy(store);
}

/* Shape 2: A→pass, delete, B offer with no op/secret; must not restore A. */
static void test_shape2_offer_nowhere(void)
{
    static hush_launch_t launch;
    static hush_launch_t again;
    hush_store_t *store = NULL;
    hush_roster_agent_in_t in;
    const hush_roster_agent_t *a;
    const hush_roster_agent_t *b;
    char home[128];
    char pass_dir[128];
    char path[HUSH_PASS_PATH_MAX];
    char loaded[HUSH_PASS_SECRET_MAX];
    char a_pub[HUSH_IDENTITY_HEX_LEN + 1];
    char slug[HUSH_ROSTER_NAME_MAX];

    wipe_helpers();
    snprintf(pass_dir, sizeof(pass_dir), "%s/hush-kc-s2-pass-%d", g_root, (int)getpid());
    expect(arm_pass(pass_dir), "shape2 arm pass");
    /* No op / secret-tool helpers: offer(use_pass=0) writes nowhere. */
    hush_pass_set_helper("tests/fake-pass.sh");
    setenv("HUSH_OP_HELPER", "/nonexistent/op", 1);
    setenv("HUSH_SECRET_TOOL_HELPER", "/nonexistent/secret-tool", 1);
    if (!raise_hive("s2", &launch, home, sizeof(home))) {
        expect(0, "shape2 hive");
        return;
    }
    expect(hush_store_create(&store) == HUSH_OK, "shape2 store");
    fill_agent(&in, "Sentry");
    expect(hush_launch_add_agent(&launch, store, &in, 1) == HUSH_OK,
           "shape2 add A save_pass=1");
    a = find_named(&launch, "Sentry");
    expect(a != NULL, "shape2 A present");
    if (a == NULL)
        return;
    snprintf(a_pub, sizeof(a_pub), "%s", a->id.pubkey_hex);
    snprintf(slug, sizeof(slug), "%s", a->slug);
    snprintf(path, sizeof(path), "agents/%s/nsec", slug);

    expect(hush_launch_remove_agent(&launch, slug) == HUSH_OK, "shape2 delete A");
    expect(hush_keystore_load(loaded, sizeof(loaded), path) == HUSH_ERR_NOT_FOUND,
           "shape2 delete cleared pass");

    fill_agent(&in, "Sentry");
    expect(hush_launch_add_agent(&launch, store, &in, 0) == HUSH_OK,
           "shape2 add B save_pass=0");
    b = find_named(&launch, "Sentry");
    expect(b != NULL, "shape2 B present");
    if (b == NULL)
        return;
    expect(strcmp(b->id.pubkey_hex, a_pub) != 0, "shape2 B minted fresh");
    expect(hush_keystore_load(loaded, sizeof(loaded), path) == HUSH_ERR_NOT_FOUND,
           "shape2 no store holds a key after offer wrote nowhere");

    hush_launch_init(&again);
    expect(hush_launch_restore_identity(&again) == HUSH_OK, "shape2 restore id");
    expect(hush_launch_restore_vibe(&again) == HUSH_OK, "shape2 restore vibe");
    b = find_named(&again, "Sentry");
    expect(b != NULL, "shape2 restored Sentry");
    if (b != NULL)
        expect(strcmp(b->id.pubkey_hex, a_pub) != 0,
               "shape2 restore is not deleted A");
    hush_store_destroy(store);
}


/* O1: wipe runs while vibe still lists the slug (before favorites/roster/vibe). */
static void test_wipe_before_vibe(void)
{
    static hush_launch_t launch;
    hush_store_t *store = NULL;
    hush_roster_agent_in_t in;
    const hush_roster_agent_t *a;
    char home[128];
    char pass_dir[128];
    char wipe_log[160];
    char slug[HUSH_ROSTER_NAME_MAX];
    char line[256];
    FILE *fp;
    int wrote = 0;

    wipe_helpers();
    snprintf(pass_dir, sizeof(pass_dir), "%s/hush-kc-o1-pass-%d", g_root, (int)getpid());
    wrote = snprintf(wipe_log, sizeof(wipe_log), "%s/hush-kc-o1-wipe-%d.json", g_root,
                     (int)getpid());
    if (wrote < 0 || (size_t)wrote >= sizeof(wipe_log)) {
        expect(0, "o1 wipe log path fits");
        return;
    }
    unlink(wipe_log);
    expect(arm_pass(pass_dir), "o1 arm pass");
    expect(setenv("HUSH_FAKE_PASS_WIPE_LOG", wipe_log, 1) == 0, "o1 wipe log env");
    if (!raise_hive("o1", &launch, home, sizeof(home))) {
        expect(0, "o1 hive");
        return;
    }
    expect(hush_store_create(&store) == HUSH_OK, "o1 store");
    fill_agent(&in, "Sentry");
    expect(hush_launch_add_agent(&launch, store, &in, 1) == HUSH_OK, "o1 add");
    a = find_named(&launch, "Sentry");
    expect(a != NULL, "o1 A present");
    if (a == NULL)
        return;
    snprintf(slug, sizeof(slug), "%s", a->slug);
    expect(hush_launch_remove_agent(&launch, slug) == HUSH_OK, "o1 delete");
    expect(find_named(&launch, "Sentry") == NULL, "o1 roster dropped A");
    fp = fopen(wipe_log, "r");
    expect(fp != NULL, "o1 wipe log written");
    if (fp == NULL)
        return;
    if (fgets(line, sizeof(line), fp) == NULL)
        line[0] = '\0';
    fclose(fp);
    expect(strstr(line, "\"agent_in_vibe\":1") != NULL,
           "o1 wipe ran before vibe forgot the slug");
    hush_store_destroy(store);
}

/* R2+R3: one hard store failure continues; first hard error is returned. */
static void test_remove_partial_continue(void)
{
    hush_identity_t a;
    hush_identity_t b;
    hush_identity_t c;
    char pass_dir[128];
    char op_dir[128];
    char secret_dir[128];
    char path[] = "agents/partialbot/nsec";
    char out[HUSH_PASS_SECRET_MAX];
    hush_status_t st;

    wipe_helpers();
    snprintf(pass_dir, sizeof(pass_dir), "%s/hush-kc-r23-pass-%d", g_root, (int)getpid());
    snprintf(op_dir, sizeof(op_dir), "%s/hush-kc-r23-op-%d", g_root, (int)getpid());
    snprintf(secret_dir, sizeof(secret_dir), "%s/hush-kc-r23-sec-%d", g_root,
             (int)getpid());
    expect(arm_pass(pass_dir), "r23 arm pass");
    expect(arm_op(op_dir), "r23 arm op");
    expect(mkdir_ok(secret_dir), "r23 secret dir");
    expect(setenv("HUSH_FAKE_SECRET_DIR", secret_dir, 1) == 0, "r23 secret env");
    expect(setenv("HUSH_SECRET_TOOL_HELPER", "tests/fake-secret-tool.py", 1) == 0,
           "r23 secret helper");
    expect(setenv("HUSH_FAKE_OP_DELETE_FAIL", "1", 1) == 0, "r23 op fail");

    expect(hush_identity_generate(&a) == HUSH_OK, "r23 gen A");
    expect(hush_identity_generate(&b) == HUSH_OK, "r23 gen B");
    expect(hush_identity_generate(&c) == HUSH_OK, "r23 gen C");
    expect(hush_keystore_save(HUSH_KEYSTORE_PASS, path, a.nsec) == HUSH_OK,
           "r23 save pass");
    expect(hush_keystore_save(HUSH_KEYSTORE_OP, path, b.nsec) == HUSH_OK,
           "r23 save op");
    expect(hush_keystore_save(HUSH_KEYSTORE_SECRET, path, c.nsec) == HUSH_OK,
           "r23 save secret");

    st = hush_keystore_remove(path);
    expect(st == HUSH_ERR_IO, "r23 remove reports first hard error");
    expect(hush_keystore_load_kind(HUSH_KEYSTORE_PASS, out, sizeof(out), path) ==
               HUSH_ERR_NOT_FOUND,
           "r23 continued: pass cleared");
    expect(hush_keystore_load_kind(HUSH_KEYSTORE_OP, out, sizeof(out), path) ==
               HUSH_OK,
           "r23 op still present after failed delete");
    expect(strcmp(out, b.nsec) == 0, "r23 op still holds B");
    expect(hush_keystore_load_kind(HUSH_KEYSTORE_SECRET, out, sizeof(out),
                                   path) == HUSH_ERR_NOT_FOUND,
           "r23 continued: secret cleared after op failure");
}

/* O2: launch delete returns wipe status; roster still drops the robot. */
static void test_launch_reports_wipe_status(void)
{
    static hush_launch_t launch;
    hush_store_t *store = NULL;
    hush_roster_agent_in_t in;
    const hush_roster_agent_t *a;
    char home[128];
    char pass_dir[128];
    char op_dir[128];
    char path[HUSH_PASS_PATH_MAX];
    char loaded[HUSH_PASS_SECRET_MAX];
    char slug[HUSH_ROSTER_NAME_MAX];
    hush_status_t st;

    wipe_helpers();
    snprintf(pass_dir, sizeof(pass_dir), "%s/hush-kc-o2-pass-%d", g_root, (int)getpid());
    snprintf(op_dir, sizeof(op_dir), "%s/hush-kc-o2-op-%d", g_root, (int)getpid());
    expect(arm_pass(pass_dir), "o2 arm pass");
    expect(arm_op(op_dir), "o2 arm op");
    expect(setenv("HUSH_FAKE_OP_DELETE_FAIL", "1", 1) == 0, "o2 op fail");
    if (!raise_hive("o2", &launch, home, sizeof(home))) {
        expect(0, "o2 hive");
        return;
    }
    expect(hush_store_create(&store) == HUSH_OK, "o2 store");
    fill_agent(&in, "Sentry");
    /* save_pass=1 lands pass; offer also writes op when ready. */
    expect(hush_launch_add_agent(&launch, store, &in, 1) == HUSH_OK, "o2 add");
    a = find_named(&launch, "Sentry");
    expect(a != NULL, "o2 A present");
    if (a == NULL)
        return;
    snprintf(slug, sizeof(slug), "%s", a->slug);
    snprintf(path, sizeof(path), "agents/%s/nsec", slug);
    expect(hush_keystore_load_kind(HUSH_KEYSTORE_PASS, loaded, sizeof(loaded),
                                   path) == HUSH_OK,
           "o2 pass holds A");
    /* save_pass=1 lands pass only; seed op too so wipe hits a hard failure. */
    expect(hush_keystore_save(HUSH_KEYSTORE_OP, path, a->id.nsec) == HUSH_OK,
           "o2 seed op");
    expect(hush_keystore_load_kind(HUSH_KEYSTORE_OP, loaded, sizeof(loaded),
                                   path) == HUSH_OK,
           "o2 op holds A");

    st = hush_launch_remove_agent(&launch, slug);
    expect(st == HUSH_ERR_IO, "o2 delete reports wipe failure");
    expect(find_named(&launch, "Sentry") == NULL, "o2 roster still dropped A");
    expect(hush_keystore_load_kind(HUSH_KEYSTORE_PASS, loaded, sizeof(loaded),
                                   path) == HUSH_ERR_NOT_FOUND,
           "o2 pass cleared despite op fail");
    expect(hush_keystore_load_kind(HUSH_KEYSTORE_OP, loaded, sizeof(loaded),
                                   path) == HUSH_OK,
           "o2 op left after failed delete");
    hush_store_destroy(store);
}

/* Direct remove clears every ready store; skip-remove mutant would fail. */
static void test_remove_all_stores(void)
{
    hush_identity_t a;
    hush_identity_t b;
    hush_identity_t c;
    char pass_dir[128];
    char op_dir[128];
    char secret_dir[128];
    char path[] = "agents/clearbot/nsec";
    char out[HUSH_PASS_SECRET_MAX];

    wipe_helpers();
    snprintf(pass_dir, sizeof(pass_dir), "%s/hush-kc-rm-pass-%d", g_root, (int)getpid());
    snprintf(op_dir, sizeof(op_dir), "%s/hush-kc-rm-op-%d", g_root, (int)getpid());
    snprintf(secret_dir, sizeof(secret_dir), "%s/hush-kc-rm-sec-%d", g_root,
             (int)getpid());
    expect(arm_pass(pass_dir), "rm arm pass");
    expect(arm_op(op_dir), "rm arm op");
    expect(mkdir_ok(secret_dir), "rm secret dir");
    expect(setenv("HUSH_FAKE_SECRET_DIR", secret_dir, 1) == 0, "rm secret env");
    expect(setenv("HUSH_SECRET_TOOL_HELPER", "tests/fake-secret-tool.py", 1) == 0,
           "rm secret helper");

    expect(hush_identity_generate(&a) == HUSH_OK, "rm gen A");
    expect(hush_identity_generate(&b) == HUSH_OK, "rm gen B");
    expect(hush_identity_generate(&c) == HUSH_OK, "rm gen C");
    expect(hush_keystore_save(HUSH_KEYSTORE_PASS, path, a.nsec) == HUSH_OK,
           "rm save pass");
    expect(hush_keystore_save(HUSH_KEYSTORE_OP, path, b.nsec) == HUSH_OK,
           "rm save op");
    expect(hush_keystore_save(HUSH_KEYSTORE_SECRET, path, c.nsec) == HUSH_OK,
           "rm save secret");
    expect(hush_keystore_remove(path) == HUSH_OK, "rm all");
    expect(hush_keystore_load_kind(HUSH_KEYSTORE_PASS, out, sizeof(out), path) ==
               HUSH_ERR_NOT_FOUND,
           "rm cleared pass");
    expect(hush_keystore_load_kind(HUSH_KEYSTORE_OP, out, sizeof(out), path) ==
               HUSH_ERR_NOT_FOUND,
           "rm cleared op");
    expect(hush_keystore_load_kind(HUSH_KEYSTORE_SECRET, out, sizeof(out),
                                   path) == HUSH_ERR_NOT_FOUND,
           "rm cleared secret");
    expect(hush_keystore_load(out, sizeof(out), path) == HUSH_ERR_NOT_FOUND,
           "rm load misses");
}

int main(void)
{
    if (!root_make())
        return 1;
    test_remove_all_stores();
    test_remove_partial_continue();
    test_wipe_before_vibe();
    test_launch_reports_wipe_status();
    test_shape1_pass_outranks_op();
    test_shape2_offer_nowhere();
    if (g_fail)
        return 1;
    printf("test_keystore_clear ok\n");
    return 0;
}

static int root_make(void)
{
    const char *base_dir = root_tmpdir();
    int wrote = 0;

    assert(g_root[0] == '\0');
    if (!root_self_check())
        return 0;
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
    if (!root_is_under_tmpdir(getenv("TMPDIR"), g_root)) {
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

static int root_self_check(void)
{
    /* nftw never hands root_remove_entry a path outside the root, so this
     * direct check is the only place the prefix-sibling case is exercised. */
    if (!root_holds("/r", "/r") || !root_holds("/r", "/r/k") ||
        root_holds("/r", "/rX/k") || root_holds("/r", "/rX")) {
        fprintf(stderr, "FAIL root_holds accepts a prefix sibling\n");
        return 0;
    }
    if (!root_is_under_tmpdir("/tmp/x", "/tmp/x/hush-k") ||
        root_is_under_tmpdir("/tmp/x", "/tmp/xprivate") ||
        root_is_under_tmpdir("/tmp/x", "/tmp/xprivate/hush-k")) {
        fprintf(stderr, "FAIL root_is_under_tmpdir accepts a prefix sibling\n");
        return 0;
    }
    return 1;
}

static int root_is_under_tmpdir(const char *tmpdir, const char *root)
{
    assert(root != NULL && root[0] != '\0');
    /* The same absolute test as root_tmpdir, repeated on purpose: the check
     * must not share the leaf it checks. No trailing-slash strip is needed:
     * root is "<tmpdir>/<name>", so tmpdir "X/" gives "X//<name>", which
     * root_holds("X/", ...) already accepts. */
    if (tmpdir == NULL || tmpdir[0] != '/')
        return 1;
    return root_holds(tmpdir, root);
}

static const char *root_tmpdir(void)
{
    const char *env = getenv("TMPDIR");
    if (env == NULL || env[0] != '/')
        return NULL;
    return env;
}
