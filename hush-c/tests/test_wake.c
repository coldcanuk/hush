/* tests/test_wake.c: durable claim ledger, lease, device, intro, replay. */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hush_agent.h"
#include "hush_cevent.h"
#include "hush_home.h"
#include "hush_presence.h"
#include "hush_store.h"
#include "hush_wake.h"

static int g_fail;

static const char k_pub[] =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static const char k_root[] =
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
static const char k_trig[] =
    "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
static const char k_trig2[] =
    "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
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

static int trail_has_lease(const hush_store_t *store)
{
    hush_event_t evs[64];
    size_t n;
    size_t i;

    n = hush_store_query(store, NULL, 0, evs, 64);
    for (i = 0; i < n; i++) {
        if (evs[i].kind != (uint32_t)HUSH_PRESENCE_KIND_TRAIL)
            continue;
        if (strcmp(evs[i].content, HUSH_PRESENCE_TRAIL_LEASE) == 0)
            return 1;
    }
    return 0;
}

static void fill_in(hush_wake_in_t *in, hush_store_t *store,
                    const char *trigger, time_t now)
{
    memset(in, 0, sizeof(*in));
    in->store = store;
    in->robot_hex = k_pub;
    in->root_hex = k_root;
    in->trigger_id = trigger;
    in->channel = "general";
    in->now = now;
}

