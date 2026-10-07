/* tests/test_keyfile.c: restart keeps npubs; the vault is not plaintext. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hush_keyfile.h"
#include "hush_launch.h"
#include "hush_pass.h"
#include "hush_store.h"

enum {
    SCAN_DEPTH_MAX = 6,
    SCAN_QUEUE_MAX = 64,
    SCAN_PATH_MAX = 512,
    SCAN_READ_MAX = 4096,
    VAULT_READ_MAX = 8192
};

static int g_fail;

typedef struct scan_job {
    char path[SCAN_PATH_MAX];
    int depth;
} scan_job_t;

typedef struct scan_state {
    scan_job_t *q;
    int *n;
    int depth;
} scan_state_t;

typedef struct hive_dirs {
    char *home;
    size_t homesz;
    char *cfg;
    size_t cfgsz;
} hive_dirs_t;

typedef struct hive_ids {
    char human[HUSH_IDENTITY_NPUB_MAX];
    char payne[HUSH_IDENTITY_HEX_LEN + 1];
    char coach[HUSH_IDENTITY_HEX_LEN + 1];
    char walk[HUSH_IDENTITY_HEX_LEN + 1];
} hive_ids_t;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

static const char *agent_pub(const hush_launch_t *launch, const char *slug)
{
    size_t i = 0;

    if (launch == NULL || slug == NULL)
        return "";
    for (i = 0; i < launch->roster.nagents; i++) {
        if (strcmp(launch->roster.agents[i].slug, slug) == 0)
            return launch->roster.agents[i].id.pubkey_hex;
    }
    return "";
}

static int file_has_nsec(const char *path)
{
    FILE *fp = NULL;
    char buf[SCAN_READ_MAX] = {0};
    size_t n = 0;

    fp = fopen(path, "r");
    if (fp == NULL)
        return 0;
    n = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[n] = '\0';
    return strstr(buf, "nsec1") != NULL;
}

static int scan_push(scan_job_t *q, int *n, const char *path, int depth)
{
    scan_job_t job = {0};
    size_t len = 0;

    assert(q != NULL && n != NULL && path != NULL);
    if (*n >= SCAN_QUEUE_MAX)
        return 0;
    len = strlen(path);
    if (len + 1 > sizeof(job.path))
        return 0;
    memcpy(job.path, path, len + 1);
    job.depth = depth;
    q[*n] = job;
    *n += 1;
    return 1;
}

static int scan_entry(const char *path, const struct stat *st, scan_state_t *state)
{
    assert(path != NULL && st != NULL && state != NULL);
    if (S_ISREG(st->st_mode))
        return file_has_nsec(path);
    if (!S_ISDIR(st->st_mode))
        return 0;
    if (state->depth + 1 > SCAN_DEPTH_MAX)
        return 0;
    if (!scan_push(state->q, state->n, path, state->depth + 1))
        return 1;
    return 0;
}

static int scan_named(const char *dir, const char *name, scan_state_t *state)
{
    char path[SCAN_PATH_MAX] = {0};
    struct stat st = {0};
    int wrote = 0;

    assert(dir != NULL && name != NULL && state != NULL);
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return 0;
    wrote = snprintf(path, sizeof(path), "%s/%s", dir, name);
    if (wrote < 0 || wrote >= (int)sizeof(path))
        return 0;
    if (stat(path, &st) != 0)
        return 0;
    return scan_entry(path, &st, state);
}

static int scan_dir(const char *dir, scan_state_t *state)
{
    DIR *handle = NULL;
    struct dirent *ent = NULL;

    assert(dir != NULL && state != NULL);
    if (state->depth > SCAN_DEPTH_MAX)
        return 0;
    handle = opendir(dir);
    if (handle == NULL)
        return 0;
    ent = readdir(handle);
    while (ent != NULL) {
        if (scan_named(dir, ent->d_name, state)) {
            closedir(handle);
            return 1;
        }
        ent = readdir(handle);
    }
    closedir(handle);
    return 0;
}

/* 1 when any file under dir contains the plaintext nsec marker. */
static int tree_has_nsec(const char *dir, int depth)
{
    scan_job_t q[SCAN_QUEUE_MAX] = {0};
    scan_state_t state = {0};
    int n = 0;
    int i = 0;

    (void)depth;
    if (dir == NULL)
        return 0;
    state.q = q;
    state.n = &n;
    state.depth = 0;
    if (scan_dir(dir, &state))
        return 1;
    for (i = 0; i < n; i++) {
        state.depth = q[i].depth;
        if (scan_dir(q[i].path, &state))
            return 1;
    }
    return 0;
}

