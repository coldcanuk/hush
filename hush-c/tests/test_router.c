/* tests/test_router.c: Router graph, silent workers, restart, skill tiers. */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hush_router.h"

enum {
    ROUTER_TEST_NODES = 32,
    ROUTER_TEST_BELONGS_EDGE = 1, /* open_item pushes assigned, then belongs_to */
    ROUTER_TEST_POLLS = 3,
    ROUTER_TEST_ROOT_MAX = 64,
    ROUTER_TEST_DIR_MODE = 0700,
    ROUTER_TEST_PATH_MAX = 192,
    ROUTER_TEST_FILE_MAX = 16384,
    ROUTER_TEST_PUBLISHED = 3 /* renames done before the simulated crash */
};

#define ROUTER_TEST_POLL_DIR "/poll"

/* Every file one save writes, in save order (#375 Floor F4). */
static const char *const ROUTER_TEST_FILES[] = {
    "workitems.json", "workers.json", "decisions.json",
    "artifacts.json", "edges.json", "router_state.json"
};

enum {
    ROUTER_TEST_NFILES = sizeof ROUTER_TEST_FILES / sizeof ROUTER_TEST_FILES[0]
};

/* Save protocol names, pinned by the half-write checks below. */
static const char ROUTER_TEST_COMMIT[] = "commit";
static const char ROUTER_TEST_STAGED[] = ".tmp";

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

static hush_router_note_t note_of(const char *text, const char *specialist)
{
    hush_router_note_t note;

    memset(&note, 0, sizeof note);
    note.text = text;
    note.channel = "general";
    note.specialist = specialist;
    note.now = 1000;
    return note;
}

static void check_phase1(void)
{
    hush_router_graph_t graph;
    hush_router_result_t result;
    hush_router_note_t note = note_of("Run a rate-limit test", "payne");

    hush_router_init(&graph);
    expect(hush_router_accept(&graph, &result, &note) == HUSH_OK, "work ok");
    expect(result.route == HUSH_ROUTER_ROUTE_WORK, "work route");
    expect(strstr(result.line, "Starting Work Item #1:") != NULL, "status line");
    expect(graph.nitems == 1, "one item");
    expect(graph.nworkers == 1, "silent worker");
    expect(strcmp(graph.workers[0].specialist_id, "payne") == 0, "specialist");
    note = note_of("nostr:npub1example please look", NULL);
    expect(hush_router_accept(&graph, &result, &note) == HUSH_OK, "legacy ok");
    expect(result.route == HUSH_ROUTER_ROUTE_LEGACY, "legacy route");
    expect(graph.nitems == 1, "legacy opens nothing");
    note = note_of("mail me at owner@hive", NULL);
    expect(hush_router_accept(&graph, &result, &note) == HUSH_OK, "email ok");
    expect(result.route == HUSH_ROUTER_ROUTE_WORK, "email is not a mention");
}

static void check_phase2(void)
{
    hush_router_graph_t graph;
    hush_router_result_t first;
    hush_router_result_t second;
    hush_router_result_t decision;
    hush_router_note_t note = note_of("Run a rate-limit test", NULL);
    char line[HUSH_ROUTER_LINE_MAX];

    hush_router_init(&graph);
    expect(hush_router_accept(&graph, &first, &note) == HUSH_OK, "first");
    note = note_of("Write the field notes", "scribe");
    expect(hush_router_accept(&graph, &second, &note) == HUSH_OK, "second");
    expect(graph.nitems == 2, "two items");
    expect(graph.items[0].phase == HUSH_ROUTER_PENDING, "first still pending");
    expect(graph.items[1].phase == HUSH_ROUTER_PENDING, "second still pending");
    expect(hush_router_drain(&graph, line, sizeof line, 1001) == HUSH_OK,
           "drain working");
    expect(strstr(line, "Working...") != NULL, "working line");
    expect(hush_router_drain(&graph, line, sizeof line, 1002) == HUSH_OK,
           "drain block");
    expect(graph.items[0].phase == HUSH_ROUTER_BLOCKED, "rate-limit blocked");
    expect(graph.items[1].phase == HUSH_ROUTER_PENDING, "peer not blocked");
    note = note_of("60s", NULL);
    expect(hush_router_accept(&graph, &decision, &note) == HUSH_OK, "decision");
    expect(decision.route == HUSH_ROUTER_ROUTE_DECISION, "decision route");
    expect(strcmp(decision.item_id, first.item_id) == 0, "right worker");
    expect(strcmp(graph.decisions[0].text, "60s") == 0, "decision text");
    expect(graph.items[1].phase == HUSH_ROUTER_PENDING, "peer untouched");
    expect(hush_router_drain(&graph, line, sizeof line, 1003) == HUSH_OK,
           "drain done");
    expect(graph.items[0].phase == HUSH_ROUTER_DONE, "decided item done");
    expect(strstr(line, "Done.") != NULL, "done line");
}