int main(void)
{
    hush_store_t *store = NULL;
    hush_wake_in_t in;
    hush_presence_in_t pin;
    char home[192];
    char json[HUSH_PRESENCE_JSON_MAX];
    char d[HUSH_PRESENCE_D_MAX];
    char d2[HUSH_PRESENCE_D_MAX];
    char ledger[HUSH_HOME_PATH_MAX];
    char device[HUSH_HOME_PATH_MAX];
    unsigned char other[HUSH_WAKE_DEVICE_LEN];
    size_t n = 0;
    time_t t0 = 1000000;
    struct stat st;

    snprintf(home, sizeof(home), "/tmp/hush-wake-test-%d", (int)getpid());
    (void)mkdir(home, 0700);
    if (setenv("HUSH_HOME", home, 1) != 0)
        return 1;
    unsetenv("HUSH_CONFIG_DIR");

    hush_cevent_init();
    hush_presence_init();
    hush_wake_init();
    expect(hush_store_create(&store) == HUSH_OK, "store");

    expect(hush_presence_make_d(d, sizeof(d), k_pub, k_root) == HUSH_OK, "d");
    expect(strcmp(d, k_d) == 0, "formula");

    fill_in(&in, store, k_trig, t0);
    expect(hush_wake_intro_seen(k_pub, k_root) == 0, "no intro yet");
    expect(hush_wake_mark_intro(&in) == HUSH_OK, "mark intro");
    expect(hush_wake_intro_seen(k_pub, k_root) == 1, "intro once");
    expect(hush_wake_mark_intro(&in) == HUSH_OK, "intro idempotent");
    expect(hush_wake_state(k_pub, k_root) == HUSH_WAKE_ST_INTRO, "state intro");
    expect(hush_wake_claim(&in) == HUSH_OK, "claim empty/intro");
    expect(hush_wake_state(k_pub, k_root) == HUSH_WAKE_ST_CLAIMED, "claimed");
    expect(hush_wake_claim(&in) == HUSH_OK, "same device reclaim");
    expect(hush_wake_intro_seen(k_pub, k_root) == 1, "intro still once");

    memset(other, 0xab, sizeof(other));
    hush_wake_test_set_device(other);
    expect(hush_wake_claim(&in) == HUSH_ERR_DENIED, "other device denied");
    hush_wake_init();
    expect(hush_wake_state(k_pub, k_root) == HUSH_WAKE_ST_CLAIMED,
           "ledger survives init");
    expect(hush_presence_make_d(d2, sizeof(d2), k_pub, k_root) == HUSH_OK,
           "d after init");
    expect(strcmp(d, d2) == 0, "same d after restart");
    expect(hush_wake_claim(&in) == HUSH_OK, "same device reclaim after init");
    expect(hush_wake_intro_seen(k_pub, k_root) == 1, "no second intro");

    hush_presence_init();
    memset(&pin, 0, sizeof(pin));
    pin.pubkey = k_pub;
    pin.role = NULL;
    pin.slug = HUSH_PRESENCE_SLUG_WORKING;
    pin.channel = "general";
    pin.root = k_root;
    pin.now = t0;
    expect(hush_presence_publish(store, &pin) == HUSH_OK, "working line");
    hush_presence_init();
    expect(hush_presence_format_json(json, sizeof(json), &n) == HUSH_OK,
           "lines wiped");
    expect(strstr(json, "\"lines\":[]") != NULL, "RAM lines empty");
    fill_in(&in, store, k_trig, t0 + 1);
    expect(hush_wake_claim(&in) == HUSH_OK, "reclaim mid-job");
    expect(hush_presence_make_d(d2, sizeof(d2), k_pub, k_root) == HUSH_OK,
           "reclaim d");
    expect(strcmp(d, d2) == 0, "reclaim same d");
    expect(hush_presence_format_json(json, sizeof(json), &n) == HUSH_OK,
           "no auto publish");
    expect(strstr(json, "Working") == NULL, "no second Working line");

    expect(hush_wake_done(&in) == HUSH_OK, "done");
    expect(hush_presence_clear(store, k_pub, k_root, "general", t0 + 2)
           == HUSH_OK, "clear on done");
    expect(hush_wake_state(k_pub, k_root) == HUSH_WAKE_ST_DONE, "state done");
    expect(hush_wake_claim(&in) == HUSH_ERR_DENIED, "same trigger deny");
    fill_in(&in, store, k_trig2, t0 + 3);
    expect(hush_wake_claim(&in) == HUSH_OK, "new trigger on root");
    expect(hush_wake_done(&in) == HUSH_OK, "done second");

    expect(hush_wake_state(k_pub, k_root) == HUSH_WAKE_ST_DONE,
           "done before agent init");
    hush_agent_init();
    hush_presence_init();
    expect(hush_wake_intro_seen(k_pub, k_root) == 1, "intro across agent init");
    fill_in(&in, store, k_trig, t0 + 4);
    expect(hush_wake_claim(&in) == HUSH_ERR_DENIED, "replay after agent init");
    fill_in(&in, store, k_trig2, t0 + 4);
    expect(hush_wake_claim(&in) == HUSH_ERR_DENIED, "trig2 still done");

    {
        const char *root2 =
            "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
        const char *trig3 =
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
        hush_wake_in_t in2;

        memset(&in2, 0, sizeof(in2));
        in2.store = store;
        in2.robot_hex = k_pub;
        in2.root_hex = root2;
        in2.trigger_id = trig3;
        in2.channel = "general";
        in2.now = t0;
        expect(hush_wake_claim(&in2) == HUSH_OK, "fresh root claim");
        pin.root = root2;
        pin.slug = HUSH_PRESENCE_SLUG_STUCK;
        pin.now = t0;
        expect(hush_presence_publish(store, &pin) == HUSH_OK, "stuck line");
        expect(hush_presence_format_json(json, sizeof(json), &n) == HUSH_OK,
               "stuck json");
        expect(strstr(json, "Stuck") != NULL, "stuck visible");
        hush_wake_expire(store, t0 + HUSH_WAKE_LEASE_S + 1);
        expect(hush_wake_state(k_pub, root2) == HUSH_WAKE_ST_DONE,
               "expired becomes done");
        expect(hush_presence_format_json(json, sizeof(json), &n) == HUSH_OK,
               "after expire json");
        expect(strstr(json, "Stuck") == NULL, "stuck not left");
        expect(strstr(json, "Working") == NULL, "not left working");
        expect(trail_has_lease(store) == 1, "1038 lease-drop");
        expect(hush_wake_claim(&in2) == HUSH_ERR_DENIED, "expired same trig");
        in2.trigger_id =
            "9999999999999999999999999999999999999999999999999999999999999999";
        expect(hush_wake_claim(&in2) == HUSH_OK, "new trig after expiry");
    }

    snprintf(ledger, sizeof(ledger), "%s/agents/%s", home, HUSH_WAKE_FILE);
    snprintf(device, sizeof(device), "%s/agents/%s", home, HUSH_WAKE_DEVICE_FILE);
    expect(stat(ledger, &st) == 0, "ledger file");
    expect(stat(device, &st) == 0, "device file");
    expect(count_kind(store, HUSH_PRESENCE_KIND_LINE) >= 1, "line events");

    hush_store_destroy(store);
    if (g_fail)
        return 1;
    printf("test_wake ok\n");
    return 0;
}
