/* tests/test_provider_gate.c: #333. Copilot and Goose never get a native
 * shell or tool auto-approve, gated or not (CoS 11:00). Copilot never gets
 * --allow-all-tools; its shell is excluded and denied; only a person-gated
 * project turn (#311 A3, #322) gets --allow-tool=write. Goose always runs
 * with GOOSE_MODE=chat, overriding an inherited GOOSE_MODE=auto. Drives the
 * real mention path on a real store and launch; a stub copilot or goose first
 * on PATH under a scratch HOME, HUSH_HOME and XDG dirs records its argv and
 * env, so no real CLI ever starts. */

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
    /* One capture: the argv (prompt included), a marker, then the env. */
    GATE_CAPTURE_BYTES = 262144,
    /* Polls for the stub's capture file: 100 x 20 ms = 2 s. */
    GATE_WAIT_TRIES = 100,
    GATE_WAIT_NS = 20000000,
    GATE_HOSTILE_COUNT = 4
};

#define GATE_DIR_TEMPLATE "/tmp/hush-provider-gate-XXXXXX"
#define GATE_CHANNEL "general"
#define GATE_SYS_PATH "/usr/bin:/bin"
#define GATE_PROJECT "Site"
/* A signed-in Copilot and a configured Goose as the provider scan sees them. */
#define GATE_COPILOT_LOGIN "{\"loggedInUsers\":[{\"login\":\"test\"}]}\n"
#define GATE_GOOSE_CONFIG "GOOSE_PROVIDER: openai\nmodel: test\n"
/* Hostile inherited env: each would auto-approve tools if it reached a CLI. */
#define GATE_ENV_TRUE "true"
#define GATE_ENV_AUTO "auto"
/* Copilot argv lines (one argv entry per line) and env lines. */
#define GATE_ALLOW_ALL "\n--allow-all-tools\n"
#define GATE_YOLO "\n--allow-all\n"
#define GATE_EXCLUDED "\n--excluded-tools=bash,powershell,write_bash,"
#define GATE_DENY_PROJECT "\n--deny-tool=shell,url,memory\n"
#define GATE_DENY_CHAT "\n--deny-tool=shell,url,memory,write\n"
#define GATE_ALLOW_WRITE "\n--allow-tool=write\n"
/* Every Copilot env name that must never reach the child. */
#define GATE_ENV_ALLOW_ALL "COPILOT_ALLOW_ALL"
#define GATE_ENV_EXTENSIONS "GITHUB_COPILOT_PROMPT_MODE_EXTENSIONS"
#define GATE_ENV_HOOKS "GITHUB_COPILOT_PROMPT_MODE_REPO_HOOKS"
#define GATE_ENV_MCP "GITHUB_COPILOT_PROMPT_MODE_WORKSPACE_MCP"
#define GATE_GOOSE_CHAT "\nGOOSE_MODE=chat\n"
#define GATE_GOOSE_ANY "GOOSE_MODE="
/* A well-formed pubkey that is neither the owner nor any robot. */
#define GATE_GUEST_PUB "abababababababababababababababababababababababababababababababab"

static const char *const gate_hostile_env[GATE_HOSTILE_COUNT] = {
    GATE_ENV_ALLOW_ALL, GATE_ENV_EXTENSIONS, GATE_ENV_HOOKS, GATE_ENV_MCP
};

typedef enum {
    GATE_NO_GRANT = 0,
    GATE_GRANT = 1
} gate_want_t;

typedef enum {
    GATE_UNBOUND = 0,
    GATE_BOUND = 1
} gate_bind_t;

static int g_fail;
static char g_capture[GATE_CAPTURE_BYTES];

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
    const char *provider;
    char dir[GATE_DIR_MAX];
    char capture_file[GATE_DIR_MAX * 2];
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

/* Makes <dir>/<rel> with mode 0700. */
static void make_sub(const gate_fixture_t *fx, const char *rel)
{
    char sub[GATE_PATH_MAX] = {0};

    snprintf(sub, sizeof(sub), "%s/%s", fx->dir, rel);
    expect(mkdir(sub, S_IRWXU) == 0, rel);
}

/* Points env name at <dir>/<rel>. */
static void set_sub_env(const gate_fixture_t *fx, const char *name, const char *rel)
{
    char sub[GATE_PATH_MAX] = {0};

    snprintf(sub, sizeof(sub), "%s/%s", fx->dir, rel);
    expect(setenv(name, sub, 1) == 0, name);
}

