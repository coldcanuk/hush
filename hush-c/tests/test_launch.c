/* tests/test_launch.c: first-launch identity → vibe → channel → project. */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hush_launch.h"
#include "hush_pass.h"
#include "hush_store.h"

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

/* Pubkey hex for slug, or "" when the roster has no such robot. */
static const char *agent_pub(const hush_launch_t *launch, const char *slug)
{
    size_t i;

    if (launch == NULL || slug == NULL)
        return "";
    for (i = 0; i < launch->roster.nagents; i++) {
        if (strcmp(launch->roster.agents[i].slug, slug) == 0)
            return launch->roster.agents[i].id.pubkey_hex;
    }
    return "";
}

/* In-memory nsec for slug, or "" when the roster has no such robot. */
static const char *agent_nsec(const hush_launch_t *launch, const char *slug)
{
    size_t i;

    if (launch == NULL || slug == NULL)
        return "";
    for (i = 0; i < launch->roster.nagents; i++) {
        if (strcmp(launch->roster.agents[i].slug, slug) == 0)
            return launch->roster.agents[i].id.nsec;
    }
    return "";
}

/* Reads agents/<slug>/nsec. 0 when missing or empty. */
static int read_nsec_file(char *out, size_t outsz, const char *home,
                          const char *slug)
{
    char path[256];
    FILE *fp;
    size_t nread;

    if (out == NULL || outsz == 0 || home == NULL || slug == NULL)
        return 0;
    out[0] = '\0';
    snprintf(path, sizeof(path), "%s/agents/%s/nsec", home, slug);
    fp = fopen(path, "r");
    if (fp == NULL)
        return 0;
    nread = fread(out, 1, outsz - 1, fp);
    fclose(fp);
    out[nread] = '\0';
    if (nread > 0 && out[nread - 1] == '\n')
        out[nread - 1] = '\0';
    return out[0] != '\0';
}

/* True when the home file bytes match nsec (optional trailing newline). */
static int file_holds_nsec(const char *home, const char *slug, const char *nsec)
{
    char body[HUSH_IDENTITY_NSEC_MAX];

    if (nsec == NULL || nsec[0] == '\0')
        return 0;
    if (!read_nsec_file(body, sizeof(body), home, slug))
        return 0;
    return strcmp(body, nsec) == 0;
}

/* Removes agents/<slug>/nsec. 0 when the file was not there. */
static int drop_nsec_file(const char *home, const char *slug)
{
    char path[256];

    snprintf(path, sizeof(path), "%s/agents/%s/nsec", home, slug);
    return unlink(path) == 0;
}

/* nopass restart must keep Payne, the templates, and Walkbot One. */
static void test_nopass_robot_keys(void)
{
    static hush_launch_t keys;
    static hush_launch_t again;
    hush_store_t *store = NULL;
    hush_roster_agent_in_t in;
    char home[128];
    char cfg[128];
    char path[256];
    char body[8192];
    struct stat st;
    FILE *fp;
    size_t nread = 0;
    const char *slugs[] = {"coach", "auditor", "marshal", "walkbot-one"};
    size_t i;

    snprintf(home, sizeof(home), "/tmp/hush-b1-home-%d", (int)getpid());
    snprintf(cfg, sizeof(cfg), "/tmp/hush-b1-cfg-%d", (int)getpid());
    if (mkdir(home, 0700) != 0 || mkdir(cfg, 0700) != 0) {
        expect(0, "nopass temp dirs");
        return;
    }
    expect(setenv("HUSH_HOME", home, 1) == 0, "nopass home");
    expect(setenv("HUSH_CONFIG_DIR", cfg, 1) == 0, "nopass config");
    hush_pass_set_helper("/nonexistent/pass");
    hush_launch_init(&keys);
    expect(hush_store_create(&store) == HUSH_OK, "nopass store");
    expect(hush_launch_create_identity(&keys) == HUSH_OK, "nopass create");
    expect(hush_launch_ack_backup(&keys, 0) == HUSH_OK, "nopass ack opt-out");
    expect(!keys.save_pass, "nopass save_pass off");
    expect(hush_launch_create_vibe(&keys, store, "HQ", "b1") == HUSH_OK,
           "nopass vibe");
    memset(&in, 0, sizeof(in));
    memcpy(in.name, "Walkbot One", 12);
    memcpy(in.prompt, "Walk the floor.", 16);
    memcpy(in.provider, HUSH_ROSTER_PROVIDER_GROK_BUILD,
           sizeof(HUSH_ROSTER_PROVIDER_GROK_BUILD));
    expect(hush_launch_add_agent(&keys, store, &in, 0) == HUSH_OK,
           "nopass walkbot");
    expect(agent_pub(&keys, "walkbot-one")[0] != '\0', "nopass walkbot key");
    expect(agent_pub(&keys, "coach")[0] != '\0', "nopass coach key");
    expect(keys.payne.pubkey_hex[0] != '\0', "nopass payne key");
    snprintf(path, sizeof(path), "%s/vibe.json", cfg);
    fp = fopen(path, "r");
    expect(fp != NULL, "nopass vibe.json");
    if (fp != NULL) {
        nread = fread(body, 1, sizeof(body) - 1, fp);
        body[nread] = '\0';
        fclose(fp);
    }
    expect(strstr(body, "nsec") == NULL, "nopass vibe.json has no nsec");
    snprintf(path, sizeof(path), "%s/agents/coach/nsec", home);
    memset(&st, 0, sizeof(st));
    expect(stat(path, &st) == 0, "nopass coach nsec file");
    if (stat(path, &st) == 0) {
        expect(S_ISREG(st.st_mode), "nopass coach nsec regular");
        expect((st.st_mode & 0777) == 0600, "nopass coach nsec mode 0600");
    }
    hush_launch_init(&again);
    expect(hush_launch_restore_identity(&again) == HUSH_OK, "nopass id again");
    expect(hush_launch_restore_vibe(&again) == HUSH_OK, "nopass vibe again");
    expect(again.has_vibe, "nopass restored vibe");
    expect(strcmp(again.payne.pubkey_hex, keys.payne.pubkey_hex) == 0,
           "nopass payne pubkey held");
    for (i = 0; i < sizeof(slugs) / sizeof(slugs[0]); i++) {
        char msg[80];

        snprintf(msg, sizeof(msg), "nopass %s pubkey held", slugs[i]);
        if (strcmp(agent_pub(&again, slugs[i]), agent_pub(&keys, slugs[i])) != 0)
            fprintf(stderr, "  %s %s -> %s\n", slugs[i],
                    agent_pub(&keys, slugs[i]), agent_pub(&again, slugs[i]));
        expect(strcmp(agent_pub(&again, slugs[i]),
                      agent_pub(&keys, slugs[i])) == 0, msg);
    }
    hush_store_destroy(store);
}

