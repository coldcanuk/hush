/* hush_router.h: hive-wide Router graph, silent workers, bounded prompts. */

#ifndef HUSH_ROUTER_H
#define HUSH_ROUTER_H

#include <stddef.h>
#include <stdint.h>

#include "hush_status.h"

enum {
    HUSH_ROUTER_ID_MAX = 16,
    HUSH_ROUTER_TITLE_MAX = 64,
    HUSH_ROUTER_TEXT_MAX = 96,
    HUSH_ROUTER_NAME_MAX = 32,
    HUSH_ROUTER_LINE_MAX = 192,
    HUSH_ROUTER_KIND_MAX = 16,
    HUSH_ROUTER_CHANNEL_MAX = 64,
    HUSH_ROUTER_JOB_MAX = 64,
    HUSH_ROUTER_ITEMS_MAX = 16,
    HUSH_ROUTER_WORKERS_MAX = 16,
    HUSH_ROUTER_EDGES_MAX = 48,
    HUSH_ROUTER_DECISIONS_MAX = 16,
    HUSH_ROUTER_ARTIFACTS_MAX = 16,
    HUSH_ROUTER_SUBGRAPH_MAX = 24,
    HUSH_ROUTER_SKILLS_MAX = 8,
    HUSH_ROUTER_BODY_MAX = 96,
    HUSH_ROUTER_PROMPT_MAX = 1024
};

typedef enum {
    HUSH_ROUTER_PENDING = 0,
    HUSH_ROUTER_RUNNING = 1,
    HUSH_ROUTER_BLOCKED = 2,
    HUSH_ROUTER_DONE = 3,
    HUSH_ROUTER_FAILED = 4
} hush_router_phase_t;

typedef enum {
    HUSH_ROUTER_ROUTE_LEGACY = 0,
    HUSH_ROUTER_ROUTE_ANSWER = 1,
    HUSH_ROUTER_ROUTE_WORK = 2,
    HUSH_ROUTER_ROUTE_DECISION = 3,
    HUSH_ROUTER_ROUTE_AMBIGUOUS = 4
} hush_router_route_t;

typedef enum {
    HUSH_ROUTER_TIER_ALWAYS = 0,
    HUSH_ROUTER_TIER_ON_CALL = 1
} hush_router_tier_t;

typedef enum {
    HUSH_ROUTER_EDGE_ASSIGNED = 0,
    HUSH_ROUTER_EDGE_DEPENDS = 1,
    HUSH_ROUTER_EDGE_BLOCKED_BY = 2,
    HUSH_ROUTER_EDGE_PRODUCED = 3,
    HUSH_ROUTER_EDGE_BELONGS = 4,
    HUSH_ROUTER_EDGE_NEEDS = 5
} hush_router_edge_kind_t;

typedef struct {
    char id[HUSH_ROUTER_ID_MAX];
    char title[HUSH_ROUTER_TITLE_MAX];
    char description[HUSH_ROUTER_TEXT_MAX];
    hush_router_phase_t phase;
    char channel_id[HUSH_ROUTER_CHANNEL_MAX];
    int64_t created;
    int64_t updated;
    char worker_id[HUSH_ROUTER_ID_MAX];
    int priority;
    char parent_id[HUSH_ROUTER_ID_MAX];
} hush_router_item_t;

typedef struct {
    char id[HUSH_ROUTER_ID_MAX];
    char item_id[HUSH_ROUTER_ID_MAX];
    char specialist_id[HUSH_ROUTER_NAME_MAX];
    hush_router_phase_t phase;
    int64_t started;
    int64_t heartbeat;
    char job_dir[HUSH_ROUTER_JOB_MAX];
    char provider_id[HUSH_ROUTER_NAME_MAX];
} hush_router_worker_t;

typedef struct {
    char id[HUSH_ROUTER_ID_MAX];
    char item_id[HUSH_ROUTER_ID_MAX];
    char text[HUSH_ROUTER_TEXT_MAX];
    int64_t created;
} hush_router_decision_t;

typedef struct {
    char id[HUSH_ROUTER_ID_MAX];
    char worker_id[HUSH_ROUTER_ID_MAX];
    char text[HUSH_ROUTER_TEXT_MAX];
} hush_router_artifact_t;

typedef struct {
    char from[HUSH_ROUTER_ID_MAX];
    hush_router_edge_kind_t kind;
    char to[HUSH_ROUTER_ID_MAX];
} hush_router_edge_t;

typedef struct {
    char id[HUSH_ROUTER_NAME_MAX];
    hush_router_tier_t tier;
    char body[HUSH_ROUTER_BODY_MAX];
} hush_router_skill_t;

