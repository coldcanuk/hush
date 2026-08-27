/* tests/test_intel.c: leash, confirm-first, hop-0, no-mention silence. */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hush_agent.h"
#include "hush_event.h"
#include "hush_intel.h"
#include "hush_launch.h"
#include "hush_pass.h"
#include "hush_roster.h"
#include "hush_store.h"

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

static void fill_note(hush_event_t *ev, const char *id, const char *pub,
                      const char *content, const char *channel,
                      const char *mention)
{
    memset(ev, 0, sizeof(*ev));
    memcpy(ev->id, id, strlen(id) + 1);
    memcpy(ev->pubkey, pub, strlen(pub) + 1);
    ev->kind = 1;
    ev->created_at = 1;
    memcpy(ev->content, content, strlen(content) + 1);
    ev->tag_count = 1;
    memcpy(ev->tags[0][0], "h", 2);
    memcpy(ev->tags[0][1], channel, strlen(channel) + 1);
    if (mention != NULL && mention[0] != '\0') {
        memcpy(ev->tags[1][0], "p", 2);
        memcpy(ev->tags[1][1], mention, strlen(mention) + 1);
        ev->tag_count = 2;
    }
}

static void fill_note_pair(hush_event_t *ev, const char *id, const char *pub,
                           const char *content, const char *channel,
                           const char *one, const char *two)
{
    fill_note(ev, id, pub, content, channel, one);
    if (two == NULL || two[0] == '\0')
        return;
    memcpy(ev->tags[2][0], "p", 2);
    memcpy(ev->tags[2][1], two, strlen(two) + 1);
    ev->tag_count = 3;
}

static size_t count_pub_needle(hush_store_t *store, const char *pub,
                               const char *needle)
{
    hush_event_t evs[32];
    size_t n;
    size_t i;
    size_t hits = 0;

    n = hush_store_query(store, NULL, 0, evs, 32);
    for (i = 0; i < n; i++) {
        if (pub != NULL && strcmp(evs[i].pubkey, pub) != 0)
            continue;
        if (strstr(evs[i].content, needle) != NULL)
            hits++;
    }
    return hits;
}

static size_t count_store(hush_store_t *store)
{
    hush_event_t evs[32];

    return hush_store_query(store, NULL, 0, evs, 32);
}

static int store_has(hush_store_t *store, const char *needle)
{
    hush_event_t evs[32];
    size_t n;
    size_t i;

    n = hush_store_query(store, NULL, 0, evs, 32);
    for (i = 0; i < n; i++) {
        if (strstr(evs[i].content, needle) != NULL)
            return 1;
    }
    return 0;
}