static void check_phase3(const char *root)
{
    hush_router_graph_t graph;
    hush_router_graph_t loaded;
    hush_router_result_t result;
    hush_router_note_t note = note_of("Check the relay", NULL);
    hush_router_node_t nodes[ROUTER_TEST_NODES];
    size_t nnodes = 0;
    char line[HUSH_ROUTER_LINE_MAX];

    hush_router_init(&graph);
    expect(hush_router_import_thread(&graph, "thread-ABC", "old desk", 9) ==
               HUSH_OK,
           "import");
    expect(hush_router_import_thread(&graph, "thread-ABC", "old desk", 9) ==
               HUSH_OK,
           "import once");
    expect(graph.nitems == 1, "imported once");
    expect(graph.items[0].phase == HUSH_ROUTER_DONE, "import is done");
    hush_router_init(&graph);
    /* text outlives the loop: the "full" accept below still reads note.text. */
    char text[HUSH_ROUTER_TEXT_MAX];
    for (int i = 0; i < HUSH_ROUTER_ITEMS_MAX; ++i) {
        snprintf(text, sizeof text, "Check item %d", i);
        note = note_of(text, NULL);
        note.now = 2000 + i;
        expect(hush_router_accept(&graph, &result, &note) == HUSH_OK, "fill");
    }
    expect(hush_router_accept(&graph, &result, &note) == HUSH_ERR_FULL, "full");
    expect(hush_router_save(&graph, root) == HUSH_OK, "save");
    expect(hush_router_load(&loaded, root) == HUSH_OK, "load");
    expect(loaded.nitems == graph.nitems, "items survived");
    expect(loaded.nworkers == graph.nworkers, "workers survived");
    expect(loaded.nedges == graph.nedges, "edges survived");
    expect(strcmp(loaded.items[0].title, graph.items[0].title) == 0, "title");
    expect(hush_router_subgraph(&loaded, nodes, &nnodes, ROUTER_TEST_NODES) ==
               HUSH_OK,
           "subgraph");
    expect(nnodes == (size_t)HUSH_ROUTER_SUBGRAPH_MAX, "cap 24");
    expect(hush_router_drain(&loaded, line, sizeof line, 3000) == HUSH_OK,
           "drain after load");
    (void)line;
}

/* #375 F3: quotes and backslashes in any stored string must not break reload. */
static void check_escape(const char *root)
{
    hush_router_graph_t graph;
    hush_router_graph_t loaded;
    hush_router_result_t result;
    hush_router_note_t note = note_of("Run the build", "a\\b");

    note.channel = "ops\"x";
    hush_router_init(&graph);
    expect(hush_router_accept(&graph, &result, &note) == HUSH_OK, "hostile accept");
    snprintf(graph.items[0].parent_id, sizeof graph.items[0].parent_id, "p\"q");
    snprintf(graph.workers[0].provider_id, sizeof graph.workers[0].provider_id,
             "v\\w");
    expect(hush_router_save(&graph, root) == HUSH_OK, "hostile save");
    if (hush_router_load(&loaded, root) != HUSH_OK) {
        expect(0, "hostile load");
        return;
    }
    expect(strcmp(loaded.items[0].channel_id, "ops\"x") == 0, "channel kept");
    expect(strcmp(loaded.items[0].parent_id, "p\"q") == 0, "parent kept");
    expect(strcmp(loaded.workers[0].specialist_id, "a\\b") == 0,
           "specialist kept");
    expect(strcmp(loaded.workers[0].provider_id, "v\\w") == 0, "provider kept");
    expect(loaded.nedges == graph.nedges, "hostile edges");
    expect(strcmp(loaded.edges[ROUTER_TEST_BELONGS_EDGE].to, "ops\"x") == 0, "belongs_to kept");
}

