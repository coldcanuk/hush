/* tests/test_approve.c: #279 approval setting. Parses the stored ids (an
 * unknown or garbled one reads as auto-approve), keeps approval lines out of
 * the turn count, and drives the gate on a real store and launch: only the
 * hive owner's Yes runs a waiting turn, No and other owner notes start
 * nothing, Auto-approve never asks, and the waiting table has a cap.
 * r2: answers go oldest-first per thread (two robots), a Yes or a void
 * touches only its own thread, the table holds exactly 8 with at most 4
 * asked for by other people, and every way a held follow or loop turn ends
 * gives back its in-flight count (so more than 8 voided threads still get
 * their next wave).
 * Hermetic: HOME is the scratch dir and a stub goose that exits at once
 * shadows any installed one, so no real runtime ever starts. */

#define _XOPEN_SOURCE 700

#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hush_agent.h"
#include "hush_agent_internal.h"
#include "hush_event.h"
#include "hush_intel.h"
#include "hush_launch.h"
#include "hush_pass.h"
#include "hush_roster.h"
#include "hush_store.h"

enum {
    TEST_APPROVE_SCAN_MAX = 128,
    TEST_APPROVE_STATUS_MAX = 4096,
    TEST_APPROVE_PATH_MAX = 256,
    TEST_APPROVE_FTW_FDS = 16,
    /* NIP-01 kind 1: a text note. */
    TEST_APPROVE_KIND_NOTE = 1,
    /* An approval mode value that names no mode. */
    TEST_APPROVE_BAD_MODE = 42
};

#define TEST_APPROVE_DIR_TEMPLATE "/tmp/hush-approve-XXXXXX"
#define TEST_APPROVE_CHANNEL "general"
/* The exact copy the owner reads (#279). */
#define TEST_APPROVE_ASK_COPY \
    "Approval needed: Happy wants to take a turn. Reply Yes or No in this thread."
#define TEST_APPROVE_NO_COPY "Turn declined: Happy stood down."
#define TEST_APPROVE_FULL_COPY "Too many turns are waiting for approval. Answer one first."
/* The stub runtime: first on PATH, exits without a word. */
#define TEST_APPROVE_STUB "#!/bin/sh\nexit 0\n"
#define TEST_APPROVE_SYS_PATH "/usr/bin:/bin"
/* note_no_runtime's opening for Happy. */
#define TEST_APPROVE_RAN_HEAD "No selected provider is ready for Happy."
#define TEST_APPROVE_RAN_SCOUT "No selected provider is ready for Scout."
#define TEST_APPROVE_ASK_SCOUT \
    "Approval needed: Scout wants to take a turn. Reply Yes or No in this thread."
#define TEST_APPROVE_NO_SCOUT "Turn declined: Scout stood down."
/* The literal table size and guest share the docs promise (#279 r2). */
#define TEST_APPROVE_TABLE 8
#define TEST_APPROVE_GUESTS 4
/* More voided threads than there are follow slots (8). */
#define TEST_APPROVE_THREADS 10
/* A well-formed pubkey that is neither the owner nor any robot. */
#define TEST_APPROVE_STRANGER_PUB \
    "abababababababababababababababababababababababababababababababab"

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

/* One gate run: launch, store, robot Happy, the current note, scratch dir. */
typedef struct {
    hush_launch_t launch;
    hush_event_t root;
    hush_event_t ev;
    hush_store_t *store;
    const hush_roster_agent_t *happy;
    const hush_roster_agent_t *scout;
    char dir[TEST_APPROVE_PATH_MAX];
} test_approve_fixture_t;

/* Counts stored notes whose content starts with head. */
static size_t count_head(hush_store_t *store, const char *head)
{
    static hush_event_t evs[TEST_APPROVE_SCAN_MAX];
    size_t n = hush_store_query(store, NULL, 0, evs, TEST_APPROVE_SCAN_MAX);
    size_t hits = 0;

    for (size_t i = 0; i < n; i++) {
        if (strncmp(evs[i].content, head, strlen(head)) == 0)
            hits++;
    }
    return hits;
}

/* Counts stored notes whose content equals line exactly. */
static size_t count_line(hush_store_t *store, const char *line)
{
    static hush_event_t evs[TEST_APPROVE_SCAN_MAX];
    size_t n = hush_store_query(store, NULL, 0, evs, TEST_APPROVE_SCAN_MAX);
    size_t hits = 0;

    for (size_t i = 0; i < n; i++) {
        if (strcmp(evs[i].content, line) == 0)
            hits++;
    }
    return hits;
}

/* True when begin_work went past the gate: it started a job (the busy-job
 * list is not empty) or reported that no runtime is ready. Every check opens
 * a fresh job table with hush_agent_init. */