/* A stub named fx->provider first on PATH: one argv entry per line, a
 * marker line, then its whole env, written atomically. */
static void stub_provider(gate_fixture_t *fx)
{
    char path[GATE_PATH_MAX] = {0};
    char body[GATE_PATH_MAX * 2] = {0};
    const char *cap = fx->capture_file;

    snprintf(path, sizeof(path), "%s-bin", fx->dir);
    expect(mkdir(path, S_IRWXU) == 0, "stub bin dir");
    /* Sibling of HUSH_HOME: agent children are jailed off $HUSH_HOME (#336). */
    snprintf(fx->capture_file, sizeof(fx->capture_file), "%s-bin/%s.capture", fx->dir,
             fx->provider);
    snprintf(body, sizeof(body), "#!/bin/sh\n{ printf '%%s\\n' \"$@\"; echo '--ENV--'; env; }"
             " > '%s.tmp'\nmv '%s.tmp' '%s'\nexit 0\n", cap, cap, cap);
    snprintf(path, sizeof(path), "%s-bin/%s", fx->dir, fx->provider);
    write_exec(path, body);
    snprintf(path, sizeof(path), "%s-bin:%s", fx->dir, GATE_SYS_PATH);
    expect(setenv("PATH", path, 1) == 0, "stub PATH");
}

/* The provider's sign-in or config file, under the scratch HOME or XDG. */
static void sign_in(const gate_fixture_t *fx)
{
    char path[GATE_PATH_MAX] = {0};

    if (strcmp(fx->provider, HUSH_ROSTER_PROVIDER_COPILOT) == 0) {
        make_sub(fx, ".copilot");
        snprintf(path, sizeof(path), "%s/.copilot/config.json", fx->dir);
        write_exec(path, GATE_COPILOT_LOGIN);
        return;
    }
    make_sub(fx, "xdg/goose");
    snprintf(path, sizeof(path), "%s/xdg/goose/config.yaml", fx->dir);
    write_exec(path, GATE_GOOSE_CONFIG);
}

/* Inherited env that must never reach the child as an auto-approve. */
static void set_hostile_env(void)
{
    for (size_t i = 0; i < (size_t)GATE_HOSTILE_COUNT; i++)
        expect(setenv(gate_hostile_env[i], GATE_ENV_TRUE, 1) == 0, gate_hostile_env[i]);
    expect(setenv("GOOSE_MODE", GATE_ENV_AUTO, 1) == 0, "hostile goose mode");
}

/* HOME, HUSH_HOME (<dir>/hush), XDG dirs, pass store and project under one
 * mkdtemp root; the provider stub sits beside it in <dir>-bin. */
static void make_scratch(gate_fixture_t *fx)
{
    hush_agent_copy(fx->dir, sizeof(fx->dir), GATE_DIR_TEMPLATE);
    expect(mkdtemp(fx->dir) != NULL, "mkdtemp scratch");
    expect(setenv("HOME", fx->dir, 1) == 0, "scratch HOME");
    /* HOME/.copilot is an allowlisted auth tree only outside $HUSH_HOME (#336). */
    make_sub(fx, "hush");
    set_sub_env(fx, "HUSH_HOME", "hush");
    set_sub_env(fx, "HUSH_CONFIG_DIR", "config");
    make_sub(fx, "xdg");
    set_sub_env(fx, "XDG_CONFIG_HOME", "xdg");
    make_sub(fx, "data");
    set_sub_env(fx, "XDG_DATA_HOME", "data");
    make_sub(fx, "pass");
    set_sub_env(fx, "HUSH_FAKE_PASS_DIR", "pass");
    make_sub(fx, "proj");
    hush_pass_set_helper("tests/fake-pass.sh");
    sign_in(fx);
    stub_provider(fx);
    set_hostile_env();
}

static int remove_entry(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
    (void)st;
    (void)flag;
    (void)ftw;
    return remove(path);
}

