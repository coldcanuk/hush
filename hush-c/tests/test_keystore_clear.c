/* tests/test_keystore_clear.c: delete clears keys so reuse is a fresh signer.
 * Pins issue #264 Shape 1 and Shape 2 against the fake stores. */

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

    wrote = snprintf(home, homesz, "/tmp/hush-kc-home-%s-%d", tag, (int)getpid());
    if (wrote < 0 || (size_t)wrote >= homesz)
        return 0;
    snprintf(cfg, sizeof(cfg), "/tmp/hush-kc-cfg-%s-%d", tag, (int)getpid());
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
    snprintf(pass_dir, sizeof(pass_dir), "/tmp/hush-kc-s1-pass-%d", (int)getpid());
    snprintf(op_dir, sizeof(op_dir), "/tmp/hush-kc-s1-op-%d", (int)getpid());
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
    snprintf(pass_dir, sizeof(pass_dir), "/tmp/hush-kc-s2-pass-%d", (int)getpid());
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
    char wipe_log[128];
    char slug[HUSH_ROSTER_NAME_MAX];
    char line[256];
    FILE *fp;

    wipe_helpers();
    snprintf(pass_dir, sizeof(pass_dir), "/tmp/hush-kc-o1-pass-%d", (int)getpid());
    snprintf(wipe_log, sizeof(wipe_log), "/tmp/hush-kc-o1-wipe-%d.json", (int)getpid());
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
    snprintf(pass_dir, sizeof(pass_dir), "/tmp/hush-kc-r23-pass-%d", (int)getpid());
    snprintf(op_dir, sizeof(op_dir), "/tmp/hush-kc-r23-op-%d", (int)getpid());
    snprintf(secret_dir, sizeof(secret_dir), "/tmp/hush-kc-r23-sec-%d",
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
    snprintf(pass_dir, sizeof(pass_dir), "/tmp/hush-kc-o2-pass-%d", (int)getpid());
    snprintf(op_dir, sizeof(op_dir), "/tmp/hush-kc-o2-op-%d", (int)getpid());
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
    snprintf(pass_dir, sizeof(pass_dir), "/tmp/hush-kc-rm-pass-%d", (int)getpid());
    snprintf(op_dir, sizeof(op_dir), "/tmp/hush-kc-rm-op-%d", (int)getpid());
    snprintf(secret_dir, sizeof(secret_dir), "/tmp/hush-kc-rm-sec-%d",
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