static int read_file(char *out, size_t outsz, const char *path)
{
    FILE *fp = NULL;
    size_t n = 0;

    if (out == NULL || outsz == 0 || path == NULL)
        return 0;
    fp = fopen(path, "r");
    if (fp == NULL)
        return 0;
    n = fread(out, 1, outsz - 1, fp);
    fclose(fp);
    out[n] = '\0';
    return 1;
}

static int open_hive(hush_launch_t *launch, hush_store_t **store, hive_dirs_t *dirs)
{
    char tmpl_home[] = "/tmp/hush-keyfile-home-XXXXXX";
    char tmpl_cfg[] = "/tmp/hush-keyfile-cfg-XXXXXX";
    char *made_home = NULL;
    char *made_cfg = NULL;

    assert(launch != NULL && store != NULL && dirs != NULL);
    made_home = mkdtemp(tmpl_home);
    made_cfg = mkdtemp(tmpl_cfg);
    if (made_home == NULL || made_cfg == NULL)
        return 0;
    if (strlen(made_home) + 1 > dirs->homesz || strlen(made_cfg) + 1 > dirs->cfgsz)
        return 0;
    memcpy(dirs->home, made_home, strlen(made_home) + 1);
    memcpy(dirs->cfg, made_cfg, strlen(made_cfg) + 1);
    if (setenv("HUSH_HOME", dirs->home, 1) != 0)
        return 0;
    if (setenv("HUSH_CONFIG_DIR", dirs->cfg, 1) != 0)
        return 0;
    if (setenv(HUSH_KEYFILE_ENV, "correct-phrase", 1) != 0)
        return 0;
    hush_pass_set_helper("/nonexistent/pass");
    hush_launch_init(launch);
    if (hush_store_create(store) != HUSH_OK)
        return 0;
    if (hush_launch_create_identity(launch) != HUSH_OK)
        return 0;
    if (hush_launch_ack_backup(launch, 0) != HUSH_OK)
        return 0;
    if (hush_launch_create_vibe(launch, *store, "HQ", "vault") != HUSH_OK)
        return 0;
    return 1;
}