/* Owner, vibe, robot Pat on provider, project Site at <dir>/proj, mode id. */
static void open_hive(gate_fixture_t *fx, const char *provider, const char *id)
{
    hush_roster_agent_in_t in = {0};
    char proj[GATE_PATH_MAX] = {0};

    fx->provider = provider;
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
    hush_agent_copy(in.provider, sizeof(in.provider), provider);
    expect(hush_launch_add_agent(&fx->launch, fx->store, &in, 0) == HUSH_OK, "raise Pat");
    fx->pat = &fx->launch.roster.agents[fx->launch.roster.nagents - 1];
    snprintf(proj, sizeof(proj), "%s/proj", fx->dir);
    expect(hush_launch_add_project(&fx->launch, fx->store, GATE_PROJECT, proj, 0) == HUSH_OK,
           "add project");
    expect(hush_launch_set_approval(&fx->launch, id) == HUSH_OK, "set approval");
}

static void close_hive(gate_fixture_t *fx)
{
    char bin[GATE_DIR_MAX * 2] = {0};

    hush_agent_shutdown();
    hush_store_destroy(fx->store);
    snprintf(bin, sizeof(bin), "%s-bin", fx->dir);
    (void)nftw(bin, remove_entry, GATE_FTW_FDS, FTW_DEPTH | FTW_PHYS);
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

/* Binds the root's desk to the project. */
static void bind_desk(gate_fixture_t *fx)
{
    hush_thread_desk_t desk = {0};

    hush_agent_copy(desk.name, sizeof(desk.name), "Site work");
    hush_agent_copy(desk.category, sizeof(desk.category), "build");
    hush_agent_copy(desk.project, sizeof(desk.project), fx->launch.projects[0].slug);
    expect(hush_thread_desk_set(fx->root.id, &desk) == HUSH_OK, "bind desk");
}

/* Opens a thread from opener, bound to the project when bind says so. When
 * pub is the opener the opening note itself mentions Pat; otherwise pub
 * mentions Pat in a reply. Hands the mention to the relay's path. */
static void mention(gate_fixture_t *fx, const char *opener, const char *pub, gate_bind_t bind)
{
    char content[HUSH_EVENT_MAX_CONTENT] = {0};
    int opens = strcmp(opener, pub) == 0;
    hush_event_t *ask = opens ? &fx->root : &fx->ev;

    snprintf(content, sizeof(content), "nostr:%s fix the build.", fx->pat->id.npub);
    fill_note(&fx->root, opener, opens ? content : "Thread about the site.", NULL);
    if (opens)
        tag_pat(fx, &fx->root);
    expect(hush_store_insert(fx->store, &fx->root) == HUSH_OK, "root insert");
    if (bind == GATE_BOUND)
        bind_desk(fx);
    if (!opens) {
        fill_note(&fx->ev, pub, content, fx->root.id);
        tag_pat(fx, &fx->ev);
        expect(hush_store_insert(fx->store, &fx->ev) == HUSH_OK, "ask insert");
    }
    hush_agent_handle_mention(fx->store, &fx->launch, ask, fx->pat->id.npub);
}

/* Waits for the stub's capture into g_capture; returns 1 when one landed.
 * The capture starts with a newline so every argv entry is "\n<word>\n". */
static int take_capture(gate_fixture_t *fx)
{
    struct timespec tick = {0, GATE_WAIT_NS};

    memset(g_capture, 0, sizeof(g_capture));
    for (int i = 0; i < GATE_WAIT_TRIES; i++) {
        FILE *f = fopen(fx->capture_file, "r");

        if (f != NULL) {
            g_capture[0] = '\n';
            size_t n = fread(g_capture + 1, 1, sizeof(g_capture) - 2, f);

            fclose(f);
            g_capture[n + 1] = '\0';
            unlink(fx->capture_file);
            return 1;
        }
        nanosleep(&tick, NULL);
    }
    return 0;
}

static int captured(const char *want)
{
    return strstr(g_capture, want) != NULL;
}

/* 1 when the captured env holds a line starting "<name>=". */
static int captured_env(const char *name)
{
    char line[GATE_PATH_MAX] = {0};

    snprintf(line, sizeof(line), "\n%s=", name);
    return captured(line);
}

/* Copilot: never allow-all, shell excluded and denied, hostile env scrubbed;
 * writes allowed only when want is GATE_GRANT. */
static void check_copilot(gate_want_t want, const char *label)
{
    expect(!captured(GATE_ALLOW_ALL), label);
    expect(!captured(GATE_YOLO), label);
    expect(captured(GATE_EXCLUDED), label);
    for (size_t i = 0; i < (size_t)GATE_HOSTILE_COUNT; i++)
        expect(!captured_env(gate_hostile_env[i]), gate_hostile_env[i]);
    if (want == GATE_GRANT) {
        expect(captured(GATE_DENY_PROJECT) && captured(GATE_ALLOW_WRITE), label);
        return;
    }
    expect(captured(GATE_DENY_CHAT) && !captured(GATE_ALLOW_WRITE), label);
}

/* Goose: chat mode on every turn; the inherited auto never survives. */
static void check_goose(const char *label)
{
    const char *mode = strstr(g_capture, GATE_GOOSE_ANY);

    expect(captured(GATE_GOOSE_CHAT), label);
    expect(mode != NULL && strstr(mode + 1, GATE_GOOSE_ANY) == NULL, label);
    expect(!captured("\nGOOSE_MODE=auto\n"), label);
}

/* Asserts the captured turn for fx->provider against want. */
static void check_turn(gate_fixture_t *fx, gate_want_t want, const char *label)
{
    expect(take_capture(fx), label);
    if (strcmp(fx->provider, HUSH_ROSTER_PROVIDER_COPILOT) == 0)
        check_copilot(want, label);
    else
        check_goose(label);
}

/* The owner's own mention in a project thread under Auto-approve: gated. */
static void check_owner_auto(const char *provider)
{
    static gate_fixture_t fx;
    const char *owner = NULL;

    open_hive(&fx, provider, HUSH_ROSTER_APPROVAL_AUTO_ID);
    owner = fx.launch.human.pubkey_hex;
    mention(&fx, owner, owner, GATE_BOUND);
    check_turn(&fx, GATE_GRANT, "owner project turn: gated grant, never a shell");
    close_hive(&fx);
}

/* A guest's mention in the owner's project thread under Auto-approve. */
static void check_guest_auto(const char *provider)
{
    static gate_fixture_t fx;

    open_hive(&fx, provider, HUSH_ROSTER_APPROVAL_AUTO_ID);
    mention(&fx, fx.launch.human.pubkey_hex, GATE_GUEST_PUB, GATE_BOUND);
    check_turn(&fx, GATE_NO_GRANT, "guest Auto-approve turn: no grant");
    close_hive(&fx);
}

/* A thread a guest opened and bound is still no person gate. */
static void check_guest_thread(const char *provider)
{
    static gate_fixture_t fx;

    open_hive(&fx, provider, HUSH_ROSTER_APPROVAL_AUTO_ID);
    mention(&fx, GATE_GUEST_PUB, GATE_GUEST_PUB, GATE_BOUND);
    check_turn(&fx, GATE_NO_GRANT, "guest's own project thread: no grant");
    close_hive(&fx);
}

/* Under Approve every action the owner's Yes is the person gate. */
static void check_guest_yes(const char *provider)
{
    static gate_fixture_t fx;

    open_hive(&fx, provider, HUSH_ROSTER_APPROVAL_EVERY_ID);
    mention(&fx, fx.launch.human.pubkey_hex, GATE_GUEST_PUB, GATE_BOUND);
    expect(!take_capture(&fx), "a held guest turn starts nothing");
    fill_note(&fx.ev, fx.launch.human.pubkey_hex, "Yes", fx.root.id);
    expect(hush_store_insert(fx.store, &fx.ev) == HUSH_OK, "yes insert");
    hush_intel_consider(fx.store, &fx.launch, &fx.ev);
    check_turn(&fx, GATE_GRANT, "owner's Yes: gated grant, never a shell");
    close_hive(&fx);
}

/* The owner's mention in a thread bound to no project: no project tools. */
static void check_no_project(const char *provider)
{
    static gate_fixture_t fx;
    const char *owner = NULL;

    open_hive(&fx, provider, HUSH_ROSTER_APPROVAL_AUTO_ID);
    owner = fx.launch.human.pubkey_hex;
    mention(&fx, owner, owner, GATE_UNBOUND);
    check_turn(&fx, GATE_NO_GRANT, "thread with no project: no grant");
    close_hive(&fx);
}

static void check_provider(const char *provider)
{
    check_owner_auto(provider);
    check_guest_auto(provider);
    check_guest_thread(provider);
    check_guest_yes(provider);
    check_no_project(provider);
}

int main(void)
{
    check_provider(HUSH_ROSTER_PROVIDER_COPILOT);
    check_provider(HUSH_ROSTER_PROVIDER_GOOSE);
    if (g_fail)
        return 1;
    printf("provider gate ok\n");
    return 0;
}
