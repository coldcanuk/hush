/* tests/test_chan_rails.c: chaperon-only vs Major dual-role, turn cap. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hush_agent.h"
#include "hush_cevent.h"
#include "hush_event.h"
#include "hush_intel.h"
#include "hush_launch.h"
#include "hush_pass.h"
#include "hush_roster.h"
#include "hush_store.h"

enum {
    HUSH_RAILS_EV_MAX = 48,
    HUSH_RAILS_STATUS_MAX = 256,
    HUSH_RAILS_JSON_MAX = HUSH_CEVENT_JSON_MAX
};

#define HUSH_RAILS_ID_REF \
    "1111111111111111111111111111111111111111111111111111111111111111"
#define HUSH_RAILS_ID_MAJ \
    "2222222222222222222222222222222222222222222222222222222222222222"
#define HUSH_RAILS_ID_HUM \
    "3333333333333333333333333333333333333333333333333333333333333333"
#define HUSH_RAILS_ID_JOKE \
    "4444444444444444444444444444444444444444444444444444444444444444"

#define HUSH_RAILS_INTRO "Standing orders"
#define HUSH_RAILS_NUDGE "That's enough robot talk. Standing by for the human."
#define HUSH_RAILS_CHAN_YARD "yard"

static int g_fail;

static void expect(int cond, const char *msg);
static void hush_rails_fill_note(hush_event_t *ev, const char *id,
                                 const char *pub, const char *content,
                                 const char *channel, const char *mention);
static void hush_rails_set_parent(hush_event_t *ev, const char *parent_id);
static size_t hush_rails_count(hush_store_t *store, const char *pub,
                               const char *needle);
static int hush_rails_has(hush_store_t *store, const char *needle);
static int hush_rails_jobs_idle(void);
static hush_status_t hush_rails_raise(hush_launch_t *launch, hush_store_t *store,
                                      const char *name, const char *prompt,
                                      const char *provider, const char *role);
static hush_status_t hush_rails_set_policy(hush_launch_t *launch,
                                           const char *slug,
                                           const hush_launch_policy_t *in);
static const hush_roster_agent_t *hush_rails_agent(const hush_launch_t *launch,
                                                   const char *slug);
static void hush_rails_bind_yard(hush_launch_t *launch);
static void hush_rails_test_chaperon(hush_launch_t *launch, hush_store_t *store);
static void hush_rails_test_major_dual(hush_launch_t *launch,
                                       hush_store_t *store);
static void hush_rails_test_turns(hush_launch_t *launch, hush_store_t *store);

int main(void)
{
    static hush_launch_t launch;
    hush_store_t *store = NULL;
    char cfg[128];
    char home[128];

    snprintf(cfg, sizeof(cfg), "/tmp/hush-rails-cfg-%d", (int)getpid());
    snprintf(home, sizeof(home), "/tmp/hush-rails-home-%d", (int)getpid());
    (void)mkdir(home, 0700);
    if (setenv("HUSH_CONFIG_DIR", cfg, 1) != 0)
        return 1;
    /* Isolate HOME so grok/provider detection can't reach a real ~/.grok and
     * spawn a live grok binary during this unit test. */
    if (setenv("HOME", home, 1) != 0)
        return 1;
    if (setenv("HUSH_FAKE_PASS_DIR", "/tmp/hush-rails-pass", 1) != 0)
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
    expect(hush_rails_raise(&launch, store, "Referee", "Babysit the yard.",
                            HUSH_ROSTER_PROVIDER_GROK_BUILD,
                            HUSH_ROSTER_ROLE_CHAPERON) == HUSH_OK,
           "raise referee");
    expect(hush_rails_raise(&launch, store, "Happy", "Tell short jokes.",
                            HUSH_ROSTER_PROVIDER_GOOSE,
                            HUSH_ROSTER_ROLE_WORKER) == HUSH_OK,
           "raise happy");
    hush_rails_test_chaperon(&launch, store);
    hush_rails_test_major_dual(&launch, store);
    hush_rails_test_turns(&launch, store);
    hush_store_destroy(store);
    if (g_fail)
        return 1;
    printf("test_chan_rails ok\n");
    return 0;
}

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

static void hush_rails_fill_note(hush_event_t *ev, const char *id,
                                 const char *pub, const char *content,
                                 const char *channel, const char *mention)
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
    if (mention == NULL || mention[0] == '\0')
        return;
    memcpy(ev->tags[1][0], "p", 2);
    memcpy(ev->tags[1][1], mention, strlen(mention) + 1);
    ev->tag_count = 2;
}

static void hush_rails_set_parent(hush_event_t *ev, const char *parent_id)
{
    assert(ev != NULL);
    assert(parent_id != NULL);
    memcpy(ev->tags[ev->tag_count][0], "e", 2);
    memcpy(ev->tags[ev->tag_count][1], parent_id, strlen(parent_id) + 1);
    ev->tag_count++;
}

