/* tests/test_roster.c: themes, MIME context, members, agent create. */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int main(void)
{
    static hush_roster_t roster;
    static char json[HUSH_ROSTER_JSON_MAX];
    hush_roster_profile_t profile;
    hush_roster_agent_in_t agent;
    hush_store_t *store = NULL;
    size_t n = 0;

    if (setenv("HUSH_FAKE_PASS_DIR", "/tmp/hush-roster-pass-store", 1) != 0)
        return 1;
    hush_pass_set_helper("tests/fake-pass.sh");
    hush_roster_init(&roster);
    expect(strcmp(roster.profile.theme, "dark") == 0, "default theme");
    expect(hush_roster_is_theme("dracula"), "dracula ok");
    expect(hush_roster_is_theme("color-blind"), "color-blind ok");
    expect(!hush_roster_is_theme("neon"), "neon rejected");
    expect(hush_roster_is_context_mime("text/plain", "notes.txt"), "plain");
    expect(hush_roster_is_context_mime("text/markdown", "brief.md"), "md mime");
    expect(hush_roster_is_context_mime("", "orders.md"), "md ext");
    expect(!hush_roster_is_context_mime("application/pdf", "x.pdf"), "pdf");
    expect(!hush_roster_is_context_mime("image/png", "x.png"), "png ctx");
    expect(hush_roster_is_provider("goose"), "goose ok");
    expect(hush_roster_is_provider("grok-build"), "grok-build ok");
    expect(hush_roster_is_provider("anthropic-api"), "anthropic ok");
    expect(hush_roster_is_provider("deepseek-api"), "deepseek ok");
    expect(hush_roster_is_provider("agy"), "agy ok");
    expect(hush_roster_is_provider("copilot"), "copilot ok");
    expect(hush_roster_is_provider("ollama"), "ollama ok");
    expect(hush_roster_is_provider("custom"), "custom ok");
    expect(!hush_roster_is_provider("chatgpt"), "chatgpt rejected");
    memset(&profile, 0, sizeof(profile));
    memcpy(profile.first_name, "Ada", 4);
    memcpy(profile.last_name, "Lovelace", 9);
    memcpy(profile.email, "ada@hive.local", 15);
    memcpy(profile.organization, "HQ", 3);
    memcpy(profile.theme, "desert", 7);
    expect(hush_roster_set_profile(&roster, &profile) == HUSH_OK, "profile");
    expect(strcmp(roster.profile.theme, "desert") == 0, "theme set");
    memcpy(profile.theme, "neon", 5);
    expect(hush_roster_set_profile(&roster, &profile) == HUSH_ERR_PARSE,
           "bad theme");
    expect(hush_store_create(&store) == HUSH_OK, "store");
    expect(hush_roster_add_member(
               &roster,
               "npub10elfcs4fr0l0r8af98jlmgdh9c8tcxjvz9qkw038js35mp4dma8qzvjptg",
               "Alice") == HUSH_OK,
           "member npub");
    expect(roster.nmembers == 1, "one member");
    memset(&agent, 0, sizeof(agent));
    memcpy(agent.name, "Sentry", 7);
    memcpy(agent.prompt, "Watch the perimeter.", 21);
    memcpy(agent.provider, "goose", 6);
    memcpy(agent.context[0].name, "brief.md", 9);
    memcpy(agent.context[0].mime, "text/markdown", 14);
    agent.context[0].text = "# stand to";
    agent.context[0].bytes = 11;
    agent.ncontext = 1;
    expect(hush_roster_add_agent(&roster, store, &agent, 1) == HUSH_OK,
           "agent");
    expect(roster.nagents == 1, "one agent");
    expect(strcmp(roster.agents[0].context[0].text, "# stand to") == 0,
           "context text stored");
    expect(strcmp(roster.agents[0].role, HUSH_ROSTER_ROLE_WORKER) == 0,
           "default worker");
    expect(strncmp(roster.agents[0].id.npub, "npub1", 5) == 0, "agent npub");
    expect(hush_pass_has("agents/sentry/nsec"), "agent nsec in pass");
    memset(&agent, 0, sizeof(agent));
    memcpy(agent.name, "NoPrompt", 9);
    memcpy(agent.provider, "goose", 6);
    expect(hush_roster_add_agent(&roster, store, &agent, 0) == HUSH_ERR_PARSE,
           "prompt required");
    memset(&agent, 0, sizeof(agent));
    memcpy(agent.name, "NoProvider", 11);
    memcpy(agent.prompt, "Watch.", 7);
    expect(hush_roster_add_agent(&roster, store, &agent, 0) == HUSH_ERR_PARSE,
           "provider required");
    memset(&agent, 0, sizeof(agent));
    memcpy(agent.name, "Badfile", 8);
    memcpy(agent.prompt, "Watch.", 7);
    memcpy(agent.provider, "goose", 6);
    memcpy(agent.context[0].name, "x.pdf", 6);
    memcpy(agent.context[0].mime, "application/pdf", 16);
    agent.context[0].text = "%PDF";
    agent.context[0].bytes = 4;
    agent.ncontext = 1;
    expect(hush_roster_add_agent(&roster, store, &agent, 0) == HUSH_ERR_DENIED,
           "pdf denied");
    expect(roster.nagents == 1, "still one agent");
    memset(&agent, 0, sizeof(agent));
    memcpy(agent.name, "TextOnly", 9);
    memcpy(agent.prompt, "Watch.", 7);
    memcpy(agent.provider, "deepseek-api", 13);
    memcpy(agent.context[0].name, "brief.md", 9);
    memcpy(agent.context[0].mime, "text/markdown", 14);
    agent.context[0].text = "# stand to";
    agent.context[0].bytes = 11;
    agent.ncontext = 1;
    expect(hush_roster_add_agent(&roster, store, &agent, 0) == HUSH_ERR_DENIED,
           "text-only provider denies file context");
    expect(roster.nagents == 1, "still one agent after context gate");
    expect(hush_roster_format_json(&roster, json, sizeof(json), &n) == HUSH_OK,
           "json");
    expect(strstr(json, "\"theme\":\"desert\"") != NULL, "theme json");
    expect(strstr(json, "\"first_name\":\"Ada\"") != NULL, "first json");
    expect(strstr(json, "\"slug\":\"sentry\"") != NULL, "sentry json");
    expect(strstr(json, "\"pubkey\":\"") != NULL, "agent pubkey json");
    expect(strstr(json, "\"provider\":\"goose\"") != NULL, "provider json");
    expect(strstr(json, "Alice") != NULL, "alice json");
    expect(strstr(json, "\"skills\":[]") != NULL, "skills json");
    expect(strstr(json, "\"role\":\"worker\"") != NULL, "role json");
    expect(roster.agents[0].intro_enabled == 1, "intro on by default");
    expect(strcmp(roster.agents[0].intro, HUSH_ROSTER_INTRO_DEFAULT) == 0,
           "default intro");
    expect(strstr(json, "\"intro_enabled\":true") != NULL, "intro json");
    memcpy(agent.name, "Sentry Two", 11);
    memcpy(agent.prompt, "Watch closer.", 14);
    memcpy(agent.voice, "alloy", 6);
    memcpy(agent.picture, "panel:dogs:4", 13);
    memcpy(agent.skills[0], "system:forge-skill", 19);
    agent.nskills = 1;
    agent.has_picture = 1;
    agent.has_voice = 1;
    agent.has_skills = 1;
    expect(roster.agents[0].enabled == 1, "default enabled");
    agent.has_enabled = 1;
    agent.enabled = 0;
    expect(hush_roster_update_agent(&roster, "sentry", &agent) == HUSH_OK,
           "update");
    expect(roster.agents[0].enabled == 0, "disabled");
    memcpy(agent.role, HUSH_ROSTER_ROLE_CHAPERON,
           sizeof(HUSH_ROSTER_ROLE_CHAPERON));
    agent.has_role = 1;
    expect(hush_roster_update_agent(&roster, "sentry", &agent) == HUSH_OK,
           "role update");
    expect(strcmp(roster.agents[0].role, HUSH_ROSTER_ROLE_CHAPERON) == 0,
           "chaperon");
    expect(strcmp(roster.agents[0].name, "Sentry Two") == 0, "renamed");
    expect(strcmp(roster.agents[0].voice, "alloy") == 0, "voice");
    expect(roster.agents[0].nskills == 1, "one skill");
    expect(hush_roster_format_json(&roster, json, sizeof(json), &n) == HUSH_OK,
           "json2");
    expect(strstr(json, "system:forge-skill") != NULL, "equipped skill");
    memset(agent.skills, 0, sizeof(agent.skills));
    agent.nskills = 0;
    agent.has_skills = 1;
    expect(hush_roster_update_agent(&roster, "sentry", &agent) == HUSH_OK,
           "prune last skill");
    expect(roster.agents[0].nskills == 0, "empty loadout");
    expect(hush_roster_format_json(&roster, json, sizeof(json), &n) == HUSH_OK,
           "json prune");
    expect(strstr(json, "\"slug\":\"sentry\"") != NULL, "sentry after prune");
    expect(strstr(json, "system:forge-skill") == NULL, "pruned skill gone");
    expect(strstr(json, "\"enabled\":false") != NULL, "disabled json");
    expect(strstr(json, "\"role\":\"chaperon\"") != NULL, "chaperon json");
    memcpy(agent.intro, "Hello from Sentry.", 19);
    agent.has_intro = 1;
    agent.has_intro_enabled = 1;
    agent.intro_enabled = 0;
    expect(hush_roster_update_agent(&roster, "sentry", &agent) == HUSH_OK,
           "intro update");
    expect(roster.agents[0].intro_enabled == 0, "intro off");
    expect(strcmp(roster.agents[0].intro, "Hello from Sentry.") == 0,
           "custom intro");
    expect(hush_roster_format_json(&roster, json, sizeof(json), &n) == HUSH_OK,
           "json intro");
    expect(strstr(json, "\"intro_enabled\":false") != NULL, "intro off json");
    expect(strstr(json, "Hello from Sentry.") != NULL, "custom intro json");
    expect(hush_roster_remove_agent(&roster, HUSH_ROSTER_PAYNE_SLUG) ==
               HUSH_ERR_DENIED,
           "payne stays");
    expect(hush_roster_clone_agent(&roster, store, HUSH_ROSTER_PAYNE_SLUG) ==
               HUSH_ERR_DENIED,
           "payne no clone");
    expect(hush_roster_clone_agent(&roster, store, "sentry") == HUSH_OK,
           "clone sentry");
    expect(roster.nagents == 2, "sentry plus copy");
    expect(strcmp(roster.agents[1].name, "Sentry Two copy") == 0, "copy name");
    expect(roster.agents[1].locked == 0, "copy unlocked");
    expect(hush_roster_remove_agent(&roster, "sentry-two-copy") == HUSH_OK,
           "drop copy");
    expect(hush_roster_remove_agent(&roster, "sentry") == HUSH_OK, "delete");
    expect(roster.nagents == 0, "empty after delete");
    expect(hush_roster_remove_agent(&roster, "sentry") == HUSH_ERR_NOT_FOUND,
           "gone");
    memset(&agent, 0, sizeof(agent));
    memcpy(agent.name, "Coach", 6);
    memcpy(agent.prompt, "Coach hive jobs.", 17);
    memcpy(agent.provider, "goose", 6);
    memcpy(agent.skills[0], "system:canvas-coach", 20);
    agent.nskills = 1;
    agent.locked = 1;
    expect(hush_roster_add_agent(&roster, store, &agent, 0) == HUSH_OK,
           "locked coach");
    expect(roster.agents[0].locked == 1, "coach locked");
    expect(roster.agents[0].nskills == 1, "coach wears skill");
    memcpy(agent.name, "Nope", 5);
    memcpy(agent.prompt, "Nope prompt", 12);
    agent.has_enabled = 1;
    agent.enabled = 0;
    expect(hush_roster_update_agent(&roster, "coach", &agent) == HUSH_OK,
           "locked enable");
    memcpy(agent.intro, "Coach is on deck.", 18);
    agent.has_intro = 1;
    agent.has_intro_enabled = 1;
    agent.intro_enabled = 0;
    expect(hush_roster_update_agent(&roster, "coach", &agent) == HUSH_OK,
           "locked intro");
    expect(roster.agents[0].intro_enabled == 0, "locked intro off");
    expect(strcmp(roster.agents[0].intro, "Coach is on deck.") == 0,
           "locked custom intro");
    expect(strcmp(roster.agents[0].name, "Coach") == 0, "locked name stays");
    expect(roster.agents[0].enabled == 0, "locked disable");
    expect(hush_roster_clone_agent(&roster, store, "coach") == HUSH_OK,
           "clone locked");
    expect(roster.nagents == 2, "coach plus copy");
    expect(roster.agents[1].locked == 0, "clone unlocked");
    expect(roster.agents[1].nskills == 1, "copy wears skill");
    expect(roster.agents[1].intro_enabled == 0, "copy intro off");
    expect(strcmp(roster.agents[1].intro, "Coach is on deck.") == 0,
           "copy intro text");
    memset(&agent, 0, sizeof(agent));
    agent.has_skills = 1;
    agent.nskills = 0;
    expect(hush_roster_update_agent(&roster, "coach-copy", &agent) == HUSH_OK,
           "prune copy 1to0");
    expect(roster.agents[1].nskills == 0, "copy empty loadout");
    expect(roster.agents[0].nskills == 1, "locked original keeps skill");
    hush_store_destroy(store);
    hush_pass_set_helper(NULL);
    if (g_fail)
        return 1;
    printf("test_roster ok\n");
    return 0;
}
