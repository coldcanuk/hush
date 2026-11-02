/* hush_router.c: owns the Router graph under $HUSH_HOME/router. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "hush_home.h"
#include "hush_json.h"
#include "hush_router.h"

enum {
    HUSH_ROUTER_FILE_MAX = 16384,
    HUSH_ROUTER_ESCAPE_MAX = 1200,
    HUSH_ROUTER_PATH_MAX = 32,
    HUSH_ROUTER_PRIORITY = 1,
    HUSH_ROUTER_FILE_MODE = 0600,
    HUSH_ROUTER_LOCK_TRIES = 8 /* EINTR retries while waiting for router/lock */
};

/* No-index sentinel. UINT32_MAX does not fit an int enumerator (C11 6.7.2.2p2). */
static const uint32_t HUSH_ROUTER_NONE = UINT32_MAX;

/* hush_json_escape grows one byte to at most six (\u00XX). A buffer of
 * field size times this growth holds any escaped field without truncation. */
enum {
    HUSH_ROUTER_ESCAPE_GROWTH = 6,
    HUSH_ROUTER_ESC_ID = HUSH_ROUTER_ID_MAX * HUSH_ROUTER_ESCAPE_GROWTH,
    HUSH_ROUTER_ESC_NAME = HUSH_ROUTER_NAME_MAX * HUSH_ROUTER_ESCAPE_GROWTH,
    HUSH_ROUTER_ESC_CHANNEL = HUSH_ROUTER_CHANNEL_MAX * HUSH_ROUTER_ESCAPE_GROWTH,
    HUSH_ROUTER_ESC_JOB = HUSH_ROUTER_JOB_MAX * HUSH_ROUTER_ESCAPE_GROWTH
};

#define HUSH_ROUTER_TRY(expr)                       \
    do {                                            \
        hush_status_t hush_router_try_s_ = (expr);  \
        if (hush_router_try_s_ != HUSH_OK)          \
            return hush_router_try_s_;              \
    } while (0)

#define HUSH_ROUTER_DIR "router"
#define HUSH_ROUTER_CHANNEL "general"
#define HUSH_ROUTER_SPECIALIST "major"
#define HUSH_ROUTER_ANSWER "Major: no job. Say what to run."
#define HUSH_ROUTER_WHICH "Name the work item, for example WI-1."

static const char *const HUSH_ROUTER_PHASES[] = {
    "pending", "running", "blocked", "done", "failed"
};

static const char *const HUSH_ROUTER_EDGES[] = {
    "assigned", "depends_on", "blocked_by", "produced", "belongs_to", "needs"
};

/* One save writes these six files as a set. Each is staged as name plus
 * HUSH_ROUTER_STAGED; the commit mark makes the staged set authoritative
 * until every rename lands (#375 Floor F4). */
static const char HUSH_ROUTER_ITEMS_FILE[] = "workitems.json";
static const char HUSH_ROUTER_WORKERS_FILE[] = "workers.json";
static const char HUSH_ROUTER_DECISIONS_FILE[] = "decisions.json";
static const char HUSH_ROUTER_ARTIFACTS_FILE[] = "artifacts.json";
static const char HUSH_ROUTER_EDGES_FILE[] = "edges.json";
static const char HUSH_ROUTER_STATE_FILE[] = "router_state.json";
static const char HUSH_ROUTER_STAGED[] = ".tmp";
static const char HUSH_ROUTER_COMMIT[] = "commit";
/* POSIX record lock held across every load and save, so two processes on
 * one $HUSH_HOME never stage the fixed .tmp names at once (#375 FLR-F10). */
static const char HUSH_ROUTER_LOCK[] = "lock";

static const char *const HUSH_ROUTER_FILES[] = {
    HUSH_ROUTER_ITEMS_FILE, HUSH_ROUTER_WORKERS_FILE, HUSH_ROUTER_DECISIONS_FILE,
    HUSH_ROUTER_ARTIFACTS_FILE, HUSH_ROUTER_EDGES_FILE, HUSH_ROUTER_STATE_FILE
};

enum {
    HUSH_ROUTER_NFILES = sizeof HUSH_ROUTER_FILES / sizeof HUSH_ROUTER_FILES[0],
    HUSH_ROUTER_STAGED_PATH_MAX = HUSH_HOME_PATH_MAX + sizeof HUSH_ROUTER_STAGED
};

static const char *const HUSH_ROUTER_WORDS[] = {
    "run", "test", "build", "fix", "check", "deploy", "open", "write", "land"
};

static void hush_router_copy(char *dst, size_t dstsz, const char *src);
static int hush_router_is_legacy(const char *text);
static int hush_router_has_question(const char *text);
static int hush_router_has_work_word(const char *text);
static int hush_router_contains(const char *hay, const char *needle);
static size_t hush_router_count_phase(const hush_router_graph_t *graph,
                                      hush_router_phase_t phase);
static uint32_t hush_router_decision_at(const hush_router_graph_t *graph,
                                        const char *text);
static hush_router_route_t hush_router_classify(const hush_router_graph_t *graph,
                                                const char *text);
static hush_status_t hush_router_validate_note(const hush_router_graph_t *graph,
                                               const hush_router_result_t *out,
                                               const hush_router_note_t *note);
static void hush_router_clear_result(hush_router_result_t *out,
                                     hush_router_route_t route, const char *line);
static hush_status_t hush_router_push_edge(hush_router_graph_t *graph,
                                           const char *from,
                                           hush_router_edge_kind_t kind,
                                           const char *to);
static hush_status_t hush_router_open_item(hush_router_graph_t *graph,
                                          hush_router_result_t *out,
                                          const hush_router_note_t *note);
static hush_status_t hush_router_apply_decision(hush_router_graph_t *graph,
                                                hush_router_result_t *out,
                                                const hush_router_note_t *note,
                                                uint32_t index);
static const char *hush_router_phase_name(hush_router_phase_t phase);
static hush_status_t hush_router_phase_parse(hush_router_phase_t *out,
                                             const char *name);
static const char *hush_router_edge_name(hush_router_edge_kind_t kind);
static hush_status_t hush_router_edge_parse(hush_router_edge_kind_t *out,
                                            const char *name);
static int hush_router_needs_block(const hush_router_item_t *item);
/* Index of the item the worker serves. A worker with no matching item
 * maps to items[0], the drain default since the Router landed. Pure. */
static size_t hush_router_item_index(const hush_router_graph_t *graph,
                                     const hush_router_worker_t *worker);
/* Phase a live worker enters on its next drain step: pending runs, an
 * undecided rate-limit item blocks, anything else is done. Pure. */
static hush_router_phase_t hush_router_next_phase(const hush_router_graph_t *graph,
                                                  const hush_router_worker_t *worker,
                                                  const hush_router_item_t *item);
/* Writes "a<item_id>" into out. Fails with HUSH_ERR_FULL when it does not fit. */
static hush_status_t hush_router_artifact_id(char *out, size_t outsz,
                                             const char *item_id);
/* True when every row the worker's next step appends fits the graph. The
 * done step appends an artifact and its produced edge as one pair, or
 * neither when the artifact table is already full. Pure. */
static int hush_router_has_room(const hush_router_graph_t *graph,
                                const hush_router_worker_t *worker);
/* Appends the worker's "done" artifact and its produced edge as one pair.
 * Appends neither when the artifact table is full. Fails with HUSH_ERR_FULL
 * before any write when the id or the edge does not fit. */
static hush_status_t hush_router_record_artifact(hush_router_graph_t *graph,
                                                 const hush_router_worker_t *worker);
/* Index of the first live (pending or running) worker whose next step has
 * room. Skipping a stalled worker keeps it from starving the ones behind
 * it. Returns HUSH_ROUTER_NONE when no live worker can step. */
static uint32_t hush_router_queued(const hush_router_graph_t *graph);
/* True for the phases drain still advances: pending and running. Pure. */
static int hush_router_is_live(hush_router_phase_t phase);
/* Status when no worker can step: HUSH_ERR_NOT_FOUND when nothing is live,
 * HUSH_ERR_FULL when every live worker waits for graph room. */
static hush_status_t hush_router_idle_status(const hush_router_graph_t *graph);
/* Writes the status line for an item entering phase. Fails with
 * HUSH_ERR_FULL when the line does not fit. */
static hush_status_t hush_router_phase_line(char *out, size_t outsz,
                                            const char *item_id,
                                            hush_router_phase_t phase);
static hush_status_t hush_router_dir(char *out, size_t outsz, const char *root);
/* Writes text to path plus HUSH_ROUTER_STAGED and fsyncs it; never touches
 * path itself. Fails with HUSH_ERR_FULL on a long path, HUSH_ERR_IO on IO. */
static hush_status_t hush_router_stage_file(const char *path, const char *text);
/* Writes path plus HUSH_ROUTER_STAGED into out. Fails with HUSH_ERR_FULL. */
static hush_status_t hush_router_staged_path(char *out, size_t outsz,
                                             const char *path);
/* Makes the staged set durable, marks it committed, then publishes it.
 * A crash before the mark keeps the old set. Fails with HUSH_ERR_IO. */
static hush_status_t hush_router_commit(const char *dir);
/* Rolls a marked set forward when a commit mark is present; without one,
 * removes the staged files an unmarked save left. Caller holds router/lock.
 * Fails with HUSH_ERR_IO. */
static hush_status_t hush_router_recover(const char *dir);
/* Unlinks every staged file in dir; a missing one is fine. Fails with
 * HUSH_ERR_IO, or HUSH_ERR_FULL on a long path. */
static hush_status_t hush_router_drop_staged(const char *dir);
/* Renames every staged file over its live name, then drops the commit mark.
 * Idempotent, so an interrupted publish can rerun. Fails with HUSH_ERR_IO. */
static hush_status_t hush_router_publish(const char *dir);
/* Renames one staged file over its live name. A missing staged file was
 * already published by an interrupted run. Fails with HUSH_ERR_IO. */
static hush_status_t hush_router_publish_file(const char *dir, const char *name);
/* Creates the empty commit mark in dir and fsyncs it. Fails with HUSH_ERR_IO. */
static hush_status_t hush_router_mark(const char *dir);
/* Removes the commit mark from dir. Fails with HUSH_ERR_IO. */
static hush_status_t hush_router_unmark(const char *dir);
/* Adapter over fsync of a directory, so renames in it survive power loss
 * (power-loss only: a process crash keeps the page cache, so no test pins
 * it). Fails with HUSH_ERR_IO. */
static hush_status_t hush_router_sync_dir(const char *dir);
/* Opens dir/lock and blocks until this process holds it exclusively. Writes
 * the holding fd to out_fd; the caller releases it with hush_router_unlock.
 * Fails with HUSH_ERR_NOT_FOUND when dir is missing, HUSH_ERR_DENIED when
 * this process may not write there (EACCES, EPERM, EROFS), HUSH_ERR_IO
 * otherwise. */
static hush_status_t hush_router_lock(int *out_fd, const char *dir);
/* Adapter over fcntl F_SETLKW for a type (F_WRLCK or F_RDLCK) lock, retrying
 * EINTR a bounded number of times. Fails with HUSH_ERR_IO. */
