/* tests/test_keystore.c: pass, op, and secret-tool restore the same public ids.
 * Also pins store order, offer routing, op naming, and nsec-off-argv. */

#define _XOPEN_SOURCE 700 /* nftw, for the private root cleanup */

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

/* <TMPDIR or /tmp>/hush-ku-XXXXXX: this run's private root, 0700 from mkdtemp.
 * Every key, pass store, home and config the test writes lives under it. */
#define ROOT_NAME "hush-ku-XXXXXX"
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

    snprintf(dir, sizeof(dir), "%s/hush-ku-%s-%d", g_root, tag, (int)getpid());
    if (mkdir(dir, 0700) != 0 && errno != EEXIST)
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

/* Arms pass, op, and secret-tool into three separate fake dirs. */
static int arm_all_stores(const char *tag, char *pass_dir, char *op_dir,
                          char *secret_dir, size_t dirsz)
{
    int wrote;

    wrote = snprintf(pass_dir, dirsz, "%s/hush-ku-%s-pass-%d", g_root, tag,
                     (int)getpid());
    if (wrote < 0 || (size_t)wrote >= dirsz)
        return 0;
    wrote = snprintf(op_dir, dirsz, "%s/hush-ku-%s-op-%d", g_root, tag, (int)getpid());
    if (wrote < 0 || (size_t)wrote >= dirsz)
        return 0;
    wrote = snprintf(secret_dir, dirsz, "%s/hush-ku-%s-sec-%d", g_root, tag,
                     (int)getpid());
    if (wrote < 0 || (size_t)wrote >= dirsz)
        return 0;
    if ((mkdir(pass_dir, 0700) != 0 && errno != EEXIST) ||
        (mkdir(op_dir, 0700) != 0 && errno != EEXIST) ||
        (mkdir(secret_dir, 0700) != 0 && errno != EEXIST))
        return 0;
    if (setenv("HUSH_FAKE_PASS_DIR", pass_dir, 1) != 0)
        return 0;
    if (setenv("HUSH_FAKE_OP_DIR", op_dir, 1) != 0)
        return 0;
    if (setenv("HUSH_FAKE_SECRET_DIR", secret_dir, 1) != 0)
        return 0;
    hush_pass_set_helper("tests/fake-pass.sh");
    if (setenv("HUSH_OP_HELPER", "tests/fake-op.py", 1) != 0)
        return 0;
    if (setenv("HUSH_SECRET_TOOL_HELPER", "tests/fake-secret-tool.py", 1) != 0)
        return 0;
    return 1;
}

static int read_file(const char *path, char *out, size_t outsz)
{
    FILE *fp;
    size_t n;

    if (out == NULL || outsz == 0)
        return 0;
    out[0] = '\0';
    fp = fopen(path, "r");
    if (fp == NULL)
        return 0;
    n = fread(out, 1, outsz - 1, fp);
    fclose(fp);
    out[n] = '\0';
    return 1;
}

static int meta_has(const char *dir, const char *name, const char *needle)
{
    char path[256];
    char buf[1024];

    snprintf(path, sizeof(path), "%s/%s", dir, name);
    if (!read_file(path, buf, sizeof(buf)))
        return 0;
    return strstr(buf, needle) != NULL;
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

    wrote = snprintf(home, homesz, "%s/hush-ku-home-%s-%d", g_root, tag, (int)getpid());
    if (wrote < 0 || (size_t)wrote >= homesz)
        return 0;
    snprintf(cfg, sizeof(cfg), "%s/hush-ku-cfg-%s-%d", g_root, tag, (int)getpid());
    if ((mkdir(home, 0700) != 0 && errno != EEXIST) ||
        (mkdir(cfg, 0700) != 0 && errno != EEXIST))
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
    expect(count_nsec_files(home) == 0, "seed writes no plain key file");
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
    expect(count_nsec_files(home) == 0, "mint wrote no plain key file");
    expect(strcmp(again.human.pubkey_hex, keys.human.pubkey_hex) != 0,
           "missing human is not the old public id");
}

