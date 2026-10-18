/* tests/test_provider_gate.c: #333, #356. Copilot, Goose and Grok never get
 * a native shell or tool auto-approve, gated or not (CoS 11:00).
 *   Copilot: never --allow-all-tools; the whole shell list excluded and
 *     denied; built-in MCP servers and the temp dir off; only a person-gated
 *     project turn (#311 A3, #322) gets --allow-tool=write; every hostile
 *     COPILOT_* / GITHUB_COPILOT_PROMPT_MODE_* entry scrubbed.
 *   Goose: GOOSE_MODE=chat on every turn, overriding GOOSE_MODE=auto.
 *   Grok: never --always-approve; dontAsk with Bash, MCPTool, WebFetch and
 *     WebSearch denied; Edit allowed only on a gated turn.
 *   Readiness (Grok, Copilot): a stub reporting an old version, or whose help
 *     lacks a required flag, refuses the turn and records no prompt run.
 * Drives the real mention path on a real store and launch; a stub CLI first
 * on PATH under a scratch HOME, HUSH_HOME and XDG dirs (under TMPDIR)
 * records its argv and env, so no real CLI ever starts. No mutable statics:
 * each run's state lives in a gate_run_t on main's stack. */

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
    GATE_DIR_MAX = 256,
    GATE_PATH_MAX = 1024,
    GATE_BODY_MAX = 4096,
    GATE_FTW_FDS = 16,
    GATE_KIND_NOTE = 1,
    /* One capture: the argv (prompt included), a marker, then the env. */
    GATE_CAPTURE_BYTES = 262144,
    /* Polls for the stub's capture file: 100 x 20 ms = 2 s. */
    GATE_WAIT_TRIES = 100,
    GATE_WAIT_NS = 20000000,
    GATE_HOSTILE_COUNT = 4
};

#define GATE_DIR_NAME "hush-provider-gate-XXXXXX"
#define GATE_TMP_DEFAULT "/tmp"
#define GATE_CHANNEL "general"
#define GATE_SYS_PATH "/usr/bin:/bin"
#define GATE_PROJECT "Site"
/* A signed-in Copilot, a configured Goose and a signed-in Grok as the
 * provider scan sees them. */
#define GATE_COPILOT_LOGIN "{\"loggedInUsers\":[{\"login\":\"test\"}]}\n"
#define GATE_GOOSE_CONFIG "GOOSE_PROVIDER: openai\nmodel: test\n"
#define GATE_GROK_AUTH "{\"ok\":true}\n"
/* Hostile inherited env: each would auto-approve tools if it reached a CLI. */
#define GATE_ENV_TRUE "true"
#define GATE_ENV_AUTO "auto"
/* Copilot argv lines (one argv entry per line) and env lines. */
#define GATE_ALLOW_ALL "\n--allow-all-tools\n"
#define GATE_YOLO "\n--allow-all\n"
/* The whole list, so dropping any one shell tool is caught (#355 P3-3). */
#define GATE_EXCLUDED "\n--excluded-tools=bash,powershell,write_bash,write_powershell," \
    "read_bash,read_powershell,list_bash,list_powershell,stop_bash,stop_powershell\n"
#define GATE_DENY_PROJECT "\n--deny-tool=shell,url,memory\n"
#define GATE_DENY_CHAT "\n--deny-tool=shell,url,memory,write\n"
#define GATE_ALLOW_WRITE "\n--allow-tool=write\n"
#define GATE_NO_MCP "\n--disable-builtin-mcps\n"
#define GATE_NO_TMP "\n--disallow-temp-dir\n"
/* Every Copilot env name that must never reach the child. */
#define GATE_ENV_ALLOW_ALL "COPILOT_ALLOW_ALL"
#define GATE_ENV_EXTENSIONS "GITHUB_COPILOT_PROMPT_MODE_EXTENSIONS"
#define GATE_ENV_HOOKS "GITHUB_COPILOT_PROMPT_MODE_REPO_HOOKS"
#define GATE_ENV_MCP "GITHUB_COPILOT_PROMPT_MODE_WORKSPACE_MCP"
#define GATE_GOOSE_CHAT "\nGOOSE_MODE=chat\n"
#define GATE_GOOSE_ANY "GOOSE_MODE="
/* Grok argv lines. */
#define GATE_ALWAYS "\n--always-approve\n"
#define GATE_DONT_ASK "\n--permission-mode\ndontAsk\n"
#define GATE_ALLOW_EDIT "\n--allow\nEdit\n"
#define GATE_DENY_EDIT "\n--deny\nEdit\n"
/* Readiness answers the stubs give (#356). */
#define GATE_VERSION_GOOD "1.0.0"
#define GATE_VERSION_GROK "grok 0.2.50"
#define GATE_VERSION_OLD "0.0.1"
#define GATE_HELP_COPILOT "--deny-tool --excluded-tools --allow-tool --disable-builtin-mcps " \
    "--disallow-temp-dir"
