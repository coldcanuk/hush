/* tests/test_cline_gate.c: #311 A3, #356. Every Cline turn runs with
 * "--auto-approve false", gated or not: headless Cline then denies any tool
 * call that needs a person, so no turn gets a native shell (CoS 11:00). A
 * person-gated project turn (the hive owner asked, or the owner answered Yes
 * under Approve every action) keeps only the project cwd and 300 s clock; a
 * guest's mention under the default Auto-approve keeps the 80 s chat clock.
 * Drives the real mention path on a real store and launch; a stub cline
 * first on PATH records its argv, so no real runtime ever starts. */

#define _XOPEN_SOURCE 700

#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "hush_agent.h"
#include "hush_agent_internal.h"
#include "hush_event.h"
#include "hush_intel.h"
#include "hush_launch.h"
#include "hush_pass.h"
#include "hush_roster.h"
#include "hush_store.h"
#include "hush_thread.h"

enum {
    /* The mkdtemp root; every derived path fits GATE_PATH_MAX. */
    GATE_DIR_MAX = 64,
    GATE_PATH_MAX = 512,
    GATE_FTW_FDS = 16,
    GATE_KIND_NOTE = 1,
    /* Polls for the stub's argv file: 100 x 20 ms = 2 s. */
    GATE_WAIT_TRIES = 100,
    GATE_WAIT_NS = 20000000
};

#define GATE_DIR_TEMPLATE "/tmp/hush-cline-gate-XXXXXX"
#define GATE_CHANNEL "general"
#define GATE_SYS_PATH "/usr/bin:/bin"
/* A signed-in Cline as the provider scan sees it. */
#define GATE_CLINE_LOGIN "{\"providers\":{\"anthropic\":{\"apiKey\":\"test\"}}}\n"
#define GATE_PROJECT "Site"
/* Any value but "false" would hand Cline its tools unasked (#356). */
#define GATE_APPROVE_ANY "--auto-approve\n"
#define GATE_APPROVE_OFF "--auto-approve\nfalse\n"
/* --timeout follows the job clock (#310): project 300 s, chat 80 s. */
#define GATE_TIMEOUT_PROJECT "--timeout\n300\n"
#define GATE_TIMEOUT_CHAT "--timeout\n80\n"
/* A well-formed pubkey that is neither the owner nor any robot. */
#define GATE_GUEST_PUB "abababababababababababababababababababababababababababababababab"

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

typedef struct {
    hush_launch_t launch;
    hush_store_t *store;
    hush_event_t root;
    hush_event_t ev;
    const hush_roster_agent_t *pat;
    char dir[GATE_DIR_MAX];
    char argv_file[GATE_DIR_MAX * 2];
} gate_fixture_t;

/* Writes path with body and mode 0700. */
static void write_exec(const char *path, const char *body)
{
    FILE *f = fopen(path, "w");

    expect(f != NULL && fputs(body, f) >= 0, "write stub");
    if (f != NULL)
        fclose(f);
    expect(chmod(path, S_IRWXU) == 0, "stub mode");
}

/* A stub cline first on PATH that writes one argv entry per line. */
static void stub_cline(gate_fixture_t *fx)
{
    char path[GATE_PATH_MAX] = {0};
    char body[GATE_PATH_MAX * 2] = {0};

    snprintf(path, sizeof(path), "%s-bin", fx->dir);
    expect(mkdir(path, S_IRWXU) == 0, "stub bin dir");
    /* Sibling of HUSH_HOME: agent children are jailed off $HUSH_HOME (#336). */
    snprintf(fx->argv_file, sizeof(fx->argv_file), "%s-bin/cline.argv", fx->dir);
    snprintf(body, sizeof(body), "#!/bin/sh\nprintf '%%s\\n' \"$@\" > '%s.tmp'\n"
             "mv '%s.tmp' '%s'\nexit 0\n", fx->argv_file, fx->argv_file, fx->argv_file);
    snprintf(path, sizeof(path), "%s-bin/cline", fx->dir);
    write_exec(path, body);
    snprintf(path, sizeof(path), "%s-bin:%s", fx->dir, GATE_SYS_PATH);
    expect(setenv("PATH", path, 1) == 0, "stub PATH");
}