/* Shared setup: vibe with file keys, pass not written (save_pass off). */
static int vault_case_open(hush_launch_t *keys, hush_store_t **store,
                           char *home, size_t homesz,
                           char *passdir, size_t passsz, const char *tag)
{
    char cfg[128];

    snprintf(home, homesz, "/tmp/hush-vault-%s-home-%d", tag, (int)getpid());
    snprintf(cfg, sizeof(cfg), "/tmp/hush-vault-%s-cfg-%d", tag, (int)getpid());
    snprintf(passdir, passsz, "/tmp/hush-vault-%s-pass-%d", tag, (int)getpid());
    if (mkdir(home, 0700) != 0 || mkdir(cfg, 0700) != 0 ||
        mkdir(passdir, 0700) != 0)
        return 0;
    if (setenv("HUSH_HOME", home, 1) != 0)
        return 0;
    if (setenv("HUSH_CONFIG_DIR", cfg, 1) != 0)
        return 0;
    if (setenv("HUSH_FAKE_PASS_DIR", passdir, 1) != 0)
        return 0;
    hush_pass_set_helper("tests/fake-pass.sh");
    hush_launch_init(keys);
    if (hush_store_create(store) != HUSH_OK)
        return 0;
    if (hush_launch_create_identity(keys) != HUSH_OK)
        return 0;
    if (hush_launch_ack_backup(keys, 0) != HUSH_OK)
        return 0;
    if (hush_launch_create_vibe(keys, *store, "HQ", "vault") != HUSH_OK)
        return 0;
    return 1;
}

/* Matching home file and pass stay the same value after restore. */
static void test_vault_match_stays(void)
{
    static hush_launch_t keys;
    static hush_launch_t again;
    hush_store_t *store = NULL;
    char home[128];
    char passdir[128];
    char file_coach[HUSH_IDENTITY_NSEC_MAX];
    char file_payne[HUSH_IDENTITY_NSEC_MAX];
    char pass_coach[HUSH_PASS_SECRET_MAX];
    char pass_payne[HUSH_PASS_SECRET_MAX];

    if (!vault_case_open(&keys, &store, home, sizeof(home),
                         passdir, sizeof(passdir), "match")) {
        expect(0, "match setup");
        return;
    }
    expect(read_nsec_file(file_coach, sizeof(file_coach), home, "coach"),
           "match coach file");
    expect(read_nsec_file(file_payne, sizeof(file_payne), home,
                          HUSH_LAUNCH_PAYNE_SLUG),
           "match payne file");
    expect(hush_pass_save("agents/coach/nsec", file_coach) == HUSH_OK,
           "match save coach pass");
    expect(hush_pass_save(HUSH_PASS_PAYNE_NSEC, file_payne) == HUSH_OK,
           "match save payne pass");
    hush_launch_init(&again);
    expect(hush_launch_restore_identity(&again) == HUSH_OK, "match id");
    expect(hush_launch_restore_vibe(&again) == HUSH_OK, "match vibe");
    expect(again.has_vibe, "match has vibe");
    expect(strcmp(agent_nsec(&again, "coach"), file_coach) == 0,
           "match coach key");
    expect(strcmp(again.payne.nsec, file_payne) == 0, "match payne key");
    expect(file_holds_nsec(home, "coach", file_coach), "match coach file stays");
    expect(file_holds_nsec(home, HUSH_LAUNCH_PAYNE_SLUG, file_payne),
           "match payne file stays");
    expect(hush_pass_get(pass_coach, sizeof(pass_coach),
                         "agents/coach/nsec") == HUSH_OK,
           "match coach pass read");
    expect(strcmp(pass_coach, file_coach) == 0, "match coach pass stays");
    expect(hush_pass_get(pass_payne, sizeof(pass_payne),
                         HUSH_PASS_PAYNE_NSEC) == HUSH_OK,
           "match payne pass read");
    expect(strcmp(pass_payne, file_payne) == 0, "match payne pass stays");
    hush_store_destroy(store);
}

