/* tests/test_cline_gate.c: #311 A3, #356. Every Cline turn runs with
 * "--auto-approve false", gated or not: headless Cline then denies any tool
 * call that needs a person, so no turn gets a native shell (CoS 11:00). A
 * person-gated project turn (the hive owner asked, or the owner answered Yes
 * under Approve every action) keeps only the project cwd and 300 s clock; a
 * guest's mention under the default Auto-approve keeps the 80 s chat clock.
 * Drives the real mention path on a real store and launch; a stub cline
 * first on PATH records its argv, so no real runtime ever starts. */

#define _XOPEN_SOURCE 700

#include <assert.h>
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
    /* The mkdtemp scratch under the private root (ROOT_PATH_MAX plus
     * "/hush-cline-gate-XXXXXX"); every derived path fits GATE_PATH_MAX. */
    GATE_DIR_MAX = 120,
    GATE_CWDS = 2,
    GATE_PATH_MAX = 512,
    GATE_FTW_FDS = 16,
    GATE_KIND_NOTE = 1,
    /* Polls for the stub's argv file: 100 x 20 ms = 2 s. */
    GATE_WAIT_TRIES = 100,
    GATE_WAIT_NS = 20000000
};

#define GATE_DIR_NAME "hush-cline-gate-XXXXXX"
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

enum {
    /* Longest private root path; every path a test derives from it fits. */
    ROOT_PATH_MAX = 96,
    /* Open-fd budget for the nftw cleanup walk. */
    ROOT_FTW_FDS = 16,
    /* Exit status when the root cannot be removed; atexit cannot return one. */
    ROOT_CLEANUP_FAIL_RC = 1
};

/* <TMPDIR or /tmp>/hush-cline-gate-XXXXXX: this run's private root, 0700 from mkdtemp.
 * Every key, pass store, home and config the test writes lives under it. */
#define ROOT_NAME "hush-cline-gate-XXXXXX"
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

/* A jailed child writes only its job cwd (#375 N2): a project turn's is
 * <dir>/proj, any other turn's is $HUSH_CONFIG_DIR/agent-cwd. */
static const char *const gate_cwds[GATE_CWDS] = {"proj", "config/agent-cwd"};

typedef struct {
    hush_launch_t launch;
    hush_store_t *store;
    hush_event_t root;
    hush_event_t ev;
    const hush_roster_agent_t *pat;
    char dir[GATE_DIR_MAX];
    char argv_file[GATE_CWDS][GATE_DIR_MAX * 2];
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

/* A stub cline first on PATH that writes one argv entry per line into its
 * cwd. */
static void stub_cline(gate_fixture_t *fx)
{
    char path[GATE_PATH_MAX] = {0};
    char body[GATE_PATH_MAX * 2] = {0};
    static const char argv_name[] = "cline.argv";

    snprintf(path, sizeof(path), "%s-bin", fx->dir);
    expect(mkdir(path, S_IRWXU) == 0, "stub bin dir");
    for (size_t i = 0; i < (size_t)GATE_CWDS; i++)
        snprintf(fx->argv_file[i], sizeof(fx->argv_file[i]), "%s/%s/%s", fx->dir,
                 gate_cwds[i], argv_name);
    snprintf(body, sizeof(body), "#!/bin/sh\nprintf '%%s\\n' \"$@\" > '%s.tmp'\n"
             "mv '%s.tmp' '%s'\nexit 0\n", argv_name, argv_name, argv_name);
    snprintf(path, sizeof(path), "%s-bin/cline", fx->dir);
    write_exec(path, body);
    snprintf(path, sizeof(path), "%s-bin:%s", fx->dir, GATE_SYS_PATH);
    expect(setenv("PATH", path, 1) == 0, "stub PATH");
}

/* HOME, config, pass store and HUSH_HOME under one mkdtemp root. */
static void make_scratch(gate_fixture_t *fx)
{
    char sub[GATE_PATH_MAX] = {0};

    snprintf(fx->dir, sizeof(fx->dir), "%s/%s", g_root, GATE_DIR_NAME);
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
        for (size_t i = 0; i < (size_t)GATE_CWDS; i++)
            unlink(fx->argv_file[i]);
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

    for (int i = 0; i < GATE_WAIT_TRIES * GATE_CWDS; i++) {
        const char *argv_path = fx->argv_file[i % GATE_CWDS];
        FILE *f = fopen(argv_path, "r");

        if (f != NULL) {
            size_t n = fread(buf, 1, sizeof(buf) - 1, f);

            fclose(f);
            buf[n] = '\0';
            unlink(argv_path);
            return strstr(buf, want) != NULL;
        }
        if (i % GATE_CWDS == GATE_CWDS - 1)
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
    if (!root_make())
        return 1;
    check_owner_auto();
    check_guest_auto();
    check_guest_thread();
    check_guest_yes();
    if (g_fail)
        return 1;
    printf("cline gate ok\n");
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