static void copy_pub(char *dst, size_t dstsz, const char *src)
{
    size_t n = 0;

    assert(dst != NULL && dstsz > 0);
    if (src == NULL)
        src = "";
    n = strlen(src);
    if (n + 1 > dstsz)
        n = dstsz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void snapshot_ids(hive_ids_t *ids, const hush_launch_t *launch)
{
    assert(ids != NULL && launch != NULL);
    copy_pub(ids->human, sizeof(ids->human), launch->human.npub);
    copy_pub(ids->payne, sizeof(ids->payne), launch->payne.pubkey_hex);
    copy_pub(ids->coach, sizeof(ids->coach), agent_pub(launch, "coach"));
    copy_pub(ids->walk, sizeof(ids->walk), agent_pub(launch, "walkbot-one"));
}

static int add_walkbot(hush_launch_t *launch, hush_store_t *store)
{
    static const char walk_name[] = "Walkbot One";
    static const char walk_prompt[] = "Walk the floor.";
    hush_roster_agent_in_t in = {0};

    assert(launch != NULL && store != NULL);
    memcpy(in.name, walk_name, sizeof(walk_name));
    memcpy(in.prompt, walk_prompt, sizeof(walk_prompt));
    memcpy(in.provider, HUSH_ROSTER_PROVIDER_GROK_BUILD,
           sizeof(HUSH_ROSTER_PROVIDER_GROK_BUILD));
    return hush_launch_add_agent(launch, store, &in, 0) == HUSH_OK;
}

static void test_restart_same_npubs(void)
{
    static hush_launch_t keys;
    static hush_launch_t again;
    hush_store_t *store = NULL;
    char home[128] = {0};
    char cfg[128] = {0};
    hive_dirs_t dirs = {0};
    hive_ids_t ids = {0};

    dirs.home = home;
    dirs.homesz = sizeof(home);
    dirs.cfg = cfg;
    dirs.cfgsz = sizeof(cfg);
    if (!open_hive(&keys, &store, &dirs)) {
        expect(0, "restart setup");
        return;
    }
    expect(add_walkbot(&keys, store), "add walkbot");
    snapshot_ids(&ids, &keys);
    expect(ids.human[0] != '\0', "human npub");
    expect(ids.payne[0] != '\0', "payne pubkey");
    expect(ids.coach[0] != '\0', "coach pubkey");
    expect(ids.walk[0] != '\0', "walkbot pubkey");
    expect(!tree_has_nsec(home, 0), "home has no plaintext nsec");
    expect(!tree_has_nsec(cfg, 0), "config has no plaintext nsec");
    hush_launch_init(&again);
    expect(hush_launch_restore_identity(&again) == HUSH_OK, "restore identity");
    expect(hush_launch_restore_vibe(&again) == HUSH_OK, "restore vibe");
    expect(again.logged_in, "restart stays logged in");
    expect(strcmp(again.human.npub, ids.human) == 0, "human npub survives");
    expect(strcmp(again.payne.pubkey_hex, ids.payne) == 0, "payne npub survives");
    expect(strcmp(agent_pub(&again, "coach"), ids.coach) == 0, "coach npub survives");
    expect(strcmp(agent_pub(&again, "walkbot-one"), ids.walk) == 0,
           "walkbot npub survives");
    expect(!again.restart_lost_login, "unlock does not ask for a re-import");
    expect(!tree_has_nsec(home, 0), "restart still has no plaintext nsec");
    hush_store_destroy(store);
}

static void expect_wrong_phrase(hush_launch_t *locked)
{
    hush_launch_init(locked);
    expect(hush_launch_restore_identity(locked) == HUSH_OK, "locked restore");
    expect(!locked->logged_in, "wrong phrase does not log in");
    expect(locked->restart_lost_login, "wrong phrase asks for a way forward");
    expect(hush_launch_restore_vibe(locked) != HUSH_OK, "wrong phrase does not mint");
}

static void expect_right_phrase(hush_launch_t *again, const hush_launch_t *keys,
                               const char *human)
{
    hush_launch_init(again);
    expect(hush_launch_restore_identity(again) == HUSH_OK, "right restore");
    expect(hush_launch_restore_vibe(again) == HUSH_OK, "right vibe");
    expect(strcmp(again->human.npub, human) == 0, "right phrase keeps the human");
    expect(strcmp(again->payne.pubkey_hex, keys->payne.pubkey_hex) == 0,
           "right phrase keeps payne");
}

static void test_wrong_phrase_keeps_vault(void)
{
    static hush_launch_t keys;
    static hush_launch_t locked;
    static hush_launch_t again;
    hush_store_t *store = NULL;
    char home[128] = {0};
    char cfg[128] = {0};
    char vault[256] = {0};
    char before[VAULT_READ_MAX] = {0};
    char after[VAULT_READ_MAX] = {0};
    char human[HUSH_IDENTITY_NPUB_MAX] = {0};
    hive_dirs_t dirs = {0};

    dirs.home = home;
    dirs.homesz = sizeof(home);
    dirs.cfg = cfg;
    dirs.cfgsz = sizeof(cfg);
    if (!open_hive(&keys, &store, &dirs)) {
        expect(0, "locked setup");
        return;
    }
    memcpy(human, keys.human.npub, sizeof(human));
    snprintf(vault, sizeof(vault), "%s/keys.vault", home);
    expect(read_file(before, sizeof(before), vault), "vault exists");
    expect(strstr(before, "nsec1") == NULL, "vault ciphertext hides nsec1");
    expect(setenv(HUSH_KEYFILE_ENV, "wrong-phrase", 1) == 0, "wrong phrase");
    expect_wrong_phrase(&locked);
    expect(read_file(after, sizeof(after), vault), "vault still readable");
    expect(strcmp(before, after) == 0, "wrong phrase does not rewrite the vault");
    expect(setenv(HUSH_KEYFILE_ENV, "correct-phrase", 1) == 0, "right phrase");
    expect_right_phrase(&again, &keys, human);
    hush_store_destroy(store);
    unsetenv(HUSH_KEYFILE_ENV);
}

int main(void)
{
    unsetenv(HUSH_KEYFILE_ENV);
    test_restart_same_npubs();
    test_wrong_phrase_keeps_vault();
    unsetenv(HUSH_KEYFILE_ENV);
    if (g_fail)
        return 1;
    printf("test_keyfile ok\n");
    return 0;
}
