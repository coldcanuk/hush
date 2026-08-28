/* tests/test_store.c: bounded ring persist, addressable replace on load. */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hush_home.h"
#include "hush_presence.h"
#include "hush_store.h"

static int g_fail;

static const char k_pub[] =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static const char k_root[] =
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
static const char k_d[] =
    "hive:6173cf9d3267e6b926102c8b149812658d05d834";

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

static void fill_note(hush_event_t *ev, const char *id, uint32_t kind,
                      const char *content)
{
    memset(ev, 0, sizeof(*ev));
    memcpy(ev->id, id, strlen(id) + 1);
    memcpy(ev->pubkey, k_pub, sizeof(k_pub));
    ev->kind = kind;
    ev->created_at = 1;
    memcpy(ev->content, content, strlen(content) + 1);
}

int main(void)
{
    hush_store_t *store = NULL;
    hush_event_t ev;
    hush_event_t got;
    char home[192];
    char path[HUSH_HOME_PATH_MAX];
    char d[HUSH_PRESENCE_D_MAX];
    struct stat st;
    size_t i;

    snprintf(home, sizeof(home), "/tmp/hush-store-test-%d", (int)getpid());
    (void)mkdir(home, 0700);
    if (setenv("HUSH_HOME", home, 1) != 0)
        return 1;
    unsetenv("HUSH_CONFIG_DIR");
    expect(hush_home_ensure() == HUSH_OK, "home");
    expect(hush_store_create(&store) == HUSH_OK, "create");
    expect(hush_store_persist_open(store) == HUSH_OK, "persist open");
    expect(hush_store_count(store) == 0, "empty");

    fill_note(&ev, "1111111111111111111111111111111111111111111111111111111111111111",
              1, "hello");
    expect(hush_store_insert(store, &ev) == HUSH_OK, "kind1");
    expect(hush_presence_make_d(d, sizeof(d), k_pub, k_root) == HUSH_OK, "d");
    expect(strcmp(d, k_d) == 0, "stable d");
    fill_note(&ev, "2222222222222222222222222222222222222222222222222222222222222222",
              (uint32_t)HUSH_PRESENCE_KIND_LINE, "Working");
    ev.tag_count = 1;
    memcpy(ev.tags[0][0], "d", 2);
    memcpy(ev.tags[0][1], d, strlen(d) + 1);
    expect(hush_store_insert(store, &ev) == HUSH_OK, "30315");
    fill_note(&ev, "3333333333333333333333333333333333333333333333333333333333333333",
              (uint32_t)HUSH_PRESENCE_KIND_LINE, "Stuck");
    ev.tag_count = 1;
    memcpy(ev.tags[0][0], "d", 2);
    memcpy(ev.tags[0][1], d, strlen(d) + 1);
    expect(hush_store_insert(store, &ev) == HUSH_OK, "30315 replace");
    expect(hush_store_count(store) == 2, "replaced not appended");

    snprintf(path, sizeof(path), "%s/%s", home, HUSH_STORE_FILE);
    expect(stat(path, &st) == 0, "store.ring exists");
    hush_store_destroy(store);
    store = NULL;
    expect(hush_store_create(&store) == HUSH_OK, "create 2");
    expect(hush_store_persist_open(store) == HUSH_OK, "reload");
    expect(hush_store_count(store) == 2, "count after load");
    expect(hush_store_get(store, 1, &got) == HUSH_OK, "get 30315");
    expect(got.kind == (uint32_t)HUSH_PRESENCE_KIND_LINE, "kind line");
    expect(strcmp(got.content, "Stuck") == 0, "replace survived load");
    expect(strcmp(got.tags[0][1], k_d) == 0, "d not fN");
    expect(strstr(got.tags[0][1], "hive:f") == NULL ||
           strcmp(got.tags[0][1], k_d) == 0, "no token d");

    hush_store_destroy(store);
    store = NULL;
    expect(hush_store_create(&store) == HUSH_OK, "create 3");
    expect(hush_store_persist_open(store) == HUSH_OK, "open 3");
    for (i = 0; i < (size_t)HUSH_STORE_CAPACITY + 2; i++) {
        char id[HUSH_EVENT_ID_HEX_LEN + 1];
        size_t k;

        memset(id, '0', (size_t)HUSH_EVENT_ID_HEX_LEN);
        id[HUSH_EVENT_ID_HEX_LEN] = '\0';
        for (k = 0; k < 8; k++)
            id[HUSH_EVENT_ID_HEX_LEN - 1 - k] = "0123456789abcdef"
                [(i >> (4 * k)) & 15u];
        fill_note(&ev, id, 1, "n");
        expect(hush_store_insert(store, &ev) == HUSH_OK, "fill");
    }
    expect(hush_store_count(store) == (size_t)HUSH_STORE_CAPACITY, "cap");
    hush_store_destroy(store);
    store = NULL;
    expect(hush_store_create(&store) == HUSH_OK, "create 4");
    expect(hush_store_persist_open(store) == HUSH_OK, "reload cap");
    expect(hush_store_count(store) == (size_t)HUSH_STORE_CAPACITY,
           "cap after load");

    hush_store_destroy(store);
    if (g_fail)
        return 1;
    printf("test_store ok\n");
    return 0;
}