static size_t hush_rails_count(hush_store_t *store, const char *pub,
                               const char *needle)
{
    hush_event_t evs[HUSH_RAILS_EV_MAX];
    size_t n;
    size_t i;
    size_t hits = 0;

    n = hush_store_query(store, NULL, 0, evs, HUSH_RAILS_EV_MAX);
    for (i = 0; i < n; i++) {
        if (pub != NULL && strcmp(evs[i].pubkey, pub) != 0)
            continue;
        if (strstr(evs[i].content, needle) != NULL)
            hits++;
    }
    return hits;
}

static int hush_rails_has(hush_store_t *store, const char *needle)
{
    return hush_rails_count(store, NULL, needle) > 0;
}

static int hush_rails_jobs_idle(void)
{
    char status[HUSH_RAILS_STATUS_MAX];

    hush_agent_status(status, sizeof(status));
    return strcmp(status, "[]") == 0;
}

static hush_status_t hush_rails_raise(hush_launch_t *launch, hush_store_t *store,
                                      const char *name, const char *prompt,
                                      const char *provider, const char *role)
{
    hush_roster_agent_in_t in;

    memset(&in, 0, sizeof(in));
    memcpy(in.name, name, strlen(name) + 1);
    memcpy(in.prompt, prompt, strlen(prompt) + 1);
    memcpy(in.provider, provider, strlen(provider) + 1);
    memcpy(in.role, role, strlen(role) + 1);
    in.has_role = 1;
    return hush_launch_add_agent(launch, store, &in, 0);
}

static hush_status_t hush_rails_set_policy(hush_launch_t *launch,
                                           const char *slug,
                                           const hush_launch_policy_t *in)
{
    return hush_launch_set_channel_policy(launch, slug, in);
}

static const hush_roster_agent_t *hush_rails_agent(const hush_launch_t *launch,
                                                   const char *slug)
{
    size_t i;

    for (i = 0; i < launch->roster.nagents; i++) {
        if (strcmp(launch->roster.agents[i].slug, slug) == 0)
            return &launch->roster.agents[i];
    }
    return NULL;
}

static void hush_rails_test_chaperon(hush_launch_t *launch, hush_store_t *store)
{
    const hush_roster_agent_t *ref;
    hush_event_t ev;
    char ask[256];

    ref = hush_rails_agent(launch, "referee");
    expect(ref != NULL, "referee on roster");
    expect(strcmp(ref->role, HUSH_ROSTER_ROLE_CHAPERON) == 0, "referee role");
    expect(snprintf(ask, sizeof(ask), "nostr:%s watch the room", ref->id.npub)
           < (int)sizeof(ask),
           "chaperon ask fits");
    hush_rails_fill_note(&ev, HUSH_RAILS_ID_REF, launch->human.pubkey_hex, ask,
                         "general", ref->id.npub);
    expect(hush_store_insert(store, &ev) == HUSH_OK, "chaperon insert");
    hush_intel_consider(store, launch, &ev);
    hush_agent_mention(store, launch, &ev, ref->id.npub);
    expect(hush_rails_count(store, ref->id.pubkey_hex, HUSH_RAILS_INTRO) == 0,
           "chaperon does not intro");
    expect(hush_rails_jobs_idle(), "chaperon starts no grok job");
}

static void hush_rails_test_major_dual(hush_launch_t *launch, hush_store_t *store)
{
    hush_launch_policy_t policy;
    hush_event_t ev;
    char ask[256];

    memset(&policy, 0, sizeof(policy));
    memcpy(policy.kind, HUSH_LAUNCH_KIND_OPEN, sizeof(HUSH_LAUNCH_KIND_OPEN));
    memcpy(policy.robot_reply, HUSH_LAUNCH_REPLY_MENTION,
           sizeof(HUSH_LAUNCH_REPLY_MENTION));
    policy.burst_ms = HUSH_LAUNCH_BURST_MS_DEFAULT;
    policy.max_jobs = HUSH_LAUNCH_MAX_JOBS_DEFAULT;
    policy.cooldown_s = HUSH_LAUNCH_COOLDOWN_S_DEFAULT;
    policy.max_robot_turns = HUSH_LAUNCH_TURNS_DEFAULT;
    memcpy(policy.chaperon, HUSH_LAUNCH_PAYNE_SLUG,
           sizeof(HUSH_LAUNCH_PAYNE_SLUG));
    expect(hush_rails_set_policy(launch, "general", &policy) == HUSH_OK,
           "major is chaperon");
    expect(strcmp(launch->channels[0].chaperon, HUSH_LAUNCH_PAYNE_SLUG) == 0,
           "chaperon field");
    expect(snprintf(ask, sizeof(ask), "nostr:%s analyze this room",
                    launch->payne.npub) < (int)sizeof(ask),
           "major ask fits");
    hush_rails_fill_note(&ev, HUSH_RAILS_ID_MAJ, launch->human.pubkey_hex, ask,
                         "general", launch->payne.npub);
    expect(hush_store_insert(store, &ev) == HUSH_OK, "major insert");
    hush_intel_consider(store, launch, &ev);
    expect(hush_rails_count(store, launch->payne.pubkey_hex, HUSH_RAILS_INTRO)
               == 1,
           "major chaperon still works");
}