/* A different pass value must not be copied onto the home file, or the
 * file onto pass. Restore fails and both stores keep their own bytes. */
static void test_vault_mismatch_stays(void)
{
    static hush_launch_t keys;
    static hush_launch_t again;
    static hush_identity_t other_coach;
    static hush_identity_t other_payne;
    hush_store_t *store = NULL;
    char home[128];
    char passdir[128];
    char file_coach[HUSH_IDENTITY_NSEC_MAX];
    char file_payne[HUSH_IDENTITY_NSEC_MAX];
    char pass_coach[HUSH_PASS_SECRET_MAX];
    char pass_payne[HUSH_PASS_SECRET_MAX];
    hush_status_t st;

    if (!vault_case_open(&keys, &store, home, sizeof(home),
                         passdir, sizeof(passdir), "mismatch")) {
        expect(0, "mismatch setup");
        return;
    }
    expect(read_nsec_file(file_coach, sizeof(file_coach), home, "coach"),
           "mismatch coach file");
    expect(read_nsec_file(file_payne, sizeof(file_payne), home,
                          HUSH_LAUNCH_PAYNE_SLUG),
           "mismatch payne file");
    expect(hush_identity_generate(&other_coach) == HUSH_OK, "mismatch other coach");
    expect(hush_identity_generate(&other_payne) == HUSH_OK, "mismatch other payne");
    expect(strcmp(file_coach, other_coach.nsec) != 0, "mismatch coach differs");
    expect(strcmp(file_payne, other_payne.nsec) != 0, "mismatch payne differs");
    expect(hush_pass_save("agents/coach/nsec", other_coach.nsec) == HUSH_OK,
           "mismatch save coach pass");
    expect(hush_pass_save(HUSH_PASS_PAYNE_NSEC, other_payne.nsec) == HUSH_OK,
           "mismatch save payne pass");
    hush_launch_init(&again);
    expect(hush_launch_restore_identity(&again) == HUSH_OK, "mismatch id");
    st = hush_launch_restore_vibe(&again);
    expect(st == HUSH_ERR_DENIED, "mismatch restore refused");
    expect(!again.has_vibe, "mismatch vibe not adopted");
    expect(file_holds_nsec(home, "coach", file_coach),
           "mismatch coach file not overwritten");
    expect(file_holds_nsec(home, HUSH_LAUNCH_PAYNE_SLUG, file_payne),
           "mismatch payne file not overwritten");
    expect(hush_pass_get(pass_coach, sizeof(pass_coach),
                         "agents/coach/nsec") == HUSH_OK,
           "mismatch coach pass read");
    expect(strcmp(pass_coach, other_coach.nsec) == 0,
           "mismatch coach pass not overwritten");
    expect(strcmp(pass_coach, file_coach) != 0, "mismatch coach still differs");
    expect(hush_pass_get(pass_payne, sizeof(pass_payne),
                         HUSH_PASS_PAYNE_NSEC) == HUSH_OK,
           "mismatch payne pass read");
    expect(strcmp(pass_payne, other_payne.nsec) == 0,
           "mismatch payne pass not overwritten");
    expect(strcmp(pass_payne, file_payne) != 0, "mismatch payne still differs");
    hush_store_destroy(store);
}

