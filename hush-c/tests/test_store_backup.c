/* tests/test_store_backup.c: round-trip proof for scripts/hush-store-backup. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hush_home.h"
#include "hush_store.h"

enum {
    BACKUP_SEED_COUNT = 3,
    BACKUP_CMD_LEN = 1024,
    BACKUP_PATH_LEN = 512,
    BACKUP_CONTENT_LEN = 32
};

static int g_fail;

static const char *k_ids[BACKUP_SEED_COUNT] = {
    "b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0",
    "b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1",
    "b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2"
};

static const char *k_pub =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

/* Records a failure without stopping the round trip. */
static void expect(int cond, const char *msg);

/* Fills a kind 1 note with fixed author. Borrowed ev, id, content. */
static void fill_note(hush_event_t *ev, const char *id, const char *content);

/* Formats the seed note content for idx. Empty on overflow. */
static void seed_content(char *out, size_t outsz, size_t idx);

/* Inserts one seed note. Borrowed store. */
static void insert_seed(hush_store_t *store, size_t idx);

/* Runs the backup script with one verb and one dir. Returns exit status. */
static int run_script(const char *verb, const char *dir);

/* True when path stats. Borrowed path. */
static int path_exists(const char *path);

/* Joins home/name into out. Empty on overflow. */
static void join_path(char *out, size_t outsz, const char *home,
                       const char *name);

/* Materializes the seed fixtures in a fresh persisted store. */
static void insert_seeds(void);

/* Removes the live pair from home. Borrowed home. */
static void wipe_pair(const char *home);

/* Asserts the restored store holds every seed with its content. */
static void check_restored(void);

int main(void)
{
    char home[] = "/tmp/hush-store-backup-XXXXXX";
    char dest[] = "/tmp/hush-store-backup-dest-XXXXXX";
    char path[BACKUP_PATH_LEN];

    if (mkdtemp(home) == NULL)
        return 1;
    if (mkdtemp(dest) == NULL)
        return 1;
    if (setenv("HUSH_HOME", home, 1) != 0)
        return 1;
    unsetenv("HUSH_CONFIG_DIR");

    insert_seeds();
    join_path(path, sizeof(path), home, HUSH_STORE_FILE);
    expect(path_exists(path), "store.ring before backup");
    join_path(path, sizeof(path), home, HUSH_STORE_LOG_FILE);
    expect(path_exists(path), "store.log before backup");

    expect(run_script("backup", dest) == 0, "backup exits 0");
    join_path(path, sizeof(path), dest, HUSH_STORE_FILE);
    expect(path_exists(path), "backup holds store.ring");
    join_path(path, sizeof(path), dest, HUSH_STORE_LOG_FILE);
    expect(path_exists(path), "backup holds store.log");

    wipe_pair(home);
    expect(run_script("restore", dest) == 0, "restore exits 0");
    check_restored();

    if (g_fail)
        return 1;
    printf("test_store_backup ok\n");
    return 0;
}

static void expect(int cond, const char *msg)
{
    assert(msg != NULL);
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

static void fill_note(hush_event_t *ev, const char *id, const char *content)
{
    assert(ev != NULL);
    assert(id != NULL);
    assert(content != NULL);
    memset(ev, 0, sizeof(*ev));
    memcpy(ev->id, id, strlen(id) + 1);
    memcpy(ev->pubkey, k_pub, strlen(k_pub) + 1);
    ev->kind = 1;
    ev->created_at = 1;
    memcpy(ev->content, content, strlen(content) + 1);
}

static int run_script(const char *verb, const char *dir)
{
    char cmd[BACKUP_CMD_LEN];
    int wrote;

    assert(verb != NULL);
    assert(dir != NULL);
    wrote = snprintf(cmd, sizeof(cmd),
                     "sh ../scripts/hush-store-backup %s %s", verb, dir);
    if (wrote <= 0 || (size_t)wrote >= sizeof(cmd))
        return 1;
    return system(cmd);
}

static int path_exists(const char *path)
{
    struct stat st;

    assert(path != NULL);
    return stat(path, &st) == 0;
}

static void join_path(char *out, size_t outsz, const char *home,
                       const char *name)
{
    int wrote;

    assert(out != NULL);
    assert(home != NULL);
    assert(name != NULL);
    wrote = snprintf(out, outsz, "%s/%s", home, name);
    if (wrote <= 0 || (size_t)wrote >= outsz)
        out[0] = '\0';
}

static void seed_content(char *out, size_t outsz, size_t idx)
{
    int wrote;

    assert(out != NULL);
    assert(outsz > 0);
    wrote = snprintf(out, outsz, "backup seed %zu", idx);
    if (wrote <= 0 || (size_t)wrote >= outsz)
        out[0] = '\0';
}

static void insert_seed(hush_store_t *store, size_t idx)
{
    hush_event_t ev;
    char content[BACKUP_CONTENT_LEN];

    assert(store != NULL);
    assert(idx < (size_t)BACKUP_SEED_COUNT);
    seed_content(content, sizeof(content), idx);
    fill_note(&ev, k_ids[idx], content);
    expect(hush_store_insert(store, &ev) == HUSH_OK, "insert seed");
}

static void insert_seeds(void)
{
    hush_store_t *store = NULL;
    size_t i;

    expect(hush_home_ensure() == HUSH_OK, "home");
    expect(hush_store_create(&store) == HUSH_OK, "create");
    if (store == NULL)
        return;
    expect(hush_store_persist_open(store) == HUSH_OK, "persist open");
    for (i = 0; i < (size_t)BACKUP_SEED_COUNT; i++) {
        insert_seed(store, i);
    }
    expect(hush_store_count(store) == (size_t)BACKUP_SEED_COUNT,
           "seed count");
    hush_store_destroy(store);
}

static void wipe_pair(const char *home)
{
    char path[BACKUP_PATH_LEN];

    assert(home != NULL);
    join_path(path, sizeof(path), home, HUSH_STORE_FILE);
    if (path[0] != '\0')
        (void)unlink(path);
    join_path(path, sizeof(path), home, HUSH_STORE_LOG_FILE);
    if (path[0] != '\0')
        (void)unlink(path);
    join_path(path, sizeof(path), home, HUSH_STORE_FILE);
    expect(!path_exists(path), "store.ring wiped");
}

static void check_restored(void)
{
    hush_store_t *store = NULL;
    hush_event_t found;
    size_t i;

    expect(hush_store_create(&store) == HUSH_OK, "reopen create");
    if (store == NULL)
        return;
    expect(hush_store_persist_open(store) == HUSH_OK, "reopen");
    expect(hush_store_count(store) == (size_t)BACKUP_SEED_COUNT,
           "count after restore");
    for (i = 0; i < (size_t)BACKUP_SEED_COUNT; i++) {
        char content[BACKUP_CONTENT_LEN];
        hush_status_t status;

        seed_content(content, sizeof(content), i);
        status = hush_store_find(store, &found, k_ids[i]);
        expect(status == HUSH_OK, "seed id restored");
        if (status != HUSH_OK)
            continue;
        expect(strcmp(found.content, content) == 0, "seed content matches");
    }
    hush_store_destroy(store);
}
