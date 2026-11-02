/* tests/test_router.c: Router graph, silent workers, restart, skill tiers. */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hush_router.h"

enum {
    ROUTER_TEST_NODES = 32
};

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
    for (int i = 0; i < HUSH_ROUTER_ITEMS_MAX; ++i) {
        char text[HUSH_ROUTER_TEXT_MAX];

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
    check_phase4();
    check_context();
    if (g_fail)
        return 1;
    printf("test_router ok\n");
    return 0;
}