static hush_status_t hush_router_wait_lock(int fd, short type);
/* Releases a lock taken by hush_router_lock by closing its fd. */
static void hush_router_unlock(int fd);
/* Recovers, stages, and commits graph into dir. Caller holds router/lock. */
static hush_status_t hush_router_save_locked(const hush_router_graph_t *graph,
                                             const char *dir);
/* Recovers, then reads every router file in dir. Caller holds router/lock. */
static hush_status_t hush_router_load_locked(hush_router_graph_t *graph,
                                             const char *dir);
/* Read-only load of a dir this process may not write (#375 N4): holds a
 * shared lock on dir/lock when it exists, writes nothing, and fails with
 * HUSH_ERR_IO when a commit mark is pending, since only a writer can finish
 * that save. */
static hush_status_t hush_router_load_shared(hush_router_graph_t *graph,
                                             const char *dir);
/* HUSH_OK when dir holds no commit mark, HUSH_ERR_IO when it does or the
 * check fails. */
static hush_status_t hush_router_unmarked(const char *dir);
/* Reads every published router file in dir into graph. Writes nothing. */
static hush_status_t hush_router_load_published(hush_router_graph_t *graph,
                                                const char *dir);
static hush_status_t hush_router_read_file(char *out, size_t outsz,
                                           const char *path);
static hush_status_t hush_router_append(char *out, size_t outsz, size_t *off,
                                        const char *piece);
static hush_status_t hush_router_save_items(const hush_router_graph_t *graph,
                                            const char *dir);
/* Formats one workitems.json object with every string field JSON-escaped.
 * sep ("" or ",") prefixes the object. Fails with HUSH_ERR_FULL on overflow. */
static hush_status_t hush_router_item_row(char *out, size_t outsz,
                                          const hush_router_item_t *item,
                                          const char *sep);
static hush_status_t hush_router_save_workers(const hush_router_graph_t *graph,
                                              const char *dir);
/* Formats one workers.json object with every string field JSON-escaped.
 * sep ("" or ",") prefixes the object. Fails with HUSH_ERR_FULL on overflow. */
static hush_status_t hush_router_worker_row(char *out, size_t outsz,
                                            const hush_router_worker_t *worker,
                                            const char *sep);
static hush_status_t hush_router_save_rest(const hush_router_graph_t *graph,
                                           const char *dir);
static hush_status_t hush_router_load_items(hush_router_graph_t *graph,
                                            const char *dir);
static hush_status_t hush_router_load_workers(hush_router_graph_t *graph,
                                              const char *dir);
static hush_status_t hush_router_load_rest(hush_router_graph_t *graph,
                                           const char *dir);
static void hush_router_bump_id(hush_router_graph_t *graph);
static hush_status_t hush_router_push_node(hush_router_node_t *out, size_t *n,
                                           size_t cap, const char *kind,
                                           const char *id);
static int hush_router_has_item(const hush_router_graph_t *graph,
                                const char *id);
static int hush_router_has_decision(const hush_router_graph_t *graph,
                                    const char *item_id);
static int hush_router_word_at(const char *text, size_t at, const char *word);
static int hush_router_id_follows(const char *text, const char *mark,
                                  const char *id);
static hush_status_t hush_router_escape(char *out, size_t outsz, const char *in);
static hush_status_t hush_router_join_file(char *out, size_t outsz,
                                           const char *dir, const char *name);
static hush_status_t hush_router_save_decisions(const hush_router_graph_t *graph,
                                                const char *dir);
/* Formats one decisions.json object with every string field JSON-escaped.
 * sep ("" or ",") prefixes the object. Fails with HUSH_ERR_FULL on overflow. */
static hush_status_t hush_router_decision_row(char *out, size_t outsz,
                                              const hush_router_decision_t *decision,
                                              const char *sep);
static hush_status_t hush_router_save_artifacts(const hush_router_graph_t *graph,
                                                const char *dir);
/* Formats one artifacts.json object with every string field JSON-escaped.
 * sep ("" or ",") prefixes the object. Fails with HUSH_ERR_FULL on overflow. */
static hush_status_t hush_router_artifact_row(char *out, size_t outsz,
                                              const hush_router_artifact_t *artifact,
                                              const char *sep);
static hush_status_t hush_router_save_edges(const hush_router_graph_t *graph,
                                            const char *dir);
/* Formats one edges.json object with both endpoints JSON-escaped.
 * sep ("" or ",") prefixes the object. Fails with HUSH_ERR_FULL on overflow. */
static hush_status_t hush_router_edge_row(char *out, size_t outsz,
                                          const hush_router_edge_t *edge,
                                          const char *sep);
static hush_status_t hush_router_save_state(const hush_router_graph_t *graph,
                                            const char *dir);
static hush_status_t hush_router_take_field(char *out, size_t outsz,
                                            const char *json, const char *path);
static hush_status_t hush_router_take_i64(int64_t *out, const char *json,
                                          const char *path);
static hush_status_t hush_router_load_decisions(hush_router_graph_t *graph,
                                                const char *dir);
static hush_status_t hush_router_load_artifacts(hush_router_graph_t *graph,
                                                const char *dir);
static hush_status_t hush_router_load_edges(hush_router_graph_t *graph,
                                            const char *dir);
static hush_status_t hush_router_load_state(hush_router_graph_t *graph,
                                            const char *dir);

void hush_router_init(hush_router_graph_t *graph)
{
    if (graph == NULL)
        return;
    memset(graph, 0, sizeof(*graph));
    graph->next_id = 1;
}

hush_status_t hush_router_accept(hush_router_graph_t *graph,
                                 hush_router_result_t *out,
                                 const hush_router_note_t *note)
{
    hush_router_route_t route;
    uint32_t index;

    HUSH_ROUTER_TRY(hush_router_validate_note(graph, out, note));
    route = hush_router_classify(graph, note->text);
    if (route == HUSH_ROUTER_ROUTE_LEGACY) {
        hush_router_clear_result(out, route, "");
        return HUSH_OK;
    }
    if (route == HUSH_ROUTER_ROUTE_ANSWER) {
        hush_router_clear_result(out, route, HUSH_ROUTER_ANSWER);
        return HUSH_OK;
    }
    if (route == HUSH_ROUTER_ROUTE_AMBIGUOUS) {
        hush_router_clear_result(out, route, HUSH_ROUTER_WHICH);
        return HUSH_OK;
    }
    if (route == HUSH_ROUTER_ROUTE_DECISION) {
        index = hush_router_decision_at(graph, note->text);
        assert(index < graph->nitems);
        return hush_router_apply_decision(graph, out, note, index);
    }
    return hush_router_open_item(graph, out, note);
}

hush_status_t hush_router_drain(hush_router_graph_t *graph, char *out_line,
                                size_t linesz, int64_t now)
{
    uint32_t index;
    hush_router_worker_t *worker;
    hush_router_item_t *item;
    hush_router_phase_t next;

    if (graph == NULL || out_line == NULL || linesz == 0)
        return HUSH_ERR_ARG;
    out_line[0] = '\0';
    index = hush_router_queued(graph);
    if (index == HUSH_ROUTER_NONE)
        return hush_router_idle_status(graph);
    worker = &graph->workers[index];
    item = &graph->items[hush_router_item_index(graph, worker)];
    next = hush_router_next_phase(graph, worker, item);
    HUSH_ROUTER_TRY(hush_router_phase_line(out_line, linesz, item->id, next));
    /* Every fallible step runs before the phase writes, so worker and item
     * change phase together or not at all (#375 Floor F2). */
    if (next == HUSH_ROUTER_DONE)
        HUSH_ROUTER_TRY(hush_router_record_artifact(graph, worker));
    if (next == HUSH_ROUTER_RUNNING)
        worker->started = now;
    worker->phase = next;
    item->phase = next;
    worker->heartbeat = now;
    item->updated = now;
    hush_router_copy(graph->last_line, sizeof graph->last_line, out_line);
    return HUSH_OK;
}

hush_status_t hush_router_save(const hush_router_graph_t *graph,
                               const char *root)
{
    char dir[HUSH_HOME_PATH_MAX];
    hush_status_t status;
    int lock_fd = -1;

    if (graph == NULL || root == NULL || root[0] == '\0')
        return HUSH_ERR_ARG;
    if (hush_router_dir(dir, sizeof dir, root) != HUSH_OK)
        return HUSH_ERR_FULL;
    status = hush_home_mkdir(dir);
    if (status != HUSH_OK)
        return status;
    status = hush_router_lock(&lock_fd, dir);
    if (status == HUSH_ERR_DENIED)
        return HUSH_ERR_IO; /* a save must write */
    if (status != HUSH_OK)
        return status;
    status = hush_router_save_locked(graph, dir);
    hush_router_unlock(lock_fd);
    return status;
}

hush_status_t hush_router_load(hush_router_graph_t *graph, const char *root)
{
    char dir[HUSH_HOME_PATH_MAX];
    hush_status_t status;
    int lock_fd = -1;

    if (graph == NULL || root == NULL || root[0] == '\0')
        return HUSH_ERR_ARG;
    hush_router_init(graph);
    if (hush_router_dir(dir, sizeof dir, root) != HUSH_OK)
        return HUSH_ERR_FULL;
    status = hush_router_lock(&lock_fd, dir);
    if (status == HUSH_ERR_NOT_FOUND)
        return HUSH_OK; /* no router/ yet: the empty graph is the state */
    if (status == HUSH_ERR_DENIED)
        return hush_router_load_shared(graph, dir);
    if (status != HUSH_OK)
        return status;
    status = hush_router_load_locked(graph, dir);
    hush_router_unlock(lock_fd);
    return status;
}

hush_status_t hush_router_subgraph(const hush_router_graph_t *graph,
                                   hush_router_node_t *out, size_t *out_n,
                                   size_t cap)
{
    size_t n = 0;
    size_t limit;

    if (graph == NULL || out == NULL || out_n == NULL)
        return HUSH_ERR_ARG;
    limit = cap;
    if (limit > (size_t)HUSH_ROUTER_SUBGRAPH_MAX)
        limit = (size_t)HUSH_ROUTER_SUBGRAPH_MAX;
    for (size_t i = 0; i < graph->nitems && n < limit; ++i) {
        hush_router_phase_t phase = graph->items[i].phase;
        if (phase == HUSH_ROUTER_DONE || phase == HUSH_ROUTER_FAILED)
            continue;
        HUSH_ROUTER_TRY(hush_router_push_node(out, &n, limit, "item",
                                              graph->items[i].id));
    }
    for (size_t i = 0; i < graph->nworkers && n < limit; ++i) {
        HUSH_ROUTER_TRY(hush_router_push_node(out, &n, limit, "worker",
                                              graph->workers[i].id));
    }
    for (size_t i = 0; i < graph->ndecisions && n < limit; ++i) {
        size_t at = graph->ndecisions - 1 - i;
        HUSH_ROUTER_TRY(hush_router_push_node(out, &n, limit, "decision",
                                              graph->decisions[at].id));
    }
    for (size_t i = 0; i < graph->nartifacts && n < limit; ++i) {
        HUSH_ROUTER_TRY(hush_router_push_node(out, &n, limit, "artifact",
                                              graph->artifacts[i].id));
    }
    *out_n = n;
    return HUSH_OK;
}