static int turn_ran(hush_store_t *store)
{
    char status[TEST_APPROVE_STATUS_MAX] = {0};

    hush_agent_status(status, sizeof(status));
    return strcmp(status, "[]") != 0 || count_head(store, TEST_APPROVE_RAN_HEAD) > 0;
}

static size_t count_all(hush_store_t *store)
{
    static hush_event_t evs[TEST_APPROVE_SCAN_MAX];

    return hush_store_query(store, NULL, 0, evs, TEST_APPROVE_SCAN_MAX);
}

/* Known ids map to their mode; anything else reads as auto-approve. */
static void check_parse(void)
{
    hush_roster_approval_t mode = HUSH_ROSTER_APPROVAL_EVERY;

    expect(hush_roster_approval_parse(HUSH_ROSTER_APPROVAL_AUTO_ID, &mode) == 1 &&
           mode == HUSH_ROSTER_APPROVAL_AUTO, "auto_approve parses");
    expect(hush_roster_approval_parse(HUSH_ROSTER_APPROVAL_EVERY_ID, &mode) == 1 &&
           mode == HUSH_ROSTER_APPROVAL_EVERY, "approve_every_action parses");
    mode = HUSH_ROSTER_APPROVAL_EVERY;
    expect(hush_roster_approval_parse("maybe", &mode) == 0 &&
           mode == HUSH_ROSTER_APPROVAL_AUTO, "an unknown id reads as auto-approve");
    mode = HUSH_ROSTER_APPROVAL_EVERY;
    expect(hush_roster_approval_parse("", &mode) == 0 &&
           mode == HUSH_ROSTER_APPROVAL_AUTO, "an empty id reads as auto-approve");
    mode = HUSH_ROSTER_APPROVAL_EVERY;
    expect(hush_roster_approval_parse(NULL, &mode) == 0 &&
           mode == HUSH_ROSTER_APPROVAL_AUTO, "a missing id reads as auto-approve");
    expect(strcmp(hush_roster_approval_id(HUSH_ROSTER_APPROVAL_EVERY), "approve_every_action") == 0,
           "approve_every_action id");
    expect(strcmp(hush_roster_approval_id(HUSH_ROSTER_APPROVAL_AUTO), "auto_approve") == 0,
           "auto_approve id");
    expect(strcmp(hush_roster_approval_id((hush_roster_approval_t)TEST_APPROVE_BAD_MODE),
                  "auto_approve") == 0, "a bad mode writes auto_approve");
}

/* Approval lines are notices, never robot turns, so the cap is unchanged. */
static void check_not_turns(void)
{
    expect(!hush_agent_is_work_note(TEST_APPROVE_ASK_COPY), "the approval line is not a turn");
    expect(!hush_agent_is_work_note(TEST_APPROVE_NO_COPY), "the decline line is not a turn");
    expect(!hush_agent_is_work_note(TEST_APPROVE_FULL_COPY), "the full notice is not a turn");
    expect(hush_agent_is_work_note("Approve the plan first, then build."),
           "an ordinary reply is still a turn");
}

/* Fills a kind-1 note on the test channel, replying to root when given. */
static void fill_note(hush_event_t *ev, const char *pub, const char *content,
                      const char *root)
{
    static unsigned seq;

    memset(ev, 0, sizeof(*ev));
    snprintf(ev->id, sizeof(ev->id), "%0*x", HUSH_EVENT_ID_HEX_LEN, ++seq);
    hush_agent_copy(ev->pubkey, sizeof(ev->pubkey), pub);
    ev->kind = TEST_APPROVE_KIND_NOTE;
    ev->created_at = 1;
    hush_agent_copy(ev->content, sizeof(ev->content), content);
    hush_agent_copy(ev->tags[0][0], sizeof(ev->tags[0][0]), "h");
    hush_agent_copy(ev->tags[0][1], sizeof(ev->tags[0][1]), TEST_APPROVE_CHANNEL);
    ev->tag_count = 1;
    if (root != NULL) {
        hush_agent_copy(ev->tags[1][0], sizeof(ev->tags[1][0]), "e");
        hush_agent_copy(ev->tags[1][1], sizeof(ev->tags[1][1]), root);
        ev->tag_count = 2;
    }
}

