/* tests/test_roster.c: themes, MIME context, members, agent create. */

#define _XOPEN_SOURCE 700 /* nftw, for the private root cleanup */

#include <assert.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hush_pass.h"
#include "hush_roster.h"
#include "hush_store.h"

enum {
    /* Longest private root path; every path a test derives from it fits. */
    ROOT_PATH_MAX = 96,
    /* Open-fd budget for the nftw cleanup walk. */
    ROOT_FTW_FDS = 16,
    /* Exit status when the root cannot be removed; atexit cannot return one. */
    ROOT_CLEANUP_FAIL_RC = 1
};

/* <TMPDIR or /tmp>/hush-roster-XXXXXX: this run's private root, 0700 from mkdtemp.
 * Every key, pass store, home and config the test writes lives under it. */
#define ROOT_NAME "hush-roster-XXXXXX"
#define ROOT_TMP_DEFAULT "/tmp"

/* The private root and the pid that made it. File scope because an atexit
 * handler takes no argument. Empty until root_make succeeds and again after
 * root_remove. */
static char g_root[ROOT_PATH_MAX];
static pid_t g_root_pid;

/* Makes g_root under root_tmpdir() (/tmp when that is NULL); root_remove,
 * registered with atexit, removes it on every return from main and every
 * exit(). Returns 0, after a FAIL line, when root_holds
 * fails its own check, the path does not fit, mkdtemp fails, atexit
 * refuses, or the root is not under an absolute TMPDIR. */
static int root_make(void);

/* atexit handler: removes g_root and everything under it, in the making
 * process only, so a forked child that exits cannot take the root early.
 * When the walk fails it prints FAIL and ends the process with
 * ROOT_CLEANUP_FAIL_RC, so a leftover root fails the test. */
static void root_remove(void);

/* nftw callback: removes one entry under g_root; FTW_DEPTH visits children
 * first. */
static int root_remove_entry(const char *path, const struct stat *st, int flag,
                             struct FTW *ftw);

/* True when path is root or lies below it: root must match whole path
 * components, so "/r" holds "/r" and "/r/k" but not the sibling "/rX/k". */
static int root_holds(const char *root, const char *path);

/* True when root_tmpdir() is NULL (root_make then uses /tmp), or when
 * g_root lies under it with one trailing slash dropped. A root made anywhere
 * else (a fixed /tmp, say) escapes the run's own cleanup and leak scan.
 * Requires a made root (g_root non-empty). */
static int root_is_under_tmpdir(void);

/* TMPDIR when it is set and absolute; NULL when it is unset, empty, or
 * relative. Borrowed from the environment, never owned or freed. */
