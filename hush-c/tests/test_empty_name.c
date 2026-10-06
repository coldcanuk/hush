/* tests/test_empty_name.c: #285 an empty first name is not "you". */

#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hush_agent.h"
#include "hush_agent_internal.h"
#include "hush_event.h"
#include "hush_launch.h"
#include "hush_pass.h"
#include "hush_roster.h"
#include "hush_store.h"

#define EMPTY_DIR_TEMPLATE "/tmp/hush-empty-name-XXXXXX"
#define EMPTY_CHANNEL "general"
#define EMPTY_FORBIDDEN "You are speaking to you."
#define EMPTY_NEUTRAL "You are speaking to the owner."
#define EMPTY_NAMED "You are speaking to Ada."
#define EMPTY_RULE_YOU "Speak to you."
#define EMPTY_RULE_OWNER "Speak to the owner."
#define EMPTY_FROM_YOU "from you:"
#define EMPTY_FROM_OWNER "from the owner:"

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

static void fill_note(hush_event_t *ev, const char *pub, const char *content)
{
    memset(ev, 0, sizeof(*ev));
    snprintf(ev->id, sizeof(ev->id), "%0*x", HUSH_EVENT_ID_HEX_LEN, 1);
    hush_agent_copy(ev->pubkey, sizeof(ev->pubkey), pub);
    ev->kind = 1;
    ev->created_at = 1;
    hush_agent_copy(ev->content, sizeof(ev->content), content);
    hush_agent_copy(ev->tags[0][0], sizeof(ev->tags[0][0]), "h");
    hush_agent_copy(ev->tags[0][1], sizeof(ev->tags[0][1]), EMPTY_CHANNEL);
    ev->tag_count = 1;
}

static void fill_prompt(hush_agent_job_t *job, hush_launch_t *launch,
                       hush_store_t *store, const hush_agent_robot_t *bot,
                       const hush_event_t *parent)
{
    hush_agent_job_in_t in = {0};

    in.store = store;
    in.launch = launch;
    in.bot = bot;
    in.parent = parent;
    in.ask = parent->content;
    expect(hush_agent_fill_job(job, &in) == HUSH_OK, "fill job");
}

int main(void)
{
    static hush_launch_t launch;
    static hush_agent_job_t job;
    hush_store_t *store = NULL;
    hush_event_t parent = {0};
    hush_agent_robot_t bot = {0};
    hush_roster_agent_in_t raised = {0};
    const hush_roster_agent_t *agent;
    char dir[256];
    char sub[288];
    char lead[512];
    char who[64];

    hush_agent_copy(dir, sizeof(dir), EMPTY_DIR_TEMPLATE);
    expect(mkdtemp(dir) != NULL, "mkdtemp");
    expect(setenv("HOME", dir, 1) == 0, "home");
    expect(setenv("XDG_CONFIG_HOME", dir, 1) == 0, "xdg");
    snprintf(sub, sizeof(sub), "%s/config", dir);
    expect(setenv("HUSH_CONFIG_DIR", sub, 1) == 0, "cfg");
    snprintf(sub, sizeof(sub), "%s/pass", dir);
    expect(mkdir(sub, S_IRWXU) == 0, "pass dir");
    expect(setenv("HUSH_FAKE_PASS_DIR", sub, 1) == 0, "pass env");
    hush_pass_set_helper("tests/fake-pass.sh");
    hush_agent_init();
    hush_launch_init(&launch);
    expect(hush_store_create(&store) == HUSH_OK, "store");
    expect(hush_launch_create_identity(&launch) == HUSH_OK, "ident");
    expect(hush_launch_ack_backup(&launch, 0) == HUSH_OK, "ack");
    expect(hush_launch_create_vibe(&launch, store, "HQ", "x") == HUSH_OK, "vibe");
    expect(launch.roster.profile.first_name[0] == '\0', "no first name yet");
    hush_agent_copy(raised.name, sizeof(raised.name), "Happy");
    hush_agent_copy(raised.prompt, sizeof(raised.prompt), "Tell short jokes.");
    hush_agent_copy(raised.provider, sizeof(raised.provider),
                    HUSH_ROSTER_PROVIDER_GOOSE);
    expect(hush_launch_add_agent(&launch, store, &raised, 0) == HUSH_OK, "raise");
    agent = &launch.roster.agents[launch.roster.nagents - 1];
    expect(hush_agent_lookup_robot(&bot, &launch, agent->id.npub) == 1, "lookup");
    fill_note(&parent, launch.human.pubkey_hex, "tell me a joke");
    expect(hush_store_insert(store, &parent) == HUSH_OK, "parent");

    hush_agent_human_name(who, sizeof(who), &launch);
    expect(strcmp(who, "the owner") == 0, "fallback name");
    fill_prompt(&job, &launch, store, &bot, &parent);
    expect(strstr(job.prompt, EMPTY_FORBIDDEN) == NULL, "no speaking-to-you");
    expect(strstr(job.prompt, EMPTY_NEUTRAL) != NULL, "speaks to the owner");
    expect(strstr(job.rules, EMPTY_RULE_YOU) == NULL, "rules skip you");
    expect(strstr(job.rules, EMPTY_RULE_OWNER) != NULL, "rules name the owner");
    hush_agent_copy(lead, sizeof(lead), "Base.");
    hush_agent_loop_append_lead(lead, sizeof(lead), who, "hello");
    expect(strstr(lead, EMPTY_FROM_YOU) == NULL, "loop lead skips you");
    expect(strstr(lead, EMPTY_FROM_OWNER) != NULL, "loop lead names the owner");

    hush_agent_copy(launch.roster.profile.first_name,
                    sizeof(launch.roster.profile.first_name), "Ada");
    fill_prompt(&job, &launch, store, &bot, &parent);
    expect(strstr(job.prompt, EMPTY_NAMED) != NULL, "named owner stays named");
    expect(strstr(job.prompt, EMPTY_FORBIDDEN) == NULL, "named prompt skips you");
    if (g_fail)
        return 1;
    printf("test_empty_name ok\n");
    return 0;
}