/* Puts a stub goose first on PATH and HOME in the scratch root. */
static void stub_runtime(const test_approve_fixture_t *fx)
{
    char path[TEST_APPROVE_PATH_MAX * 2] = {0};
    FILE *f = NULL;

    snprintf(path, sizeof(path), "%s/bin", fx->dir);
    expect(mkdir(path, S_IRWXU) == 0, "stub bin dir");
    snprintf(path, sizeof(path), "%s/bin/goose", fx->dir);
    f = fopen(path, "w");
    expect(f != NULL && fputs(TEST_APPROVE_STUB, f) >= 0, "stub goose");
    if (f != NULL)
        fclose(f);
    expect(chmod(path, S_IRWXU) == 0, "stub goose mode");
    snprintf(path, sizeof(path), "%s/bin:%s", fx->dir, TEST_APPROVE_SYS_PATH);
    expect(setenv("PATH", path, 1) == 0, "stub PATH");
    expect(setenv("HOME", fx->dir, 1) == 0, "scratch HOME");
}

/* Points config and the fake pass store at fresh dirs under a mkdtemp root. */
static void make_scratch(test_approve_fixture_t *fx)
{
    char sub[TEST_APPROVE_PATH_MAX + sizeof("/config")] = {0};

    hush_agent_copy(fx->dir, sizeof(fx->dir), TEST_APPROVE_DIR_TEMPLATE);
    expect(mkdtemp(fx->dir) != NULL, "mkdtemp scratch");
    stub_runtime(fx);
    snprintf(sub, sizeof(sub), "%s/config", fx->dir);
    expect(setenv("HUSH_CONFIG_DIR", sub, 1) == 0, "cfg env");
    snprintf(sub, sizeof(sub), "%s/pass", fx->dir);
    expect(mkdir(sub, S_IRWXU) == 0, "pass dir");
    expect(setenv("HUSH_FAKE_PASS_DIR", sub, 1) == 0, "pass env");
    hush_pass_set_helper("tests/fake-pass.sh");
}

/* nftw callback: removes one entry; FTW_DEPTH visits children first. */
static int remove_entry(const char *path, const struct stat *st, int flag,
                        struct FTW *ftw)
{
    (void)st;
    (void)flag;
    (void)ftw;
    return remove(path);
}

/* Raises a robot on uninstalled goose, so no job ever spawns. */
static const hush_roster_agent_t *raise_robot(test_approve_fixture_t *fx, const char *name)
{
    hush_roster_agent_in_t in = {0};

    hush_agent_copy(in.name, sizeof(in.name), name);
    hush_agent_copy(in.prompt, sizeof(in.prompt), "Tell jokes.");
    hush_agent_copy(in.provider, sizeof(in.provider), HUSH_ROSTER_PROVIDER_GOOSE);
    expect(hush_launch_add_agent(&fx->launch, fx->store, &in, 0) == HUSH_OK, "raise robot");
    return &fx->launch.roster.agents[fx->launch.roster.nagents - 1];
}

/* Opens the hive (owner, vibe, Happy and Scout on uninstalled goose) in
 * mode id. */
static void open_hive(test_approve_fixture_t *fx, const char *id)
{
    make_scratch(fx);
    hush_intel_init();
    hush_agent_init();
    hush_launch_init(&fx->launch);
    expect(hush_store_create(&fx->store) == HUSH_OK, "store");
    expect(hush_launch_create_identity(&fx->launch) == HUSH_OK, "ident");
    expect(hush_launch_ack_backup(&fx->launch, 0) == HUSH_OK, "ack");
    expect(hush_launch_create_vibe(&fx->launch, fx->store, "HQ", "x") == HUSH_OK, "vibe");
    fx->happy = raise_robot(fx, "Happy");
    fx->scout = raise_robot(fx, "Scout");
    expect(hush_launch_set_approval(&fx->launch, id) == HUSH_OK, "set approval");
}

static void close_hive(test_approve_fixture_t *fx)
{
    hush_agent_shutdown();
    hush_store_destroy(fx->store);
    expect(nftw(fx->dir, remove_entry, TEST_APPROVE_FTW_FDS, FTW_DEPTH | FTW_PHYS) == 0,
           "scratch cleanup");
}

/* Stores an owner note that mentions Happy and hands it to the mention path. */
static void mention_happy(test_approve_fixture_t *fx, const char *text)
{
    char content[HUSH_EVENT_MAX_CONTENT] = {0};
    size_t at = 0;

    snprintf(content, sizeof(content), "nostr:%s %s", fx->happy->id.npub, text);
    fill_note(&fx->root, fx->launch.human.pubkey_hex, content, NULL);
    at = fx->root.tag_count;
    hush_agent_copy(fx->root.tags[at][0], sizeof(fx->root.tags[at][0]), "p");
    hush_agent_copy(fx->root.tags[at][1], sizeof(fx->root.tags[at][1]), fx->happy->id.npub);
    fx->root.tag_count = at + 1;
    expect(hush_store_insert(fx->store, &fx->root) == HUSH_OK, "root insert");
    hush_agent_handle_mention(fx->store, &fx->launch, &fx->root, fx->happy->id.npub);
}