static const char *root_tmpdir(void);

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
    char key[HUSH_ROSTER_NAME_MAX];
    char pass_store[128];
    size_t n = 0;

    if (!root_make())
        return 1;
    snprintf(pass_store, sizeof(pass_store), "%s/hush-roster-pass-store", g_root);
    if (setenv("HUSH_FAKE_PASS_DIR", pass_store, 1) != 0)
        return 1;
    hush_pass_set_helper("tests/fake-pass.sh");
    hush_roster_init(&roster);
    expect(strcmp(roster.profile.theme, "field-office") == 0, "default theme");
    expect(hush_roster_is_theme("dracula"), "dracula ok");
    expect(hush_roster_is_theme("color-blind"), "color-blind ok");
    expect(!hush_roster_is_theme("neon"), "neon rejected");
    expect(hush_roster_is_theme("field-office"), "field-office ok");
    expect(!hush_roster_is_theme("feild-office"), "field-office typo rejected");
    expect(hush_roster_is_context_mime("text/plain", "notes.txt"), "plain");
    expect(hush_roster_is_context_mime("text/markdown", "brief.md"), "md mime");
    expect(hush_roster_is_context_mime("", "orders.md"), "md ext");
    expect(!hush_roster_is_context_mime("application/pdf", "x.pdf"), "pdf");
    expect(!hush_roster_is_context_mime("image/png", "x.png"), "png ctx");
    expect(hush_roster_is_provider("goose"), "goose ok");
    expect(hush_roster_is_provider("grok-build"), "grok-build ok");
    expect(hush_roster_is_provider("anthropic-api"), "anthropic ok");
    expect(hush_roster_is_provider("deepseek-api"), "deepseek ok");
    expect(!hush_roster_is_provider("agy"), "removed provider rejected");
    expect(hush_roster_is_provider("codex"), "codex ok");
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
    expect(hush_roster_add_agent(&roster, store, &agent, HUSH_ROSTER_KEY_PASS, NULL) == HUSH_OK,
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
    expect(hush_roster_add_agent(&roster, store, &agent, HUSH_ROSTER_KEY_OFFER, NULL) == HUSH_ERR_PARSE,
           "prompt required");
    memset(&agent, 0, sizeof(agent));
    memcpy(agent.name, "NoProvider", 11);
    memcpy(agent.prompt, "Watch.", 7);
    expect(hush_roster_add_agent(&roster, store, &agent, HUSH_ROSTER_KEY_OFFER, NULL) == HUSH_ERR_PARSE,
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
    expect(hush_roster_add_agent(&roster, store, &agent, HUSH_ROSTER_KEY_OFFER, NULL) == HUSH_ERR_DENIED,
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
    expect(hush_roster_add_agent(&roster, store, &agent, HUSH_ROSTER_KEY_OFFER, NULL) == HUSH_ERR_DENIED,
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
    expect(hush_roster_add_agent(&roster, store, &agent, HUSH_ROSTER_KEY_OFFER, NULL) == HUSH_OK,
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
    /* HTTP reads at most 8 skill_N keys; only C callers reach this refusal. */
    memcpy(agent.name, "Zed", 4);
    agent.nskills = (size_t)HUSH_SKILL_EQUIP_MAX + 1;
    expect(hush_roster_update_agent(&roster, "coach-copy", &agent) ==
               HUSH_ERR_FULL,
           "nine skills refused");
    expect(strcmp(roster.agents[1].name, "Coach copy") == 0,
           "refused skills keep the name");
    /* A name needs a letter or digit: nothing slugs to a stand-in. */
    memset(&agent, 0, sizeof(agent));
    memcpy(agent.name, "!!!", 4);
    memcpy(agent.prompt, "Watch.", 7);
    memcpy(agent.provider, "goose", 6);
    expect(hush_roster_add_agent(&roster, store, &agent, HUSH_ROSTER_KEY_OFFER, NULL) == HUSH_ERR_PARSE,
           "symbol-only name refused");
    expect(roster.nagents == 2, "no robot for a symbol-only name");
    /* Letters outside A-Z do not count yet ("Robot" in Cyrillic, UTF-8). */
    memcpy(agent.name, "\xd0\xa0\xd0\xbe\xd0\xb1\xd0\xbe\xd1\x82", 11);
    expect(hush_roster_add_agent(&roster, store, &agent, HUSH_ROSTER_KEY_OFFER, NULL) == HUSH_ERR_PARSE,
           "Cyrillic-only name refused");
    expect(roster.nagents == 2, "no robot for a Cyrillic-only name");
    memset(&agent, 0, sizeof(agent));
    memcpy(agent.name, "???", 4);
    expect(hush_roster_update_agent(&roster, "coach-copy", &agent) ==
               HUSH_ERR_PARSE,
           "symbol-only rename refused");
    expect(strcmp(roster.agents[1].name, "Coach copy") == 0,
           "refused symbol rename keeps the name");
    /* A symbol-only name saved before the rule may be saved unchanged. */
    memcpy(roster.agents[1].name, "!!!", 4);
    memcpy(agent.name, " !!! ", 6);
    expect(hush_roster_update_agent(&roster, "coach-copy", &agent) == HUSH_OK,
           "older symbol-only name kept");
    memcpy(agent.name, "@@@", 4);
    expect(hush_roster_update_agent(&roster, "coach-copy", &agent) ==
               HUSH_ERR_PARSE,
           "older symbol-only name cannot become other symbols");
    expect(strcmp(roster.agents[1].slug, "coach-copy") == 0,
           "symbol names never move the slug");
    /* Names clash on other robots' current names, never on ids. */
    memset(&agent, 0, sizeof(agent));
    memcpy(agent.name, "Walkbot Two", 12);
    memcpy(agent.prompt, "Watch.", 7);
    memcpy(agent.provider, "goose", 6);
    expect(hush_roster_add_agent(&roster, store, &agent, HUSH_ROSTER_KEY_OFFER, NULL) == HUSH_OK,
           "walkbot two");
    memcpy(agent.name, "Calm Hand", 10);
    expect(hush_roster_update_agent(&roster, "walkbot-two", &agent) == HUSH_OK,
           "rename walkbot-two");
    memcpy(agent.name, "Walkbot Two", 12);
    expect(hush_roster_add_agent(&roster, store, &agent, HUSH_ROSTER_KEY_OFFER, NULL) == HUSH_OK,
           "a renamed robot's old name is free");
    expect(strcmp(roster.agents[3].slug, "walkbot-two-2") == 0,
           "held id gets -2");
    memcpy(agent.name, "walkbot two!", 13);
    expect(hush_roster_add_agent(&roster, store, &agent, HUSH_ROSTER_KEY_OFFER, NULL) == HUSH_ERR_PARSE,
           "same letters and digits refused");
    memcpy(agent.name, "Calm Hand", 10);
    expect(hush_roster_add_agent(&roster, store, &agent, HUSH_ROSTER_KEY_OFFER, NULL) == HUSH_ERR_PARSE,
           "a current name refused");
    memcpy(agent.name, "WALKBOT TWO", 12);
    expect(hush_roster_update_agent(&roster, "walkbot-two", &agent) ==
               HUSH_ERR_PARSE,
           "rename onto another current name refused");
    expect(strcmp(roster.agents[2].name, "Calm Hand") == 0,
           "refused rename keeps the name");
    expect(roster.nagents == 4, "no robot for refused names");
    memset(agent.name, 0, sizeof(agent.name));
    memcpy(agent.name, "Sgt Major Payne", 16);
    expect(hush_roster_add_agent(&roster, store, &agent, HUSH_ROSTER_KEY_OFFER, NULL) == HUSH_OK,
           "payne-like name");
    expect(strcmp(roster.agents[4].slug, "sgt-major-payne-2") == 0,
           "payne's id is never given out");
    /* A 63-character id cut to fit its suffix. */
    memset(agent.name, 'a', (size_t)HUSH_ROSTER_NAME_MAX - 1);
    expect(hush_roster_add_agent(&roster, store, &agent, HUSH_ROSTER_KEY_OFFER, NULL) == HUSH_OK,
           "63-byte name");
    memset(agent.name, 0, sizeof(agent.name));
    memcpy(agent.name, "Short", 6);
    expect(hush_roster_update_agent(&roster, roster.agents[5].slug, &agent) ==
               HUSH_OK,
           "rename the 63-byte robot");
    memset(agent.name, 'a', (size_t)HUSH_ROSTER_NAME_MAX - 1);
    expect(hush_roster_add_agent(&roster, store, &agent, HUSH_ROSTER_KEY_OFFER, NULL) == HUSH_OK,
           "63-byte name again");
    expect(strlen(roster.agents[6].slug) == (size_t)HUSH_ROSTER_NAME_MAX - 1 &&
               strcmp(roster.agents[6].slug + strlen(roster.agents[6].slug) - 2,
                      "-2") == 0,
           "suffixed id fits");
    /* Names shared before the rule may be saved unchanged, not re-taken. */
    memcpy(roster.agents[4].name, "Calm Hand", 10);
    memset(agent.name, 0, sizeof(agent.name));
    memcpy(agent.name, " Calm Hand ", 12);
    expect(hush_roster_update_agent(&roster, "sgt-major-payne-2", &agent) ==
               HUSH_OK,
           "older shared name kept");
    memcpy(agent.name, "Walkbot-Two", 12);
    expect(hush_roster_update_agent(&roster, "sgt-major-payne-2", &agent) ==
               HUSH_ERR_PARSE,
           "older shared name cannot move onto another");
    /* The clash key keeps ASCII letters and digits only, no word breaks;
     * ids still come from the slug. */
    hush_roster_name_key(key, sizeof(key), " W.a-l k\xc3\xa9 1!");
    expect(strcmp(key, "walk1") == 0, "key drops every other byte");
    hush_roster_slug_of(key, sizeof(key), "Walk-bot One");
    expect(strcmp(key, "walk-bot-one") == 0, "slug keeps its word breaks");
    expect(hush_roster_is_name_clash("WalkbotOne", "Walkbot One"),
           "no word break: WalkbotOne");
    expect(hush_roster_is_name_clash("Walk-bot One", "Walkbot One"),
           "no word break: Walk-bot One");
    expect(hush_roster_is_name_clash("W.a.l.k.b.o.t One", "Walkbot One"),
           "no word break: dotted");
    expect(hush_roster_is_name_clash("Walkbot O'ne", "Walkbot One"),
           "no word break: apostrophe");
    expect(hush_roster_is_name_clash("Robot1", "Robot 1"), "Robot1");
    expect(hush_roster_is_name_clash("Ma jor", "Major"), "Ma jor");
    expect(hush_roster_is_name_clash("Walkbot\xc3\xa9One", "WalkbotOne"),
           "non-ASCII letters dropped");
    expect(!hush_roster_is_name_clash("Walkbot One", "Walkbot Two"),
           "other letters differ");
    expect(!hush_roster_is_name_clash("!!!", "@@@"), "empty keys never clash");
    memset(agent.name, 0, sizeof(agent.name));
    memcpy(agent.name, "WalkbotTwo", 11);
    expect(hush_roster_add_agent(&roster, store, &agent, HUSH_ROSTER_KEY_OFFER, NULL) == HUSH_ERR_PARSE,
           "no word break refused on add");
    expect(roster.nagents == 7, "no robot for a key clash");
    /* Rename never compares ids: the name Delta is free while its id is
     * held by a robot now named Echo. */
    memcpy(agent.name, "Delta", 6);
    expect(hush_roster_add_agent(&roster, store, &agent, HUSH_ROSTER_KEY_OFFER, NULL) == HUSH_OK,
           "delta");
    memcpy(agent.name, "Echo", 5);
    expect(hush_roster_update_agent(&roster, "delta", &agent) == HUSH_OK,
           "rename delta to Echo");
    memcpy(agent.name, "Foxtrot", 8);
    expect(hush_roster_add_agent(&roster, store, &agent, HUSH_ROSTER_KEY_OFFER, NULL) == HUSH_OK,
           "foxtrot");
    memset(agent.name, 0, sizeof(agent.name));
    memcpy(agent.name, "Delta", 6);
    expect(hush_roster_update_agent(&roster, "foxtrot", &agent) == HUSH_OK,
           "rename onto a held id's name");
    expect(strcmp(roster.agents[8].name, "Delta") == 0 &&
               strcmp(roster.agents[8].slug, "foxtrot") == 0,
           "renamed foxtrot keeps its id");
    /* A cut that ends on '-' drops it before the suffix: no "a--2". */
    memset(agent.name, 'A', 60);
    memcpy(agent.name + 60, " BC", 3);
    expect(hush_roster_add_agent(&roster, store, &agent, HUSH_ROSTER_KEY_OFFER, NULL) == HUSH_OK,
           "63-byte name ending in a word");
    expect(strlen(roster.agents[9].slug) == 63, "63-byte slug");
    memset(agent.name, 0, sizeof(agent.name));
    memcpy(agent.name, "Holder", 7);
    expect(hush_roster_update_agent(&roster, roster.agents[9].slug, &agent) ==
               HUSH_OK,
           "rename the 63-byte slug robot");
    memset(agent.name, 'A', 60);
    memcpy(agent.name + 60, " BC", 3);
    expect(hush_roster_add_agent(&roster, store, &agent, HUSH_ROSTER_KEY_OFFER, NULL) == HUSH_OK,
           "63-byte name ending in a word again");
    memset(key, 'a', 60);
    memcpy(key + 60, "-2", 3);
    expect(strcmp(roster.agents[10].slug, key) == 0, "cut drops '-' before -2");
    /* A 63-byte cap used to keep a split é (lone 0xC3). Store whole characters. */
    memset(&agent, 0, sizeof(agent));
    memcpy(agent.prompt, "Watch.", 7);
    memcpy(agent.provider, "goose", 6);
    memcpy(agent.name, "Cafe", 4);
    for (n = 0; n < 29; n++) {
        agent.name[4 + n * 2] = (char)0xC3;
        agent.name[5 + n * 2] = (char)0xA9;
    }
    agent.name[4 + 29 * 2] = (char)0xC3;
    expect(hush_roster_add_agent(&roster, store, &agent, HUSH_ROSTER_KEY_OFFER,
                                 NULL) == HUSH_OK,
           "name with a split é is stored");
    {
        const char *stored = roster.agents[roster.nagents - 1].name;
        size_t len = strlen(stored);

        expect(len == 4 + 29 * 2, "split é trailer dropped");
        expect((unsigned char)stored[len - 1] == 0xA9, "stored name ends on é");
    }
    hush_store_destroy(store);
    hush_pass_set_helper(NULL);
    if (g_fail)
        return 1;
    printf("test_roster ok\n");
    return 0;
}

static int root_make(void)
{
    const char *base_dir = root_tmpdir();
    int wrote = 0;

    assert(g_root[0] == '\0');
    /* nftw never hands root_remove_entry a path outside the root, so this
     * direct check is the only place the prefix-sibling case is exercised. */
    if (!root_holds("/r", "/r") || !root_holds("/r", "/r/k") ||
        root_holds("/r", "/rX/k") || root_holds("/r", "/rX")) {
        fprintf(stderr, "FAIL root_holds accepts a prefix sibling\n");
        return 0;
    }
    /* A relative TMPDIR would leave g_root relative, and a test that
     * chdirs would then walk the wrong dir at exit. */
    if (base_dir == NULL)
        base_dir = ROOT_TMP_DEFAULT;
    wrote = snprintf(g_root, sizeof(g_root), "%s/%s", base_dir, ROOT_NAME);
    if (wrote < 0 || (size_t)wrote >= sizeof(g_root) || mkdtemp(g_root) == NULL) {
        g_root[0] = '\0';
        fprintf(stderr, "FAIL private root under %s\n", base_dir);
        return 0;
    }
    g_root_pid = getpid();
    /* Deviation (section 7): atexit takes a bare callback; no table fits. */
    if (atexit(root_remove) != 0) {
        root_remove();
        fprintf(stderr, "FAIL atexit for the private root\n");
        return 0;
    }
    /* atexit is already set, so the root is still removed on this FAIL. */
    if (!root_is_under_tmpdir()) {
        fprintf(stderr, "FAIL private root %s is not under TMPDIR\n", g_root);
        return 0;
    }
    return 1;
}

static void root_remove(void)
{
    if (g_root[0] == '\0' || getpid() != g_root_pid)
        return;
    /* FTW_PHYS never follows a symlink out of the root; FTW_MOUNT never
     * walks into another filesystem mounted under it. Deviation (section 7):
     * nftw takes a bare callback; no table fits. */
    if (nftw(g_root, root_remove_entry, ROOT_FTW_FDS,
             FTW_DEPTH | FTW_PHYS | FTW_MOUNT) != 0) {
        fprintf(stderr, "FAIL private root cleanup: %s\n", g_root);
        (void)fflush(NULL);
        /* exit() may not run inside an atexit handler; _exit may. */
        _exit(ROOT_CLEANUP_FAIL_RC);
    }
    g_root[0] = '\0';
}

static int root_remove_entry(const char *path, const struct stat *st, int flag,
                             struct FTW *ftw)
{
    (void)st;
    (void)flag;
    (void)ftw;
    /* Defense in depth only: FTW_PHYS and FTW_MOUNT keep the walk inside
     * g_root, so this assert cannot fire through nftw. root_make checks
     * root_holds directly instead. */
    assert(root_holds(g_root, path));
    return remove(path);
}

static int root_holds(const char *root, const char *path)
{
    size_t n = 0;

    assert(root != NULL && path != NULL);
    n = strlen(root);
    return n > 0 && strncmp(path, root, n) == 0 &&
           (path[n] == '\0' || path[n] == '/');
}

static int root_is_under_tmpdir(void)
{
    assert(g_root[0] != '\0');
    const char *env = root_tmpdir();
    if (env == NULL)
        return 1;

    size_t n = strlen(env);
    if (n > 1 && env[n - 1] == '/')
        n--;
    /* g_root is shorter than ROOT_PATH_MAX, so no longer TMPDIR can hold it:
     * "not under TMPDIR" is the true answer here, not a second error. */
    if (n >= (size_t)ROOT_PATH_MAX)
        return 0;
    char base[ROOT_PATH_MAX] = "";
    memcpy(base, env, n);
    return root_holds(base, g_root);
}

static const char *root_tmpdir(void)
{
    const char *env = getenv("TMPDIR");
    if (env == NULL || env[0] != '/')
        return NULL;
    return env;
}