hush_status_t hush_router_import_thread(hush_router_graph_t *graph,
                                        const char *thread_id,
                                        const char *title, int64_t now)
{
    hush_router_item_t *item;
    char id[HUSH_ROUTER_ID_MAX];
    size_t j = 0;

    if (graph == NULL || thread_id == NULL || thread_id[0] == '\0')
        return HUSH_ERR_ARG;
    for (size_t i = 0; thread_id[i] != '\0' && j + 1 < sizeof id; ++i) {
        unsigned char ch = (unsigned char)thread_id[i];
        if (isalnum(ch))
            id[j++] = (char)tolower(ch);
    }
    id[j] = '\0';
    if (id[0] == '\0')
        return HUSH_ERR_ARG;
    if (hush_router_has_item(graph, id))
        return HUSH_OK;
    if (graph->nitems >= (size_t)HUSH_ROUTER_ITEMS_MAX)
        return HUSH_ERR_FULL;
    item = &graph->items[graph->nitems];
    memset(item, 0, sizeof(*item));
    hush_router_copy(item->id, sizeof item->id, id);
    hush_router_copy(item->title, sizeof item->title,
                     title != NULL ? title : id);
    item->phase = HUSH_ROUTER_DONE;
    hush_router_copy(item->channel_id, sizeof item->channel_id,
                     HUSH_ROUTER_CHANNEL);
    item->created = now;
    item->updated = now;
    item->priority = HUSH_ROUTER_PRIORITY;
    graph->nitems++;
    return hush_router_push_edge(graph, item->id, HUSH_ROUTER_EDGE_BELONGS,
                                 item->channel_id);
}

hush_status_t hush_router_select_skills(char *out, size_t outsz,
                                        const hush_router_skill_t *skills,
                                        size_t nskills, int for_worker)
{
    size_t off = 0;
    const char *head;

    if (out == NULL || outsz == 0 || (skills == NULL && nskills > 0))
        return HUSH_ERR_ARG;
    if (nskills > (size_t)HUSH_ROUTER_SKILLS_MAX)
        return HUSH_ERR_FULL;
    head = for_worker ? "WORKER\n" : "ROUTER\n";
    HUSH_ROUTER_TRY(hush_router_append(out, outsz, &off, head));
    for (size_t i = 0; i < nskills; ++i) {
        int want_worker = skills[i].tier == HUSH_ROUTER_TIER_ON_CALL;
        char row[HUSH_ROUTER_BODY_MAX + HUSH_ROUTER_NAME_MAX + 8];
        int n;

        if (want_worker != for_worker)
            continue;
        if (skills[i].tier != HUSH_ROUTER_TIER_ALWAYS &&
            skills[i].tier != HUSH_ROUTER_TIER_ON_CALL)
            continue;
        n = snprintf(row, sizeof row, "- %s: %s\n", skills[i].id,
                     skills[i].body);
        if (n < 0 || (size_t)n >= sizeof row)
            return HUSH_ERR_FULL;
        HUSH_ROUTER_TRY(hush_router_append(out, outsz, &off, row));
    }
    return HUSH_OK;
}

hush_status_t hush_router_context_line(const hush_router_graph_t *graph,
                                       char *out, size_t outsz)
{
    size_t active;
    size_t blocked;
    int n;

    if (graph == NULL || out == NULL || outsz == 0)
        return HUSH_ERR_ARG;
    active = hush_router_count_phase(graph, HUSH_ROUTER_PENDING);
    active += hush_router_count_phase(graph, HUSH_ROUTER_RUNNING);
    active += hush_router_count_phase(graph, HUSH_ROUTER_BLOCKED);
    blocked = hush_router_count_phase(graph, HUSH_ROUTER_BLOCKED);
    n = snprintf(out, outsz, "%u active · %u blocked", (unsigned)active,
                 (unsigned)blocked);
    if (n < 0 || (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    return HUSH_OK;
}

void hush_router_poll(void)
{
    char root[HUSH_HOME_PATH_MAX];
    char dir[HUSH_HOME_PATH_MAX];
    struct stat st;
    hush_router_graph_t graph;
    char line[HUSH_ROUTER_LINE_MAX];

    hush_home_root(root, sizeof root);
    if (root[0] == '\0')
        return;
    if (hush_router_dir(dir, sizeof dir, root) != HUSH_OK)
        return;
    if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode))
        return;
    if (hush_router_load(&graph, root) != HUSH_OK)
        return;
    if (hush_router_drain(&graph, line, sizeof line, (int64_t)time(NULL)) !=
        HUSH_OK)
        return;
    (void)hush_router_save(&graph, root);
}