typedef struct {
    char kind[HUSH_ROUTER_KIND_MAX];
    char id[HUSH_ROUTER_ID_MAX];
} hush_router_node_t;

typedef struct {
    const char *text;       /* required, not owned */
    const char *channel;    /* optional; NULL means general */
    const char *specialist; /* optional hint, not owned */
    int64_t now;
} hush_router_note_t;

typedef struct {
    hush_router_route_t route;
    char line[HUSH_ROUTER_LINE_MAX];
    char item_id[HUSH_ROUTER_ID_MAX];
} hush_router_result_t;

/* n* counts are the live prefix of each array. next_id is the next item id. */
typedef struct {
    hush_router_item_t items[HUSH_ROUTER_ITEMS_MAX];
    hush_router_worker_t workers[HUSH_ROUTER_WORKERS_MAX];
    hush_router_decision_t decisions[HUSH_ROUTER_DECISIONS_MAX];
    hush_router_artifact_t artifacts[HUSH_ROUTER_ARTIFACTS_MAX];
    hush_router_edge_t edges[HUSH_ROUTER_EDGES_MAX];
    size_t nitems;
    size_t nworkers;
    size_t ndecisions;
    size_t nartifacts;
    size_t nedges;
    uint32_t next_id;
    char last_line[HUSH_ROUTER_LINE_MAX];
} hush_router_graph_t;

/* Zeros the graph and arms the first work-item id. NULL is ignored. */
void hush_router_init(hush_router_graph_t *graph);

/* Classifies one human note. Legacy mentions do not open a work item.
 * Fails with HUSH_ERR_ARG on a NULL pointer or an empty note. */
hush_status_t hush_router_accept(hush_router_graph_t *graph,
                                 hush_router_result_t *out,
                                 const hush_router_note_t *note);

/* Advances the first pending or running worker whose step fits the graph.
 * Blocked workers wait; a worker whose done step has no edge room is
 * skipped, so it cannot starve the workers behind it. On any failure the
 * graph is unchanged. Fails with HUSH_ERR_NOT_FOUND when nothing is queued
 * and HUSH_ERR_FULL when every queued worker waits for room. */
hush_status_t hush_router_drain(hush_router_graph_t *graph, char *out_line,
                                size_t linesz, int64_t now);

/* Writes the six router files under root/router as one generation: each is
 * staged as <name>.tmp and fsynced, then a commit mark lets the set replace
 * the old one by rename. Holds root/router/lock throughout. A failed save
 * leaves the old generation, or the new one once its mark exists. */
hush_status_t hush_router_save(const hush_router_graph_t *graph,
                               const char *root);

/* Loads those files. A missing file is an empty array, not an error.
 * Not read-only when root/router exists: it creates root/router/lock, finishes
 * a save interrupted after its commit mark (renames the staged files, removes
 * the mark) and removes staged files an unmarked save left. On a root/router
 * it may not write (e.g. a read-only home) it only reads, under a shared lock
 * on an existing root/router/lock, and fails with HUSH_ERR_IO when a commit
 * mark is pending there. Fails with HUSH_ERR_IO on other IO errors. */
hush_status_t hush_router_load(hush_router_graph_t *graph, const char *root);

/* Copies the bounded subgraph the Router is allowed to see.
 * Cap is clamped to HUSH_ROUTER_SUBGRAPH_MAX. */
hush_status_t hush_router_subgraph(const hush_router_graph_t *graph,
                                   hush_router_node_t *out, size_t *out_n,
                                   size_t cap);

/* Copies one thread in as a done work item. A second call is a no-op. */
hush_status_t hush_router_import_thread(hush_router_graph_t *graph,
                                        const char *thread_id,
                                        const char *title, int64_t now);

/* Writes ALWAYS-ON bodies when for_worker is 0.
 * Writes ON-CALL bodies when for_worker is 1. */
hush_status_t hush_router_select_skills(char *out, size_t outsz,
                                        const hush_router_skill_t *skills,
                                        size_t nskills, int for_worker);

/* Writes "N active · M blocked" into out. */
hush_status_t hush_router_context_line(const hush_router_graph_t *graph,
                                       char *out, size_t outsz);

/* One poll(2) step: if root/router exists, load, drain once, save. */
void hush_router_poll(void);

/* GET /api/router. Auth is enforced by the HTTP guard before this runs. */
hush_status_t hush_api_router_get(int fd);

/* POST /api/router. JSON fields: content, optional channel, optional specialist. */
hush_status_t hush_api_router_post(int fd, const char *req, size_t len);

#endif /* HUSH_ROUTER_H */
