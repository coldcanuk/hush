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
    expect(hush_roster_format_json(&roster, json, sizeof(json), &n) == HUSH_OK,
           "json");
    expect(strstr(json, "\"theme\":\"desert\"") != NULL, "theme json");
    expect(strstr(json, "\"first_name\":\"Ada\"") != NULL, "first json");
    expect(strstr(json, "\"slug\":\"sentry\"") != NULL, "sentry json");
    expect(strstr(json, "\"provider\":\"goose\"") != NULL, "provider json");
    expect(strstr(json, "Alice") != NULL, "alice json");
    expect(hush_roster_remove_agent(&roster, HUSH_ROSTER_PAYNE_SLUG) ==
               HUSH_ERR_DENIED,
           "payne stays");
    expect(hush_roster_remove_agent(&roster, "sentry") == HUSH_OK, "delete");
    expect(roster.nagents == 0, "empty after delete");
    expect(hush_roster_remove_agent(&roster, "sentry") == HUSH_ERR_NOT_FOUND,
           "gone");
    hush_store_destroy(store);
    hush_pass_set_helper(NULL);
    if (g_fail)
        return 1;
    printf("test_roster ok\n");
    return 0;
}