#define GATE_HELP_COPILOT_SHORT "--deny-tool --excluded-tools --allow-tool --disallow-temp-dir"
#define GATE_HELP_GROK "--permission-mode dontAsk --allow --deny --disallowed-tools"
#define GATE_HELP_GROK_SHORT "--permission-mode dontAsk --allow --deny"
/* A well-formed pubkey that is neither the owner nor any robot. */
#define GATE_GUEST_PUB "abababababababababababababababababababababababababababababababab"

static const char *const gate_hostile_env[GATE_HOSTILE_COUNT] = {
    GATE_ENV_ALLOW_ALL, GATE_ENV_EXTENSIONS, GATE_ENV_HOOKS, GATE_ENV_MCP
};

static const char *const gate_grok_denied[] = {
    "\n--deny\nBash\n", "\n--deny\nMCPTool\n", "\n--deny\nWebFetch\n", "\n--deny\nWebSearch\n"
};

typedef enum {
    GATE_NO_GRANT = 0,
    GATE_GRANT = 1
} gate_want_t;

typedef enum {
    GATE_UNBOUND = 0,
    GATE_BOUND = 1
} gate_bind_t;

/* What the stub answers to the readiness probes. */
typedef enum {
    GATE_STUB_READY = 0,
    GATE_STUB_OLD = 1,
    GATE_STUB_SHORT_HELP = 2
} gate_stub_t;

/* One test run: the failure flag, the note counter and the capture buffer. */
typedef struct {
    int fail;
    unsigned seq;
    char *capture;
} gate_run_t;

typedef struct {
    gate_run_t *run;
    hush_launch_t launch;
    hush_store_t *store;
    hush_event_t root;
    hush_event_t ev;
    const hush_roster_agent_t *pat;
    const char *provider;
    gate_stub_t stub;
    char dir[GATE_DIR_MAX];
    char capture_file[GATE_DIR_MAX * 2];
    char probe_file[GATE_DIR_MAX * 2];
} gate_fixture_t;

static void expect(gate_run_t *run, int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        run->fail = 1;
    }
}

static int is_provider(const gate_fixture_t *fx, const char *id)
{
    return strcmp(fx->provider, id) == 0;
}

/* Writes path with body and mode 0700. */
static void write_exec(gate_run_t *run, const char *path, const char *body)
{
    FILE *f = fopen(path, "w");

    expect(run, f != NULL && fputs(body, f) >= 0, "write stub");
    if (f != NULL)
        fclose(f);
    expect(run, chmod(path, S_IRWXU) == 0, "stub mode");
}

/* Makes <dir>/<rel> with mode 0700. */
static void make_sub(gate_fixture_t *fx, const char *rel)
{
    char sub[GATE_PATH_MAX] = {0};

    snprintf(sub, sizeof(sub), "%s/%s", fx->dir, rel);
    expect(fx->run, mkdir(sub, S_IRWXU) == 0, rel);
}

/* Points env name at <dir>/<rel>. */
static void set_sub_env(gate_fixture_t *fx, const char *name, const char *rel)
{
    char sub[GATE_PATH_MAX] = {0};

    snprintf(sub, sizeof(sub), "%s/%s", fx->dir, rel);
    expect(fx->run, setenv(name, sub, 1) == 0, name);
}

