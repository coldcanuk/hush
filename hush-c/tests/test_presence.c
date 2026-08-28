/* tests/test_presence.c: NIP-38 slugs, stable d, replace, expiry, REQ privacy. */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "hush_cevent.h"
#include "hush_presence.h"
#include "hush_roster.h"
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

static size_t count_kind(const hush_store_t *store, uint32_t kind)
{
    hush_event_t evs[64];
    size_t n;
    size_t i;
    size_t c = 0;

    n = hush_store_query(store, NULL, 0, evs, 64);
    for (i = 0; i < n; i++) {
        if (evs[i].kind == kind)
            c++;
    }
    return c;
}

int main(void)
{
    hush_store_t *store = NULL;
    hush_presence_in_t in;
    char json[HUSH_PRESENCE_JSON_MAX];
    char d[HUSH_PRESENCE_D_MAX];
    char d2[HUSH_PRESENCE_D_MAX];
    size_t n = 0;
    time_t t0 = 1000000;

    hush_cevent_init();
    hush_presence_init();
    expect(hush_presence_slug_ok(HUSH_PRESENCE_SLUG_WORKING) == 1, "working");
    expect(hush_presence_slug_ok("Debugging Algorithms") == 1, "debug family");
    expect(hush_presence_slug_ok("Debugging HTML") == 1, "debug html");
    expect(hush_presence_slug_ok("Debugging") == 0, "debug bare");
    expect(hush_presence_slug_ok("napping") == 0, "unknown slug");
    expect(hush_presence_role_ok(NULL) == 1, "null role");
    expect(hush_presence_role_ok(HUSH_ROSTER_ROLE_WORKER) == 1, "worker");
    expect(hush_presence_role_ok(HUSH_ROSTER_ROLE_CHAPERON) == 0, "chaperon");
    expect(hush_presence_req_ok(HUSH_PRESENCE_KIND_LINE, 1) == 1, "public line");
    expect(hush_presence_req_ok(HUSH_PRESENCE_KIND_LINE, 0) == 0, "private line");
    expect(hush_presence_req_ok(HUSH_PRESENCE_KIND_TRAIL, 0) == 0, "private trail");
    expect(hush_presence_req_ok(1, 0) == 1, "kind1 private ok");
    expect(hush_presence_make_d(d, sizeof(d), k_pub, k_root) == HUSH_OK,
           "make d");
    expect(strlen(d) == (size_t)HUSH_PRESENCE_D_LEN, "d len 45");
    expect(strcmp(d, k_d) == 0, "d formula");
    expect(strstr(d, "hive:f") == NULL || strcmp(d, k_d) == 0, "not token d");
    expect(hush_presence_make_d(d2, sizeof(d2), k_pub, k_root) == HUSH_OK,
           "make d twice");
    expect(strcmp(d, d2) == 0, "same robot+root same d");
    hush_presence_init();
    expect(hush_presence_make_d(d2, sizeof(d2), k_pub, k_root) == HUSH_OK,
           "make d after init");
    expect(strcmp(d, d2) == 0, "init does not change d");

    expect(hush_store_create(&store) == HUSH_OK, "store");
    memset(&in, 0, sizeof(in));
    in.pubkey = k_pub;
    in.role = HUSH_ROSTER_ROLE_CHAPERON;
    in.slug = HUSH_PRESENCE_SLUG_WORKING;
    in.channel = "general";
    in.root = k_root;
    in.now = t0;
    expect(hush_presence_publish(store, &in) == HUSH_ERR_DENIED, "chaperon denied");

    in.role = HUSH_ROSTER_ROLE_WORKER;
    expect(hush_presence_publish(store, &in) == HUSH_OK, "publish working");
    expect(count_kind(store, HUSH_PRESENCE_KIND_LINE) == 1, "one line");
    expect(count_kind(store, HUSH_PRESENCE_KIND_TRAIL) == 1, "one trail");
    expect(hush_presence_format_json(json, sizeof(json), &n) == HUSH_OK,
           "json live");
    expect(strstr(json, k_d) != NULL, "stable d in json");
    expect(strstr(json, "hive:f1") == NULL, "fN not in json d");
    expect(hush_presence_req_ok(HUSH_PRESENCE_KIND_LINE, 0) == 0,
           "private hides REQ");
    expect(strstr(json, "Working") != NULL, "private JSON still lists");

    in.slug = HUSH_PRESENCE_SLUG_STUCK;
    in.now = t0 + 5;
    expect(hush_presence_publish(store, &in) == HUSH_OK, "publish stuck");
    expect(count_kind(store, HUSH_PRESENCE_KIND_LINE) == 1, "line replaced");
    expect(count_kind(store, HUSH_PRESENCE_KIND_TRAIL) == 2, "trail appended");
    expect(hush_presence_stuck_due(k_pub, k_root, t0 + 5) == 0,
           "just published");
    expect(hush_presence_stuck_due(k_pub, k_root,
                                   t0 + 5 + HUSH_PRESENCE_HEARTBEAT_S)
           == 1, "stuck due");

    in.slug = HUSH_PRESENCE_SLUG_IDLE;
    in.now = t0 + 10;
    expect(hush_presence_publish(store, &in) == HUSH_OK, "idle");
    expect(hush_presence_stall_s(k_pub, k_root, t0 + 10) == 0, "fresh beat");
    expect(hush_presence_beat(k_pub, k_root, t0 + 20) == HUSH_OK, "beat");
    hush_presence_expire(store, t0 + 20 + HUSH_PRESENCE_IDLE_S);
    expect(hush_presence_format_json(json, sizeof(json), &n) == HUSH_OK, "json");
    expect(strstr(json, "\"ok\":true") != NULL, "json ok");
    expect(strstr(json, "\"lines\":[]") != NULL, "expired gone");

    hush_presence_init();
    in.slug = HUSH_PRESENCE_SLUG_WORKING;
    in.now = t0;
    expect(hush_presence_publish(store, &in) == HUSH_OK, "working again");
    expect(hush_presence_format_json(json, sizeof(json), &n) == HUSH_OK,
           "json live 2");
    expect(strstr(json, "Working") != NULL, "slug in json");
    expect(strstr(json, k_d) != NULL, "same d after republish");

    hush_store_destroy(store);
    if (g_fail)
        return 1;
    printf("test_presence ok\n");
    return 0;
}