static void hush_router_copy(char *dst, size_t dstsz, const char *src)
{
    size_t i = 0;

    assert(dst != NULL && dstsz > 0);
    if (src == NULL)
        src = "";
    while (src[i] != '\0' && i + 1 < dstsz) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int hush_router_is_legacy(const char *text)
{
    const char *at;

    assert(text != NULL);
    if (strstr(text, "nostr:") != NULL)
        return 1;
    at = text;
    while ((at = strchr(at, '@')) != NULL) {
        if (at == text || !isalnum((unsigned char)at[-1]))
            return 1;
        at++;
    }
    return 0;
}

static int hush_router_has_question(const char *text)
{
    assert(text != NULL);
    return strchr(text, '?') != NULL;
}

static int hush_router_word_at(const char *text, size_t at, const char *word)
{
    size_t n = strlen(word);

    if (strncmp(text + at, word, n) != 0)
        return 0;
    if (at > 0 && isalpha((unsigned char)text[at - 1]))
        return 0;
    if (isalpha((unsigned char)text[at + n]))
        return 0;
    return 1;
}

static int hush_router_has_work_word(const char *text)
{
    char folded[HUSH_ROUTER_TEXT_MAX];
    size_t i;

    assert(text != NULL);
    hush_router_copy(folded, sizeof folded, text);
    for (i = 0; folded[i] != '\0'; ++i)
        folded[i] = (char)tolower((unsigned char)folded[i]);
    if (hush_router_contains(folded, "rate-limit"))
        return 1;
    if (hush_router_contains(folded, "rate limit"))
        return 1;
    for (i = 0; folded[i] != '\0'; ++i) {
        size_t w;
        for (w = 0; w < sizeof HUSH_ROUTER_WORDS / sizeof HUSH_ROUTER_WORDS[0];
             ++w) {
            if (hush_router_word_at(folded, i, HUSH_ROUTER_WORDS[w]))
                return 1;
        }
    }
    return 0;
}

static int hush_router_contains(const char *hay, const char *needle)
{
    assert(hay != NULL && needle != NULL);
    return strstr(hay, needle) != NULL;
}

static size_t hush_router_count_phase(const hush_router_graph_t *graph,
                                      hush_router_phase_t phase)
{
    size_t n = 0;

    assert(graph != NULL);
    for (size_t i = 0; i < graph->nitems; ++i) {
        if (graph->items[i].phase == phase)
            n++;
    }
    return n;
}

static int hush_router_id_follows(const char *text, const char *mark,
                                  const char *id)
{
    const char *hit = text;
    size_t n = strlen(id);

    assert(text != NULL && mark != NULL && id != NULL);
    while ((hit = strstr(hit, mark)) != NULL) {
        const char *p = hit + strlen(mark);
        if (strncmp(p, id, n) == 0 &&
            !isalnum((unsigned char)p[n]))
            return 1;
        hit += strlen(mark);
    }
    return 0;
}

static uint32_t hush_router_decision_at(const hush_router_graph_t *graph,
                                        const char *text)
{
    size_t blocked = 0;
    uint32_t only = HUSH_ROUTER_NONE;

    assert(graph != NULL && text != NULL);
    for (size_t i = 0; i < graph->nitems; ++i) {
        if (graph->items[i].phase != HUSH_ROUTER_BLOCKED)
            continue;
        blocked++;
        only = (uint32_t)i;
        if (hush_router_id_follows(text, "WI-", graph->items[i].id))
            return (uint32_t)i;
        if (hush_router_id_follows(text, "#", graph->items[i].id))
            return (uint32_t)i;
    }
    if (blocked == 1 && !hush_router_has_work_word(text))
        return only;
    return HUSH_ROUTER_NONE;
}

static hush_router_route_t hush_router_classify(const hush_router_graph_t *graph,
                                                const char *text)
{
    size_t blocked;

    assert(graph != NULL && text != NULL);
    if (hush_router_is_legacy(text))
        return HUSH_ROUTER_ROUTE_LEGACY;
    if (hush_router_decision_at(graph, text) != HUSH_ROUTER_NONE)
        return HUSH_ROUTER_ROUTE_DECISION;
    blocked = hush_router_count_phase(graph, HUSH_ROUTER_BLOCKED);
    if (blocked > 1 && !hush_router_has_work_word(text) &&
        !hush_router_has_question(text))
        return HUSH_ROUTER_ROUTE_AMBIGUOUS;
    if (hush_router_has_work_word(text))
        return HUSH_ROUTER_ROUTE_WORK;
    if (hush_router_has_question(text))
        return HUSH_ROUTER_ROUTE_ANSWER;
    return HUSH_ROUTER_ROUTE_WORK;
}

static hush_status_t hush_router_validate_note(const hush_router_graph_t *graph,
                                               const hush_router_result_t *out,
                                               const hush_router_note_t *note)
{
    if (graph == NULL || out == NULL || note == NULL || note->text == NULL)
        return HUSH_ERR_ARG;
    if (note->text[0] == '\0')
        return HUSH_ERR_ARG;
    return HUSH_OK;
}

static void hush_router_clear_result(hush_router_result_t *out,
                                     hush_router_route_t route, const char *line)
{
    assert(out != NULL && line != NULL);
    memset(out, 0, sizeof(*out));
    out->route = route;
    hush_router_copy(out->line, sizeof out->line, line);
}

static hush_status_t hush_router_push_edge(hush_router_graph_t *graph,
                                           const char *from,
                                           hush_router_edge_kind_t kind,
                                           const char *to)
{
    hush_router_edge_t *edge;

    assert(graph != NULL && from != NULL && to != NULL);
    if (graph->nedges >= (size_t)HUSH_ROUTER_EDGES_MAX)
        return HUSH_ERR_FULL;
    edge = &graph->edges[graph->nedges];
    memset(edge, 0, sizeof(*edge));
    hush_router_copy(edge->from, sizeof edge->from, from);
    edge->kind = kind;
    hush_router_copy(edge->to, sizeof edge->to, to);
    graph->nedges++;
    return HUSH_OK;
}

static hush_status_t hush_router_open_item(hush_router_graph_t *graph,
                                          hush_router_result_t *out,
                                          const hush_router_note_t *note)
{
    hush_router_item_t *item;
    hush_router_worker_t *worker;
    char id[HUSH_ROUTER_ID_MAX];
    char wid[HUSH_ROUTER_ID_MAX];
    const char *channel;
    const char *specialist;
    int n;

    assert(graph != NULL && out != NULL && note != NULL);
    if (graph->nitems >= (size_t)HUSH_ROUTER_ITEMS_MAX)
        return HUSH_ERR_FULL;
    if (graph->nworkers >= (size_t)HUSH_ROUTER_WORKERS_MAX)
        return HUSH_ERR_FULL;
    n = snprintf(id, sizeof id, "%u", graph->next_id);
    if (n < 0 || (size_t)n >= sizeof id)
        return HUSH_ERR_FULL;
    n = snprintf(wid, sizeof wid, "w%s", id);
    if (n < 0 || (size_t)n >= sizeof wid)
        return HUSH_ERR_FULL;
    channel = note->channel != NULL && note->channel[0] != '\0'
                  ? note->channel
                  : HUSH_ROUTER_CHANNEL;
    specialist = note->specialist != NULL && note->specialist[0] != '\0'
                     ? note->specialist
                     : HUSH_ROUTER_SPECIALIST;
    item = &graph->items[graph->nitems];
    memset(item, 0, sizeof(*item));
    hush_router_copy(item->id, sizeof item->id, id);
    hush_router_copy(item->title, sizeof item->title, note->text);
    hush_router_copy(item->description, sizeof item->description, note->text);
    item->phase = HUSH_ROUTER_PENDING;
    hush_router_copy(item->channel_id, sizeof item->channel_id, channel);
    item->created = note->now;
    item->updated = note->now;
    hush_router_copy(item->worker_id, sizeof item->worker_id, wid);
    item->priority = HUSH_ROUTER_PRIORITY;
    worker = &graph->workers[graph->nworkers];
    memset(worker, 0, sizeof(*worker));
    hush_router_copy(worker->id, sizeof worker->id, wid);
    hush_router_copy(worker->item_id, sizeof worker->item_id, id);
    hush_router_copy(worker->specialist_id, sizeof worker->specialist_id,
                     specialist);
    worker->phase = HUSH_ROUTER_PENDING;
    n = snprintf(worker->job_dir, sizeof worker->job_dir, "jobs/%s", id);
    if (n < 0 || (size_t)n >= sizeof worker->job_dir)
        return HUSH_ERR_FULL;
    graph->nitems++;
    graph->nworkers++;
    graph->next_id++;
    HUSH_ROUTER_TRY(hush_router_push_edge(graph, id, HUSH_ROUTER_EDGE_ASSIGNED,
                                          wid));
    HUSH_ROUTER_TRY(hush_router_push_edge(graph, id, HUSH_ROUTER_EDGE_BELONGS,
                                          channel));
    n = snprintf(out->line, sizeof out->line, "Starting Work Item #%s: %s.",
                 id, item->title);
    if (n < 0 || (size_t)n >= sizeof out->line)
        return HUSH_ERR_FULL;
    out->route = HUSH_ROUTER_ROUTE_WORK;
    hush_router_copy(out->item_id, sizeof out->item_id, id);
    hush_router_copy(graph->last_line, sizeof graph->last_line, out->line);
    return HUSH_OK;
}

static hush_status_t hush_router_apply_decision(hush_router_graph_t *graph,
                                                hush_router_result_t *out,
                                                const hush_router_note_t *note,
                                                uint32_t index)
{
    hush_router_item_t *item;
    hush_router_decision_t *decision;
    int n;

    assert(graph != NULL && out != NULL && note != NULL);
    assert(index < graph->nitems);
    if (graph->ndecisions >= (size_t)HUSH_ROUTER_DECISIONS_MAX)
        return HUSH_ERR_FULL;
    item = &graph->items[index];
    decision = &graph->decisions[graph->ndecisions];
    memset(decision, 0, sizeof(*decision));
    {
        char item_id[HUSH_ROUTER_ID_MAX];

        hush_router_copy(item_id, sizeof item_id, item->id);
        n = snprintf(decision->id, sizeof decision->id, "d%s", item_id);
    }
    if (n < 0 || (size_t)n >= sizeof decision->id)
        return HUSH_ERR_FULL;
    hush_router_copy(decision->item_id, sizeof decision->item_id, item->id);
    hush_router_copy(decision->text, sizeof decision->text, note->text);
    decision->created = note->now;
    graph->ndecisions++;
    item->phase = HUSH_ROUTER_RUNNING;
    item->updated = note->now;
    for (size_t i = 0; i < graph->nworkers; ++i) {
        if (strcmp(graph->workers[i].item_id, item->id) != 0)
            continue;
        graph->workers[i].phase = HUSH_ROUTER_RUNNING;
        graph->workers[i].heartbeat = note->now;
    }
    HUSH_ROUTER_TRY(hush_router_push_edge(graph, item->id,
                                          HUSH_ROUTER_EDGE_NEEDS, decision->id));
    n = snprintf(out->line, sizeof out->line, "[WI-%s] Decision recorded.",
                 item->id);
    if (n < 0 || (size_t)n >= sizeof out->line)
        return HUSH_ERR_FULL;
    out->route = HUSH_ROUTER_ROUTE_DECISION;
    hush_router_copy(out->item_id, sizeof out->item_id, item->id);
    hush_router_copy(graph->last_line, sizeof graph->last_line, out->line);
    return HUSH_OK;
}

static const char *hush_router_phase_name(hush_router_phase_t phase)
{
    if ((unsigned)phase >= sizeof HUSH_ROUTER_PHASES / sizeof HUSH_ROUTER_PHASES[0])
        return "failed";
    return HUSH_ROUTER_PHASES[phase];
}

static hush_status_t hush_router_phase_parse(hush_router_phase_t *out,
                                             const char *name)
{
    size_t i;

    assert(out != NULL && name != NULL);
    for (i = 0; i < sizeof HUSH_ROUTER_PHASES / sizeof HUSH_ROUTER_PHASES[0];
         ++i) {
        if (strcmp(name, HUSH_ROUTER_PHASES[i]) == 0) {
            *out = (hush_router_phase_t)i;
            return HUSH_OK;
        }
    }
    return HUSH_ERR_PARSE;
}

static const char *hush_router_edge_name(hush_router_edge_kind_t kind)
{
    if ((unsigned)kind >= sizeof HUSH_ROUTER_EDGES / sizeof HUSH_ROUTER_EDGES[0])
        return "assigned";
    return HUSH_ROUTER_EDGES[kind];
}

static hush_status_t hush_router_edge_parse(hush_router_edge_kind_t *out,
                                            const char *name)
{
    size_t i;

    assert(out != NULL && name != NULL);
    for (i = 0; i < sizeof HUSH_ROUTER_EDGES / sizeof HUSH_ROUTER_EDGES[0]; ++i) {
        if (strcmp(name, HUSH_ROUTER_EDGES[i]) == 0) {
            *out = (hush_router_edge_kind_t)i;
            return HUSH_OK;
        }
    }
    return HUSH_ERR_PARSE;
}

static int hush_router_needs_block(const hush_router_item_t *item)
{
    char folded[HUSH_ROUTER_TEXT_MAX];

    assert(item != NULL);
    hush_router_copy(folded, sizeof folded, item->description);
    for (size_t i = 0; folded[i] != '\0'; ++i)
        folded[i] = (char)tolower((unsigned char)folded[i]);
    if (hush_router_contains(folded, "rate-limit"))
        return 1;
    return hush_router_contains(folded, "rate limit");
}

static size_t hush_router_item_index(const hush_router_graph_t *graph,
                                     const hush_router_worker_t *worker)
{
    assert(graph != NULL && worker != NULL);
    for (size_t i = 0; i < graph->nitems; ++i) {
        if (strcmp(graph->items[i].id, worker->item_id) == 0)
            return i;
    }
    return 0;
}

static hush_router_phase_t hush_router_next_phase(const hush_router_graph_t *graph,
                                                  const hush_router_worker_t *worker,
                                                  const hush_router_item_t *item)
{
    assert(graph != NULL && worker != NULL && item != NULL);
    if (worker->phase == HUSH_ROUTER_PENDING)
        return HUSH_ROUTER_RUNNING;
    if (hush_router_needs_block(item) && !hush_router_has_decision(graph, item->id))
        return HUSH_ROUTER_BLOCKED;
    return HUSH_ROUTER_DONE;
}

static hush_status_t hush_router_artifact_id(char *out, size_t outsz,
                                             const char *item_id)
{
    int n;

    assert(out != NULL && item_id != NULL);
    n = snprintf(out, outsz, "a%s", item_id);
    if (n < 0 || (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    return HUSH_OK;
}

static int hush_router_has_room(const hush_router_graph_t *graph,
                                const hush_router_worker_t *worker)
{
    char id[HUSH_ROUTER_ID_MAX];
    const hush_router_item_t *item;

    assert(graph != NULL && worker != NULL);
    item = &graph->items[hush_router_item_index(graph, worker)];
    if (hush_router_next_phase(graph, worker, item) != HUSH_ROUTER_DONE)
        return 1;
    if (graph->nartifacts >= (size_t)HUSH_ROUTER_ARTIFACTS_MAX)
        return 1;
    if (graph->nedges >= (size_t)HUSH_ROUTER_EDGES_MAX)
        return 0;
    return hush_router_artifact_id(id, sizeof id, worker->item_id) == HUSH_OK;
}

static hush_status_t hush_router_record_artifact(hush_router_graph_t *graph,
                                                 const hush_router_worker_t *worker)
{
    char id[HUSH_ROUTER_ID_MAX];
    hush_router_artifact_t *artifact;

    assert(graph != NULL && worker != NULL);
    assert(hush_router_has_room(graph, worker));
    if (graph->nartifacts >= (size_t)HUSH_ROUTER_ARTIFACTS_MAX)
        return HUSH_OK;
    HUSH_ROUTER_TRY(hush_router_artifact_id(id, sizeof id, worker->item_id));
    /* The edge is the only fallible write, so it goes first. */
    HUSH_ROUTER_TRY(hush_router_push_edge(graph, worker->id,
                                          HUSH_ROUTER_EDGE_PRODUCED, id));
    artifact = &graph->artifacts[graph->nartifacts];
    memset(artifact, 0, sizeof(*artifact));
    hush_router_copy(artifact->id, sizeof artifact->id, id);
    hush_router_copy(artifact->worker_id, sizeof artifact->worker_id, worker->id);
    hush_router_copy(artifact->text, sizeof artifact->text, "done");
    graph->nartifacts++;
    return HUSH_OK;
}

static uint32_t hush_router_queued(const hush_router_graph_t *graph)
{
    assert(graph != NULL);
    for (size_t i = 0; i < graph->nworkers; ++i) {
        if (!hush_router_is_live(graph->workers[i].phase))
            continue;
        if (hush_router_has_room(graph, &graph->workers[i]))
            return (uint32_t)i;
    }
    return HUSH_ROUTER_NONE;
}

static int hush_router_is_live(hush_router_phase_t phase)
{
    return phase == HUSH_ROUTER_PENDING || phase == HUSH_ROUTER_RUNNING;
}

static hush_status_t hush_router_idle_status(const hush_router_graph_t *graph)
{
    assert(graph != NULL);
    assert(hush_router_queued(graph) == HUSH_ROUTER_NONE);
    for (size_t i = 0; i < graph->nworkers; ++i) {
        if (hush_router_is_live(graph->workers[i].phase))
            return HUSH_ERR_FULL;
    }
    return HUSH_ERR_NOT_FOUND;
}

static hush_status_t hush_router_phase_line(char *out, size_t outsz,
                                            const char *item_id,
                                            hush_router_phase_t phase)
{
    int n;

    assert(out != NULL && item_id != NULL);
    switch (phase) {
    case HUSH_ROUTER_RUNNING:
        n = snprintf(out, outsz, "[WI-%s] Working...", item_id);
        break;
    case HUSH_ROUTER_BLOCKED:
        n = snprintf(out, outsz, "[WI-%s] Blocked - reply with 60s or 300s.",
                     item_id);
        break;
    default:
        n = snprintf(out, outsz, "[WI-%s] Done.", item_id);
        break;
    }
    if (n < 0 || (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    return HUSH_OK;
}

static hush_status_t hush_router_dir(char *out, size_t outsz, const char *root)
{
    assert(out != NULL && root != NULL);
    hush_home_join(out, outsz, root, HUSH_ROUTER_DIR);
    if (out[0] == '\0')
        return HUSH_ERR_FULL;
    return HUSH_OK;
}

static hush_status_t hush_router_stage_file(const char *path, const char *text)
{
    char staged[HUSH_ROUTER_STAGED_PATH_MAX];
    size_t len;
    ssize_t written;
    int fd;

    assert(path != NULL && text != NULL);
    HUSH_ROUTER_TRY(hush_router_staged_path(staged, sizeof staged, path));
    len = strlen(text);
    fd = open(staged, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW,
              HUSH_ROUTER_FILE_MODE);
    if (fd < 0)
        return HUSH_ERR_IO;
    written = write(fd, text, len);
    if (written != (ssize_t)len || fsync(fd) != 0) {
        (void)close(fd);
        (void)unlink(staged);
        return HUSH_ERR_IO;
    }
    if (close(fd) != 0) {
        (void)unlink(staged);
        return HUSH_ERR_IO;
    }
    return HUSH_OK;
}

static hush_status_t hush_router_staged_path(char *out, size_t outsz,
                                             const char *path)
{
    int n;

    assert(out != NULL && path != NULL);
    n = snprintf(out, outsz, "%s%s", path, HUSH_ROUTER_STAGED);
    if (n <= 0 || (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    return HUSH_OK;
}

static hush_status_t hush_router_commit(const char *dir)
{
    assert(dir != NULL);
    /* Staged names must be durable before the mark can vouch for them
     * (power-loss only, not test-pinned). */
    HUSH_ROUTER_TRY(hush_router_sync_dir(dir));
    HUSH_ROUTER_TRY(hush_router_mark(dir));
    HUSH_ROUTER_TRY(hush_router_sync_dir(dir));
    return hush_router_publish(dir);
}

static hush_status_t hush_router_recover(const char *dir)
{
    char mark[HUSH_HOME_PATH_MAX];

    assert(dir != NULL);
    HUSH_ROUTER_TRY(hush_router_join_file(mark, sizeof mark, dir,
                                          HUSH_ROUTER_COMMIT));
    if (access(mark, F_OK) == 0)
        return hush_router_publish(dir);
    if (errno != ENOENT)
        return HUSH_ERR_IO;
    return hush_router_drop_staged(dir);
}

static hush_status_t hush_router_drop_staged(const char *dir)
{
    char path[HUSH_HOME_PATH_MAX];
    char staged[HUSH_ROUTER_STAGED_PATH_MAX];

    assert(dir != NULL);
    for (size_t i = 0; i < (size_t)HUSH_ROUTER_NFILES; ++i) {
        HUSH_ROUTER_TRY(hush_router_join_file(path, sizeof path, dir,
                                              HUSH_ROUTER_FILES[i]));
        HUSH_ROUTER_TRY(hush_router_staged_path(staged, sizeof staged, path));
        if (unlink(staged) != 0 && errno != ENOENT)
            return HUSH_ERR_IO;
    }
    return HUSH_OK;
}

static hush_status_t hush_router_publish(const char *dir)
{
    assert(dir != NULL);
    for (size_t i = 0; i < (size_t)HUSH_ROUTER_NFILES; ++i)
        HUSH_ROUTER_TRY(hush_router_publish_file(dir, HUSH_ROUTER_FILES[i]));
    /* Every rename must be durable before the mark stops vouching
     * (power-loss only, not test-pinned). */
    HUSH_ROUTER_TRY(hush_router_sync_dir(dir));
    HUSH_ROUTER_TRY(hush_router_unmark(dir));
    return hush_router_sync_dir(dir);
}

static hush_status_t hush_router_publish_file(const char *dir, const char *name)
{
    char path[HUSH_HOME_PATH_MAX];
    char staged[HUSH_ROUTER_STAGED_PATH_MAX];

    assert(dir != NULL && name != NULL);
    HUSH_ROUTER_TRY(hush_router_join_file(path, sizeof path, dir, name));
    HUSH_ROUTER_TRY(hush_router_staged_path(staged, sizeof staged, path));
    if (rename(staged, path) == 0)
        return HUSH_OK;
    if (errno == ENOENT)
        return HUSH_OK;
    return HUSH_ERR_IO;
}

static hush_status_t hush_router_mark(const char *dir)
{
    char mark[HUSH_HOME_PATH_MAX];
    int fd;

    assert(dir != NULL);
    HUSH_ROUTER_TRY(hush_router_join_file(mark, sizeof mark, dir,
                                          HUSH_ROUTER_COMMIT));
    fd = open(mark, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW,
              HUSH_ROUTER_FILE_MODE);
    if (fd < 0)
        return HUSH_ERR_IO;
    if (fsync(fd) != 0) {
        (void)close(fd);
        return HUSH_ERR_IO;
    }
    if (close(fd) != 0)
        return HUSH_ERR_IO;
    return HUSH_OK;
}

static hush_status_t hush_router_unmark(const char *dir)
{
    char mark[HUSH_HOME_PATH_MAX];

    assert(dir != NULL);
    HUSH_ROUTER_TRY(hush_router_join_file(mark, sizeof mark, dir,
                                          HUSH_ROUTER_COMMIT));
    if (unlink(mark) == 0 || errno == ENOENT)
        return HUSH_OK;
    return HUSH_ERR_IO;
}

static hush_status_t hush_router_sync_dir(const char *dir)
{
    int fd;

    assert(dir != NULL);
    fd = open(dir, O_RDONLY | O_DIRECTORY);
    if (fd < 0)
        return HUSH_ERR_IO;
    if (fsync(fd) != 0) {
        (void)close(fd);
        return HUSH_ERR_IO;
    }
    if (close(fd) != 0)
        return HUSH_ERR_IO;
    return HUSH_OK;
}

static hush_status_t hush_router_lock(int *out_fd, const char *dir)
{
    char path[HUSH_HOME_PATH_MAX];
    int fd;

    assert(out_fd != NULL && dir != NULL);
    HUSH_ROUTER_TRY(hush_router_join_file(path, sizeof path, dir, HUSH_ROUTER_LOCK));
    fd = open(path, O_RDWR | O_CREAT | O_NOFOLLOW, HUSH_ROUTER_FILE_MODE);
    if (fd < 0 && errno == ENOENT)
        return HUSH_ERR_NOT_FOUND;
    if (fd < 0)
        return errno == EACCES || errno == EPERM || errno == EROFS ? HUSH_ERR_DENIED
                                                                   : HUSH_ERR_IO;
    if (hush_router_wait_lock(fd, F_WRLCK) != HUSH_OK) {
        (void)close(fd);
        return HUSH_ERR_IO;
    }
    *out_fd = fd;
    return HUSH_OK;
}

static hush_status_t hush_router_wait_lock(int fd, short type)
{
    struct flock lock = {.l_type = type, .l_whence = SEEK_SET};

    assert(fd >= 0 && (type == F_WRLCK || type == F_RDLCK));
    for (int i = 0; i < HUSH_ROUTER_LOCK_TRIES; ++i) {
        if (fcntl(fd, F_SETLKW, &lock) == 0)
            return HUSH_OK;
        if (errno != EINTR)
            return HUSH_ERR_IO;
    }
    return HUSH_ERR_IO;
}

static void hush_router_unlock(int fd)
{
    assert(fd >= 0);
    /* Closing the only fd on router/lock drops this process's record lock. */
    (void)close(fd);
}

static hush_status_t hush_router_save_locked(const hush_router_graph_t *graph,
                                             const char *dir)
{
    assert(graph != NULL && dir != NULL);
    /* Finish any marked set first, so staging never overwrites one. */
    HUSH_ROUTER_TRY(hush_router_recover(dir));
    HUSH_ROUTER_TRY(hush_router_save_items(graph, dir));
    HUSH_ROUTER_TRY(hush_router_save_workers(graph, dir));
    HUSH_ROUTER_TRY(hush_router_save_rest(graph, dir));
    return hush_router_commit(dir);
}

static hush_status_t hush_router_load_locked(hush_router_graph_t *graph,
                                             const char *dir)
{
    assert(graph != NULL && dir != NULL);
    HUSH_ROUTER_TRY(hush_router_recover(dir));
    return hush_router_load_published(graph, dir);
}

static hush_status_t hush_router_load_shared(hush_router_graph_t *graph,
                                             const char *dir)
{
    char path[HUSH_HOME_PATH_MAX];
    hush_status_t status;
    int fd;

    assert(graph != NULL && dir != NULL);
    HUSH_ROUTER_TRY(hush_router_join_file(path, sizeof path, dir, HUSH_ROUTER_LOCK));
    fd = open(path, O_RDONLY | O_NOFOLLOW);
    if (fd < 0 && errno != ENOENT)
        return HUSH_ERR_IO;
    /* No lock file in an unwritable dir: no writer here could have made one. */
    if (fd >= 0 && hush_router_wait_lock(fd, F_RDLCK) != HUSH_OK) {
        (void)close(fd);
        return HUSH_ERR_IO;
    }
    status = hush_router_unmarked(dir);
    if (status == HUSH_OK)
        status = hush_router_load_published(graph, dir);
    if (fd >= 0)
        hush_router_unlock(fd);
    return status;
}

static hush_status_t hush_router_unmarked(const char *dir)
{
    char mark[HUSH_HOME_PATH_MAX];

    assert(dir != NULL);
    HUSH_ROUTER_TRY(hush_router_join_file(mark, sizeof mark, dir, HUSH_ROUTER_COMMIT));
    if (access(mark, F_OK) != 0 && errno == ENOENT)
        return HUSH_OK;
    return HUSH_ERR_IO;
}

static hush_status_t hush_router_load_published(hush_router_graph_t *graph,
                                                const char *dir)
{
    assert(graph != NULL && dir != NULL);
    HUSH_ROUTER_TRY(hush_router_load_items(graph, dir));
    HUSH_ROUTER_TRY(hush_router_load_workers(graph, dir));
    HUSH_ROUTER_TRY(hush_router_load_rest(graph, dir));
    hush_router_bump_id(graph);
    return HUSH_OK;
}

static hush_status_t hush_router_read_file(char *out, size_t outsz,
                                           const char *path)
{
    FILE *fp;
    size_t n;

    assert(out != NULL && outsz > 1 && path != NULL);
    fp = fopen(path, "r");
    if (fp == NULL)
        return HUSH_ERR_NOT_FOUND;
    n = fread(out, 1, outsz - 1, fp);
    if (ferror(fp) != 0) {
        (void)fclose(fp);
        return HUSH_ERR_IO;
    }
    if (n == outsz - 1 && fgetc(fp) != EOF) {
        (void)fclose(fp);
        return HUSH_ERR_FULL;
    }
    (void)fclose(fp);
    out[n] = '\0';
    return HUSH_OK;
}

static hush_status_t hush_router_append(char *out, size_t outsz, size_t *off,
                                        const char *piece)
{
    size_t n;

    assert(out != NULL && off != NULL && piece != NULL);
    n = strlen(piece);
    if (*off + n >= outsz)
        return HUSH_ERR_FULL;
    memcpy(out + *off, piece, n);
    *off += n;
    out[*off] = '\0';
    return HUSH_OK;
}

static hush_status_t hush_router_escape(char *out, size_t outsz, const char *in)
{
    size_t n;

    assert(out != NULL && outsz > 0);
    n = hush_json_escape(in, out, outsz);
    if (in != NULL && in[0] != '\0' && n == 0)
        return HUSH_ERR_FULL;
    return HUSH_OK;
}

static hush_status_t hush_router_join_file(char *out, size_t outsz,
                                           const char *dir, const char *name)
{
    assert(out != NULL && dir != NULL && name != NULL);
    hush_home_join(out, outsz, dir, name);
    if (out[0] == '\0')
        return HUSH_ERR_FULL;
    return HUSH_OK;
}

static hush_status_t hush_router_save_items(const hush_router_graph_t *graph,
                                            const char *dir)
{
    char body[HUSH_ROUTER_FILE_MAX];
    char path[HUSH_HOME_PATH_MAX];
    size_t off = 0;

    assert(graph != NULL && dir != NULL);
    HUSH_ROUTER_TRY(hush_router_append(body, sizeof body, &off, "["));
    for (size_t i = 0; i < graph->nitems; ++i) {
        char row[2048];

        HUSH_ROUTER_TRY(hush_router_item_row(row, sizeof row, &graph->items[i],
                                             i == 0 ? "" : ","));
        HUSH_ROUTER_TRY(hush_router_append(body, sizeof body, &off, row));
    }
    HUSH_ROUTER_TRY(hush_router_append(body, sizeof body, &off, "]\n"));
    HUSH_ROUTER_TRY(hush_router_join_file(path, sizeof path, dir, HUSH_ROUTER_ITEMS_FILE));
    return hush_router_stage_file(path, body);
}

static hush_status_t hush_router_item_row(char *out, size_t outsz,
                                          const hush_router_item_t *item,
                                          const char *sep)
{
    char id[HUSH_ROUTER_ESC_ID];
    char title[HUSH_ROUTER_ESCAPE_MAX];
    char desc[HUSH_ROUTER_ESCAPE_MAX];
    char channel[HUSH_ROUTER_ESC_CHANNEL];
    char worker[HUSH_ROUTER_ESC_ID];
    char parent[HUSH_ROUTER_ESC_ID];
    int n;

    assert(out != NULL && item != NULL && sep != NULL);
    HUSH_ROUTER_TRY(hush_router_escape(id, sizeof id, item->id));
    HUSH_ROUTER_TRY(hush_router_escape(title, sizeof title, item->title));
    HUSH_ROUTER_TRY(hush_router_escape(desc, sizeof desc, item->description));
    HUSH_ROUTER_TRY(hush_router_escape(channel, sizeof channel, item->channel_id));
    HUSH_ROUTER_TRY(hush_router_escape(worker, sizeof worker, item->worker_id));
    HUSH_ROUTER_TRY(hush_router_escape(parent, sizeof parent, item->parent_id));
    n = snprintf(out, outsz,
                 "%s{\"id\":\"%s\",\"title\":\"%s\",\"description\":\"%s\","
                 "\"status\":\"%s\",\"channel_id\":\"%s\",\"created\":%lld,"
                 "\"updated\":%lld,\"assigned_worker_id\":\"%s\","
                 "\"priority\":%d,\"parent_id\":\"%s\"}",
                 sep, id, title, desc, hush_router_phase_name(item->phase), channel,
                 (long long)item->created, (long long)item->updated, worker,
                 item->priority, parent);
    if (n < 0 || (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    return HUSH_OK;
}

static hush_status_t hush_router_save_workers(const hush_router_graph_t *graph,
                                              const char *dir)
{
    char body[HUSH_ROUTER_FILE_MAX];
    char path[HUSH_HOME_PATH_MAX];
    size_t off = 0;

    assert(graph != NULL && dir != NULL);
    HUSH_ROUTER_TRY(hush_router_append(body, sizeof body, &off, "["));
    for (size_t i = 0; i < graph->nworkers; ++i) {
        char row[1024];

        HUSH_ROUTER_TRY(hush_router_worker_row(row, sizeof row, &graph->workers[i],
                                               i == 0 ? "" : ","));
        HUSH_ROUTER_TRY(hush_router_append(body, sizeof body, &off, row));
    }
    HUSH_ROUTER_TRY(hush_router_append(body, sizeof body, &off, "]\n"));
    HUSH_ROUTER_TRY(hush_router_join_file(path, sizeof path, dir, HUSH_ROUTER_WORKERS_FILE));
    return hush_router_stage_file(path, body);
}

static hush_status_t hush_router_worker_row(char *out, size_t outsz,
                                            const hush_router_worker_t *worker,
                                            const char *sep)
{
    char id[HUSH_ROUTER_ESC_ID];
    char item[HUSH_ROUTER_ESC_ID];
    char specialist[HUSH_ROUTER_ESC_NAME];
    char job[HUSH_ROUTER_ESC_JOB];
    char provider[HUSH_ROUTER_ESC_NAME];
    int n;

    assert(out != NULL && worker != NULL && sep != NULL);
    HUSH_ROUTER_TRY(hush_router_escape(id, sizeof id, worker->id));
    HUSH_ROUTER_TRY(hush_router_escape(item, sizeof item, worker->item_id));
    HUSH_ROUTER_TRY(hush_router_escape(specialist, sizeof specialist,
                                       worker->specialist_id));
    HUSH_ROUTER_TRY(hush_router_escape(job, sizeof job, worker->job_dir));
    HUSH_ROUTER_TRY(hush_router_escape(provider, sizeof provider,
                                       worker->provider_id));
    n = snprintf(out, outsz,
                 "%s{\"id\":\"%s\",\"workitem_id\":\"%s\","
                 "\"specialist_id\":\"%s\",\"status\":\"%s\","
                 "\"started\":%lld,\"last_heartbeat\":%lld,"
                 "\"job_dir\":\"%s\",\"provider_id\":\"%s\"}",
                 sep, id, item, specialist, hush_router_phase_name(worker->phase),
                 (long long)worker->started, (long long)worker->heartbeat, job,
                 provider);
    if (n < 0 || (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    return HUSH_OK;
}

static hush_status_t hush_router_save_decisions(const hush_router_graph_t *graph,
                                                const char *dir)
{
    char body[HUSH_ROUTER_FILE_MAX];
    char path[HUSH_HOME_PATH_MAX];
    size_t off = 0;

    assert(graph != NULL && dir != NULL);
    HUSH_ROUTER_TRY(hush_router_append(body, sizeof body, &off, "["));
    for (size_t i = 0; i < graph->ndecisions; ++i) {
        char row[1024];

        HUSH_ROUTER_TRY(hush_router_decision_row(row, sizeof row,
                                                 &graph->decisions[i],
                                                 i == 0 ? "" : ","));
        HUSH_ROUTER_TRY(hush_router_append(body, sizeof body, &off, row));
    }
    HUSH_ROUTER_TRY(hush_router_append(body, sizeof body, &off, "]\n"));
    HUSH_ROUTER_TRY(hush_router_join_file(path, sizeof path, dir,
                                          HUSH_ROUTER_DECISIONS_FILE));
    return hush_router_stage_file(path, body);
}

static hush_status_t hush_router_decision_row(char *out, size_t outsz,
                                              const hush_router_decision_t *decision,
                                              const char *sep)
{
    char id[HUSH_ROUTER_ESC_ID];
    char item[HUSH_ROUTER_ESC_ID];
    char text[HUSH_ROUTER_ESCAPE_MAX];
    int n;

    assert(out != NULL && decision != NULL && sep != NULL);
    HUSH_ROUTER_TRY(hush_router_escape(id, sizeof id, decision->id));
    HUSH_ROUTER_TRY(hush_router_escape(item, sizeof item, decision->item_id));
    HUSH_ROUTER_TRY(hush_router_escape(text, sizeof text, decision->text));
    n = snprintf(out, outsz,
                 "%s{\"id\":\"%s\",\"workitem_id\":\"%s\",\"text\":\"%s\","
                 "\"created\":%lld}",
                 sep, id, item, text, (long long)decision->created);
    if (n < 0 || (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    return HUSH_OK;
}

static hush_status_t hush_router_save_artifacts(const hush_router_graph_t *graph,
                                                const char *dir)
{
    char body[HUSH_ROUTER_FILE_MAX];
    char path[HUSH_HOME_PATH_MAX];
    size_t off = 0;

    assert(graph != NULL && dir != NULL);
    HUSH_ROUTER_TRY(hush_router_append(body, sizeof body, &off, "["));
    for (size_t i = 0; i < graph->nartifacts; ++i) {
        char row[1024];

        HUSH_ROUTER_TRY(hush_router_artifact_row(row, sizeof row,
                                                 &graph->artifacts[i],
                                                 i == 0 ? "" : ","));
        HUSH_ROUTER_TRY(hush_router_append(body, sizeof body, &off, row));
    }
    HUSH_ROUTER_TRY(hush_router_append(body, sizeof body, &off, "]\n"));
    HUSH_ROUTER_TRY(hush_router_join_file(path, sizeof path, dir, HUSH_ROUTER_ARTIFACTS_FILE));
    return hush_router_stage_file(path, body);
}

static hush_status_t hush_router_artifact_row(char *out, size_t outsz,
                                              const hush_router_artifact_t *artifact,
                                              const char *sep)
{
    char id[HUSH_ROUTER_ESC_ID];
    char worker[HUSH_ROUTER_ESC_ID];
    char text[HUSH_ROUTER_ESCAPE_MAX];
    int n;

    assert(out != NULL && artifact != NULL && sep != NULL);
    HUSH_ROUTER_TRY(hush_router_escape(id, sizeof id, artifact->id));
    HUSH_ROUTER_TRY(hush_router_escape(worker, sizeof worker, artifact->worker_id));
    HUSH_ROUTER_TRY(hush_router_escape(text, sizeof text, artifact->text));
    n = snprintf(out, outsz,
                 "%s{\"id\":\"%s\",\"worker_id\":\"%s\",\"text\":\"%s\"}",
                 sep, id, worker, text);
    if (n < 0 || (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    return HUSH_OK;
}

static hush_status_t hush_router_save_edges(const hush_router_graph_t *graph,
                                            const char *dir)
{
    char body[HUSH_ROUTER_FILE_MAX];
    char path[HUSH_HOME_PATH_MAX];
    size_t off = 0;

    assert(graph != NULL && dir != NULL);
    HUSH_ROUTER_TRY(hush_router_append(body, sizeof body, &off, "["));
    for (size_t i = 0; i < graph->nedges; ++i) {
        char row[256];

        HUSH_ROUTER_TRY(hush_router_edge_row(row, sizeof row, &graph->edges[i],
                                             i == 0 ? "" : ","));
        HUSH_ROUTER_TRY(hush_router_append(body, sizeof body, &off, row));
    }
    HUSH_ROUTER_TRY(hush_router_append(body, sizeof body, &off, "]\n"));
    HUSH_ROUTER_TRY(hush_router_join_file(path, sizeof path, dir, HUSH_ROUTER_EDGES_FILE));
    return hush_router_stage_file(path, body);
}

static hush_status_t hush_router_edge_row(char *out, size_t outsz,
                                          const hush_router_edge_t *edge,
                                          const char *sep)
{
    char from[HUSH_ROUTER_ESC_ID];
    char to[HUSH_ROUTER_ESC_ID];
    int n;

    assert(out != NULL && edge != NULL && sep != NULL);
    HUSH_ROUTER_TRY(hush_router_escape(from, sizeof from, edge->from));
    HUSH_ROUTER_TRY(hush_router_escape(to, sizeof to, edge->to));
    n = snprintf(out, outsz, "%s{\"from\":\"%s\",\"type\":\"%s\",\"to\":\"%s\"}",
                 sep, from, hush_router_edge_name(edge->kind), to);
    if (n < 0 || (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    return HUSH_OK;
}

static hush_status_t hush_router_save_state(const hush_router_graph_t *graph,
                                            const char *dir)
{
    char body[HUSH_ROUTER_FILE_MAX];
    char path[HUSH_HOME_PATH_MAX];
    char line[HUSH_ROUTER_ESCAPE_MAX];
    int n;

    assert(graph != NULL && dir != NULL);
    HUSH_ROUTER_TRY(hush_router_escape(line, sizeof line, graph->last_line));
    n = snprintf(body, sizeof body,
                 "{\"next_id\":%u,\"last_line\":\"%s\"}\n", graph->next_id,
                 line);
    if (n < 0 || (size_t)n >= sizeof body)
        return HUSH_ERR_FULL;
    HUSH_ROUTER_TRY(hush_router_join_file(path, sizeof path, dir,
                                          HUSH_ROUTER_STATE_FILE));
    return hush_router_stage_file(path, body);
}

static hush_status_t hush_router_save_rest(const hush_router_graph_t *graph,
                                           const char *dir)
{
    assert(graph != NULL && dir != NULL);
    HUSH_ROUTER_TRY(hush_router_save_decisions(graph, dir));
    HUSH_ROUTER_TRY(hush_router_save_artifacts(graph, dir));
    HUSH_ROUTER_TRY(hush_router_save_edges(graph, dir));
    return hush_router_save_state(graph, dir);
}

static hush_status_t hush_router_take_field(char *out, size_t outsz,
                                            const char *json, const char *path)
{
    hush_json_value_t value;
    hush_status_t status = hush_json_lookup(&value, json, path);

    if (status != HUSH_OK)
        return status;
    if (value.len > 0 && value.start[0] == '"')
        return hush_json_decode(out, outsz, &value);
    if (value.len + 1 > outsz)
        return HUSH_ERR_FULL;
    memcpy(out, value.start, value.len);
    out[value.len] = '\0';
    return HUSH_OK;
}

static hush_status_t hush_router_take_i64(int64_t *out, const char *json,
                                          const char *path)
{
    char raw[32];
    char *end = NULL;
    long long num;

    assert(out != NULL);
    HUSH_ROUTER_TRY(hush_router_take_field(raw, sizeof raw, json, path));
    errno = 0;
    num = strtoll(raw, &end, 10);
    if (end == raw || *end != '\0' || errno != 0)
        return HUSH_ERR_PARSE;
    *out = (int64_t)num;
    return HUSH_OK;
}

static hush_status_t hush_router_load_items(hush_router_graph_t *graph,
                                            const char *dir)
{
    char path[HUSH_HOME_PATH_MAX];
    char body[HUSH_ROUTER_FILE_MAX];
    hush_status_t status;

    assert(graph != NULL && dir != NULL);
    HUSH_ROUTER_TRY(hush_router_join_file(path, sizeof path, dir,
                                          HUSH_ROUTER_ITEMS_FILE));
    status = hush_router_read_file(body, sizeof body, path);
    if (status == HUSH_ERR_NOT_FOUND)
        return HUSH_OK;
    if (status != HUSH_OK)
        return status;
    for (size_t i = 0; i < (size_t)HUSH_ROUTER_ITEMS_MAX; ++i) {
        char key[HUSH_ROUTER_PATH_MAX];
        char phase[HUSH_ROUTER_KIND_MAX];
        hush_router_item_t *item = &graph->items[graph->nitems];
        int n = snprintf(key, sizeof key, "/%u/id", (unsigned)i);

        if (n < 0 || (size_t)n >= sizeof key)
            return HUSH_ERR_FULL;
        status = hush_router_take_field(item->id, sizeof item->id, body, key);
        if (status == HUSH_ERR_NOT_FOUND)
            return HUSH_OK;
        if (status != HUSH_OK)
            return status;
        n = snprintf(key, sizeof key, "/%u/title", (unsigned)i);
        if (n < 0 || (size_t)n >= sizeof key)
            return HUSH_ERR_FULL;
        HUSH_ROUTER_TRY(hush_router_take_field(item->title, sizeof item->title,
                                               body, key));
        n = snprintf(key, sizeof key, "/%u/description", (unsigned)i);
        if (n < 0 || (size_t)n >= sizeof key)
            return HUSH_ERR_FULL;
        HUSH_ROUTER_TRY(hush_router_take_field(item->description,
                                               sizeof item->description, body,
                                               key));
        n = snprintf(key, sizeof key, "/%u/status", (unsigned)i);
        if (n < 0 || (size_t)n >= sizeof key)
            return HUSH_ERR_FULL;
        HUSH_ROUTER_TRY(hush_router_take_field(phase, sizeof phase, body, key));
        HUSH_ROUTER_TRY(hush_router_phase_parse(&item->phase, phase));
        n = snprintf(key, sizeof key, "/%u/channel_id", (unsigned)i);
        if (n < 0 || (size_t)n >= sizeof key)
            return HUSH_ERR_FULL;
        HUSH_ROUTER_TRY(hush_router_take_field(item->channel_id,
                                               sizeof item->channel_id, body,
                                               key));
        n = snprintf(key, sizeof key, "/%u/created", (unsigned)i);
        if (n < 0 || (size_t)n >= sizeof key)
            return HUSH_ERR_FULL;
        HUSH_ROUTER_TRY(hush_router_take_i64(&item->created, body, key));
        n = snprintf(key, sizeof key, "/%u/updated", (unsigned)i);
        if (n < 0 || (size_t)n >= sizeof key)
            return HUSH_ERR_FULL;
        HUSH_ROUTER_TRY(hush_router_take_i64(&item->updated, body, key));
        n = snprintf(key, sizeof key, "/%u/assigned_worker_id", (unsigned)i);
        if (n < 0 || (size_t)n >= sizeof key)
            return HUSH_ERR_FULL;
        HUSH_ROUTER_TRY(hush_router_take_field(item->worker_id,
                                               sizeof item->worker_id, body,
                                               key));
        n = snprintf(key, sizeof key, "/%u/parent_id", (unsigned)i);
        if (n < 0 || (size_t)n >= sizeof key)
            return HUSH_ERR_FULL;
        HUSH_ROUTER_TRY(hush_router_take_field(item->parent_id,
                                               sizeof item->parent_id, body,
                                               key));
        item->priority = HUSH_ROUTER_PRIORITY;
        graph->nitems++;
    }
    return HUSH_OK;
}

static hush_status_t hush_router_load_workers(hush_router_graph_t *graph,
                                              const char *dir)
{
    char path[HUSH_HOME_PATH_MAX];
    char body[HUSH_ROUTER_FILE_MAX];
    hush_status_t status;

    assert(graph != NULL && dir != NULL);
    HUSH_ROUTER_TRY(hush_router_join_file(path, sizeof path, dir, HUSH_ROUTER_WORKERS_FILE));
    status = hush_router_read_file(body, sizeof body, path);
    if (status == HUSH_ERR_NOT_FOUND)
        return HUSH_OK;
    if (status != HUSH_OK)
        return status;
    for (size_t i = 0; i < (size_t)HUSH_ROUTER_WORKERS_MAX; ++i) {
        char key[HUSH_ROUTER_PATH_MAX];
        char phase[HUSH_ROUTER_KIND_MAX];
        hush_router_worker_t *worker = &graph->workers[graph->nworkers];
        int n = snprintf(key, sizeof key, "/%u/id", (unsigned)i);

        if (n < 0 || (size_t)n >= sizeof key)
            return HUSH_ERR_FULL;
        status = hush_router_take_field(worker->id, sizeof worker->id, body, key);
        if (status == HUSH_ERR_NOT_FOUND)
            return HUSH_OK;
        if (status != HUSH_OK)
            return status;
        n = snprintf(key, sizeof key, "/%u/workitem_id", (unsigned)i);
        if (n < 0 || (size_t)n >= sizeof key)
            return HUSH_ERR_FULL;
        HUSH_ROUTER_TRY(hush_router_take_field(worker->item_id,
                                               sizeof worker->item_id, body,
                                               key));
        n = snprintf(key, sizeof key, "/%u/specialist_id", (unsigned)i);
        if (n < 0 || (size_t)n >= sizeof key)
            return HUSH_ERR_FULL;
        HUSH_ROUTER_TRY(hush_router_take_field(worker->specialist_id,
                                               sizeof worker->specialist_id,
                                               body, key));
        n = snprintf(key, sizeof key, "/%u/status", (unsigned)i);
        if (n < 0 || (size_t)n >= sizeof key)
            return HUSH_ERR_FULL;
        HUSH_ROUTER_TRY(hush_router_take_field(phase, sizeof phase, body, key));
        HUSH_ROUTER_TRY(hush_router_phase_parse(&worker->phase, phase));
        n = snprintf(key, sizeof key, "/%u/started", (unsigned)i);
        if (n < 0 || (size_t)n >= sizeof key)
            return HUSH_ERR_FULL;
        HUSH_ROUTER_TRY(hush_router_take_i64(&worker->started, body, key));
        n = snprintf(key, sizeof key, "/%u/last_heartbeat", (unsigned)i);
        if (n < 0 || (size_t)n >= sizeof key)
            return HUSH_ERR_FULL;
        HUSH_ROUTER_TRY(hush_router_take_i64(&worker->heartbeat, body, key));
        n = snprintf(key, sizeof key, "/%u/job_dir", (unsigned)i);
        if (n < 0 || (size_t)n >= sizeof key)
            return HUSH_ERR_FULL;
        HUSH_ROUTER_TRY(hush_router_take_field(worker->job_dir,
                                               sizeof worker->job_dir, body,
                                               key));
        n = snprintf(key, sizeof key, "/%u/provider_id", (unsigned)i);
        if (n < 0 || (size_t)n >= sizeof key)
            return HUSH_ERR_FULL;
        HUSH_ROUTER_TRY(hush_router_take_field(worker->provider_id,
                                               sizeof worker->provider_id, body,
                                               key));
        graph->nworkers++;
    }
    return HUSH_OK;
}

static hush_status_t hush_router_load_decisions(hush_router_graph_t *graph,
                                                const char *dir)
{
    char path[HUSH_HOME_PATH_MAX];
    char body[HUSH_ROUTER_FILE_MAX];
    hush_status_t status;

    assert(graph != NULL && dir != NULL);
    HUSH_ROUTER_TRY(hush_router_join_file(path, sizeof path, dir,
                                          HUSH_ROUTER_DECISIONS_FILE));
    status = hush_router_read_file(body, sizeof body, path);
    if (status == HUSH_ERR_NOT_FOUND)
        return HUSH_OK;
    if (status != HUSH_OK)
        return status;
    for (size_t i = 0; i < (size_t)HUSH_ROUTER_DECISIONS_MAX; ++i) {
        char key[HUSH_ROUTER_PATH_MAX];
        hush_router_decision_t *decision = &graph->decisions[graph->ndecisions];
        int n = snprintf(key, sizeof key, "/%u/id", (unsigned)i);

        if (n < 0 || (size_t)n >= sizeof key)
            return HUSH_ERR_FULL;
        status = hush_router_take_field(decision->id, sizeof decision->id, body,
                                        key);
        if (status == HUSH_ERR_NOT_FOUND)
            return HUSH_OK;
        if (status != HUSH_OK)
            return status;
        n = snprintf(key, sizeof key, "/%u/workitem_id", (unsigned)i);
        if (n < 0 || (size_t)n >= sizeof key)
            return HUSH_ERR_FULL;
        HUSH_ROUTER_TRY(hush_router_take_field(decision->item_id,
                                               sizeof decision->item_id, body,
                                               key));
        n = snprintf(key, sizeof key, "/%u/text", (unsigned)i);
        if (n < 0 || (size_t)n >= sizeof key)
            return HUSH_ERR_FULL;
        HUSH_ROUTER_TRY(hush_router_take_field(decision->text,
                                               sizeof decision->text, body, key));
        n = snprintf(key, sizeof key, "/%u/created", (unsigned)i);
        if (n < 0 || (size_t)n >= sizeof key)
            return HUSH_ERR_FULL;
        HUSH_ROUTER_TRY(hush_router_take_i64(&decision->created, body, key));
        graph->ndecisions++;
    }
    return HUSH_OK;
}

static hush_status_t hush_router_load_artifacts(hush_router_graph_t *graph,
                                                const char *dir)
{
    char path[HUSH_HOME_PATH_MAX];
    char body[HUSH_ROUTER_FILE_MAX];
    hush_status_t status;

    assert(graph != NULL && dir != NULL);
    HUSH_ROUTER_TRY(hush_router_join_file(path, sizeof path, dir,
                                          HUSH_ROUTER_ARTIFACTS_FILE));
    status = hush_router_read_file(body, sizeof body, path);
    if (status == HUSH_ERR_NOT_FOUND)
        return HUSH_OK;
    if (status != HUSH_OK)
        return status;
    for (size_t i = 0; i < (size_t)HUSH_ROUTER_ARTIFACTS_MAX; ++i) {
        char key[HUSH_ROUTER_PATH_MAX];
        hush_router_artifact_t *artifact = &graph->artifacts[graph->nartifacts];
        int n = snprintf(key, sizeof key, "/%u/id", (unsigned)i);

        if (n < 0 || (size_t)n >= sizeof key)
            return HUSH_ERR_FULL;
        status = hush_router_take_field(artifact->id, sizeof artifact->id, body,
                                        key);
        if (status == HUSH_ERR_NOT_FOUND)
            return HUSH_OK;
        if (status != HUSH_OK)
            return status;
        n = snprintf(key, sizeof key, "/%u/worker_id", (unsigned)i);
        if (n < 0 || (size_t)n >= sizeof key)
            return HUSH_ERR_FULL;
        HUSH_ROUTER_TRY(hush_router_take_field(artifact->worker_id,
                                               sizeof artifact->worker_id, body,
                                               key));
        n = snprintf(key, sizeof key, "/%u/text", (unsigned)i);
        if (n < 0 || (size_t)n >= sizeof key)
            return HUSH_ERR_FULL;
        HUSH_ROUTER_TRY(hush_router_take_field(artifact->text,
                                               sizeof artifact->text, body, key));
        graph->nartifacts++;
    }
    return HUSH_OK;
}

static hush_status_t hush_router_load_edges(hush_router_graph_t *graph,
                                            const char *dir)
{
    char path[HUSH_HOME_PATH_MAX];
    char body[HUSH_ROUTER_FILE_MAX];
    hush_status_t status;

    assert(graph != NULL && dir != NULL);
    HUSH_ROUTER_TRY(hush_router_join_file(path, sizeof path, dir, HUSH_ROUTER_EDGES_FILE));
    status = hush_router_read_file(body, sizeof body, path);
    if (status == HUSH_ERR_NOT_FOUND)
        return HUSH_OK;
    if (status != HUSH_OK)
        return status;
    for (size_t i = 0; i < (size_t)HUSH_ROUTER_EDGES_MAX; ++i) {
        char key[HUSH_ROUTER_PATH_MAX];
        char kind[HUSH_ROUTER_KIND_MAX];
        hush_router_edge_t *edge = &graph->edges[graph->nedges];
        int n = snprintf(key, sizeof key, "/%u/from", (unsigned)i);

        if (n < 0 || (size_t)n >= sizeof key)
            return HUSH_ERR_FULL;
        status = hush_router_take_field(edge->from, sizeof edge->from, body, key);
        if (status == HUSH_ERR_NOT_FOUND)
            return HUSH_OK;
        if (status != HUSH_OK)
            return status;
        n = snprintf(key, sizeof key, "/%u/type", (unsigned)i);
        if (n < 0 || (size_t)n >= sizeof key)
            return HUSH_ERR_FULL;
        HUSH_ROUTER_TRY(hush_router_take_field(kind, sizeof kind, body, key));
        HUSH_ROUTER_TRY(hush_router_edge_parse(&edge->kind, kind));
        n = snprintf(key, sizeof key, "/%u/to", (unsigned)i);
        if (n < 0 || (size_t)n >= sizeof key)
            return HUSH_ERR_FULL;
        HUSH_ROUTER_TRY(hush_router_take_field(edge->to, sizeof edge->to, body,
                                               key));
        graph->nedges++;
    }
    return HUSH_OK;
}

static hush_status_t hush_router_load_state(hush_router_graph_t *graph,
                                            const char *dir)
{
    char path[HUSH_HOME_PATH_MAX];
    char body[HUSH_ROUTER_FILE_MAX];
    char raw[32];
    hush_status_t status;
    unsigned long num;
    char *end = NULL;

    assert(graph != NULL && dir != NULL);
    HUSH_ROUTER_TRY(hush_router_join_file(path, sizeof path, dir,
                                          HUSH_ROUTER_STATE_FILE));
    status = hush_router_read_file(body, sizeof body, path);
    if (status == HUSH_ERR_NOT_FOUND)
        return HUSH_OK;
    if (status != HUSH_OK)
        return status;
    HUSH_ROUTER_TRY(hush_router_take_field(raw, sizeof raw, body, "/next_id"));
    errno = 0;
    num = strtoul(raw, &end, 10);
    if (end == raw || *end != '\0' || errno != 0 || num > 1000000ul)
        return HUSH_ERR_PARSE;
    graph->next_id = (uint32_t)num;
    return hush_router_take_field(graph->last_line, sizeof graph->last_line,
                                  body, "/last_line");
}

static hush_status_t hush_router_load_rest(hush_router_graph_t *graph,
                                           const char *dir)
{
    assert(graph != NULL && dir != NULL);
    HUSH_ROUTER_TRY(hush_router_load_decisions(graph, dir));
    HUSH_ROUTER_TRY(hush_router_load_artifacts(graph, dir));
    HUSH_ROUTER_TRY(hush_router_load_edges(graph, dir));
    return hush_router_load_state(graph, dir);
}

static void hush_router_bump_id(hush_router_graph_t *graph)
{
    assert(graph != NULL);
    for (size_t i = 0; i < graph->nitems; ++i) {
        unsigned long num;
        char *end = NULL;

        if (graph->items[i].id[0] == '\0')
            continue;
        errno = 0;
        num = strtoul(graph->items[i].id, &end, 10);
        if (end == graph->items[i].id || *end != '\0' || errno != 0)
            continue;
        if (num >= graph->next_id && num < 1000000ul)
            graph->next_id = (uint32_t)num + 1;
    }
    if (graph->next_id == 0)
        graph->next_id = 1;
}

static hush_status_t hush_router_push_node(hush_router_node_t *out, size_t *n,
                                           size_t cap, const char *kind,
                                           const char *id)
{
    assert(out != NULL && n != NULL && kind != NULL && id != NULL);
    if (*n >= cap)
        return HUSH_OK;
    hush_router_copy(out[*n].kind, sizeof out[*n].kind, kind);
    hush_router_copy(out[*n].id, sizeof out[*n].id, id);
    (*n)++;
    return HUSH_OK;
}

static int hush_router_has_item(const hush_router_graph_t *graph,
                                const char *id)
{
    assert(graph != NULL && id != NULL);
    for (size_t i = 0; i < graph->nitems; ++i) {
        if (strcmp(graph->items[i].id, id) == 0)
            return 1;
    }
    return 0;
}

static int hush_router_has_decision(const hush_router_graph_t *graph,
                                    const char *item_id)
{
    assert(graph != NULL && item_id != NULL);
    for (size_t i = 0; i < graph->ndecisions; ++i) {
        if (strcmp(graph->decisions[i].item_id, item_id) == 0)
            return 1;
    }
    return 0;
}