/* Stores a reply from pub in thread root and hands it to the relay's
 * consider path. */
static void answer_in(test_approve_fixture_t *fx, const char *root, const char *pub,
                      const char *text)
{
    fill_note(&fx->ev, pub, text, root);
    expect(hush_store_insert(fx->store, &fx->ev) == HUSH_OK, "answer insert");
    hush_intel_consider(fx->store, &fx->launch, &fx->ev);
}

/* Stores a thread reply from pub in the fixture's thread. */
static void answer(test_approve_fixture_t *fx, const char *pub, const char *text)
{
    answer_in(fx, fx->root.id, pub, text);
}

/* A robot or another human typing Yes is refused; the owner's No declines
 * and nothing runs. */
static void check_owner_only(void)
{
    static test_approve_fixture_t fx;
    size_t before = 0;

    open_hive(&fx, HUSH_ROSTER_APPROVAL_EVERY_ID);
    mention_happy(&fx, "tell a joke");
    expect(count_line(fx.store, TEST_APPROVE_ASK_COPY) == 1, "the turn waits with one line");
    expect(!turn_ran(fx.store), "a waiting turn does not run");
    before = count_all(fx.store);
    answer(&fx, TEST_APPROVE_STRANGER_PUB, "Yes");
    answer(&fx, fx.happy->id.pubkey_hex, "Yes.");
    expect(!turn_ran(fx.store), "a non-owner Yes is refused");
    expect(count_all(fx.store) == before + 2, "refused answers post nothing");
    answer(&fx, fx.launch.human.pubkey_hex, "No");
    expect(count_line(fx.store, TEST_APPROVE_NO_COPY) == 1, "the owner's No declines");
    expect(!turn_ran(fx.store), "a declined turn does not run");
    answer(&fx, fx.launch.human.pubkey_hex, "yes");
    expect(!turn_ran(fx.store), "a Yes after No runs nothing");
    close_hive(&fx);
}

/* The owner's Yes runs the waiting turn through begin_work once. */
static void check_owner_yes(void)
{
    static test_approve_fixture_t fx;

    open_hive(&fx, HUSH_ROSTER_APPROVAL_EVERY_ID);
    mention_happy(&fx, "tell a riddle");
    answer(&fx, fx.launch.human.pubkey_hex, "Yes");
    expect(turn_ran(fx.store), "the owner's Yes runs the turn");
    expect(count_line(fx.store, TEST_APPROVE_ASK_COPY) == 1, "an approved turn asks once");
    expect(count_head(fx.store, HUSH_AGENT_APPROVAL_NO_HEAD) == 0, "Yes declines nothing");
    close_hive(&fx);
}

/* Auto-approve (today's behaviour) never asks. */
static void check_auto(void)
{
    static test_approve_fixture_t fx;

    open_hive(&fx, HUSH_ROSTER_APPROVAL_AUTO_ID);
    mention_happy(&fx, "say hi");
    expect(count_head(fx.store, HUSH_AGENT_APPROVAL_ASK_HEAD) == 0, "Auto-approve does not ask");
    expect(turn_ran(fx.store), "Auto-approve runs at once");
    expect(hush_launch_set_approval(&fx.launch, "maybe") == HUSH_ERR_PARSE,
           "an unknown id is refused");
    expect(fx.launch.roster.profile.approval == HUSH_ROSTER_APPROVAL_AUTO,
           "a refused id changes nothing");
    close_hive(&fx);
}

/* Another owner note voids the waiting turn; a later Yes runs nothing. */
static void check_void(void)
{
    static test_approve_fixture_t fx;

    open_hive(&fx, HUSH_ROSTER_APPROVAL_EVERY_ID);
    mention_happy(&fx, "count to three");
    answer(&fx, fx.launch.human.pubkey_hex, "actually, never mind");
    answer(&fx, fx.launch.human.pubkey_hex, "Yes");
    expect(!turn_ran(fx.store), "a voided turn never runs");
    close_hive(&fx);
}

/* Two turns wait in one thread: each answer settles one, oldest first. */
static void check_one_each(void)
{
    static test_approve_fixture_t fx;

    open_hive(&fx, HUSH_ROSTER_APPROVAL_EVERY_ID);
    mention_happy(&fx, "two jokes please");
    hush_agent_handle_mention(fx.store, &fx.launch, &fx.root, fx.happy->id.npub);
    expect(count_line(fx.store, TEST_APPROVE_ASK_COPY) == 2, "two turns wait in one thread");
    answer(&fx, fx.launch.human.pubkey_hex, "No");
    expect(count_line(fx.store, TEST_APPROVE_NO_COPY) == 1, "one No declines one turn");
    expect(!turn_ran(fx.store), "the other turn still waits");
    answer(&fx, fx.launch.human.pubkey_hex, "Yes");
    expect(turn_ran(fx.store), "the next Yes runs the turn still waiting");
    close_hive(&fx);
}