/* Generate with no pass entry and no file must write the new nsec. */
static void test_generate_writes_nsec(void)
{
    static hush_launch_t keys;
    static hush_launch_t again;
    static hush_launch_t third;
    hush_store_t *store = NULL;
    char home[128];
    char cfg[128];
    char passdir[128];

    snprintf(home, sizeof(home), "/tmp/hush-b1-gen-home-%d", (int)getpid());
    snprintf(cfg, sizeof(cfg), "/tmp/hush-b1-gen-cfg-%d", (int)getpid());
    snprintf(passdir, sizeof(passdir), "/tmp/hush-b1-gen-pass-%d",
             (int)getpid());
    if (mkdir(home, 0700) != 0 || mkdir(cfg, 0700) != 0 ||
        mkdir(passdir, 0700) != 0) {
        expect(0, "genwrite temp dirs");
        return;
    }
    expect(setenv("HUSH_HOME", home, 1) == 0, "genwrite home");
    expect(setenv("HUSH_CONFIG_DIR", cfg, 1) == 0, "genwrite config");
    expect(setenv("HUSH_FAKE_PASS_DIR", passdir, 1) == 0, "genwrite pass dir");
    hush_pass_set_helper("tests/fake-pass.sh");
    hush_launch_init(&keys);
    expect(hush_store_create(&store) == HUSH_OK, "genwrite store");
    expect(hush_launch_create_identity(&keys) == HUSH_OK, "genwrite create");
    expect(hush_launch_ack_backup(&keys, 0) == HUSH_OK, "genwrite ack opt-out");
    expect(!keys.save_pass, "genwrite save_pass off");
    expect(hush_launch_create_vibe(&keys, store, "HQ", "b1") == HUSH_OK,
           "genwrite vibe");
    expect(agent_pub(&keys, "coach")[0] != '\0', "genwrite coach seeded");
    expect(keys.payne.pubkey_hex[0] != '\0', "genwrite payne seeded");
    expect(drop_nsec_file(home, "coach"), "genwrite drop coach");
    expect(drop_nsec_file(home, HUSH_LAUNCH_PAYNE_SLUG), "genwrite drop payne");
    hush_launch_init(&again);
    expect(hush_launch_restore_identity(&again) == HUSH_OK, "genwrite id");
    expect(hush_launch_restore_vibe(&again) == HUSH_OK, "genwrite vibe again");
    expect(strcmp(agent_pub(&again, "coach"), agent_pub(&keys, "coach")) != 0,
           "genwrite coach minted");
    expect(file_holds_nsec(home, "coach", agent_nsec(&again, "coach")),
           "genwrite coach file matches key");
    expect(strcmp(again.payne.pubkey_hex, keys.payne.pubkey_hex) != 0,
           "genwrite payne minted");
    expect(file_holds_nsec(home, HUSH_LAUNCH_PAYNE_SLUG, again.payne.nsec),
           "genwrite payne file matches key");
    expect(drop_nsec_file(home, "coach"), "genwrite drop coach again");
    expect(drop_nsec_file(home, HUSH_LAUNCH_PAYNE_SLUG),
           "genwrite drop payne again");
    hush_launch_init(&third);
    expect(hush_launch_restore_identity(&third) == HUSH_OK, "genwrite id third");
    expect(hush_launch_restore_vibe(&third) == HUSH_OK, "genwrite vibe third");
    expect(strcmp(agent_pub(&third, "coach"), agent_pub(&again, "coach")) != 0,
           "genwrite coach minted again");
    expect(file_holds_nsec(home, "coach", agent_nsec(&third, "coach")),
           "genwrite coach file matches new key");
    expect(strcmp(third.payne.pubkey_hex, again.payne.pubkey_hex) != 0,
           "genwrite payne minted again");
    expect(file_holds_nsec(home, HUSH_LAUNCH_PAYNE_SLUG, third.payne.nsec),
           "genwrite payne file matches new key");
    hush_store_destroy(store);
}