/* The --version text the stub prints. */
static const char *stub_version(const gate_fixture_t *fx)
{
    if (fx->stub == GATE_STUB_OLD)
        return GATE_VERSION_OLD;
    return is_provider(fx, HUSH_ROSTER_PROVIDER_GROK_BUILD) ? GATE_VERSION_GROK
                                                           : GATE_VERSION_GOOD;
}

/* The help text the stub prints; the short one lacks one required flag. */
static const char *stub_help(const gate_fixture_t *fx)
{
    int shorter = fx->stub == GATE_STUB_SHORT_HELP;

    if (is_provider(fx, HUSH_ROSTER_PROVIDER_GROK_BUILD))
        return shorter ? GATE_HELP_GROK_SHORT : GATE_HELP_GROK;
    return shorter ? GATE_HELP_COPILOT_SHORT : GATE_HELP_COPILOT;
}

/* A stub named after fx->provider first on PATH. Readiness probes log their
 * argument to probe_file and answer; any other run writes one argv entry per
 * line, a marker line, then its whole env, atomically. */
static void stub_provider(gate_fixture_t *fx, const char *bin_name)
{
    char path[GATE_PATH_MAX] = {0};
    char body[GATE_BODY_MAX] = {0};
    const char *cap = fx->capture_file;

    snprintf(path, sizeof(path), "%s-bin", fx->dir);
    expect(fx->run, mkdir(path, S_IRWXU) == 0, "stub bin dir");
    /* Siblings of HUSH_HOME: agent children are jailed off $HUSH_HOME (#336). */
    snprintf(fx->capture_file, sizeof(fx->capture_file), "%s-bin/%s.capture", fx->dir, bin_name);
    snprintf(fx->probe_file, sizeof(fx->probe_file), "%s-bin/%s.probes", fx->dir, bin_name);
    snprintf(body, sizeof(body), "#!/bin/sh\ncase \"$1\" in\n"
             "--version) echo \"$1\" >> '%s'; echo '%s'; exit 0;;\n"
             "--help|help) echo \"$1\" >> '%s'; echo '%s'; exit 0;;\nesac\n"
             "{ printf '%%s\\n' \"$@\"; echo '--ENV--'; env; } > '%s.tmp'\n"
             "mv '%s.tmp' '%s'\nexit 0\n", fx->probe_file, stub_version(fx),
             fx->probe_file, stub_help(fx), cap, cap, cap);
    snprintf(path, sizeof(path), "%s-bin/%s", fx->dir, bin_name);
    write_exec(fx->run, path, body);
    snprintf(path, sizeof(path), "%s-bin:%s", fx->dir, GATE_SYS_PATH);
    expect(fx->run, setenv("PATH", path, 1) == 0, "stub PATH");
}

/* The provider's sign-in or config file, under the scratch HOME or XDG. */
static void sign_in(gate_fixture_t *fx)
{
    char path[GATE_PATH_MAX] = {0};

    if (is_provider(fx, HUSH_ROSTER_PROVIDER_COPILOT)) {
        make_sub(fx, ".copilot");
        snprintf(path, sizeof(path), "%s/.copilot/config.json", fx->dir);
        write_exec(fx->run, path, GATE_COPILOT_LOGIN);
        return;
    }
    if (is_provider(fx, HUSH_ROSTER_PROVIDER_GROK_BUILD)) {
        make_sub(fx, ".grok");
        snprintf(path, sizeof(path), "%s/.grok/auth.json", fx->dir);
        write_exec(fx->run, path, GATE_GROK_AUTH);
        return;
    }
    make_sub(fx, "xdg/goose");
    snprintf(path, sizeof(path), "%s/xdg/goose/config.yaml", fx->dir);
    write_exec(fx->run, path, GATE_GOOSE_CONFIG);
}

/* Inherited env that must never reach the child as an auto-approve. */
static void set_hostile_env(gate_run_t *run)
{
    for (size_t i = 0; i < (size_t)GATE_HOSTILE_COUNT; i++)
        expect(run, setenv(gate_hostile_env[i], GATE_ENV_TRUE, 1) == 0, gate_hostile_env[i]);
    expect(run, setenv("GOOSE_MODE", GATE_ENV_AUTO, 1) == 0, "hostile goose mode");
}