/* Pads edges so the next push lands the last slot. */
static void fill_edges(hush_router_graph_t *graph, size_t upto)
{
    while (graph->nedges < upto && graph->nedges < (size_t)HUSH_ROUTER_EDGES_MAX) {
        hush_router_edge_t *edge = &graph->edges[graph->nedges];
        memset(edge, 0, sizeof(*edge));
        snprintf(edge->from, sizeof edge->from, "pad");
        snprintf(edge->to, sizeof edge->to, "pad");
        graph->nedges++;
    }
}

/* Floor F2 setup: item 1 blocked then decided, item 2 queued behind it,
 * and the decision's needs edge takes the last of the 48 edge slots. */
static void build_edges_full(hush_router_graph_t *graph)
{
    hush_router_result_t result;
    hush_router_note_t note = note_of("Run a rate-limit test", NULL);
    char line[HUSH_ROUTER_LINE_MAX];

    hush_router_init(graph);
    expect(hush_router_accept(graph, &result, &note) == HUSH_OK, "full first");
    expect(hush_router_drain(graph, line, sizeof line, 1) == HUSH_OK, "full run");
    expect(hush_router_drain(graph, line, sizeof line, 2) == HUSH_OK, "full block");
    note = note_of("Write the field notes", NULL);
    expect(hush_router_accept(graph, &result, &note) == HUSH_OK, "full second");
    fill_edges(graph, (size_t)HUSH_ROUTER_EDGES_MAX - 1);
    note = note_of("60s", NULL);
    expect(hush_router_accept(graph, &result, &note) == HUSH_OK, "full decide");
    expect(graph->nedges == (size_t)HUSH_ROUTER_EDGES_MAX, "edges at 48");
}

/* #375 Floor F2: a full edge table must not split an item from its worker,
 * and the stalled head must not stop the worker queued behind it. */
static void check_edges_full(void)
{
    hush_router_graph_t graph;
    hush_router_graph_t before;
    char line[HUSH_ROUTER_LINE_MAX];
    size_t nartifacts;

    build_edges_full(&graph);
    nartifacts = graph.nartifacts;
    expect(hush_router_drain(&graph, line, sizeof line, 3) == HUSH_OK,
           "full drain advances peer");
    expect(graph.workers[0].phase == graph.items[0].phase, "no split phase");
    expect(graph.items[0].phase == HUSH_ROUTER_RUNNING, "head waits running");
    expect(graph.nartifacts == nartifacts, "no orphan artifact");
    expect(graph.items[1].phase == HUSH_ROUTER_RUNNING, "peer progressed");
    expect(graph.workers[1].phase == HUSH_ROUTER_RUNNING, "peer worker runs");
    memcpy(&before, &graph, sizeof before);
    expect(hush_router_drain(&graph, line, sizeof line, 4) == HUSH_ERR_FULL,
           "all stalled is full");
    expect(memcmp(&before, &graph, sizeof before) == 0, "full drain mutates nothing");
}

/* #375 Floor F2 via poll: reload from disk each turn must still advance
 * the peer and save it, not retry the stalled head forever. */