int main(void)
{
    static hush_launch_t launch;
    static char json[HUSH_LAUNCH_JSON_MAX];
    hush_store_t *store = NULL;
    const char *gitdir = "/tmp/hush-launch-proj";
    size_t n = 0;

    if (setenv("HUSH_FAKE_PASS_DIR", "/tmp/hush-launch-pass-store", 1) != 0)
        return 1;
    {
        char cfg[128];

        snprintf(cfg, sizeof(cfg), "/tmp/hush-launch-cfg-%d", (int)getpid());
        if (setenv("HUSH_CONFIG_DIR", cfg, 1) != 0)
            return 1;
    }
    /* Isolated from tests/test_pass.c, which uses /tmp/hush-unit-pass-<pid>. */
    hush_pass_set_helper("tests/fake-pass.sh");
    hush_launch_init(&launch);
    expect(!hush_launch_is_ready(&launch), "cold not ready");
    expect(hush_store_create(&store) == HUSH_OK, "store");
    expect(hush_launch_create_identity(&launch) == HUSH_OK, "create");
    expect(launch.logged_in, "logged in");
    expect(!launch.backup_acked, "needs backup");
    expect(hush_launch_format_session(&launch, 10555, json, sizeof(json),
                                      &n) == HUSH_OK,
           "session after create");
    expect(strstr(json, "\"nsec\":\"nsec1") != NULL, "nsec once");
    expect(hush_launch_ack_backup(&launch, 1) == HUSH_OK, "ack");
    expect(launch.pass_saved, "saved to pass");
    expect(hush_pass_has(HUSH_PASS_IDENTITY_NSEC), "identity in store");
    expect(hush_launch_format_session(&launch, 10555, json, sizeof(json),
                                      &n) == HUSH_OK,
           "session after ack");
    expect(strstr(json, "\"nsec\":\"\"") != NULL, "nsec cleared");
    expect(hush_launch_create_vibe(&launch, store, "HQ",
                                   "primary endpoint") == HUSH_OK,
           "vibe");
    expect(hush_launch_is_ready(&launch), "ready");
    expect(launch.vibe_public == 1, "vibe public default");
    {
        size_t i;
        int coach = 0;
        int auditor = 0;
        int marshal = 0;

        for (i = 0; i < launch.roster.nagents; i++) {
            if (strcmp(launch.roster.agents[i].slug, "coach") == 0 &&
                launch.roster.agents[i].locked) {
                coach = 1;
                expect(strcmp(launch.roster.agents[i].picture,
                              "panel:robots:0") == 0,
                       "coach icon");
            }
            if (strcmp(launch.roster.agents[i].slug, "auditor") == 0 &&
                launch.roster.agents[i].locked) {
                auditor = 1;
                expect(strcmp(launch.roster.agents[i].picture,
                              "panel:robots:2") == 0,
                       "auditor icon");
            }
            if (strcmp(launch.roster.agents[i].slug, "marshal") == 0 &&
                launch.roster.agents[i].locked) {
                marshal = 1;
                expect(strcmp(launch.roster.agents[i].role,
                              HUSH_ROSTER_ROLE_CHAPERON) == 0,
                       "marshal chaperon");
                expect(strcmp(launch.roster.agents[i].picture,
                              "panel:angevin:3") == 0,
                       "marshal icon");
                expect(launch.roster.agents[i].nskills == 8, "marshal 8 rails");
            }
        }
        expect(coach, "coach template");
        expect(auditor, "auditor template");
        expect(marshal, "marshal template");
        expect(strcmp(launch.payne_picture, "panel:robots:1") == 0,
               "major icon");
        expect(launch.npayne_skills == 1, "major default skill");
        expect(strcmp(launch.payne_skills[0], "system:hive-patterns") == 0,
               "major hive-patterns");
        expect(hush_launch_clone_agent(&launch, store, HUSH_LAUNCH_PAYNE_SLUG)
                   == HUSH_ERR_DENIED,
               "major no clone");
        expect(hush_launch_clone_agent(&launch, store, "coach") == HUSH_OK,
               "clone coach");
        expect(hush_launch_format_session(&launch, 10555, json, sizeof(json),
                                          &n) == HUSH_OK,
               "session after clone");
        expect(strstr(json, "\"slug\":\"coach-copy\"") != NULL, "coach copy");
        expect(strstr(json, "sgt-major-payne-copy") == NULL, "not a second Major");
        {
            hush_roster_agent_in_t lock_in;

            memset(&lock_in, 0, sizeof(lock_in));
            memcpy(lock_in.name, "Nope", 5);
            lock_in.has_enabled = 1;
            lock_in.enabled = 0;
            expect(hush_launch_update_agent(&launch, "coach", &lock_in) ==
                       HUSH_OK,
                   "locked enable");
        }
        for (i = 0; i < launch.roster.nagents; i++) {
            if (strcmp(launch.roster.agents[i].slug, "coach") != 0)
                continue;
            expect(strcmp(launch.roster.agents[i].name, "Coach") == 0,
                   "coach name stays");
            expect(launch.roster.agents[i].enabled == 0, "coach off");
        }
    }
    expect(launch.vibe_token[0] != '\0', "join token");
    expect(hush_launch_set_vibe_visibility(&launch, 0) == HUSH_OK, "private");
    expect(launch.vibe_public == 0, "vibe private");
    expect(hush_launch_format_session(&launch, 10555, json, sizeof(json),
                                      &n) == HUSH_OK,
           "session private");
    expect(strstr(json, "\"visibility\":\"private\"") != NULL, "vis private");
    expect(hush_launch_set_vibe_visibility(&launch, 1) == HUSH_OK, "public");
    expect(launch.nchannels == 3, "starter channels");
    expect(strncmp(launch.payne.npub, "npub1", 5) == 0, "payne");
    expect(launch.npayne_providers == 1, "default one provider");
    expect(strcmp(launch.payne_providers[0],
                  HUSH_ROSTER_PROVIDER_GROK_BUILD) == 0,
           "default grok-build");
    {
        const char *ids[] = {
            HUSH_ROSTER_PROVIDER_GROK_BUILD,
            HUSH_ROSTER_PROVIDER_GOOSE,
            HUSH_ROSTER_PROVIDER_GROK_BUILD,
            "not-a-provider"
        };

        expect(hush_launch_set_payne_providers(&launch, ids, 4) == HUSH_OK,
               "set payne providers");
        expect(launch.npayne_providers == 2, "deduped two");
        expect(strcmp(launch.payne_providers[0],
                      HUSH_ROSTER_PROVIDER_GROK_BUILD) == 0,
               "primary grok");
        expect(strcmp(launch.payne_providers[1],
                      HUSH_ROSTER_PROVIDER_GOOSE) == 0,
               "fallback goose");
    }
    expect(hush_launch_format_session(&launch, 10555, json, sizeof(json),
                                      &n) == HUSH_OK,
           "session payne providers");
    expect(strstr(json, "\"provider\":\"grok-build\"") != NULL,
           "session primary");
    expect(strstr(json, "\"providers\":[\"grok-build\",\"goose\"]") != NULL,
           "session order");
    expect(strstr(json, HUSH_LAUNCH_PAYNE_NAME) != NULL, "payne Major");
    {
        hush_roster_agent_in_t payne_in;

        memset(&payne_in, 0, sizeof(payne_in));
        memcpy(payne_in.picture, "panel:robots:1", 15);
        memcpy(payne_in.voice, "alloy", 6);
        memcpy(payne_in.skills[0], "system:forge-skill", 19);
        payne_in.nskills = 1;
        payne_in.has_picture = 1;
        payne_in.has_voice = 1;
        payne_in.has_skills = 1;
        expect(hush_launch_update_payne_profile(&launch, &payne_in) == HUSH_OK,
               "payne profile");
        expect(strcmp(launch.payne_picture, "panel:robots:1") == 0, "payne pic");
        memcpy(payne_in.name, "Nope", 5);
        memcpy(payne_in.prompt, "Nope prompt", 12);
        payne_in.has_enabled = 1;
        payne_in.enabled = 0;
        expect(hush_launch_update_payne_profile(&launch, &payne_in) == HUSH_OK,
               "payne disable");
        expect(strcmp(launch.payne_name, HUSH_LAUNCH_PAYNE_NAME) == 0,
               "name locked");
        expect(launch.payne_enabled == 0, "payne off");
        expect(hush_launch_format_session(&launch, 10555, json, sizeof(json),
                                          &n) == HUSH_OK,
               "session payne extras");
        expect(strstr(json, "system:forge-skill") != NULL, "payne skill json");
        memset(payne_in.skills, 0, sizeof(payne_in.skills));
        payne_in.nskills = 0;
        payne_in.has_skills = 1;
        expect(hush_launch_update_payne_profile(&launch, &payne_in) == HUSH_OK,
               "payne prune");
        expect(launch.npayne_skills == 0, "payne empty loadout");
        expect(hush_launch_format_session(&launch, 10555, json, sizeof(json),
                                          &n) == HUSH_OK,
               "session payne prune");
        expect(strstr(json, "system:forge-skill") == NULL, "payne skill gone");
        expect(strstr(json, "\"voice\":\"alloy\"") != NULL, "payne voice json");
        expect(strstr(json, "\"enabled\":false") != NULL, "payne off json");
    }
    expect(hush_launch_add_channel(&launch, "incidents") == HUSH_OK, "channel");
    expect(launch.nchannels == 4, "four channels");
    expect(strlen(launch.channels[0].id) == (size_t)HUSH_LAUNCH_ID_HEX,
           "channel uuid");
    expect(hush_launch_add_group(&launch, "Duty") == HUSH_OK, "group");
    expect(launch.ngroups == 1, "one group");
    expect(strlen(launch.groups[0].id) == (size_t)HUSH_LAUNCH_ID_HEX,
           "group uuid");
    expect(hush_launch_set_channel_group(&launch, "incidents",
                                        launch.groups[0].id) == HUSH_OK,
           "add to group");
    expect(strcmp(launch.channels[3].group_id, launch.groups[0].id) == 0,
           "grouped");
    {
        const char *humans[] = {
            "npub10elfcs4fr0l0r8af98jlmgdh9c8tcxjvz9qkw038js35mp4dma8qzvjptg"
        };
        const char *robots[] = { HUSH_LAUNCH_PAYNE_SLUG };

        expect(hush_launch_set_channel_roster(&launch, "incidents",
                                              humans, 1, robots, 1) == HUSH_OK,
               "roster");
        expect(launch.channels[3].nhumans == 1, "one human");
        expect(launch.channels[3].nrobots == 1, "one robot");
        expect(strcmp(launch.channels[3].kind, HUSH_LAUNCH_KIND_OPEN) == 0,
               "default kind");
        expect(strcmp(launch.channels[3].robot_reply,
                      HUSH_LAUNCH_REPLY_MENTION) == 0,
               "default reply");
        expect(launch.channels[3].burst_ms == HUSH_LAUNCH_BURST_MS_DEFAULT,
               "default burst");
        expect(launch.channels[3].max_robot_turns == HUSH_LAUNCH_TURNS_DEFAULT,
               "default turns");
        expect(launch.channels[3].chaperon[0] == '\0', "default chaperon empty");
    }
    {
        hush_launch_policy_t policy;

        memset(&policy, 0, sizeof(policy));
        memcpy(policy.kind, HUSH_LAUNCH_KIND_HUMANS,
               sizeof(HUSH_LAUNCH_KIND_HUMANS));
        memcpy(policy.robot_reply, HUSH_LAUNCH_REPLY_OFF,
               sizeof(HUSH_LAUNCH_REPLY_OFF));
        policy.burst_ms = HUSH_LAUNCH_BURST_MS_SLOW;
        policy.max_jobs = HUSH_LAUNCH_MAX_JOBS_MIN;
        policy.cooldown_s = HUSH_LAUNCH_COOLDOWN_S_LONG;
        expect(hush_launch_set_channel_policy(&launch, "incidents",
                                              &policy) == HUSH_OK,
               "policy");
        expect(strcmp(launch.channels[3].kind, HUSH_LAUNCH_KIND_HUMANS) == 0,
               "humans kind");
        expect(strcmp(launch.channels[3].robot_reply,
                      HUSH_LAUNCH_REPLY_OFF) == 0,
               "reply off");
        expect(launch.channels[3].burst_ms == HUSH_LAUNCH_BURST_MS_SLOW,
               "slow burst");
        expect(launch.channels[3].max_robot_turns == HUSH_LAUNCH_TURNS_DEFAULT,
               "turns stay default on zero");
        memcpy(policy.chaperon, HUSH_LAUNCH_PAYNE_SLUG,
               sizeof(HUSH_LAUNCH_PAYNE_SLUG));
        policy.max_robot_turns = HUSH_LAUNCH_TURNS_PAIR;
        expect(hush_launch_set_channel_policy(&launch, "incidents",
                                              &policy) == HUSH_OK,
               "rails");
        expect(launch.channels[3].max_robot_turns == HUSH_LAUNCH_TURNS_PAIR,
               "pair turns");
        expect(strcmp(launch.channels[3].chaperon, HUSH_LAUNCH_PAYNE_SLUG) == 0,
               "major chaperon");
    }
    {
        hush_launch_policy_t policy;

        memset(&policy, 0, sizeof(policy));
        memcpy(policy.kind, HUSH_LAUNCH_KIND_HUMANS,
               sizeof(HUSH_LAUNCH_KIND_HUMANS));
        memcpy(policy.robot_reply, HUSH_LAUNCH_REPLY_OFF,
               sizeof(HUSH_LAUNCH_REPLY_OFF));
        policy.burst_ms = HUSH_LAUNCH_BURST_MS_SLOW;
        policy.max_jobs = HUSH_LAUNCH_MAX_JOBS_MIN;
        policy.cooldown_s = HUSH_LAUNCH_COOLDOWN_S_LONG;
        policy.robot_talk = 1;
        expect(hush_launch_set_channel_policy(&launch, "incidents",
                                              &policy) == HUSH_OK,
               "standalone policy");
        expect(strcmp(launch.channels[3].chaperon, HUSH_LAUNCH_PAYNE_SLUG) == 0,
               "standalone robots auto-assign chaperon");
    }
    expect(hush_launch_set_channel_group(&launch, "incidents", "") == HUSH_OK,
           "ungroup");
    expect(launch.channels[3].group_id[0] == '\0', "ungrouped");
    expect(hush_launch_remove_channel(&launch, "incidents") == HUSH_OK,
           "delete channel");
    expect(launch.nchannels == 3, "three after delete");
    expect(hush_launch_remove_channel(&launch, "general") == HUSH_OK,
           "drop general");
    expect(hush_launch_remove_channel(&launch, "welcome") == HUSH_OK,
           "drop welcome");
    expect(hush_launch_remove_channel(&launch, "agents") == HUSH_ERR_DENIED,
           "last channel stays");
    expect(hush_launch_add_channel(&launch, "incidents") == HUSH_OK,
           "channel again");
    expect(hush_launch_add_project(&launch, store, "alpha", gitdir, 1) == HUSH_OK,
           "project");
    expect(launch.nprojects == 1, "one project");
    {
        char evil[HUSH_LAUNCH_PATH_MAX];
        char marker[64];

        snprintf(marker, sizeof(marker), "hush-launch-pwned-%d", (int)getpid());
        (void)unlink(marker);
        snprintf(evil, sizeof(evil),
                 "/tmp/hush-launch-quote-%d/x'; touch %s; echo '", (int)getpid(),
                 marker);
        expect(hush_launch_add_project(&launch, store, "evil", evil, 1) == HUSH_OK,
               "quote path stays literal");
        expect(access(marker, F_OK) != 0, "no shell execution");
    }
    expect(hush_launch_add_project(&launch, store, "rel", "relative/dir", 1) ==
               HUSH_ERR_ARG,
           "relative path rejected");
    expect(hush_launch_add_project(&launch, store, "up", "/tmp/hush-up/../escape",
                                   1) == HUSH_ERR_ARG,
           "dotdot path rejected");
    expect(hush_launch_add_project(&launch, store, "root", "/", 1) == HUSH_ERR_ARG,
           "root path rejected");
    expect(hush_launch_format_session(&launch, 10555, json, sizeof(json),
                                      &n) == HUSH_OK,
           "final session");
    expect(strstr(json, "\"name\":\"Major\"") != NULL, "payne name");
    expect(strstr(json, "\"pubkey\":\"") != NULL, "payne pubkey");
    expect(strstr(json, "\"slug\":\"incidents\"") != NULL, "incidents");
    expect(strstr(json, "\"robot_reply\":\"mention\"") != NULL, "session reply");
    expect(strstr(json, "\"burst_ms\":") != NULL, "session burst");
    expect(strstr(json, "\"slug\":\"alpha\"") != NULL, "alpha");
    expect(hush_launch_import_identity(
               &launch,
               "nsec1vl029mgpspedva04g90vltkh6fvh240zqtv9k0t9af8935ke9laqsnlfe5") ==
               HUSH_OK,
           "import");
    expect(!launch.backup_acked, "import still needs backup");
    expect(strcmp(launch.human.npub,
                  "npub10elfcs4fr0l0r8af98jlmgdh9c8tcxjvz9qkw038js35mp4dma8qzvjptg") ==
               0,
           "imported npub");
    expect(hush_launch_ack_backup(&launch, 0) == HUSH_OK, "import ack opt-out");
    expect(!launch.pass_saved || launch.save_pass == 0, "opt-out skips pass");
    {
        hush_roster_profile_t profile;

        memset(&profile, 0, sizeof(profile));
        memcpy(profile.first_name, "Ada", 4);
        memcpy(profile.theme, "dracula", 8);
        expect(hush_launch_set_profile(&launch, &profile) == HUSH_OK, "profile");
        expect(hush_launch_format_session(&launch, 10555, json, sizeof(json),
                                          &n) == HUSH_OK,
               "session profile");
        expect(strstr(json, "\"first_name\":\"Ada\"") != NULL, "first in session");
        expect(strstr(json, "\"theme\":\"dracula\"") != NULL, "theme in session");
    }
    expect(hush_launch_logout(&launch) == HUSH_OK, "logout");
    expect(!launch.logged_in, "logged out");
    expect(!hush_launch_is_ready(&launch), "logout not ready");
    {
        static hush_launch_t again;
        FILE *fp;
        char path[192];
        char body[4096];
        size_t nread = 0;
        const char *cfg = getenv("HUSH_CONFIG_DIR");

        expect(cfg != NULL && cfg[0] != '\0', "config dir");
        snprintf(path, sizeof(path), "%s/vibe.json", cfg);
        fp = fopen(path, "r");
        expect(fp != NULL, "vibe.json exists");
        if (fp != NULL) {
            nread = fread(body, 1, sizeof(body) - 1, fp);
            body[nread] = '\0';
            fclose(fp);
        }
        expect(strstr(body, "nsec") == NULL, "vibe.json has no nsec");
        expect(strstr(body, "\"vibe_name\":\"HQ\"") != NULL, "saved name");
        hush_launch_init(&again);
        expect(hush_launch_restore_identity(&again) == HUSH_OK, "id again");
        expect(hush_launch_restore_vibe(&again) == HUSH_OK, "vibe again");
        expect(again.has_vibe, "restored has_vibe");
        expect(strcmp(again.vibe_name, "HQ") == 0, "restored name");
        expect(again.nchannels >= 2, "restored channels");
        expect(again.ngroups == 1, "restored group");
        expect(strlen(again.channels[0].id) == (size_t)HUSH_LAUNCH_ID_HEX,
               "restored channel uuid");
        expect(strncmp(again.payne.npub, "npub1", 5) == 0, "restored payne");
        expect(again.npayne_providers == 2, "restored two providers");
        expect(strcmp(again.payne_providers[0],
                      HUSH_ROSTER_PROVIDER_GROK_BUILD) == 0,
               "restored primary");
        expect(strstr(body, "payne_provider_0") != NULL, "vibe has payne_provider");
    }
    hush_store_destroy(store);
    test_nopass_robot_keys();
    test_vault_match_stays();
    test_vault_mismatch_stays();
    test_generate_writes_nsec();
    hush_pass_set_helper(NULL);
    if (g_fail)
        return 1;
    printf("test_launch ok\n");
    return 0;
}