/* Caps the test channel at one robot turn per thread (HUSH_LAUNCH_TURNS_MIN). */
static void cap_one(test_approve_fixture_t *fx)
{
    hush_launch_policy_t policy = {0};

    hush_agent_copy(policy.kind, sizeof(policy.kind), HUSH_LAUNCH_KIND_OPEN);
    hush_agent_copy(policy.robot_reply, sizeof(policy.robot_reply), HUSH_LAUNCH_REPLY_MENTION);
    policy.burst_ms = HUSH_LAUNCH_BURST_MS_DEFAULT;
    policy.max_jobs = HUSH_LAUNCH_MAX_JOBS_DEFAULT;
    policy.max_robot_turns = HUSH_LAUNCH_TURNS_MIN;
    expect(hush_launch_set_channel_policy(&fx->launch, TEST_APPROVE_CHANNEL, &policy) == HUSH_OK,
           "cap 1");
}

/* Caps the test channel at two robot turns per thread. */
static void cap_two(test_approve_fixture_t *fx)
{
    hush_launch_policy_t policy = {0};

    hush_agent_copy(policy.kind, sizeof(policy.kind), HUSH_LAUNCH_KIND_OPEN);
    hush_agent_copy(policy.robot_reply, sizeof(policy.robot_reply), HUSH_LAUNCH_REPLY_MENTION);
    policy.burst_ms = HUSH_LAUNCH_BURST_MS_DEFAULT;
    policy.max_jobs = HUSH_LAUNCH_MAX_JOBS_DEFAULT;
    policy.max_robot_turns = HUSH_LAUNCH_TURNS_MIN + 1;
    expect(hush_launch_set_channel_policy(&fx->launch, TEST_APPROVE_CHANNEL, &policy) == HUSH_OK,
           "cap 2");
}

/* With the cap at one turn, a waiting turn's approval line must not use it up:
 * a second turn in the same thread still asks instead of hitting the cap. */
static void check_cap_window(void)
{
    static test_approve_fixture_t fx;

    open_hive(&fx, HUSH_ROSTER_APPROVAL_EVERY_ID);
    cap_one(&fx);
    mention_happy(&fx, "two riddles please");
    hush_agent_handle_mention(fx.store, &fx.launch, &fx.root, fx.happy->id.npub);
    expect(count_line(fx.store, TEST_APPROVE_ASK_COPY) == 2,
           "an approval line does not count toward the turn cap");
    expect(!turn_ran(fx.store), "both turns still wait");
    close_hive(&fx);
}

/* Exactly 8 turns wait (the documented number, not the constant); the
 * ninth gets one notice. */
static void check_full(void)
{
    static test_approve_fixture_t fx;
    char text[TEST_APPROVE_PATH_MAX] = {0};

    open_hive(&fx, HUSH_ROSTER_APPROVAL_EVERY_ID);
    for (int i = 0; i < TEST_APPROVE_TABLE + 1; i++) {
        snprintf(text, sizeof(text), "joke number %d", i);
        mention_happy(&fx, text);
    }
    expect(count_line(fx.store, TEST_APPROVE_ASK_COPY) == TEST_APPROVE_TABLE,
           "the table holds exactly 8 turns");
    expect(count_line(fx.store, TEST_APPROVE_FULL_COPY) == 1, "the next turn gets one notice");
    expect(!turn_ran(fx.store), "a turn over the cap does not run");
    close_hive(&fx);
}

/* Opens a thread from pub naming bot (and bot2 when set, as an explicit
 * two-robot note) and hands every mention to the mention path. */
static void open_thread(test_approve_fixture_t *fx, hush_event_t *ev, const char *pub,
                        const hush_roster_agent_t *bot2)
{
    char content[HUSH_EVENT_MAX_CONTENT] = {0};
    size_t at = 0;

    if (bot2 != NULL)
        snprintf(content, sizeof(content), "nostr:%s write a line. nostr:%s review it.",
                 fx->happy->id.npub, bot2->id.npub);
    else
        snprintf(content, sizeof(content), "nostr:%s write a line.", fx->happy->id.npub);
    fill_note(ev, pub, content, NULL);
    at = ev->tag_count;
    hush_agent_copy(ev->tags[at][0], sizeof(ev->tags[at][0]), "p");
    hush_agent_copy(ev->tags[at][1], sizeof(ev->tags[at][1]), fx->happy->id.npub);
    ev->tag_count = ++at;
    if (bot2 != NULL) {
        hush_agent_copy(ev->tags[at][0], sizeof(ev->tags[at][0]), "p");
        hush_agent_copy(ev->tags[at][1], sizeof(ev->tags[at][1]), bot2->id.npub);
        ev->tag_count = at + 1;
    }
    expect(hush_store_insert(fx->store, ev) == HUSH_OK, "thread insert");
    for (size_t i = 0; i < ev->tag_count; i++) {
        if (strcmp(ev->tags[i][0], "p") == 0)
            hush_agent_handle_mention(fx->store, &fx->launch, ev, ev->tags[i][1]);
    }
}