/* F1: pass → op → secret-tool; first hit wins (kills M1, M2). */
static void test_load_order_first_hit(void)
{
    char pass_dir[128];
    char op_dir[128];
    char secret_dir[128];
    char out[HUSH_PASS_SECRET_MAX];
    hush_keystore_kind found = HUSH_KEYSTORE_NONE;
    static const char *pass_sec = "nsec1passorderhitaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    static const char *op_sec = "nsec1oporderhitbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    static const char *secret_sec =
        "nsec1secretorderhitccccccccccccccccccccccccccccc";

    wipe_helpers();
    expect(arm_all_stores("order", pass_dir, op_dir, secret_dir, sizeof(pass_dir)),
           "arm all stores for order");
    expect(hush_keystore_save(HUSH_KEYSTORE_PASS, HUSH_PASS_IDENTITY_NSEC,
                              pass_sec) == HUSH_OK,
           "order save pass");
    expect(hush_keystore_save(HUSH_KEYSTORE_OP, HUSH_PASS_IDENTITY_NSEC,
                              op_sec) == HUSH_OK,
           "order save op");
    expect(hush_keystore_save(HUSH_KEYSTORE_SECRET, HUSH_PASS_IDENTITY_NSEC,
                              secret_sec) == HUSH_OK,
           "order save secret");
    expect(hush_keystore_load_from(&found, out, sizeof(out),
                                   HUSH_PASS_IDENTITY_NSEC) == HUSH_OK,
           "order load hits");
    expect(found == HUSH_KEYSTORE_PASS, "first-hit store is pass");
    expect(strcmp(out, pass_sec) == 0, "first-hit secret is pass");
    /* Drop pass: next must be op, not secret (order op before secret). */
    unsetenv("HUSH_FAKE_PASS_DIR");
    hush_pass_set_helper("/nonexistent/hush-pass-helper");
    found = HUSH_KEYSTORE_NONE;
    memset(out, 0, sizeof(out));
    expect(hush_keystore_load_from(&found, out, sizeof(out),
                                   HUSH_PASS_IDENTITY_NSEC) == HUSH_OK,
           "order load without pass");
    expect(found == HUSH_KEYSTORE_OP, "second-hit store is op");
    expect(strcmp(out, op_sec) == 0, "second-hit secret is op");
}