static void check_poll_full(const char *root)
{
    hush_router_graph_t graph;
    hush_router_graph_t loaded;
    char home[ROUTER_TEST_ROOT_MAX];

    snprintf(home, sizeof home, "%s%s", root, ROUTER_TEST_POLL_DIR);
    expect(mkdir(home, ROUTER_TEST_DIR_MODE) == 0, "poll home");
    build_edges_full(&graph);
    expect(hush_router_save(&graph, home) == HUSH_OK, "poll seed");
    expect(setenv("HUSH_HOME", home, 1) == 0, "poll env");
    for (int i = 0; i < ROUTER_TEST_POLLS; ++i)
        hush_router_poll();
    expect(hush_router_load(&loaded, home) == HUSH_OK, "poll reload");
    expect(loaded.workers[0].phase == loaded.items[0].phase, "poll no split");
    expect(loaded.items[1].phase == HUSH_ROUTER_RUNNING, "poll peer saved");
    expect(loaded.nedges == (size_t)HUSH_ROUTER_EDGES_MAX, "poll edges kept");
}

/* Writes dir/name, plus suffix when non-empty, into out. */
static void join_path(char *out, size_t outsz, const char *dir, const char *name,
                      const char *suffix)
{
    int n = snprintf(out, outsz, "%s/%s%s", dir, name, suffix);

    expect(n > 0 && (size_t)n < outsz, "half path fits");
}

/* True when two graphs hold one generation: counts, cursor, phases. */
static int same_graph(const hush_router_graph_t *a, const hush_router_graph_t *b)
{
    if (a->nitems != b->nitems || a->nworkers != b->nworkers)
        return 0;
    if (a->ndecisions != b->ndecisions || a->nedges != b->nedges)
        return 0;
    if (a->next_id != b->next_id || strcmp(a->last_line, b->last_line) != 0)
        return 0;
    for (size_t i = 0; i < a->nitems; ++i) {
        if (a->items[i].phase != b->items[i].phase)
            return 0;
    }
    for (size_t i = 0; i < a->nworkers; ++i) {
        if (a->workers[i].phase != b->workers[i].phase)
            return 0;
    }
    return 1;
}

/* Reports which home broke when a generation check fails. */
static void expect_at(int cond, const char *msg, const char *home)
{
    expect(cond, msg);
    if (!cond)
        fprintf(stderr, "     at %s\n", home);
}

/* Old generation shared by both half-write cases: WI-1 blocked. */
static void build_blocked(hush_router_graph_t *graph)
{
    hush_router_result_t result;
    hush_router_note_t note = note_of("Run a rate-limit test", NULL);
    char line[HUSH_ROUTER_LINE_MAX];

    hush_router_init(graph);
    expect(hush_router_accept(graph, &result, &note) == HUSH_OK, "half first");
    expect(hush_router_drain(graph, line, sizeof line, 1) == HUSH_OK, "half run");
    expect(hush_router_drain(graph, line, sizeof line, 2) == HUSH_OK, "half block");
}

/* Copies old, then applies one note that must take the given route. */
static void build_next(hush_router_graph_t *next, const hush_router_graph_t *old,
                       const char *text, hush_router_route_t route)
{
    hush_router_result_t result;
    hush_router_note_t note = note_of(text, NULL);

    memcpy(next, old, sizeof *next);
    expect(hush_router_accept(next, &result, &note) == HUSH_OK, "half accept");
    expect(result.route == route, "half route");
}

/* Fault injection: a directory squats on file k's staging name, so the save
 * of next fails at file k. Reload must show old whole; a retry lands next. */
static void check_fault_at(const char *home, const hush_router_graph_t *old,
                           const hush_router_graph_t *next, size_t k)
{
    char dir[ROUTER_TEST_PATH_MAX];
    char block[ROUTER_TEST_PATH_MAX];
    hush_router_graph_t loaded;

    expect(mkdir(home, ROUTER_TEST_DIR_MODE) == 0, "half home");
    expect(hush_router_save(old, home) == HUSH_OK, "half seed");
    join_path(dir, sizeof dir, home, "router", "");
    join_path(block, sizeof block, dir, ROUTER_TEST_FILES[k], ROUTER_TEST_STAGED);
    expect(mkdir(block, ROUTER_TEST_DIR_MODE) == 0, "half squat");
    expect(hush_router_save(next, home) != HUSH_OK, "half save fails");
    expect(rmdir(block) == 0, "half unsquat");
    expect(hush_router_load(&loaded, home) == HUSH_OK, "half reload");
    expect_at(same_graph(&loaded, old), "half write left mixed state", home);
    expect(hush_router_save(next, home) == HUSH_OK, "half resave");
    expect(hush_router_load(&loaded, home) == HUSH_OK, "half new reload");
    expect_at(same_graph(&loaded, next), "half retry lost new state", home);
}