/* Feeds a finished turn by bot in the fixture's thread back to the agent,
 * as the relay does when a job exits (no runtime ever starts). */
static void finish_turn(test_approve_fixture_t *fx, const hush_roster_agent_t *bot, int role,
                        const char *out)
{
    static hush_agent_job_t job;

    memset(&job, 0, sizeof(job));
    job.fd = -1;
    job.busy = 1;
    job.launch = &fx->launch;
    job.loop_role = role;
    hush_agent_copy(job.parent_id, sizeof(job.parent_id), fx->root.id);
    hush_agent_copy(job.trigger_id, sizeof(job.trigger_id), fx->root.id);
    hush_agent_copy(job.channel, sizeof(job.channel), TEST_APPROVE_CHANNEL);
    hush_agent_copy(job.human_pub, sizeof(job.human_pub), fx->root.pubkey);
    hush_agent_copy(job.robot_pub, sizeof(job.robot_pub), bot->id.pubkey_hex);
    hush_agent_copy(job.robot_name, sizeof(job.robot_name), bot->name);
    hush_agent_copy(job.out, sizeof(job.out), out);
    hush_agent_finish_job(fx->store, &job, 1);
}

/* P2-3: two different robots wait in one thread; Yes runs the older. */
static void check_fifo(void)
{
    static test_approve_fixture_t fx;

    open_hive(&fx, HUSH_ROSTER_APPROVAL_EVERY_ID);
    open_thread(&fx, &fx.root, TEST_APPROVE_STRANGER_PUB, NULL);
    hush_agent_handle_mention(fx.store, &fx.launch, &fx.root, fx.scout->id.npub);
    expect(count_line(fx.store, TEST_APPROVE_ASK_COPY) == 1 &&
           count_line(fx.store, TEST_APPROVE_ASK_SCOUT) == 1, "Happy then Scout wait");
    answer(&fx, fx.launch.human.pubkey_hex, "Yes");
    expect(count_head(fx.store, TEST_APPROVE_RAN_HEAD) == 1, "the first Yes runs Happy (oldest)");
    expect(count_head(fx.store, TEST_APPROVE_RAN_SCOUT) == 0, "the first Yes leaves Scout waiting");
    answer(&fx, fx.launch.human.pubkey_hex, "No");
    expect(count_line(fx.store, TEST_APPROVE_NO_SCOUT) == 1, "the next answer settles Scout");
    close_hive(&fx);
}

/* P2-4: a Yes answers only its own thread's turn, and a void drops only its
 * own thread's turns. */
static void check_threads(void)
{
    static test_approve_fixture_t fx;
    static hush_event_t a;
    static hush_event_t b;

    open_hive(&fx, HUSH_ROSTER_APPROVAL_EVERY_ID);
    open_thread(&fx, &a, TEST_APPROVE_STRANGER_PUB, NULL);
    fill_note(&b, TEST_APPROVE_STRANGER_PUB, "scout, a riddle", NULL);
    expect(hush_store_insert(fx.store, &b) == HUSH_OK, "thread b insert");
    hush_agent_handle_mention(fx.store, &fx.launch, &b, fx.scout->id.npub);
    answer_in(&fx, b.id, fx.launch.human.pubkey_hex, "Yes");
    expect(count_head(fx.store, TEST_APPROVE_RAN_SCOUT) == 1, "a Yes in B runs B's turn");
    expect(count_head(fx.store, TEST_APPROVE_RAN_HEAD) == 0, "a Yes in B leaves A's turn");
    answer_in(&fx, b.id, fx.launch.human.pubkey_hex, "never mind");
    answer_in(&fx, a.id, fx.launch.human.pubkey_hex, "Yes");
    expect(count_head(fx.store, TEST_APPROVE_RAN_HEAD) == 1, "a void in B leaves A's turn");
    close_hive(&fx);
}

/* Promoted P3: other people share at most 4 entries; the owner keeps the
 * rest, and an answered guest turn frees its entry. */
