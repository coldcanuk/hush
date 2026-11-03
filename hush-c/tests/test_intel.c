/* tests/test_intel.c: leash, confirm-first, hop-0, no-mention silence. */

#define _XOPEN_SOURCE 700 /* nftw, for the private root cleanup */

#include <assert.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hush_agent.h"
#include "hush_event.h"
#include "hush_intel.h"
#include "hush_launch.h"
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

/* <TMPDIR or /tmp>/hush-intel-XXXXXX: this run's private root, 0700 from mkdtemp.
 * Every key, pass store, home and config the test writes lives under it. */
#define ROOT_NAME "hush-intel-XXXXXX"
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

static void fill_note(hush_event_t *ev, const char *id, const char *pub,
                      const char *content, const char *channel,
                      const char *mention)
{
    memset(ev, 0, sizeof(*ev));
    memcpy(ev->id, id, strlen(id) + 1);
    memcpy(ev->pubkey, pub, strlen(pub) + 1);
    ev->kind = 1;
    ev->created_at = 1;
    memcpy(ev->content, content, strlen(content) + 1);
    ev->tag_count = 1;
    memcpy(ev->tags[0][0], "h", 2);
    memcpy(ev->tags[0][1], channel, strlen(channel) + 1);
    if (mention != NULL && mention[0] != '\0') {
        memcpy(ev->tags[1][0], "p", 2);
        memcpy(ev->tags[1][1], mention, strlen(mention) + 1);
        ev->tag_count = 2;
    }
}

static void fill_note_pair(hush_event_t *ev, const char *id, const char *pub,
                           const char *content, const char *channel,
                           const char *one, const char *two)
{
    fill_note(ev, id, pub, content, channel, one);
    if (two == NULL || two[0] == '\0')
        return;
    memcpy(ev->tags[2][0], "p", 2);
    memcpy(ev->tags[2][1], two, strlen(two) + 1);
    ev->tag_count = 3;
}

static size_t count_pub_needle(hush_store_t *store, const char *pub,
                               const char *needle)
{
    hush_event_t evs[32];
    size_t n;
    size_t i;
    size_t hits = 0;

    n = hush_store_query(store, NULL, 0, evs, 32);
    for (i = 0; i < n; i++) {
        if (pub != NULL && strcmp(evs[i].pubkey, pub) != 0)
            continue;
        if (strstr(evs[i].content, needle) != NULL)
            hits++;
    }
    return hits;
}

static size_t count_store(hush_store_t *store)
{
    hush_event_t evs[32];

    return hush_store_query(store, NULL, 0, evs, 32);
}

static size_t count_needle(hush_store_t *store, const char *needle)
{
    size_t total;
    size_t hits = 0;
    size_t i;

    total = hush_store_count(store);
    for (i = 0; i < total && i < (size_t)HUSH_STORE_CAPACITY; ++i) {
        hush_event_t item = {0};

        if (hush_store_get(store, i, &item) != HUSH_OK)
            break;
        if (strstr(item.content, needle) != NULL)
            hits++;
    }
    return hits;
}

static int store_has(hush_store_t *store, const char *needle)
{
    hush_event_t evs[32];
    size_t n;
    size_t i;

    n = hush_store_query(store, NULL, 0, evs, 32);
    for (i = 0; i < n; i++) {
        if (strstr(evs[i].content, needle) != NULL)
            return 1;
    }
    return 0;
}