/* F2: offer routes pass | op | secret-tool | memory (kills M4, M5, M6). */
static void test_offer_routing(void)
{
    char pass_dir[128];
    char op_dir[128];
    char secret_dir[128];
    char out[HUSH_PASS_SECRET_MAX];
    static const char *secret =
        "nsec1offerroutingaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

    /* use_pass selected → pass only (M5). */
    wipe_helpers();
    expect(arm_all_stores("offp", pass_dir, op_dir, secret_dir, sizeof(pass_dir)),
           "arm offer pass");
    hush_keystore_offer(1, HUSH_PASS_IDENTITY_NSEC, secret);
    expect(hush_keystore_load_kind(HUSH_KEYSTORE_PASS, out, sizeof(out),
                                   HUSH_PASS_IDENTITY_NSEC) == HUSH_OK,
           "offer use_pass wrote pass");
    expect(strcmp(out, secret) == 0, "offer use_pass secret");
    expect(hush_keystore_load_kind(HUSH_KEYSTORE_OP, out, sizeof(out),
                                   HUSH_PASS_IDENTITY_NSEC) == HUSH_ERR_NOT_FOUND,
           "offer use_pass skipped op");
    expect(hush_keystore_load_kind(HUSH_KEYSTORE_SECRET, out, sizeof(out),
                                   HUSH_PASS_IDENTITY_NSEC) ==
               HUSH_ERR_NOT_FOUND,
           "offer use_pass skipped secret");

    /* No pass: op wins over secret-tool (M4). */
    wipe_helpers();
    expect(arm_all_stores("offo", pass_dir, op_dir, secret_dir, sizeof(pass_dir)),
           "arm offer op");
    hush_pass_set_helper("/nonexistent/hush-pass-helper");
    unsetenv("HUSH_FAKE_PASS_DIR");
    hush_keystore_offer(0, HUSH_PASS_IDENTITY_NSEC, secret);
    expect(hush_keystore_load_kind(HUSH_KEYSTORE_OP, out, sizeof(out),
                                   HUSH_PASS_IDENTITY_NSEC) == HUSH_OK,
           "offer wrote op");
    expect(hush_keystore_load_kind(HUSH_KEYSTORE_SECRET, out, sizeof(out),
                                   HUSH_PASS_IDENTITY_NSEC) ==
               HUSH_ERR_NOT_FOUND,
           "offer preferred op over secret");

    /* No pass, no op: secret-tool. */
    wipe_helpers();
    expect(arm_store(HUSH_KEYSTORE_SECRET, "offs"), "arm offer secret");
    hush_keystore_offer(0, HUSH_PASS_IDENTITY_NSEC, secret);
    expect(hush_keystore_load_kind(HUSH_KEYSTORE_SECRET, out, sizeof(out),
                                   HUSH_PASS_IDENTITY_NSEC) == HUSH_OK,
           "offer wrote secret-tool");

    /* Nothing ready: stays in memory (M6). */
    wipe_helpers();
    hush_pass_set_helper("/nonexistent/hush-pass-helper");
    setenv("HUSH_OP_HELPER", "/nonexistent/op", 1);
    setenv("HUSH_SECRET_TOOL_HELPER", "/nonexistent/secret-tool", 1);
    hush_keystore_offer(0, HUSH_PASS_IDENTITY_NSEC, secret);
    expect(hush_keystore_load(out, sizeof(out), HUSH_PASS_IDENTITY_NSEC) ==
               HUSH_ERR_NOT_FOUND,
           "offer with no store leaves memory only");
}

/* F3: vault Hush, HUSH_OP_VAULT, title hush-*-nsec (kills M7, M8, M9). */
static void test_op_naming(void)
{
    char op_dir[128];
    char out[HUSH_PASS_SECRET_MAX];
    static const char *secret =
        "nsec1opnamingpinnaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

    wipe_helpers();
    snprintf(op_dir, sizeof(op_dir), "%s/hush-ku-name-%d", g_root, (int)getpid());
    expect(mkdir(op_dir, 0700) == 0 || errno == EEXIST, "op naming dir");
    expect(setenv("HUSH_FAKE_OP_DIR", op_dir, 1) == 0, "op naming env");
    hush_pass_set_helper("/nonexistent/hush-pass-helper");
    expect(setenv("HUSH_OP_HELPER", "tests/fake-op.py", 1) == 0, "op helper");
    unsetenv("HUSH_OP_VAULT");
    expect(hush_keystore_save(HUSH_KEYSTORE_OP, HUSH_PASS_IDENTITY_NSEC,
                              secret) == HUSH_OK,
           "op default save");
    expect(meta_has(op_dir, "last_create.json", "\"vault\": \"Hush\""),
           "default vault is Hush");
    expect(meta_has(op_dir, "last_create.json",
                    "\"title\": \"hush-identity-nsec\""),
           "title hush-identity-nsec");
    expect(hush_keystore_load_kind(HUSH_KEYSTORE_OP, out, sizeof(out),
                                   HUSH_PASS_IDENTITY_NSEC) == HUSH_OK,
           "op default load");
    expect(strcmp(out, secret) == 0, "op default round-trip");
    expect(meta_has(op_dir, "last_read.json", "\"vault\": \"Hush\""),
           "read URI uses Hush");

    /* Override vault. */
    wipe_helpers();
    snprintf(op_dir, sizeof(op_dir), "%s/hush-ku-vault-%d", g_root, (int)getpid());
    expect(mkdir(op_dir, 0700) == 0 || errno == EEXIST, "op vault dir");
    expect(setenv("HUSH_FAKE_OP_DIR", op_dir, 1) == 0, "op vault env");
    hush_pass_set_helper("/nonexistent/hush-pass-helper");
    expect(setenv("HUSH_OP_HELPER", "tests/fake-op.py", 1) == 0, "op vault helper");
    expect(setenv("HUSH_OP_VAULT", "AltVault", 1) == 0, "set HUSH_OP_VAULT");
    expect(hush_keystore_save(HUSH_KEYSTORE_OP, HUSH_PASS_IDENTITY_NSEC,
                              secret) == HUSH_OK,
           "op override save");
    expect(meta_has(op_dir, "last_create.json", "\"vault\": \"AltVault\""),
           "HUSH_OP_VAULT overrides vault");
    expect(hush_keystore_load_kind(HUSH_KEYSTORE_OP, out, sizeof(out),
                                   HUSH_PASS_IDENTITY_NSEC) == HUSH_OK,
           "op override load");
    expect(strcmp(out, secret) == 0, "op override round-trip");
}