/* HOME, config, pass store and HUSH_HOME under one mkdtemp root. */
static void make_scratch(gate_fixture_t *fx)
{
    char sub[GATE_PATH_MAX] = {0};

    hush_agent_copy(fx->dir, sizeof(fx->dir), GATE_DIR_TEMPLATE);
    expect(mkdtemp(fx->dir) != NULL, "mkdtemp scratch");
    expect(setenv("HOME", fx->dir, 1) == 0, "scratch HOME");
    expect(setenv("HUSH_HOME", fx->dir, 1) == 0, "scratch HUSH_HOME");
    snprintf(sub, sizeof(sub), "%s/config", fx->dir);
    expect(setenv("HUSH_CONFIG_DIR", sub, 1) == 0, "cfg env");
    snprintf(sub, sizeof(sub), "%s/pass", fx->dir);
    expect(mkdir(sub, S_IRWXU) == 0, "pass dir");
    expect(setenv("HUSH_FAKE_PASS_DIR", sub, 1) == 0, "pass env");
    snprintf(sub, sizeof(sub), "%s/proj", fx->dir);
    expect(mkdir(sub, S_IRWXU) == 0, "project dir");
    hush_pass_set_helper("tests/fake-pass.sh");
    /* Outside $HUSH_HOME so the jailed child can still read providers.json. */
    snprintf(sub, sizeof(sub), "%s-cline", fx->dir);
    expect(mkdir(sub, S_IRWXU) == 0 && setenv("CLINE_DATA_DIR", sub, 1) == 0, "cline data");
    snprintf(sub, sizeof(sub), "%s-cline/settings", fx->dir);
    expect(mkdir(sub, S_IRWXU) == 0, "cline settings");
    snprintf(sub, sizeof(sub), "%s-cline/settings/providers.json", fx->dir);
    write_exec(sub, GATE_CLINE_LOGIN);
    stub_cline(fx);
}

static int remove_entry(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
    (void)st;
    (void)flag;
    (void)ftw;
    return remove(path);
}

/* Owner, vibe, robot Pat on Cline, project Site at <dir>/proj, mode id. */
static void open_hive(gate_fixture_t *fx, const char *id)
{
    hush_roster_agent_in_t in = {0};
    char proj[GATE_PATH_MAX] = {0};

    make_scratch(fx);
    hush_intel_init();
    hush_agent_init();
    hush_launch_init(&fx->launch);
    expect(hush_store_create(&fx->store) == HUSH_OK, "store");
    expect(hush_launch_create_identity(&fx->launch) == HUSH_OK, "ident");
    expect(hush_launch_ack_backup(&fx->launch, 0) == HUSH_OK, "ack");
    expect(hush_launch_create_vibe(&fx->launch, fx->store, "HQ", "x") == HUSH_OK, "vibe");
    hush_agent_copy(in.name, sizeof(in.name), "Pat");
    hush_agent_copy(in.prompt, sizeof(in.prompt), "Build.");
    hush_agent_copy(in.provider, sizeof(in.provider), HUSH_ROSTER_PROVIDER_CLINE);
    expect(hush_launch_add_agent(&fx->launch, fx->store, &in, 0) == HUSH_OK, "raise Pat");
    fx->pat = &fx->launch.roster.agents[fx->launch.roster.nagents - 1];
    snprintf(proj, sizeof(proj), "%s/proj", fx->dir);
    expect(hush_launch_add_project(&fx->launch, fx->store, GATE_PROJECT, proj, 0) == HUSH_OK,
           "add project");
    expect(hush_launch_set_approval(&fx->launch, id) == HUSH_OK, "set approval");
}