static void check_guest_share(void)
{
    static test_approve_fixture_t fx;
    static hush_event_t guest[TEST_APPROVE_GUESTS + 2];
    char text[TEST_APPROVE_PATH_MAX] = {0};

    open_hive(&fx, HUSH_ROSTER_APPROVAL_EVERY_ID);
    for (int i = 0; i < TEST_APPROVE_GUESTS + 1; i++)
        open_thread(&fx, &guest[i], TEST_APPROVE_STRANGER_PUB, NULL);
    expect(count_line(fx.store, TEST_APPROVE_ASK_COPY) == TEST_APPROVE_GUESTS,
           "other people hold at most 4 entries");
    expect(count_line(fx.store, TEST_APPROVE_FULL_COPY) == 1, "the fifth guest turn is refused");
    for (int i = 0; i < TEST_APPROVE_TABLE - TEST_APPROVE_GUESTS + 1; i++) {
        snprintf(text, sizeof(text), "owner joke %d", i);
        mention_happy(&fx, text);
    }
    expect(count_line(fx.store, TEST_APPROVE_ASK_COPY) == TEST_APPROVE_TABLE,
           "the owner still fills the other 4");
    expect(count_line(fx.store, TEST_APPROVE_FULL_COPY) == 2, "the table is full at 8");
    answer_in(&fx, guest[0].id, fx.launch.human.pubkey_hex, "No");
    open_thread(&fx, &guest[TEST_APPROVE_GUESTS + 1], TEST_APPROVE_STRANGER_PUB, NULL);
    expect(count_line(fx.store, TEST_APPROVE_ASK_COPY) == TEST_APPROVE_TABLE + 1,
           "an answered guest turn frees its entry");
    close_hive(&fx);
}

/* Opens an owner pair thread, runs Happy's turn (Yes; no runtime), and
 * finishes it with out, so Scout's follow-wave turn waits. */
static void pair_to_scout(test_approve_fixture_t *fx, int role, const char *out)
{
    open_thread(fx, &fx->root, fx->launch.human.pubkey_hex, fx->scout);
    answer(fx, fx->launch.human.pubkey_hex, "Yes");
    finish_turn(fx, fx->happy, role, out);
    expect(count_line(fx->store, TEST_APPROVE_ASK_SCOUT) >= 1, "Scout's follow turn waits");
    expect(hush_agent_follow_peek(fx->root.id, NULL) == 1, "a held follow turn counts in flight");
}

/* P2-7: a void, a No, a Yes with no runtime, and a removed robot each give
 * back the held follow turn's in-flight count. */
static void check_release(void)
{
    static test_approve_fixture_t fx;
    int loop = -1;

    open_hive(&fx, HUSH_ROSTER_APPROVAL_EVERY_ID);
    pair_to_scout(&fx, HUSH_AGENT_LOOP_ROLE_NONE, "A line.");
    answer(&fx, fx.launch.human.pubkey_hex, "never mind");
    expect(hush_agent_follow_peek(fx.root.id, NULL) == -1, "a void frees the follow slot");
    pair_to_scout(&fx, HUSH_AGENT_LOOP_ROLE_NONE, "A line.");
    answer(&fx, fx.launch.human.pubkey_hex, "No");
    expect(hush_agent_follow_peek(fx.root.id, &loop) == 0 && loop == 0,
           "a No gives back the count and stops the loop");
    pair_to_scout(&fx, HUSH_AGENT_LOOP_ROLE_NONE, "A line.");
    answer(&fx, fx.launch.human.pubkey_hex, "Yes");
    expect(count_head(fx.store, TEST_APPROVE_RAN_SCOUT) == 1, "Scout's approved turn ran");
    expect(hush_agent_follow_peek(fx.root.id, NULL) == 0, "a turn that starts no job gives back");
    pair_to_scout(&fx, HUSH_AGENT_LOOP_ROLE_NONE, "A line.");
    expect(hush_launch_remove_agent(&fx.launch, "scout") == HUSH_OK, "remove Scout");
    answer(&fx, fx.launch.human.pubkey_hex, "Yes");
    expect(hush_agent_follow_peek(fx.root.id, NULL) == 0, "a removed robot's turn gives back");
    close_hive(&fx);
}

/* P2-7: a held loop turn counts in flight; a void frees the slot, a No ends
 * the loop and gives back the count. */
static void check_loop_release(void)
{
    static test_approve_fixture_t fx;
    int loop = -1;

    for (int no = 0; no < 2; no++) {
        open_hive(&fx, HUSH_ROSTER_APPROVAL_EVERY_ID);
        pair_to_scout(&fx, HUSH_AGENT_LOOP_ROLE_LEAD, "Riddle one?\nLOOP: continue");
        answer(&fx, fx.launch.human.pubkey_hex, "Yes");
        finish_turn(&fx, fx.scout, HUSH_AGENT_LOOP_ROLE_PARTNER, "An answer.");
        expect(count_line(fx.store, TEST_APPROVE_ASK_COPY) == 2, "Happy's loop turn waits");
        expect(hush_agent_follow_peek(fx.root.id, &loop) == 1 && loop == 1,
               "a held loop turn counts in flight");
        answer(&fx, fx.launch.human.pubkey_hex, no ? "No" : "never mind");
        if (no)
            expect(hush_agent_follow_peek(fx.root.id, &loop) == 0 && loop == 0,
                   "a declined loop turn gives back the count and ends the loop");
        else
            expect(hush_agent_follow_peek(fx.root.id, NULL) == -1, "a void frees the loop's slot");
        close_hive(&fx);
    }
}