/* <TMPDIR>/hush-provider-gate-XXXXXX (#355 P3-5: TMPDIR honoured). */
static void make_root(gate_fixture_t *fx)
{
    const char *tmp = getenv("TMPDIR");

    if (tmp == NULL || tmp[0] == '\0')
        tmp = GATE_TMP_DEFAULT;
    snprintf(fx->dir, sizeof(fx->dir), "%s/%s", tmp, GATE_DIR_NAME);
    expect(fx->run, mkdtemp(fx->dir) != NULL, "mkdtemp scratch");
}

/* HOME, HUSH_HOME (<dir>/hush), XDG dirs, pass store and project under one
 * mkdtemp root; the provider stub sits beside it in <dir>-bin. */
static void make_scratch(gate_fixture_t *fx)
{
    make_root(fx);
    expect(fx->run, setenv("HOME", fx->dir, 1) == 0, "scratch HOME");
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
    stub_provider(fx, is_provider(fx, HUSH_ROSTER_PROVIDER_GROK_BUILD) ? "grok" : fx->provider);
    set_hostile_env(fx->run);
}

static int remove_entry(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
    (void)st;
    (void)flag;
    (void)ftw;
    return remove(path);
}

/* Owner, vibe, robot Pat on provider, project Site at <dir>/proj, mode id. */
static void open_hive(gate_fixture_t *fx, const char *id)
{
    hush_roster_agent_in_t in = {0};
    char proj[GATE_PATH_MAX] = {0};
    gate_run_t *run = fx->run;

    make_scratch(fx);
    hush_intel_init();
    hush_agent_init();
    hush_launch_init(&fx->launch);
    expect(run, hush_store_create(&fx->store) == HUSH_OK, "store");
    expect(run, hush_launch_create_identity(&fx->launch) == HUSH_OK, "ident");
    expect(run, hush_launch_ack_backup(&fx->launch, 0) == HUSH_OK, "ack");
    expect(run, hush_launch_create_vibe(&fx->launch, fx->store, "HQ", "x") == HUSH_OK, "vibe");
    hush_agent_copy(in.name, sizeof(in.name), "Pat");
    hush_agent_copy(in.prompt, sizeof(in.prompt), "Build.");
    hush_agent_copy(in.provider, sizeof(in.provider), fx->provider);
    expect(run, hush_launch_add_agent(&fx->launch, fx->store, &in, 0) == HUSH_OK, "raise Pat");
    fx->pat = &fx->launch.roster.agents[fx->launch.roster.nagents - 1];
    snprintf(proj, sizeof(proj), "%s/proj", fx->dir);
    expect(run, hush_launch_add_project(&fx->launch, fx->store, GATE_PROJECT, proj, 0) == HUSH_OK,
           "add project");
    expect(run, hush_launch_set_approval(&fx->launch, id) == HUSH_OK, "set approval");
}

/* A zeroed fixture on the heap (hush_launch_t is large); NULL on failure. */
static gate_fixture_t *new_fixture(gate_run_t *run, const char *provider, gate_stub_t stub)
{
    gate_fixture_t *fx = calloc(1, sizeof(*fx));

    expect(run, fx != NULL, "fixture alloc");
    if (fx == NULL)
        return NULL;
    fx->run = run;
    fx->provider = provider;
    fx->stub = stub;
    return fx;
}

static void close_hive(gate_fixture_t *fx)
{
    char bin[GATE_DIR_MAX * 2] = {0};

    hush_agent_shutdown();
    hush_store_destroy(fx->store);
    snprintf(bin, sizeof(bin), "%s-bin", fx->dir);
    (void)nftw(bin, remove_entry, GATE_FTW_FDS, FTW_DEPTH | FTW_PHYS);
    expect(fx->run, nftw(fx->dir, remove_entry, GATE_FTW_FDS, FTW_DEPTH | FTW_PHYS) == 0,
           "scratch cleanup");
    free(fx);
}