/* Runs the fault at every file of one save, each in a fresh home. */
static void check_fault_each(const char *root, const char *op,
                             const hush_router_graph_t *old,
                             const hush_router_graph_t *next)
{
    for (size_t k = 0; k < (size_t)ROUTER_TEST_NFILES; ++k) {
        char home[ROUTER_TEST_PATH_MAX];
        int n = snprintf(home, sizeof home, "%s/half-%s-%u", root, op,
                         (unsigned)k);

        expect(n > 0 && (size_t)n < sizeof home, "half home fits");
        check_fault_at(home, old, next, k);
    }
}

/* Copies one whole file. Fails the test on any short read or write. */
static void copy_file(const char *to, const char *from)
{
    char body[ROUTER_TEST_FILE_MAX];
    FILE *in = fopen(from, "rb");
    size_t n = 0;

    expect(in != NULL, "half copy open");
    if (in == NULL)
        return;
    n = fread(body, 1, sizeof body, in);
    (void)fclose(in);
    FILE *out = fopen(to, "wb");
    expect(out != NULL, "half copy create");
    if (out == NULL)
        return;
    expect(fwrite(body, 1, n, out) == n, "half copy write");
    expect(fclose(out) == 0, "half copy close");
}

/* Lays out a crash after the commit mark: every file of next staged, the
 * mark present, only the first ROUTER_TEST_PUBLISHED renames done. */
static void stage_crash(const char *dir, const char *next_dir)
{
    char from[ROUTER_TEST_PATH_MAX];
    char to[ROUTER_TEST_PATH_MAX];
    char mark[ROUTER_TEST_PATH_MAX];

    for (size_t i = 0; i < (size_t)ROUTER_TEST_NFILES; ++i) {
        join_path(from, sizeof from, next_dir, ROUTER_TEST_FILES[i], "");
        join_path(to, sizeof to, dir, ROUTER_TEST_FILES[i], ROUTER_TEST_STAGED);
        copy_file(to, from);
    }
    join_path(mark, sizeof mark, dir, ROUTER_TEST_COMMIT, "");
    FILE *fp = fopen(mark, "wb");
    expect(fp != NULL && fclose(fp) == 0, "half commit mark");
    for (size_t i = 0; i < (size_t)ROUTER_TEST_PUBLISHED; ++i) {
        join_path(from, sizeof from, dir, ROUTER_TEST_FILES[i], ROUTER_TEST_STAGED);
        join_path(to, sizeof to, dir, ROUTER_TEST_FILES[i], "");
        expect(rename(from, to) == 0, "half partial publish");
    }
}

/* Crash mid-publish: reload must roll next forward whole and drop the mark. */
static void check_publish_crash(const char *root, const char *op,
                                const hush_router_graph_t *old,
                                const hush_router_graph_t *next)
{
    char home[ROUTER_TEST_PATH_MAX];
    char spare[ROUTER_TEST_PATH_MAX];
    char dir[ROUTER_TEST_PATH_MAX];
    char next_dir[ROUTER_TEST_PATH_MAX];
    hush_router_graph_t loaded;

    join_path(home, sizeof home, root, "half-pub-", op);
    join_path(spare, sizeof spare, root, "half-next-", op);
    expect(mkdir(home, ROUTER_TEST_DIR_MODE) == 0, "pub home");
    expect(mkdir(spare, ROUTER_TEST_DIR_MODE) == 0, "pub spare");
    expect(hush_router_save(old, home) == HUSH_OK, "pub seed");
    expect(hush_router_save(next, spare) == HUSH_OK, "pub next");
    join_path(dir, sizeof dir, home, "router", "");
    join_path(next_dir, sizeof next_dir, spare, "router", "");
    stage_crash(dir, next_dir);
    expect(hush_router_load(&loaded, home) == HUSH_OK, "pub reload");
    expect_at(same_graph(&loaded, next), "publish crash left mixed state", home);
    join_path(spare, sizeof spare, dir, ROUTER_TEST_COMMIT, "");
    expect_at(access(spare, F_OK) != 0, "commit mark survived recovery", home);
}

