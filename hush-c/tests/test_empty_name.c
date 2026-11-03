/* tests/test_empty_name.c: #285 an empty first name is not "you". */

#define _XOPEN_SOURCE 700

#include <assert.h>
#include <ftw.h>
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

#define EMPTY_DIR_NAME "hush-empty-name-XXXXXX"
#define EMPTY_CHANNEL "general"
#define EMPTY_FORBIDDEN "You are speaking to you."
#define EMPTY_NEUTRAL "You are speaking to the owner."
#define EMPTY_NAMED "You are speaking to Ada."
#define EMPTY_RULE_YOU "Speak to you."
#define EMPTY_RULE_OWNER "Speak to the owner."
#define EMPTY_FROM_YOU "from you:"
#define EMPTY_FROM_OWNER "from the owner:"

enum {
    /* Longest private root path; every path a test derives from it fits. */
    ROOT_PATH_MAX = 96,
    /* Open-fd budget for the nftw cleanup walk. */
    ROOT_FTW_FDS = 16,
    /* Exit status when the root cannot be removed; atexit cannot return one. */
    ROOT_CLEANUP_FAIL_RC = 1
};

/* <TMPDIR or /tmp>/hush-empty-name-XXXXXX: this run's private root, 0700 from mkdtemp.
 * Every key, pass store, home and config the test writes lives under it. */
#define ROOT_NAME "hush-empty-name-XXXXXX"
#define ROOT_TMP_DEFAULT "/tmp"

/* The private root and the pid that made it. File scope because an atexit
 * handler takes no argument. Empty until root_make succeeds and again after
 * root_remove. */
static char g_root[ROOT_PATH_MAX];
static pid_t g_root_pid;

/* Makes g_root under root_tmpdir() (/tmp when that is NULL); root_remove,
 * registered with atexit, removes it on every return from main and every
 * exit(). Returns 0, after a FAIL line, when root_self_check
 * fails, the path does not fit, mkdtemp fails, atexit
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

/* Self-test of the two path checks, run before the root is made: returns
 * 0, after a FAIL line, when root_holds or root_is_under_tmpdir accepts a
 * prefix sibling ("/tmp/xprivate" for "/tmp/x"). A real root never sits in
 * such a sibling, so nothing else exercises that boundary. */
static int root_self_check(void);

/* True when tmpdir is NULL, empty or relative (root_make then uses /tmp), or
 * when root lies under tmpdir by whole path components. A root made anywhere
 * else (a fixed /tmp, say) escapes the run's own cleanup and leak scan.
 * root_make passes getenv("TMPDIR") here, never root_tmpdir(), so a fault in
 * the leaf that placed the root cannot also blind this check. */
static int root_is_under_tmpdir(const char *tmpdir, const char *root);

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

    if (!root_make())
        return 1;
    snprintf(dir, sizeof(dir), "%s/%s", g_root, EMPTY_DIR_NAME);
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

static int root_make(void)
{
    const char *base_dir = root_tmpdir();
    int wrote = 0;

    assert(g_root[0] == '\0');
    if (!root_self_check())
        return 0;
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
    if (!root_is_under_tmpdir(getenv("TMPDIR"), g_root)) {
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

static int root_self_check(void)
{
    /* nftw never hands root_remove_entry a path outside the root, so this
     * direct check is the only place the prefix-sibling case is exercised. */
    if (!root_holds("/r", "/r") || !root_holds("/r", "/r/k") ||
        root_holds("/r", "/rX/k") || root_holds("/r", "/rX")) {
        fprintf(stderr, "FAIL root_holds accepts a prefix sibling\n");
        return 0;
    }
    if (!root_is_under_tmpdir("/tmp/x", "/tmp/x/hush-k") ||
        root_is_under_tmpdir("/tmp/x", "/tmp/xprivate") ||
        root_is_under_tmpdir("/tmp/x", "/tmp/xprivate/hush-k")) {
        fprintf(stderr, "FAIL root_is_under_tmpdir accepts a prefix sibling\n");
        return 0;
    }
    return 1;
}

static int root_is_under_tmpdir(const char *tmpdir, const char *root)
{
    assert(root != NULL && root[0] != '\0');
    /* The same absolute test as root_tmpdir, repeated on purpose: the check
     * must not share the leaf it checks. No trailing-slash strip is needed:
     * root is "<tmpdir>/<name>", so tmpdir "X/" gives "X//<name>", which
     * root_holds("X/", ...) already accepts. */
    if (tmpdir == NULL || tmpdir[0] != '/')
        return 1;
    return root_holds(tmpdir, root);
}

static const char *root_tmpdir(void)
{
    const char *env = getenv("TMPDIR");
    if (env == NULL || env[0] != '/')
        return NULL;
    return env;
}