/* F4: nsec never on argv for op; also secret-tool (M10, M11). */
static void test_nsec_off_argv(void)
{
    char op_dir[128];
    char secret_dir[128];
    char meta[1024];
    char path[256];
    static const char *secret =
        "nsec1argvneveroncliaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

    wipe_helpers();
    snprintf(op_dir, sizeof(op_dir), "%s/hush-ku-argv-op-%d", g_root, (int)getpid());
    expect(mkdir(op_dir, 0700) == 0 || errno == EEXIST, "argv op dir");
    expect(setenv("HUSH_FAKE_OP_DIR", op_dir, 1) == 0, "argv op env");
    hush_pass_set_helper("/nonexistent/hush-pass-helper");
    expect(setenv("HUSH_OP_HELPER", "tests/fake-op.py", 1) == 0, "argv op helper");
    expect(hush_keystore_save(HUSH_KEYSTORE_OP, HUSH_PASS_IDENTITY_NSEC,
                              secret) == HUSH_OK,
           "op save with nsec on stdin only");
    snprintf(path, sizeof(path), "%s/last_create.json", op_dir);
    expect(read_file(path, meta, sizeof(meta)), "op create meta");
    expect(strstr(meta, secret) == NULL, "op argv meta has no nsec");
    expect(meta_has(op_dir, "last_create.json", "\"stdin_has_password\": true"),
           "op create used stdin");

    wipe_helpers();
    snprintf(secret_dir, sizeof(secret_dir), "%s/hush-ku-argv-sec-%d", g_root,
             (int)getpid());
    expect(mkdir(secret_dir, 0700) == 0 || errno == EEXIST, "argv secret dir");
    expect(setenv("HUSH_FAKE_SECRET_DIR", secret_dir, 1) == 0, "argv secret env");
    hush_pass_set_helper("/nonexistent/hush-pass-helper");
    expect(setenv("HUSH_SECRET_TOOL_HELPER", "tests/fake-secret-tool.py", 1) == 0,
           "argv secret helper");
    expect(hush_keystore_save(HUSH_KEYSTORE_SECRET, HUSH_PASS_IDENTITY_NSEC,
                              secret) == HUSH_OK,
           "secret-tool save with nsec on stdin only");
    snprintf(path, sizeof(path), "%s/last_store.json", secret_dir);
    expect(read_file(path, meta, sizeof(meta)), "secret store meta");
    expect(strstr(meta, secret) == NULL, "secret-tool argv meta has no nsec");
}

int main(void)
{
    if (!root_make())
        return 1;
    test_store_restores(HUSH_KEYSTORE_PASS, "pass");
    test_store_restores(HUSH_KEYSTORE_OP, "op");
    test_store_restores(HUSH_KEYSTORE_SECRET, "secret");
    test_missing_refuses_lookalike();
    test_load_order_first_hit();
    test_offer_routing();
    test_op_naming();
    test_nsec_off_argv();
    if (g_fail)
        return 1;
    printf("test_keystore ok\n");
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