/* #375 Floor F4: open_item and apply_decision persist as one generation. */
static void check_halfwrite(const char *root)
{
    hush_router_graph_t old;
    hush_router_graph_t opened;
    hush_router_graph_t decided;

    build_blocked(&old);
    build_next(&opened, &old, "Write the field notes", HUSH_ROUTER_ROUTE_WORK);
    build_next(&decided, &old, "60s", HUSH_ROUTER_ROUTE_DECISION);
    check_fault_each(root, "open", &old, &opened);
    check_fault_each(root, "decide", &old, &decided);
    check_publish_crash(root, "open", &old, &opened);
    check_publish_crash(root, "decide", &old, &decided);
}

static void check_phase4(void)
{
    hush_router_skill_t skills[2];
    char router[HUSH_ROUTER_PROMPT_MAX];
    char worker[HUSH_ROUTER_PROMPT_MAX];

    memset(skills, 0, sizeof skills);
    snprintf(skills[0].id, sizeof skills[0].id, "always-watch");
    skills[0].tier = HUSH_ROUTER_TIER_ALWAYS;
    snprintf(skills[0].body, sizeof skills[0].body, "watch the desk");
    snprintf(skills[1].id, sizeof skills[1].id, "call-limit");
    skills[1].tier = HUSH_ROUTER_TIER_ON_CALL;
    snprintf(skills[1].body, sizeof skills[1].body, "measure the limit");
    expect(hush_router_select_skills(router, sizeof router, skills, 2, 0) ==
               HUSH_OK,
           "router prompt");
    expect(hush_router_select_skills(worker, sizeof worker, skills, 2, 1) ==
               HUSH_OK,
           "worker prompt");
    expect(strstr(router, "watch the desk") != NULL, "always on router");
    expect(strstr(router, "measure the limit") == NULL, "on-call stays off");
    expect(strstr(worker, "measure the limit") != NULL, "on-call on worker");
    expect(strstr(worker, "watch the desk") == NULL, "always stays off worker");
}

static void check_context(void)
{
    hush_router_graph_t graph;
    hush_router_result_t result;
    hush_router_note_t note = note_of("Run a rate-limit test", NULL);
    char line[HUSH_ROUTER_LINE_MAX];

    hush_router_init(&graph);
    expect(hush_router_accept(&graph, &result, &note) == HUSH_OK, "ctx work");
    expect(hush_router_drain(&graph, line, sizeof line, 1) == HUSH_OK, "ctx run");
    expect(hush_router_drain(&graph, line, sizeof line, 2) == HUSH_OK, "ctx block");
    expect(hush_router_context_line(&graph, line, sizeof line) == HUSH_OK, "ctx");
    expect(strcmp(line, "1 active · 1 blocked") == 0, "context line");
    note = note_of("What time is it?", NULL);
    hush_router_init(&graph);
    expect(hush_router_accept(&graph, &result, &note) == HUSH_OK, "question");
    expect(result.route == HUSH_ROUTER_ROUTE_ANSWER, "direct answer");
    expect(graph.nitems == 0, "question opens nothing");
}

int main(void)
{
    char root[] = "/tmp/hush-router-XXXXXX";

    if (mkdtemp(root) == NULL) {
        perror("mkdtemp");
        return 1;
    }
    check_phase1();
    check_phase2();
    check_phase3(root);
    check_escape(root);
    check_edges_full();
    check_poll_full(root);
    check_halfwrite(root);
    check_phase4();
    check_context();
    if (g_fail)
        return 1;
    printf("test_router ok\n");
    return 0;
}