static void close_hive(gate_fixture_t *fx)
{
    hush_agent_shutdown();
    hush_store_destroy(fx->store);
    {
        char extra[GATE_DIR_MAX * 2];
        snprintf(extra, sizeof(extra), "%s-cline", fx->dir);
        (void)nftw(extra, remove_entry, GATE_FTW_FDS, FTW_DEPTH | FTW_PHYS);
        snprintf(extra, sizeof(extra), "%s-bin", fx->dir);
        (void)nftw(extra, remove_entry, GATE_FTW_FDS, FTW_DEPTH | FTW_PHYS);
        unlink(fx->argv_file);
    }
    expect(nftw(fx->dir, remove_entry, GATE_FTW_FDS, FTW_DEPTH | FTW_PHYS) == 0,
           "scratch cleanup");
}

/* Fills a kind-1 note from pub; replies to root when given. */
static void fill_note(hush_event_t *ev, const char *pub, const char *content, const char *root)
{
    static unsigned seq;

    memset(ev, 0, sizeof(*ev));
    snprintf(ev->id, sizeof(ev->id), "%0*x", HUSH_EVENT_ID_HEX_LEN, ++seq);
    hush_agent_copy(ev->pubkey, sizeof(ev->pubkey), pub);
    ev->kind = GATE_KIND_NOTE;
    ev->created_at = 1;
    hush_agent_copy(ev->content, sizeof(ev->content), content);
    hush_agent_copy(ev->tags[0][0], sizeof(ev->tags[0][0]), "h");
    hush_agent_copy(ev->tags[0][1], sizeof(ev->tags[0][1]), GATE_CHANNEL);
    ev->tag_count = 1;
    if (root != NULL) {
        hush_agent_copy(ev->tags[1][0], sizeof(ev->tags[1][0]), "e");
        hush_agent_copy(ev->tags[1][1], sizeof(ev->tags[1][1]), root);
        ev->tag_count = 2;
    }
}

/* Adds a p-tag for Pat to ev. */
static void tag_pat(gate_fixture_t *fx, hush_event_t *ev)
{
    size_t at = ev->tag_count;

    hush_agent_copy(ev->tags[at][0], sizeof(ev->tags[at][0]), "p");
    hush_agent_copy(ev->tags[at][1], sizeof(ev->tags[at][1]), fx->pat->id.npub);
    ev->tag_count = at + 1;
}

/* Opens a thread from opener and binds its desk to the project. When pub
 * is the opener the opening note itself mentions Pat; otherwise pub
 * mentions Pat in a reply. Hands the mention to the relay's path. */
static void mention_in_project(gate_fixture_t *fx, const char *opener, const char *pub)
{
    hush_thread_desk_t desk = {0};
    char content[HUSH_EVENT_MAX_CONTENT] = {0};
    int opens = strcmp(opener, pub) == 0;
    hush_event_t *ask = opens ? &fx->root : &fx->ev;

    snprintf(content, sizeof(content), "nostr:%s fix the build.", fx->pat->id.npub);
    fill_note(&fx->root, opener, opens ? content : "Thread about the site.", NULL);
    if (opens)
        tag_pat(fx, &fx->root);
    expect(hush_store_insert(fx->store, &fx->root) == HUSH_OK, "root insert");
    hush_agent_copy(desk.name, sizeof(desk.name), "Site work");
    hush_agent_copy(desk.category, sizeof(desk.category), "build");
    hush_agent_copy(desk.project, sizeof(desk.project), fx->launch.projects[0].slug);
    expect(hush_thread_desk_set(fx->root.id, &desk) == HUSH_OK, "bind desk");
    if (!opens) {
        fill_note(&fx->ev, pub, content, fx->root.id);
        tag_pat(fx, &fx->ev);
        expect(hush_store_insert(fx->store, &fx->ev) == HUSH_OK, "ask insert");
    }
    hush_agent_handle_mention(fx->store, &fx->launch, ask, fx->pat->id.npub);
}