/* P2-7 behaviour: more voided threads than follow slots still get their
 * next wave (a leaked count would leave every slot live). */
static void check_many_voids(void)
{
    static test_approve_fixture_t fx;

    open_hive(&fx, HUSH_ROSTER_APPROVAL_EVERY_ID);
    for (int i = 0; i < TEST_APPROVE_THREADS; i++) {
        open_thread(&fx, &fx.root, fx.launch.human.pubkey_hex, fx.scout);
        answer(&fx, fx.launch.human.pubkey_hex, "Yes");
        finish_turn(&fx, fx.happy, HUSH_AGENT_LOOP_ROLE_NONE, "A line.");
        answer(&fx, fx.launch.human.pubkey_hex, "never mind");
        answer(&fx, fx.launch.human.pubkey_hex, "No");
    }
    expect(count_line(fx.store, TEST_APPROVE_ASK_SCOUT) == TEST_APPROVE_THREADS,
           "every one of 10 voided threads got its follow wave");
    close_hive(&fx);
}

/* An owner answer settles a waiting turn before a paused loop's question
 * in the same thread; the question still waits afterwards. Happy's first
 * turn is declined (no runtime note), so cap 2 counts only replies. */
static void check_answer_order(void)
{
    static test_approve_fixture_t fx;
    static hush_event_t ask;
    char content[HUSH_EVENT_MAX_CONTENT] = {0};

    open_hive(&fx, HUSH_ROSTER_APPROVAL_EVERY_ID);
    cap_two(&fx);
    open_thread(&fx, &fx.root, fx.launch.human.pubkey_hex, fx.scout);
    answer(&fx, fx.launch.human.pubkey_hex, "No");
    finish_turn(&fx, fx.happy, HUSH_AGENT_LOOP_ROLE_LEAD, "Riddle one?\nLOOP: continue");
    snprintf(content, sizeof(content), "nostr:%s one more", fx.happy->id.npub);
    fill_note(&ask, TEST_APPROVE_STRANGER_PUB, content, fx.root.id);
    expect(hush_store_insert(fx.store, &ask) == HUSH_OK, "guest ask insert");
    hush_agent_handle_mention(fx.store, &fx.launch, &ask, fx.happy->id.npub);
    expect(count_line(fx.store, TEST_APPROVE_ASK_SCOUT) == 1 &&
           count_line(fx.store, TEST_APPROVE_ASK_COPY) == 2, "Scout and the guest's Happy wait");
    answer(&fx, fx.launch.human.pubkey_hex, "Yes");
    finish_turn(&fx, fx.scout, HUSH_AGENT_LOOP_ROLE_PARTNER, "An answer.");
    expect(count_line(fx.store, HUSH_AGENT_LOOP_ASK_LINE) == 1, "the loop pauses at the cap");
    answer(&fx, fx.launch.human.pubkey_hex, "Yes");
    expect(count_head(fx.store, TEST_APPROVE_RAN_HEAD) == 1, "the Yes runs the waiting turn first");
    answer(&fx, fx.launch.human.pubkey_hex, "No");
    expect(count_line(fx.store, HUSH_AGENT_LOOP_STOPPED_LINE) == 1, "the loop question still waited");
    close_hive(&fx);
}

/* Without a signed-in identity the setting is refused and unchanged. */
static void check_login(void)
{
    static hush_launch_t launch;

    hush_launch_init(&launch);
    expect(hush_launch_set_approval(&launch, HUSH_ROSTER_APPROVAL_EVERY_ID) == HUSH_ERR_ARG,
           "no identity: the setting is refused");
    expect(launch.roster.profile.approval == HUSH_ROSTER_APPROVAL_AUTO,
           "no identity: the setting is unchanged");
}

int main(void)
{
    check_parse();
    check_not_turns();
    check_owner_only();
    check_owner_yes();
    check_auto();
    check_void();
    check_one_each();
    check_cap_window();
    check_full();
    check_fifo();
    check_threads();
    check_guest_share();
    check_release();
    check_loop_release();
    check_many_voids();
    check_answer_order();
    check_login();
    if (g_fail)
        return 1;
    printf("approve ok\n");
    return 0;
}