static void hush_rails_bind_yard(hush_launch_t *launch)
{
    hush_launch_policy_t policy;
    const char *robots[2];

    expect(hush_launch_add_channel(launch, "Yard") == HUSH_OK, "add yard");
    memset(&policy, 0, sizeof(policy));
    memcpy(policy.kind, HUSH_LAUNCH_KIND_ROBOTS, sizeof(HUSH_LAUNCH_KIND_ROBOTS));
    memcpy(policy.robot_reply, HUSH_LAUNCH_REPLY_MENTION,
           sizeof(HUSH_LAUNCH_REPLY_MENTION));
    policy.burst_ms = HUSH_LAUNCH_BURST_MS_DEFAULT;
    policy.max_jobs = HUSH_LAUNCH_MAX_JOBS_DEFAULT;
    policy.cooldown_s = HUSH_LAUNCH_COOLDOWN_S_DEFAULT;
    policy.robot_talk = 1;
    policy.robot_hops = 1;
    policy.max_robot_turns = HUSH_LAUNCH_TURNS_MIN;
    memcpy(policy.chaperon, HUSH_LAUNCH_PAYNE_SLUG,
           sizeof(HUSH_LAUNCH_PAYNE_SLUG));
    expect(hush_rails_set_policy(launch, HUSH_RAILS_CHAN_YARD, &policy)
               == HUSH_OK,
           "yard rails");
    robots[0] = "happy";
    robots[1] = HUSH_LAUNCH_PAYNE_SLUG;
    expect(hush_launch_set_channel_roster(launch, HUSH_RAILS_CHAN_YARD, NULL, 0,
                                          robots, 2) == HUSH_OK,
           "yard roster");
}

static void hush_rails_test_turns(hush_launch_t *launch, hush_store_t *store)
{
    const hush_roster_agent_t *happy;
    hush_event_t human;
    hush_event_t joke;
    char ask[256];
    char json[HUSH_RAILS_JSON_MAX];
    size_t n = 0;
    size_t major_before;

    happy = hush_rails_agent(launch, "happy");
    expect(happy != NULL, "happy on roster");
    hush_rails_bind_yard(launch);
    major_before = hush_rails_count(store, launch->payne.pubkey_hex,
                                    HUSH_RAILS_INTRO);
    expect(snprintf(ask, sizeof(ask), "nostr:%s tell a joke", happy->id.npub)
           < (int)sizeof(ask),
           "yard ask fits");
    hush_rails_fill_note(&human, HUSH_RAILS_ID_HUM, launch->human.pubkey_hex,
                         ask, HUSH_RAILS_CHAN_YARD, happy->id.npub);
    expect(hush_store_insert(store, &human) == HUSH_OK, "yard human");
    hush_intel_consider(store, launch, &human);
    expect(hush_rails_count(store, happy->id.pubkey_hex, HUSH_RAILS_INTRO) == 1,
           "happy intros on yard");
    hush_rails_fill_note(&joke, HUSH_RAILS_ID_JOKE, happy->id.pubkey_hex,
                         "Why did the robot laugh? Byte me.",
                         HUSH_RAILS_CHAN_YARD, launch->payne.npub);
    hush_rails_set_parent(&joke, human.id);
    expect(hush_store_insert(store, &joke) == HUSH_OK, "yard joke");
    hush_intel_consider(store, launch, &joke);
    hush_agent_on_posted(store, launch, &joke);
    expect(hush_rails_has(store, HUSH_RAILS_NUDGE), "canned chaperon line");
    expect(hush_rails_count(store, launch->payne.pubkey_hex, HUSH_RAILS_INTRO)
               == major_before,
           "turn cap blocks next grok/intro");
    expect(hush_rails_jobs_idle(), "no extra grok after cap");
    expect(hush_cevent_format_json(json, sizeof(json), &n) == HUSH_OK,
           "cevent json");
    expect(strstr(json, "\"type\":\"chaperon\"") != NULL, "cevent chaperon type");
    expect(strstr(json, "\"seq\":") != NULL, "cevent seq");
    expect(strstr(json, "\"due\":") != NULL, "cevent due");
}