int main(void)
{
    static hush_launch_t launch;
    hush_store_t *store = NULL;
    hush_event_t ev;
    char cfg[128];
    char pass_dir[128];

    if (!root_make())
        return 1;
    snprintf(cfg, sizeof(cfg), "%s/hush-intel-cfg-%d", g_root, (int)getpid());
    if (setenv("HUSH_CONFIG_DIR", cfg, 1) != 0)
        return 1;
    snprintf(pass_dir, sizeof(pass_dir), "%s/hush-intel-pass", g_root);
    if (setenv("HUSH_FAKE_PASS_DIR", pass_dir, 1) != 0)
        return 1;
    hush_pass_set_helper("tests/fake-pass.sh");
    hush_intel_init();
    hush_agent_init();
    hush_launch_init(&launch);
    expect(hush_store_create(&store) == HUSH_OK, "store");
    expect(hush_launch_create_identity(&launch) == HUSH_OK, "ident");
    expect(hush_launch_ack_backup(&launch, 0) == HUSH_OK, "ack");
    expect(hush_launch_create_vibe(&launch, store, "HQ", "x") == HUSH_OK,
           "vibe");

    {
        size_t before = count_store(store);

        fill_note(&ev, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                  launch.human.pubkey_hex, "hello room", "general", NULL);
        expect(hush_store_insert(store, &ev) == HUSH_OK, "plain insert");
        hush_intel_consider(store, &launch, &ev);
        expect(count_store(store) == before + 1, "no mention stays silent");
    }

    {
        hush_launch_policy_t policy;

        memset(&policy, 0, sizeof(policy));
        memcpy(policy.kind, HUSH_LAUNCH_KIND_OPEN,
               sizeof(HUSH_LAUNCH_KIND_OPEN));
        memcpy(policy.robot_reply, HUSH_LAUNCH_REPLY_OFF,
               sizeof(HUSH_LAUNCH_REPLY_OFF));
        policy.burst_ms = HUSH_LAUNCH_BURST_MS_DEFAULT;
        policy.max_jobs = HUSH_LAUNCH_MAX_JOBS_DEFAULT;
        policy.cooldown_s = HUSH_LAUNCH_COOLDOWN_S_DEFAULT;
        expect(hush_launch_set_channel_policy(&launch, "general", &policy)
                   == HUSH_OK,
               "off policy");
    }
    fill_note(&ev, "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
              launch.human.pubkey_hex, "nostr:payne please help", "general",
              launch.payne.pubkey_hex);
    expect(hush_store_insert(store, &ev) == HUSH_OK, "off insert");
    hush_intel_consider(store, &launch, &ev);
    expect(store_has(store, "humans talking"), "off posts deny");

    {
        hush_launch_policy_t policy;

        memset(&policy, 0, sizeof(policy));
        memcpy(policy.kind, HUSH_LAUNCH_KIND_OPEN,
               sizeof(HUSH_LAUNCH_KIND_OPEN));
        memcpy(policy.robot_reply, HUSH_LAUNCH_REPLY_CONFIRM,
               sizeof(HUSH_LAUNCH_REPLY_CONFIRM));
        policy.burst_ms = HUSH_LAUNCH_BURST_MS_DEFAULT;
        policy.max_jobs = HUSH_LAUNCH_MAX_JOBS_DEFAULT;
        policy.cooldown_s = HUSH_LAUNCH_COOLDOWN_S_DEFAULT;
        expect(hush_launch_set_channel_policy(&launch, "general", &policy)
                   == HUSH_OK,
               "confirm policy");
    }
    fill_note(&ev, "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
              launch.human.pubkey_hex, "nostr:payne tell a joke", "general",
              launch.payne.pubkey_hex);
    expect(hush_store_insert(store, &ev) == HUSH_OK, "confirm insert");
    hush_intel_consider(store, &launch, &ev);
    expect(store_has(store, "I heard:"), "confirm recaps");

    {
        hush_launch_policy_t policy;

        memset(&policy, 0, sizeof(policy));
        memcpy(policy.kind, HUSH_LAUNCH_KIND_OPEN,
               sizeof(HUSH_LAUNCH_KIND_OPEN));
        memcpy(policy.robot_reply, HUSH_LAUNCH_REPLY_MENTION,
               sizeof(HUSH_LAUNCH_REPLY_MENTION));
        policy.burst_ms = HUSH_LAUNCH_BURST_MS_DEFAULT;
        policy.max_jobs = HUSH_LAUNCH_MAX_JOBS_DEFAULT;
        policy.cooldown_s = HUSH_LAUNCH_COOLDOWN_S_DEFAULT;
        policy.robot_hops = 0;
        expect(hush_launch_set_channel_policy(&launch, "general", &policy)
                   == HUSH_OK,
               "hop policy");
    }
    fill_note(&ev, "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd",
              launch.payne.pubkey_hex, "nostr:payne ping yourself", "general",
              launch.payne.pubkey_hex);
    expect(hush_store_insert(store, &ev) == HUSH_OK, "hop insert");
    hush_intel_consider(store, &launch, &ev);
    expect(store_has(store, "do not chain"), "hop-0 denies robot author");

    {
        hush_roster_agent_in_t in;
        char content[512];
        const char *happy_npub;
        const char *happy_hex;
        const char *payne_npub;
        const char *payne_hex;
        hush_event_t joke;

        memset(&in, 0, sizeof(in));
        memcpy(in.name, "Happy", 6);
        memcpy(in.prompt, "Tell short jokes.", 18);
        memcpy(in.provider, HUSH_ROSTER_PROVIDER_GOOSE,
               sizeof(HUSH_ROSTER_PROVIDER_GOOSE));
        expect(hush_launch_add_agent(&launch, store, &in, 0) == HUSH_OK,
               "raise happy");
        expect(launch.roster.nagents >= 1, "happy on roster");
        happy_npub = launch.roster.agents[0].id.npub;
        happy_hex = launch.roster.agents[0].id.pubkey_hex;
        payne_npub = launch.payne.npub;
        payne_hex = launch.payne.pubkey_hex;
        expect(snprintf(content, sizeof(content),
                        "nostr:%s tell a joke nostr:%s analyze the joke",
                        happy_npub, payne_npub) < (int)sizeof(content),
               "ask fits");
        fill_note_pair(&ev,
                       "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee",
                       launch.human.pubkey_hex, content, "general",
                       happy_npub, payne_npub);
        expect(hush_store_insert(store, &ev) == HUSH_OK, "pair insert");
        hush_intel_consider(store, &launch, &ev);
        expect(!store_has(store, "Holding."), "no jobs-held chat");
        expect(count_pub_needle(store, happy_hex, "Standing orders") == 0,
               "happy posts no stock intro");
        expect(count_pub_needle(store, payne_hex, "Standing orders") == 0,
               "major posts no stock intro");
        fill_note(&joke,
                  "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
                  happy_hex, "Why did the robot laugh? Byte me.", "general",
                  NULL);
        memcpy(joke.tags[1][0], "e", 2);
        memcpy(joke.tags[1][1], ev.id, strlen(ev.id) + 1);
        joke.tag_count = 2;
        expect(hush_store_insert(store, &joke) == HUSH_OK, "joke insert");
        hush_agent_on_posted(store, &launch, &joke);
        expect(count_pub_needle(store, happy_hex, "Standing orders") == 0,
               "happy stock intro stays absent");
        expect(count_pub_needle(store, payne_hex, "Standing orders") == 0,
               "major stock intro stays absent");
        hush_agent_on_posted(store, &launch, &joke);
        expect(count_pub_needle(store, payne_hex, "Standing orders") == 0,
               "major stock intro not posted later");
    }

    {
        hush_launch_policy_t policy;
        size_t recaps;
        size_t i;

        /* Confirm-mode holds stay live; the ninth mention must be denied rather
         * than folded into slot 0 and recapped as an unrelated conversation. */
        hush_intel_init();
        hush_agent_init();
        memset(&policy, 0, sizeof(policy));
        memcpy(policy.kind, HUSH_LAUNCH_KIND_OPEN,
               sizeof(HUSH_LAUNCH_KIND_OPEN));
        memcpy(policy.robot_reply, HUSH_LAUNCH_REPLY_CONFIRM,
               sizeof(HUSH_LAUNCH_REPLY_CONFIRM));
        policy.burst_ms = HUSH_LAUNCH_BURST_MS_DEFAULT;
        policy.max_jobs = HUSH_LAUNCH_MAX_JOBS_DEFAULT;
        policy.cooldown_s = HUSH_LAUNCH_COOLDOWN_S_DEFAULT;
        expect(hush_launch_set_channel_policy(&launch, "general", &policy) ==
                   HUSH_OK,
               "overflow policy");
        recaps = count_needle(store, "I heard:");
        for (i = 0; i < (size_t)HUSH_INTEL_HOLD_MAX; ++i) {
            char id[HUSH_EVENT_ID_HEX_LEN + 1];

            memset(id, 'a', sizeof(id) - 1);
            id[0] = "0123456789abcdef"[i];
            id[HUSH_EVENT_ID_HEX_LEN] = '\0';
            fill_note(&ev, id, launch.human.pubkey_hex, "nostr:payne hold open",
                      "general", launch.payne.pubkey_hex);
            expect(hush_store_insert(store, &ev) == HUSH_OK,
                   "hold fill insert");
            hush_intel_consider(store, &launch, &ev);
        }
        expect(count_needle(store, "I heard:") ==
                   recaps + (size_t)HUSH_INTEL_HOLD_MAX,
               "eight live holds recap");
        {
            char id[HUSH_EVENT_ID_HEX_LEN + 1];

            memset(id, 'b', sizeof(id) - 1);
            id[HUSH_EVENT_ID_HEX_LEN] = '\0';
            fill_note(&ev, id, launch.human.pubkey_hex, "nostr:payne hold open",
                      "general", launch.payne.pubkey_hex);
        }
        expect(hush_store_insert(store, &ev) == HUSH_OK, "ninth insert");
        hush_intel_consider(store, &launch, &ev);
        expect(count_needle(store, "I heard:") ==
                   recaps + (size_t)HUSH_INTEL_HOLD_MAX,
               "ninth denied without recap");
        expect(count_needle(store, "Too many live conversations") == 1,
               "ninth denial posted");
    }

    {
        hush_launch_policy_t policy;
        size_t cooling;

        /* The first dispatch starts the channel cooldown; the next mention in
         * the same room is denied until it expires. */
        hush_intel_init();
        hush_agent_init();
        memset(&policy, 0, sizeof(policy));
        memcpy(policy.kind, HUSH_LAUNCH_KIND_OPEN,
               sizeof(HUSH_LAUNCH_KIND_OPEN));
        memcpy(policy.robot_reply, HUSH_LAUNCH_REPLY_MENTION,
               sizeof(HUSH_LAUNCH_REPLY_MENTION));
        policy.burst_ms = HUSH_LAUNCH_BURST_MS_DEFAULT;
        policy.max_jobs = HUSH_LAUNCH_MAX_JOBS_DEFAULT;
        policy.cooldown_s = HUSH_LAUNCH_COOLDOWN_S_LONG;
        policy.robot_hops = 1;
        expect(hush_launch_set_channel_policy(&launch, "general", &policy) ==
                   HUSH_OK,
               "cooldown policy");
        fill_note(&ev,
                  "c000000000000000000000000000000000000000000000000000000000000000",
                  launch.human.pubkey_hex, "nostr:payne first ask", "general",
                  launch.payne.pubkey_hex);
        expect(hush_store_insert(store, &ev) == HUSH_OK,
               "cooldown first insert");
        hush_intel_consider(store, &launch, &ev);
        cooling = count_needle(store, "Cooling down");
        /* A robot-triggered chain is what the cooldown throttles. */
        fill_note(&ev,
                  "c100000000000000000000000000000000000000000000000000000000000000",
                  launch.payne.pubkey_hex, "nostr:payne second ask", "general",
                  launch.payne.pubkey_hex);
        expect(hush_store_insert(store, &ev) == HUSH_OK,
               "cooldown second insert");
        hush_intel_consider(store, &launch, &ev);
        expect(count_needle(store, "Cooling down") == cooling + 1,
               "cooldown denies the second mention");
    }

    hush_store_destroy(store);
    if (g_fail)
        return 1;
    printf("test_intel ok\n");
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