/* Waits for the stub's argv; returns 1 when it carries want. */
static int argv_has(gate_fixture_t *fx, const char *want)
{
    char buf[HUSH_EVENT_MAX_CONTENT] = {0};
    struct timespec tick = {0, GATE_WAIT_NS};

    for (int i = 0; i < GATE_WAIT_TRIES; i++) {
        FILE *f = fopen(fx->argv_file, "r");

        if (f != NULL) {
            size_t n = fread(buf, 1, sizeof(buf) - 1, f);

            fclose(f);
            buf[n] = '\0';
            unlink(fx->argv_file);
            return strstr(buf, want) != NULL;
        }
        nanosleep(&tick, NULL);
    }
    return 0;
}

/* The owner's own mention in a project thread runs Cline on the project
 * clock (#310, #311) with tool auto-approve still off (#356). */
static void check_owner_auto(void)
{
    static gate_fixture_t fx;
    const char *owner = NULL;

    open_hive(&fx, HUSH_ROSTER_APPROVAL_AUTO_ID);
    owner = fx.launch.human.pubkey_hex;
    mention_in_project(&fx, owner, owner);
    expect(argv_has(&fx, GATE_APPROVE_OFF GATE_TIMEOUT_PROJECT),
           "the owner's project turn runs Cline with tools off and the 300 s clock");
    close_hive(&fx);
}

/* A3: a guest's mention in the owner's project thread under the default
 * Auto-approve passed no person gate, so Cline keeps tools off. */
static void check_guest_auto(void)
{
    static gate_fixture_t fx;

    open_hive(&fx, HUSH_ROSTER_APPROVAL_AUTO_ID);
    mention_in_project(&fx, fx.launch.human.pubkey_hex, GATE_GUEST_PUB);
    expect(argv_has(&fx, GATE_APPROVE_OFF GATE_TIMEOUT_CHAT),
           "a guest's Auto-approve turn in a project thread gets no Cline shell");
    close_hive(&fx);
}

/* A3: a thread a guest opened and bound is still no person gate. */
static void check_guest_thread(void)
{
    static gate_fixture_t fx;

    open_hive(&fx, HUSH_ROSTER_APPROVAL_AUTO_ID);
    mention_in_project(&fx, GATE_GUEST_PUB, GATE_GUEST_PUB);
    expect(argv_has(&fx, GATE_APPROVE_OFF GATE_TIMEOUT_CHAT),
           "a guest's own project thread gets no Cline shell");
    close_hive(&fx);
}

/* Under Approve every action the owner's Yes is the person gate, so the
 * guest's approved turn gets the project clock, still with tools off. */
static void check_guest_yes(void)
{
    static gate_fixture_t fx;

    open_hive(&fx, HUSH_ROSTER_APPROVAL_EVERY_ID);
    mention_in_project(&fx, fx.launch.human.pubkey_hex, GATE_GUEST_PUB);
    expect(!argv_has(&fx, GATE_APPROVE_ANY), "a held guest turn starts nothing");
    fill_note(&fx.ev, fx.launch.human.pubkey_hex, "Yes", fx.root.id);
    expect(hush_store_insert(fx.store, &fx.ev) == HUSH_OK, "yes insert");
    hush_intel_consider(fx.store, &fx.launch, &fx.ev);
    expect(argv_has(&fx, GATE_APPROVE_OFF GATE_TIMEOUT_PROJECT),
           "the owner's Yes gives the guest's turn the project clock, tools still off");
    close_hive(&fx);
}

int main(void)
{
    check_owner_auto();
    check_guest_auto();
    check_guest_thread();
    check_guest_yes();
    if (g_fail)
        return 1;
    printf("cline gate ok\n");
    return 0;
}