int main(void)
{
    static hush_launch_t launch;
    hush_store_t *store = NULL;
    hush_event_t ev;
    char cfg[128];

    snprintf(cfg, sizeof(cfg), "/tmp/hush-intel-cfg-%d", (int)getpid());
    if (setenv("HUSH_CONFIG_DIR", cfg, 1) != 0)
        return 1;
    if (setenv("HUSH_FAKE_PASS_DIR", "/tmp/hush-intel-pass", 1) != 0)
        return 1;
    hush_pass_set_helper("tests/fake-pass.sh");
    hush_intel_init();
    hush_agent_init();
    hush_launch_init(&launch);
    expect(hush_store_create(&store) == HUSH_OK, "store");
    expect(hush_launch_create_identity(&launch) == HUSH_OK, "ident");
    expect(hush_launch_ack_backup(&launch, 0) == HUSH_OK, "ack");
    expect(hush_launch_create_vibe(&launch, store, "HQ", "x") == HUSH_OK,
           "vibe");

    {
        size_t before = count_store(store);

        fill_note(&ev, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                  launch.human.pubkey_hex, "hello room", "general", NULL);
        expect(hush_store_insert(store, &ev) == HUSH_OK, "plain insert");
        hush_intel_consider(store, &launch, &ev);
        expect(count_store(store) == before + 1, "no mention stays silent");
    }

    {
        hush_launch_policy_t policy;

        memset(&policy, 0, sizeof(policy));
        memcpy(policy.kind, HUSH_LAUNCH_KIND_OPEN,
               sizeof(HUSH_LAUNCH_KIND_OPEN));
        memcpy(policy.robot_reply, HUSH_LAUNCH_REPLY_OFF,
               sizeof(HUSH_LAUNCH_REPLY_OFF));
        policy.burst_ms = HUSH_LAUNCH_BURST_MS_DEFAULT;
        policy.max_jobs = HUSH_LAUNCH_MAX_JOBS_DEFAULT;
        policy.cooldown_s = HUSH_LAUNCH_COOLDOWN_S_DEFAULT;
        expect(hush_launch_set_channel_policy(&launch, "general", &policy)
                   == HUSH_OK,
               "off policy");
    }
    fill_note(&ev, "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
              launch.human.pubkey_hex, "nostr:payne please help", "general",
              launch.payne.pubkey_hex);
    expect(hush_store_insert(store, &ev) == HUSH_OK, "off insert");
    hush_intel_consider(store, &launch, &ev);
    expect(store_has(store, "humans talking"), "off posts deny");

    {
        hush_launch_policy_t policy;

        memset(&policy, 0, sizeof(policy));
        memcpy(policy.kind, HUSH_LAUNCH_KIND_OPEN,
               sizeof(HUSH_LAUNCH_KIND_OPEN));
        memcpy(policy.robot_reply, HUSH_LAUNCH_REPLY_CONFIRM,
               sizeof(HUSH_LAUNCH_REPLY_CONFIRM));
        policy.burst_ms = HUSH_LAUNCH_BURST_MS_DEFAULT;
        policy.max_jobs = HUSH_LAUNCH_MAX_JOBS_DEFAULT;
        policy.cooldown_s = HUSH_LAUNCH_COOLDOWN_S_DEFAULT;
        expect(hush_launch_set_channel_policy(&launch, "general", &policy)
                   == HUSH_OK,
               "confirm policy");
    }
    fill_note(&ev, "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
              launch.human.pubkey_hex, "nostr:payne tell a joke", "general",
              launch.payne.pubkey_hex);
    expect(hush_store_insert(store, &ev) == HUSH_OK, "confirm insert");
    hush_intel_consider(store, &launch, &ev);
    expect(store_has(store, "I heard:"), "confirm recaps");

    {
        hush_launch_policy_t policy;

        memset(&policy, 0, sizeof(policy));
        memcpy(policy.kind, HUSH_LAUNCH_KIND_OPEN,
               sizeof(HUSH_LAUNCH_KIND_OPEN));
        memcpy(policy.robot_reply, HUSH_LAUNCH_REPLY_MENTION,
               sizeof(HUSH_LAUNCH_REPLY_MENTION));
        policy.burst_ms = HUSH_LAUNCH_BURST_MS_DEFAULT;
        policy.max_jobs = HUSH_LAUNCH_MAX_JOBS_DEFAULT;
        policy.cooldown_s = HUSH_LAUNCH_COOLDOWN_S_DEFAULT;
        policy.robot_hops = 0;
        expect(hush_launch_set_channel_policy(&launch, "general", &policy)
                   == HUSH_OK,
               "hop policy");
    }
    fill_note(&ev, "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd",
              launch.payne.pubkey_hex, "nostr:payne ping yourself", "general",
              launch.payne.pubkey_hex);
    expect(hush_store_insert(store, &ev) == HUSH_OK, "hop insert");
    hush_intel_consider(store, &launch, &ev);
    expect(store_has(store, "do not chain"), "hop-0 denies robot author");

    {
        hush_roster_agent_in_t in;
        char content[512];
        const char *happy_npub;
        const char *happy_hex;
        const char *payne_npub;
        const char *payne_hex;
        hush_event_t joke;

        memset(&in, 0, sizeof(in));
        memcpy(in.name, "Happy", 6);
        memcpy(in.prompt, "Tell short jokes.", 18);
        memcpy(in.provider, HUSH_ROSTER_PROVIDER_GOOSE,
               sizeof(HUSH_ROSTER_PROVIDER_GOOSE));
        expect(hush_launch_add_agent(&launch, store, &in, 0) == HUSH_OK,
               "raise happy");
        expect(launch.roster.nagents >= 1, "happy on roster");
        happy_npub = launch.roster.agents[0].id.npub;
        happy_hex = launch.roster.agents[0].id.pubkey_hex;
        payne_npub = launch.payne.npub;
        payne_hex = launch.payne.pubkey_hex;
        expect(snprintf(content, sizeof(content),
                        "nostr:%s tell a joke nostr:%s analyze the joke",
                        happy_npub, payne_npub) < (int)sizeof(content),
               "ask fits");
        fill_note_pair(&ev,
                       "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee",
                       launch.human.pubkey_hex, content, "general",
                       happy_npub, payne_npub);
        expect(hush_store_insert(store, &ev) == HUSH_OK, "pair insert");
        hush_intel_consider(store, &launch, &ev);
        expect(!store_has(store, "Holding."), "no jobs-held chat");
        expect(count_pub_needle(store, happy_hex, "Standing orders") == 1,
               "happy intro once");
        expect(count_pub_needle(store, payne_hex, "Standing orders") == 0,
               "major waits");
        fill_note(&joke,
                  "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
                  happy_hex, "Why did the robot laugh? Byte me.", "general",
                  NULL);
        memcpy(joke.tags[1][0], "e", 2);
        memcpy(joke.tags[1][1], ev.id, strlen(ev.id) + 1);
        joke.tag_count = 2;
        expect(hush_store_insert(store, &joke) == HUSH_OK, "joke insert");
        hush_agent_on_posted(store, &launch, &joke);
        expect(count_pub_needle(store, happy_hex, "Standing orders") == 1,
               "happy intro stays one");
        expect(count_pub_needle(store, payne_hex, "Standing orders") == 1,
               "major intro after joke");
        hush_agent_on_posted(store, &launch, &joke);
        expect(count_pub_needle(store, payne_hex, "Standing orders") == 1,
               "major intro not twice");
    }

    hush_store_destroy(store);
    if (g_fail)
        return 1;
    printf("test_intel ok\n");
    return 0;
}