/* Fills a kind-1 note from pub; replies to root when given. */
static void fill_note(gate_fixture_t *fx, hush_event_t *ev, const char *pub,
                      const char *content)
{
    memset(ev, 0, sizeof(*ev));
    snprintf(ev->id, sizeof(ev->id), "%0*x", HUSH_EVENT_ID_HEX_LEN, ++fx->run->seq);
    hush_agent_copy(ev->pubkey, sizeof(ev->pubkey), pub);
    ev->kind = GATE_KIND_NOTE;
    ev->created_at = 1;
    hush_agent_copy(ev->content, sizeof(ev->content), content);
    hush_agent_copy(ev->tags[0][0], sizeof(ev->tags[0][0]), "h");
    hush_agent_copy(ev->tags[0][1], sizeof(ev->tags[0][1]), GATE_CHANNEL);
    ev->tag_count = 1;
    if (ev != &fx->root) {
        hush_agent_copy(ev->tags[1][0], sizeof(ev->tags[1][0]), "e");
        hush_agent_copy(ev->tags[1][1], sizeof(ev->tags[1][1]), fx->root.id);
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
    expect(fx->run, hush_thread_desk_set(fx->root.id, &desk) == HUSH_OK, "bind desk");
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
    fill_note(fx, &fx->root, opener, opens ? content : "Thread about the site.");
    if (opens)
        tag_pat(fx, &fx->root);
    expect(fx->run, hush_store_insert(fx->store, &fx->root) == HUSH_OK, "root insert");
    if (bind == GATE_BOUND)
        bind_desk(fx);
    if (!opens) {
        fill_note(fx, &fx->ev, pub, content);
        tag_pat(fx, &fx->ev);
        expect(fx->run, hush_store_insert(fx->store, &fx->ev) == HUSH_OK, "ask insert");
    }
    hush_agent_handle_mention(fx->store, &fx->launch, ask, fx->pat->id.npub);
}

/* Reads path into the run's capture after a leading newline, so every argv
 * entry is "\n<word>\n". Returns 1 when the file was there; unlinks it. */
static int read_capture(gate_fixture_t *fx, const char *path)
{
    char *buf = fx->run->capture;
    FILE *f = fopen(path, "r");

    memset(buf, 0, GATE_CAPTURE_BYTES);
    if (f == NULL)
        return 0;
    buf[0] = '\n';
    size_t n = fread(buf + 1, 1, GATE_CAPTURE_BYTES - 2, f);

    fclose(f);
    buf[n + 1] = '\0';
    unlink(path);
    return 1;
}

/* Waits for the stub's prompt-run capture; returns 1 when one landed. */
static int take_capture(gate_fixture_t *fx)
{
    struct timespec tick = {0, GATE_WAIT_NS};

    for (int i = 0; i < GATE_WAIT_TRIES; i++) {
        if (read_capture(fx, fx->capture_file))
            return 1;
        nanosleep(&tick, NULL);
    }
    return 0;
}

static int captured(const gate_fixture_t *fx, const char *want)
{
    return strstr(fx->run->capture, want) != NULL;
}

/* 1 when the captured env holds a line starting "<name>=". */
static int captured_env(const gate_fixture_t *fx, const char *name)
{
    char line[GATE_PATH_MAX] = {0};

    snprintf(line, sizeof(line), "\n%s=", name);
    return captured(fx, line);
}

/* Copilot: never allow-all; the whole shell list excluded; shell denied;
 * built-in MCP and the temp dir off; every hostile env entry scrubbed;
 * writes allowed only when want is GATE_GRANT. */
static void check_copilot(gate_fixture_t *fx, gate_want_t want, const char *label)
{
    gate_run_t *run = fx->run;

    expect(run, !captured(fx, GATE_ALLOW_ALL) && !captured(fx, GATE_YOLO), label);
    expect(run, captured(fx, GATE_EXCLUDED), "copilot: whole excluded-tools list");
    expect(run, captured(fx, GATE_NO_MCP), "copilot: --disable-builtin-mcps (#356)");
    expect(run, captured(fx, GATE_NO_TMP), "copilot: --disallow-temp-dir (#355 P3-2)");
    for (size_t i = 0; i < (size_t)GATE_HOSTILE_COUNT; i++)
        expect(run, !captured_env(fx, gate_hostile_env[i]), gate_hostile_env[i]);
    if (want == GATE_GRANT) {
        expect(run, captured(fx, GATE_DENY_PROJECT) && captured(fx, GATE_ALLOW_WRITE), label);
        return;
    }
    expect(run, captured(fx, GATE_DENY_CHAT) && !captured(fx, GATE_ALLOW_WRITE), label);
}

/* Goose: chat mode on every turn; the inherited auto never survives. */
static void check_goose(gate_fixture_t *fx, const char *label)
{
    const char *mode = strstr(fx->run->capture, GATE_GOOSE_ANY);

    expect(fx->run, captured(fx, GATE_GOOSE_CHAT), label);
    expect(fx->run, mode != NULL && strstr(mode + 1, GATE_GOOSE_ANY) == NULL, label);
    expect(fx->run, !captured(fx, "\nGOOSE_MODE=auto\n"), label);
}

/* Grok: never always-approve; dontAsk; shell, MCP and web denied; Edit
 * allowed only when want is GATE_GRANT. */
static void check_grok(gate_fixture_t *fx, gate_want_t want, const char *label)
{
    gate_run_t *run = fx->run;

    expect(run, !captured(fx, GATE_ALWAYS), "grok: never --always-approve (#356)");
    expect(run, captured(fx, GATE_DONT_ASK), "grok: dontAsk");
    for (size_t i = 0; i < sizeof(gate_grok_denied) / sizeof(gate_grok_denied[0]); i++)
        expect(run, captured(fx, gate_grok_denied[i]), gate_grok_denied[i]);
    if (want == GATE_GRANT) {
        expect(run, captured(fx, GATE_ALLOW_EDIT) && !captured(fx, GATE_DENY_EDIT), label);
        return;
    }
    expect(run, captured(fx, GATE_DENY_EDIT) && !captured(fx, GATE_ALLOW_EDIT), label);
}

/* 1 when fx's provider must pass readiness probes before a turn. */
static int probed(const gate_fixture_t *fx)
{
    return !is_provider(fx, HUSH_ROSTER_PROVIDER_GOOSE);
}

/* Asserts the captured turn for fx->provider against want; for a probed
 * provider, both readiness probes ran first. */
static void check_turn(gate_fixture_t *fx, gate_want_t want, const char *label)
{
    expect(fx->run, take_capture(fx), label);
    if (is_provider(fx, HUSH_ROSTER_PROVIDER_COPILOT))
        check_copilot(fx, want, label);
    else if (is_provider(fx, HUSH_ROSTER_PROVIDER_GROK_BUILD))
        check_grok(fx, want, label);
    else
        check_goose(fx, label);
    if (!probed(fx))
        return;
    expect(fx->run, read_capture(fx, fx->probe_file) && captured(fx, "\n--version\n") &&
           (captured(fx, "\n--help\n") || captured(fx, "\nhelp\n")), "readiness probes ran");
}

/* The owner's own mention in a project thread under Auto-approve: gated. */
static void check_owner_auto(gate_run_t *run, const char *provider)
{
    gate_fixture_t *fx = new_fixture(run, provider, GATE_STUB_READY);

    if (fx == NULL)
        return;
    open_hive(fx, HUSH_ROSTER_APPROVAL_AUTO_ID);
    mention(fx, fx->launch.human.pubkey_hex, fx->launch.human.pubkey_hex, GATE_BOUND);
    check_turn(fx, GATE_GRANT, "owner project turn: gated grant, never a shell");
    close_hive(fx);
}

/* A guest's mention in the owner's project thread under Auto-approve. */
static void check_guest_auto(gate_run_t *run, const char *provider)
{
    gate_fixture_t *fx = new_fixture(run, provider, GATE_STUB_READY);

    if (fx == NULL)
        return;
    open_hive(fx, HUSH_ROSTER_APPROVAL_AUTO_ID);
    mention(fx, fx->launch.human.pubkey_hex, GATE_GUEST_PUB, GATE_BOUND);
    check_turn(fx, GATE_NO_GRANT, "guest Auto-approve turn: no grant");
    close_hive(fx);
}

/* A thread a guest opened and bound is still no person gate. */
static void check_guest_thread(gate_run_t *run, const char *provider)
{
    gate_fixture_t *fx = new_fixture(run, provider, GATE_STUB_READY);

    if (fx == NULL)
        return;
    open_hive(fx, HUSH_ROSTER_APPROVAL_AUTO_ID);
    mention(fx, GATE_GUEST_PUB, GATE_GUEST_PUB, GATE_BOUND);
    check_turn(fx, GATE_NO_GRANT, "guest's own project thread: no grant");
    close_hive(fx);
}

/* Under Approve every action the owner's Yes is the person gate. */
static void check_guest_yes(gate_run_t *run, const char *provider)
{
    gate_fixture_t *fx = new_fixture(run, provider, GATE_STUB_READY);

    if (fx == NULL)
        return;
    open_hive(fx, HUSH_ROSTER_APPROVAL_EVERY_ID);
    mention(fx, fx->launch.human.pubkey_hex, GATE_GUEST_PUB, GATE_BOUND);
    expect(run, !take_capture(fx), "a held guest turn starts nothing");
    fill_note(fx, &fx->ev, fx->launch.human.pubkey_hex, "Yes");
    expect(run, hush_store_insert(fx->store, &fx->ev) == HUSH_OK, "yes insert");
    hush_intel_consider(fx->store, &fx->launch, &fx->ev);
    check_turn(fx, GATE_GRANT, "owner's Yes: gated grant, never a shell");
    close_hive(fx);
}

/* The owner's mention in a thread bound to no project: no project tools. */
static void check_no_project(gate_run_t *run, const char *provider)
{
    gate_fixture_t *fx = new_fixture(run, provider, GATE_STUB_READY);

    if (fx == NULL)
        return;
    open_hive(fx, HUSH_ROSTER_APPROVAL_AUTO_ID);
    mention(fx, fx->launch.human.pubkey_hex, fx->launch.human.pubkey_hex, GATE_UNBOUND);
    check_turn(fx, GATE_NO_GRANT, "thread with no project: no grant");
    close_hive(fx);
}

/* An unready CLI (stub says so) refuses the owner's gated turn: the probe
 * that fails ran, and no prompt run was recorded (#356). */
static void check_unready(gate_run_t *run, const char *provider, gate_stub_t stub)
{
    gate_fixture_t *fx = new_fixture(run, provider, stub);
    const char *probe = stub == GATE_STUB_OLD ? "\n--version\n" : "\n--help\n";

    if (fx == NULL)
        return;
    if (stub == GATE_STUB_SHORT_HELP && is_provider(fx, HUSH_ROSTER_PROVIDER_COPILOT))
        probe = "\nhelp\n";
    open_hive(fx, HUSH_ROSTER_APPROVAL_AUTO_ID);
    mention(fx, fx->launch.human.pubkey_hex, fx->launch.human.pubkey_hex, GATE_BOUND);
    expect(run, !take_capture(fx), stub == GATE_STUB_OLD ? "old version: no prompt run"
                                                         : "short help: no prompt run");
    expect(run, read_capture(fx, fx->probe_file) && captured(fx, probe),
           "the failing readiness probe ran");
    close_hive(fx);
}

static void check_provider(gate_run_t *run, const char *provider)
{
    check_owner_auto(run, provider);
    check_guest_auto(run, provider);
    check_guest_thread(run, provider);
    check_guest_yes(run, provider);
    check_no_project(run, provider);
    if (strcmp(provider, HUSH_ROSTER_PROVIDER_GOOSE) == 0)
        return;
    check_unready(run, provider, GATE_STUB_OLD);
    check_unready(run, provider, GATE_STUB_SHORT_HELP);
}

int main(void)
{
    gate_run_t run = {0, 0, NULL};

    run.capture = calloc(1, GATE_CAPTURE_BYTES);
    if (run.capture == NULL)
        return 1;
    check_provider(&run, HUSH_ROSTER_PROVIDER_COPILOT);
    check_provider(&run, HUSH_ROSTER_PROVIDER_GOOSE);
    check_provider(&run, HUSH_ROSTER_PROVIDER_GROK_BUILD);
    free(run.capture);
    if (run.fail)
        return 1;
    printf("provider gate ok\n");
    return 0;
}
