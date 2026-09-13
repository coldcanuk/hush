/* agent_dispatch.c: job lifecycle and dispatch/follow flow for the agent
 * core. Owns the follow table, mention dispatch, worker reading/completion,
 * and the leader election/planning waves. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "hush_agent.h"
#include "hush_agent_internal.h"
#include "hush_cevent.h"
#include "hush_provider.h"
#include "hush_thread.h"
#include "hush_wake.h"

#define HUSH_AGENT_PLAN_FENCE "```plan"
#define HUSH_AGENT_PLAN_END "```"
#define HUSH_AGENT_ASSIGN " YOUR assignment: "
#define HUSH_AGENT_ACK_LINE "Mention received."
#define HUSH_AGENT_FOLLOW_FULL_LINE \
    "I cannot start another wave: too many live conversations are already in flight."
#define HUSH_AGENT_CHAPERON_LINE \
    "That's enough robot talk. Standing by for the human."

static hush_agent_follow_t g_follow[HUSH_AGENT_FOLLOW_MAX];

/* Clears the follow table. Called once from hush_agent_init(). */
void hush_agent_follow_init(void)
{
    memset(g_follow, 0, sizeof(g_follow));
}

/* Classifies a human request into bounded assignments; returns whether to begin this job. */
static int hush_agent_prepare_human_job(hush_agent_job_in_t *job, hush_agent_mentions_t *mentions);
/* Introduces the required request's bounded mentioned roster. */
static void hush_agent_greet_mentions(const hush_agent_job_in_t *job,
                                       const hush_agent_mentions_t *mentions);
/* Posts a robot notice when the follow table cannot accept another root. */
static void hush_agent_note_follow_busy(const hush_agent_job_in_t *job);
/* Starts this request's election or planning on its selected provider. */
static void hush_agent_dispatch_group(const hush_agent_job_in_t *job,
                                      const hush_agent_mentions_t *mentions);
/* Prepares the required group's handoff slot with the current human assignment. */
static hush_agent_follow_t *hush_agent_prepare_group(const hush_agent_job_in_t *job,
                                                     const hush_agent_mentions_t *mentions);
/* True when this follow slot has no later wave after slot->at. */
static int hush_agent_follow_last_wave(const hush_agent_follow_t *slot);
/* Reports a required failed job to its conversation; store may be NULL at shutdown. */
static void hush_agent_note_failure(hush_store_t *store, const hush_agent_job_t *job);
/* Runs planning after a required completed election. */
static void hush_agent_finish_election(hush_store_t *store, const hush_agent_job_t *job);
/* Resolves a required election response to a known robot, falling back to convener. */
static void hush_agent_select_leader(char *out, size_t outsz, const hush_agent_job_t *job,
                                     const hush_agent_follow_t *slot);
/* Posts the required worker result before dispatching the next handoff. */
static hush_status_t hush_agent_publish_reply(hush_store_t *store, const hush_agent_job_t *job);
/* Fills borrowed note inputs for a required completed job. */
static void hush_agent_fill_reply(hush_agent_note_in_t *out, const hush_agent_job_t *job);
/* Reads one chunk into required job; more indicates immediately available progress. */
static hush_status_t hush_agent_read_chunk(hush_agent_job_t *job, int *more);
static int hush_agent_robot_busy(const hush_agent_robot_t *bot,
                                 const hush_event_t *parent);
static int hush_agent_is_work_ok(const hush_launch_t *launch,
                                 const hush_agent_robot_t *bot);
static size_t hush_agent_collect_hexes(const hush_launch_t *launch,
                                       const hush_event_t *ev,
                                       char hexes[][HUSH_EVENT_PUBKEY_HEX_LEN + 1],
                                       size_t maxn);
static hush_agent_follow_t *hush_agent_follow_find(const char *root);
/* Finds or creates the root's follow slot. NULL when every slot is live. */
static hush_agent_follow_t *hush_agent_follow_take(const char *root);
static int hush_agent_extract_clause(char *out, size_t outsz,
                                     const char *content, const char *npub);
static hush_agent_mode_t hush_agent_classify(
    const hush_launch_t *launch, const hush_event_t *ev,
    const char hexes[][HUSH_EVENT_PUBKEY_HEX_LEN + 1], size_t nhex,
    hush_agent_assign_t *assigns);
static int hush_agent_is_leadership_skill(const char *id);
static int hush_agent_leadership_score(const hush_launch_t *launch,
                                       const char *hex);
static int hush_agent_lookup_hex_by_name(const hush_launch_t *launch,
                                         const char *name,
                                         char *out, size_t outsz);
static void hush_agent_parse_plan(const hush_launch_t *launch,
                                  const char *text,
                                  hush_agent_follow_t *slot);
static void hush_agent_begin_plan(const hush_agent_job_in_t *in);
static size_t hush_agent_leader_candidates(
    const hush_launch_t *launch,
    const char hexes[][HUSH_EVENT_PUBKEY_HEX_LEN + 1], size_t nhex,
    char out[][HUSH_EVENT_PUBKEY_HEX_LEN + 1]);
static void hush_agent_follow_remove(hush_agent_follow_t *slot,
                                     const char *hex);
static void hush_agent_begin_elect(
    hush_store_t *store, const hush_launch_t *launch,
    hush_agent_follow_t *slot, const hush_event_t *ev,
    const char cands[][HUSH_EVENT_PUBKEY_HEX_LEN + 1], size_t ncand);
static void hush_agent_start_plan_from_slot(
    hush_store_t *store, const hush_launch_t *launch,
    hush_agent_follow_t *slot, const char *leader_hex);
static void hush_agent_follow_push_hex(hush_agent_follow_t *slot,
                                       const char *hex, const char *ask);
/* Queues the remaining assignees on the root's follow slot. Returns 0 and
 * posts a notice when every follow slot is live. */
static int hush_agent_follow_push(const hush_agent_job_in_t *job,
                                  const hush_agent_mentions_t *mentions,
                                  size_t start);
static void hush_agent_emit(const char *type, const char *channel,
                            const char *root, const char *actor,
                            const char *note);
/* Starts one robot turn. Returns 1 when a job was created, 0 when a cap, a
 * missing runtime, or a start failure stopped it (a notice is posted). */
static int hush_agent_begin_work(const hush_agent_job_in_t *in);
static void hush_agent_push_hex(char hexes[][HUSH_EVENT_PUBKEY_HEX_LEN + 1],
                                size_t *n, size_t maxn, const char *hex);
/* Counts only work notes since the latest human message in this thread. */
static size_t hush_agent_count_turns(hush_store_t *store,
                                     const hush_launch_t *launch,
                                     const char *root);
/* True when content is a robot work note, not an ack, intro, or deny. */
int hush_agent_is_work_note(const char *content);
static int hush_agent_turns_full(hush_store_t *store,
                                 const hush_launch_t *launch,
                                 const hush_event_t *ev);
static int hush_agent_lookup_slug(hush_agent_robot_t *out,
                                  const hush_launch_t *launch,
                                  const char *slug);
static void hush_agent_nudge_chaperon(hush_store_t *store,
                                      const hush_launch_t *launch,
                                      const hush_event_t *ev);

static int hush_agent_follow_last_wave(const hush_agent_follow_t *slot)
{
    size_t i;
    int group;

    assert(slot != NULL);
    if (slot->at >= slot->nnext)
        return 1;
    group = slot->group[slot->at];
    for (i = slot->at; i < slot->nnext && i < (size_t)HUSH_AGENT_FOLLOW_ROBOTS;
         i++) {
        if (slot->group[i] != group)
            return 0;
    }
    return 1;
}

static void hush_agent_note_failure(hush_store_t *store, const hush_agent_job_t *job)
{
    assert(job != NULL);
    if (store == NULL)
        return;
    hush_agent_follow_t *follow = hush_agent_follow_find(job->parent_id);
    if (follow != NULL)
        follow->live = 0;
    hush_provider_status_t status = {0};
    char reason[HUSH_PROVIDER_ERR_MAX] = {0};
    char message[HUSH_EVENT_MAX_CONTENT] = {0};
    int written;
    if (job->diag[0] != '\0') {
        hush_agent_copy(reason, sizeof(reason), job->diag);
    } else if (hush_provider_status(&status, job->provider) == HUSH_OK) {
        hush_provider_missing_reason(reason, sizeof(reason), &status);
    }
    if (reason[0] != '\0')
        written = snprintf(message, sizeof(message),
            "%s did not return a usable reply through %s (%s). Fix that, then send your "
            "request again.", job->robot_name, job->provider, reason);
    else
        written = snprintf(message, sizeof(message),
            "%s did not return a usable reply through %s. Check that provider's login, model, "
            "and connection, then send your request again.", job->robot_name, job->provider);
    if (written < 0 || (size_t)written >= sizeof(message))
        return;
    hush_agent_note_in_t notice = {.pubkey = job->robot_pub, .content = message,
        .channel = job->channel, .parent_id = job->parent_id, .human_pub = job->human_pub};
    if (hush_agent_insert_note(store, &notice) != HUSH_OK)
        return;
    hush_agent_emit(HUSH_CEVENT_JOB_DONE, job->channel, job->parent_id,
                    job->robot_pub, "provider_failed");
}

/* Posts one line when the robot's first-choice provider is unready and a
 * fallback will run instead. Borrowed pointers; no-op when the choice is
 * the robot's own or nothing is missing. */
static void hush_agent_note_fallback(hush_store_t *store, const hush_agent_robot_t *bot,
                                     const hush_event_t *parent)
{
    hush_provider_status_t primary = {0};
    hush_provider_status_t picked = {0};
    char reason[HUSH_PROVIDER_ERR_MAX] = {0};
    char message[HUSH_EVENT_MAX_CONTENT] = {0};
    char root[HUSH_EVENT_ID_HEX_LEN + 1];
    char channel[HUSH_EVENT_MAX_TAG_LEN + 1];
    const char *choice;
    const char *pick;
    int written;

    assert(bot != NULL);
    assert(parent != NULL);
    if (store == NULL)
        return;
    choice = bot->nproviders > 0 && bot->providers[0] != NULL
        ? bot->providers[0] : bot->provider;
    pick = hush_agent_pick_provider(bot);
    if (choice == NULL || pick == NULL || strcmp(choice, pick) == 0)
        return;
    if (hush_provider_status(&primary, choice) != HUSH_OK || hush_provider_ready(&primary))
        return;
    hush_provider_missing_reason(reason, sizeof(reason), &primary);
    if (reason[0] == '\0')
        return;
    (void)hush_provider_status(&picked, pick);
    written = snprintf(message, sizeof(message), "%s is not ready (%s); using %s instead.",
                       primary.label, reason,
                       picked.label[0] != '\0' ? picked.label : pick);
    if (written < 0 || (size_t)written >= sizeof(message))
        return;
    hush_agent_event_root(root, sizeof(root), parent);
    hush_agent_event_channel(channel, sizeof(channel), parent);
    hush_agent_note_in_t notice = {.pubkey = bot->hex, .content = message,
        .channel = channel, .parent_id = root, .human_pub = parent->pubkey};
    (void)hush_agent_insert_note(store, &notice);
}

/* Posts the honest note for a job the human stopped. */
static void hush_agent_note_stopped(hush_store_t *store, const hush_agent_job_t *job)
{
    assert(job != NULL);
    if (store == NULL)
        return;
    hush_agent_follow_t *follow = hush_agent_follow_find(job->parent_id);
    if (follow != NULL)
        follow->live = 0;
    char message[HUSH_EVENT_MAX_CONTENT] = {0};
    int written = snprintf(message, sizeof(message),
        "%s stopped on request before finishing. Send the ask again when you want it to continue.",
        job->robot_name);
    if (written < 0 || (size_t)written >= sizeof(message))
        return;
    hush_agent_note_in_t notice = {.pubkey = job->robot_pub, .content = message,
        .channel = job->channel, .parent_id = job->parent_id, .human_pub = job->human_pub};
    if (hush_agent_insert_note(store, &notice) != HUSH_OK)
        return;
    hush_agent_emit(HUSH_CEVENT_JOB_DONE, job->channel, job->parent_id,
                    job->robot_pub, "cancelled");
}

void hush_agent_finish_job(hush_store_t *store, hush_agent_job_t *job, int ok)
{
    assert(job != NULL);
    hush_agent_trim(job->out);
    hush_agent_rewrite_mentions(job);
    if (job->kind == HUSH_AGENT_KIND_FIXUP) {
        job->ok = ok && job->out[0] != '\0';
        hush_agent_close_job(job);
        return;
    }
    if (job->cancelled) {
        hush_agent_note_stopped(store, job);
        hush_agent_release_line(store, job);
        hush_agent_close_job(job);
        return;
    }
    if (!ok || job->out[0] == '\0')
        hush_agent_note_failure(store, job);
    else if (job->kind == HUSH_AGENT_KIND_ELECT)
        hush_agent_finish_election(store, job);
    else if (store != NULL && hush_agent_publish_reply(store, job) != HUSH_OK)
        hush_agent_note_failure(store, job);
    hush_agent_release_line(store, job);
    hush_agent_close_job(job);
}

static void hush_agent_select_leader(char *out, size_t outsz, const hush_agent_job_t *job,
                                     const hush_agent_follow_t *slot)
{
    assert(out != NULL && outsz > 0);
    assert(job != NULL && slot != NULL);
    char name[HUSH_ROSTER_NAME_MAX] = {0};
    hush_agent_copy(name, sizeof(name), job->out);
    size_t len = strlen(name);
    for (size_t i = 0; i < sizeof(name) && len > 0; ++i) {
        if (strchr(".,!?", name[len - 1]) == NULL) break;
        name[--len] = '\0';
    }
    if (!hush_agent_lookup_hex_by_name(job->launch, name, out, outsz))
        hush_agent_copy(out, outsz, slot->convener);
}

static void hush_agent_finish_election(hush_store_t *store, const hush_agent_job_t *job)
{
    assert(job != NULL);
    if (store == NULL || job->launch == NULL)
        return;
    hush_agent_follow_t *slot = hush_agent_follow_find(job->parent_id);
    if (slot == NULL)
        return;
    char leader[HUSH_EVENT_PUBKEY_HEX_LEN + 1] = {0};
    hush_agent_select_leader(leader, sizeof(leader), job, slot);
    slot->electing = 0;
    hush_agent_follow_remove(slot, leader);
    hush_agent_start_plan_from_slot(store, job->launch, slot, leader);
}

static void hush_agent_fill_reply(hush_agent_note_in_t *out, const hush_agent_job_t *job)
{
    assert(out != NULL && job != NULL);
    *out = (hush_agent_note_in_t){.pubkey = job->robot_pub, .content = job->out,
        .channel = job->channel, .parent_id = job->parent_id, .human_pub = job->human_pub};
    if (job->last)
        return;
    size_t used = 0;
    for (size_t i = 0; i < (size_t)job->n_co_robots && i < sizeof(out->extra_p) / sizeof(out->extra_p[0]); ++i) {
        if (job->co_npubs[i][0] != '\0' && strcmp(job->co_npubs[i], job->robot_pub) != 0 &&
            strstr(job->out, job->co_npubs[i]) != NULL)
            out->extra_p[used++] = job->co_npubs[i];
    }
}

/* Rolls the thread brief forward to the robot's latest answer. */
static void hush_agent_brief_update(const hush_event_t *posted, const char *answer)
{
    char root[HUSH_EVENT_ID_HEX_LEN + 1];

    assert(posted != NULL);
    assert(answer != NULL);
    hush_agent_event_root(root, sizeof(root), posted);
    hush_thread_brief_set(root, answer);
}

static hush_status_t hush_agent_publish_reply(hush_store_t *store, const hush_agent_job_t *job)
{
    assert(store != NULL && job != NULL);
    hush_agent_note_in_t note = {0};
    hush_agent_fill_reply(&note, job);
    hush_event_t posted = {0};
    hush_agent_fill_note(&posted, &note);
    HUSH_TRY(hush_store_insert(store, &posted));
    hush_thread_record(&posted);
    hush_agent_brief_update(&posted, job->out);
    if (job->launch == NULL)
        return HUSH_OK;
    if (job->kind == HUSH_AGENT_KIND_PLAN) {
        hush_agent_follow_t *slot = hush_agent_follow_find(job->parent_id);
        if (slot != NULL) hush_agent_parse_plan(job->launch, job->out, slot);
    }
    hush_agent_emit(HUSH_CEVENT_JOB_DONE, job->channel, job->parent_id, job->robot_pub, "job_done");
    hush_agent_on_posted(store, job->launch, &posted);
    return HUSH_OK;
}

void hush_agent_read_job(hush_agent_job_t *job)
{
    assert(job != NULL);
    if (job->fd < 0)
        return;
    for (size_t i = 0; i < sizeof(job->out); ++i) {
        int more = 0;
        char *mark;

        if (hush_agent_read_chunk(job, &more) != HUSH_OK) { job->out[0] = '\0'; return; }
        /* A worker that dies mid-answer writes one HUSH_JOB_ERR line into
         * the stream. Keep the text before it; carry the reason to the
         * failure note instead of publishing the marker. */
        mark = strstr(job->out, HUSH_AGENT_ERR_MARK);
        if (mark != NULL) {
            char *line_end;

            hush_agent_copy(job->diag, sizeof(job->diag),
                            mark + HUSH_AGENT_ERR_MARK_LEN);
            line_end = strchr(job->diag, '\n');
            if (line_end != NULL)
                *line_end = '\0';
            hush_agent_trim(job->diag);
            *mark = '\0';
            job->done = 1;
            return;
        }
        if (!more) return;
    }
}

static hush_status_t hush_agent_read_chunk(hush_agent_job_t *job, int *more)
{
    assert(job != NULL && job->fd >= 0);
    assert(more != NULL && job->out_n < sizeof(job->out));
    *more = 0;
    char buf[HUSH_AGENT_PATH_MAX] = {0};
    size_t room = sizeof(job->out) - 1 - job->out_n;
    size_t requested = room < sizeof(buf) ? room : sizeof(buf);
    ssize_t count = read(job->fd, buf, requested == 0 ? 1 : requested);
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return HUSH_OK;
    if (count > 0 && (size_t)count <= room) {
        memcpy(job->out + job->out_n, buf, (size_t)count);
        job->out_n += (size_t)count;
        job->out[job->out_n] = '\0';
        *more = 1;
        return HUSH_OK;
    }
    int closed = close(job->fd);
    job->fd = HUSH_AGENT_FD_NONE;
    return count == 0 && closed == 0 ? HUSH_OK : HUSH_ERR_IO;
}

int hush_agent_job_timed_out(const hush_agent_job_t *job, time_t now)
{
    assert(job != NULL);
    if (job->started <= 0)
        return 0;
    return now >= job->started + (time_t)HUSH_AGENT_TIMEOUT_S;
}

static int hush_agent_robot_busy(const hush_agent_robot_t *bot,
                                 const hush_event_t *parent)
{
    char root[HUSH_EVENT_ID_HEX_LEN + 1];
    const char *hex;
    hush_agent_job_t *jobs = hush_agent_jobs();
    size_t i;

    assert(bot != NULL);
    assert(parent != NULL);
    hex = bot->hex != NULL ? bot->hex : "";
    if (hex[0] == '\0')
        return 0;
    hush_agent_event_root(root, sizeof(root), parent);
    for (i = 0; i < (size_t)HUSH_AGENT_JOBS_MAX; i++) {
        if (!jobs[i].busy)
            continue;
        if (strcmp(jobs[i].robot_pub, hex) != 0)
            continue;
        if (strcmp(jobs[i].parent_id, root) == 0)
            return 1;
    }
    return 0;
}



void hush_agent_handle_mention(hush_store_t *store, const hush_launch_t *launch,
                                      const hush_event_t *event, const char *mention)
{
    assert(store != NULL && launch != NULL && event != NULL);
    if (mention == NULL || mention[0] == '\0' || hush_agent_is_human(launch, mention))
        return;
    hush_agent_robot_t robot = {0};
    if (!hush_agent_lookup_robot(&robot, launch, mention) ||
        !hush_agent_is_work_ok(launch, &robot) || hush_agent_robot_busy(&robot, event))
        return;
    hush_agent_emit(HUSH_CEVENT_MENTION, NULL, NULL, robot.hex, mention);
    hush_agent_job_in_t job = {.store = store, .launch = launch, .bot = &robot,
        .parent = event, .ask = event->content, .last = 1};
    hush_agent_mentions_t mentions = {0};
    if (hush_agent_is_human(launch, event->pubkey) &&
        !hush_agent_prepare_human_job(&job, &mentions))
        return;
    (void)hush_agent_begin_work(&job);
}

static void hush_agent_greet_mentions(const hush_agent_job_in_t *job,
                                       const hush_agent_mentions_t *mentions)
{
    assert(job != NULL && mentions != NULL);
    for (size_t i = 0; i < mentions->count && i < (size_t)HUSH_AGENT_FOLLOW_ROBOTS; ++i) {
        hush_agent_robot_t peer = {0};
        if (hush_agent_lookup_robot(&peer, job->launch, mentions->hexes[i]))
            hush_agent_on_deck(job->store, &peer, job->parent, HUSH_ROSTER_INTRO_DEFAULT);
    }
}

static int hush_agent_prepare_human_job(hush_agent_job_in_t *job, hush_agent_mentions_t *mentions)
{
    assert(job != NULL && mentions != NULL);
    mentions->count = hush_agent_collect_hexes(job->launch, job->parent, mentions->hexes,
                                               HUSH_AGENT_FOLLOW_ROBOTS);
    mentions->mode = hush_agent_classify(job->launch, job->parent, mentions->hexes,
                                         mentions->count, mentions->assigns);
    hush_agent_greet_mentions(job, mentions);
    job->scoped = mentions->mode == HUSH_AGENT_MODE_EXPLICIT;
    job->mode = (int)mentions->mode;
    job->last = mentions->count <= 1;
    if (mentions->mode == HUSH_AGENT_MODE_BROADCAST && mentions->count > HUSH_AGENT_PAIR_COUNT) {
        hush_agent_dispatch_group(job, mentions);
        return 0;
    }
    size_t idx = 0;
    for (; idx < mentions->count && idx < (size_t)HUSH_AGENT_FOLLOW_ROBOTS; ++idx) {
        if (strcmp(mentions->hexes[idx], job->bot->hex) == 0) break;
    }
    if (mentions->count > 1) {
        size_t start = idx > 0 ? idx : 1;
        if (!hush_agent_follow_push(job, mentions, start))
            return 0;
    }
    if (idx > 0 && idx < mentions->count)
        return 0;
    if (job->scoped && idx < mentions->count && mentions->assigns[idx].has_ask)
        job->ask = mentions->assigns[idx].ask;
    return 1;
}

static hush_agent_follow_t *hush_agent_prepare_group(const hush_agent_job_in_t *job,
                                                     const hush_agent_mentions_t *mentions)
{
    assert(job != NULL && mentions != NULL);
    char root[HUSH_EVENT_ID_HEX_LEN + 1] = {0};
    hush_agent_event_root(root, sizeof(root), job->parent);
    hush_agent_follow_t *slot = hush_agent_follow_take(root);
    if (slot == NULL)
        return NULL;
    hush_agent_event_channel(slot->channel, sizeof(slot->channel), job->parent);
    hush_agent_copy(slot->human_pub, sizeof(slot->human_pub), job->parent->pubkey);
    hush_agent_copy(slot->ask, sizeof(slot->ask), job->parent->content);
    slot->mode = job->mode;
    for (size_t i = 0; i < mentions->count && i < (size_t)HUSH_AGENT_FOLLOW_ROBOTS; ++i)
        hush_agent_follow_push_hex(slot, mentions->hexes[i], NULL);
    return slot;
}

static void hush_agent_note_follow_busy(const hush_agent_job_in_t *job)
{
    hush_agent_note_in_t notice = {0};
    char channel[HUSH_EVENT_MAX_TAG_LEN + 1];
    char root[HUSH_EVENT_ID_HEX_LEN + 1];

    assert(job != NULL);
    if (job->store == NULL || job->bot == NULL || job->parent == NULL)
        return;
    hush_agent_event_channel(channel, sizeof(channel), job->parent);
    hush_agent_event_root(root, sizeof(root), job->parent);
    notice.pubkey = job->bot->hex;
    notice.content = HUSH_AGENT_FOLLOW_FULL_LINE;
    notice.channel = channel;
    notice.parent_id = root;
    notice.human_pub = job->parent->pubkey;
    (void)hush_agent_insert_note(job->store, &notice);
    hush_agent_emit(HUSH_CEVENT_JOB_DONE, channel, root, job->bot->hex,
                    "follow_full");
}

static void hush_agent_dispatch_group(const hush_agent_job_in_t *job,
                                      const hush_agent_mentions_t *mentions)
{
    assert(job != NULL && mentions != NULL);
    char candidates[HUSH_AGENT_FOLLOW_ROBOTS][HUSH_EVENT_PUBKEY_HEX_LEN + 1] = {{0}};
    size_t count = hush_agent_leader_candidates(job->launch, mentions->hexes,
                                                mentions->count, candidates);
    if (count == 0 || strcmp(job->bot->hex, candidates[0]) != 0)
        return;
    hush_agent_follow_t *slot = hush_agent_prepare_group(job, mentions);
    if (slot == NULL) {
        hush_agent_note_follow_busy(job);
        return;
    }
    if (count == 1) {
        hush_agent_follow_remove(slot, candidates[0]);
        hush_agent_start_plan_from_slot(job->store, job->launch, slot, candidates[0]);
        return;
    }
    hush_agent_copy(slot->convener, sizeof(slot->convener), candidates[0]);
    slot->electing = 1;
    hush_agent_begin_elect(job->store, job->launch, slot, job->parent, candidates, count);
}

static int hush_agent_is_work_ok(const hush_launch_t *launch,
                                 const hush_agent_robot_t *bot)
{
    assert(launch != NULL);
    assert(bot != NULL);
    if (bot->slug != NULL && strcmp(bot->slug, HUSH_LAUNCH_PAYNE_SLUG) == 0)
        return 1;
    if (bot->role != NULL && strcmp(bot->role, HUSH_ROSTER_ROLE_CHAPERON) == 0)
        return 0;
    return 1;
}

static void hush_agent_push_hex(char hexes[][HUSH_EVENT_PUBKEY_HEX_LEN + 1],
                                size_t *n, size_t maxn, const char *hex)
{
    size_t i;

    assert(n != NULL);
    if (hex == NULL || hex[0] == '\0' || *n >= maxn)
        return;
    for (i = 0; i < *n; i++) {
        if (strcmp(hexes[i], hex) == 0)
            return;
    }
    hush_agent_copy(hexes[*n], sizeof(hexes[0]), hex);
    (*n)++;
}

/* Copies the clause following `nostr:<npub>` up to the next `nostr:` token or
 * end-of-string, collapsing whitespace to single spaces. Returns 0 when the
 * token is missing or has no trailing text. */
static int hush_agent_extract_clause(char *out, size_t outsz,
                                     const char *content, const char *npub)
{
    char key[HUSH_IDENTITY_NPUB_MAX + 8];
    const char *p;
    const char *q;
    size_t o;
    size_t cap;
    int gap;

    assert(out != NULL);
    assert(outsz > 0);
    out[0] = '\0';
    if (content == NULL || npub == NULL || npub[0] == '\0')
        return 0;
    (void)snprintf(key, sizeof(key), "nostr:%s", npub);
    p = strstr(content, key);
    if (p == NULL)
        return 0;
    p += strlen(key);
    while (*p != '\0' && hush_agent_is_space(*p))
        p++;
    q = strstr(p, "nostr:");
    cap = outsz - 1;
    if (cap > (size_t)HUSH_AGENT_TASK_MAX)
        cap = (size_t)HUSH_AGENT_TASK_MAX;
    o = 0;
    gap = 0;
    for (; *p != '\0' && p != q && o < cap; p++) {
        if (hush_agent_is_space(*p)) {
            gap = 1;
            continue;
        }
        if (gap && o > 0) {
            out[o++] = ' ';
            if (o >= cap)
                break;
        }
        gap = 0;
        out[o++] = *p;
    }
    out[o] = '\0';
    return o > 0;
}

/* Classifies a human note over `nhex` tagged robots and fills `assigns` with
 * each robot's extracted clause. Deterministic: explicit when every robot has
 * its own clause, broadcast when at most one does, ambiguous otherwise. */
static hush_agent_mode_t hush_agent_classify(
    const hush_launch_t *launch, const hush_event_t *ev,
    const char hexes[][HUSH_EVENT_PUBKEY_HEX_LEN + 1], size_t nhex,
    hush_agent_assign_t *assigns)
{
    hush_agent_robot_t bot;
    size_t i;
    size_t nhas;

    assert(launch != NULL);
    assert(ev != NULL);
    assert(assigns != NULL);
    nhas = 0;
    for (i = 0; i < nhex; i++) {
        memset(&assigns[i], 0, sizeof(assigns[i]));
        hush_agent_copy(assigns[i].hex, sizeof(assigns[i].hex), hexes[i]);
        if (hush_agent_lookup_robot(&bot, launch, hexes[i]) &&
            bot.npub != NULL && bot.npub[0] != '\0')
            assigns[i].has_ask = hush_agent_extract_clause(
                assigns[i].ask, sizeof(assigns[i].ask), ev->content, bot.npub);
        /* Ignore tiny filler clauses ("and", "to", "or") so a clustered
         * broadcast with a trailing connector is not read as explicit. */
        if (assigns[i].has_ask && strlen(assigns[i].ask) < 4)
            assigns[i].has_ask = 0;
        if (assigns[i].has_ask)
            nhas++;
    }
    if (nhex <= 1)
        return HUSH_AGENT_MODE_SOLO;
    if (nhas >= nhex)
        return HUSH_AGENT_MODE_EXPLICIT;
    /* Everything not clearly explicit goes through the LLM (cooperate for a
     * pair, leader election + planning for three or more robots). */
    return HUSH_AGENT_MODE_BROADCAST;
}

/* Leadership/leadership-enhancing skill ids. Used to rank leader candidates
 * when Major is absent from a 3+ robot broadcast. Tunable list. */
static int hush_agent_is_leadership_skill(const char *id)
{
    static const char *const skills[] = {
        "system:hive-patterns",
        "system:conflict-break",
        "system:canvas-coach",
        "system:summary-handoff",
        "system:job-cap"
    };
    size_t i;

    if (id == NULL)
        return 0;
    for (i = 0; i < sizeof(skills) / sizeof(skills[0]); i++)
        if (strcmp(id, skills[i]) == 0)
            return 1;
    return 0;
}

static int hush_agent_leadership_score(const hush_launch_t *launch,
                                       const char *hex)
{
    size_t i;
    size_t k;
    int score;

    if (launch == NULL || hex == NULL || hex[0] == '\0')
        return 0;
    if (strcmp(hex, launch->payne.pubkey_hex) == 0) {
        score = 0;
        for (k = 0; k < launch->npayne_skills; k++)
            if (hush_agent_is_leadership_skill(launch->payne_skills[k]))
                score++;
        return score;
    }
    for (i = 0; i < launch->roster.nagents; i++) {
        if (strcmp(launch->roster.agents[i].id.pubkey_hex, hex) != 0)
            continue;
        score = 0;
        for (k = 0; k < launch->roster.agents[i].nskills; k++)
            if (hush_agent_is_leadership_skill(
                    launch->roster.agents[i].skills[k]))
                score++;
        return score;
    }
    return 0;
}

/* Builds the leader candidate pool for a 3+ robot group: Major is the sole
 * candidate when present, else leadership-skilled robots, else all robots. */
static size_t hush_agent_leader_candidates(
    const hush_launch_t *launch,
    const char hexes[][HUSH_EVENT_PUBKEY_HEX_LEN + 1], size_t nhex,
    char out[][HUSH_EVENT_PUBKEY_HEX_LEN + 1])
{
    size_t i;
    size_t n = 0;

    assert(launch != NULL);
    assert(out != NULL);
    for (i = 0; i < nhex; i++) {
        if (strcmp(hexes[i], launch->payne.pubkey_hex) == 0) {
            hush_agent_copy(out[0], sizeof(out[0]), hexes[i]);
            return 1;
        }
    }
    for (i = 0; i < nhex; i++) {
        if (hush_agent_leadership_score(launch, hexes[i]) > 0)
            hush_agent_copy(out[n++], sizeof(out[0]), hexes[i]);
    }
    if (n > 0)
        return n;
    for (i = 0; i < nhex; i++)
        hush_agent_copy(out[n++], sizeof(out[0]), hexes[i]);
    return n;
}

/* Maps a robot display name to its pubkey hex. Checks Payne then roster. */
static int hush_agent_lookup_hex_by_name(const hush_launch_t *launch,
                                         const char *name,
                                         char *out, size_t outsz)
{
    size_t i;

    assert(launch != NULL);
    assert(out != NULL);
    assert(outsz > 0);
    out[0] = '\0';
    if (name == NULL || name[0] == '\0')
        return 0;
    if (launch->payne_name[0] != '\0' &&
        strcmp(launch->payne_name, name) == 0) {
        hush_agent_copy(out, outsz, launch->payne.pubkey_hex);
        return 1;
    }
    for (i = 0; i < launch->roster.nagents; i++) {
        if (strcmp(launch->roster.agents[i].name, name) == 0) {
            hush_agent_copy(out, outsz,
                            launch->roster.agents[i].id.pubkey_hex);
            return 1;
        }
    }
    return 0;
}

/* Parses a leader's ```plan fence into slot->next[]/next_ask[]/group[]/order.
 * Each task line may carry an integer wave prefix; tasks sharing a wave run in
 * parallel and waves run in order (fifo) or reverse order (lifo). A worker in
 * slot->next[] missing from the plan is appended as its own sequential wave
 * (falls back to the shared ask). */
static void hush_agent_parse_plan(const hush_launch_t *launch,
                                  const char *text,
                                  hush_agent_follow_t *slot)
{
    char tmp_hex[HUSH_AGENT_FOLLOW_ROBOTS][HUSH_EVENT_PUBKEY_HEX_LEN + 1];
    char tmp_ask[HUSH_AGENT_FOLLOW_ROBOTS][HUSH_AGENT_TASK_MAX];
    int tmp_group[HUSH_AGENT_FOLLOW_ROBOTS];
    size_t ntmp = 0;
    size_t i;
    size_t j;
    const char *p;
    const char *end;
    int next_auto = 1;

    assert(launch != NULL);
    assert(slot != NULL);
    if (text == NULL)
        return;
    p = strstr(text, HUSH_AGENT_PLAN_FENCE);
    if (p == NULL)
        return;
    p += strlen(HUSH_AGENT_PLAN_FENCE);
    end = strstr(p, HUSH_AGENT_PLAN_END);
    slot->order = 0;
    slot->parallel = 0;

    while (p != NULL && *p != '\0' && (end == NULL || p < end) &&
           ntmp < (size_t)HUSH_AGENT_FOLLOW_ROBOTS) {
        char line[HUSH_AGENT_TASK_MAX];
        const char *q = strchr(p, '\n');
        size_t len = (q != NULL && (end == NULL || q < end))
            ? (size_t)(q - p) : (end != NULL ? (size_t)(end - p) : strlen(p));
        if (len >= sizeof(line))
            len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = '\0';
        if (len > 0 && line[len - 1] == '\r')
            line[len - 1] = '\0';

        if (strncmp(line, "order:", 6) == 0) {
            const char *v = line + 6;
            while (*v == ' ' || *v == '\t')
                v++;
            slot->order = (strncmp(v, "lifo", 4) == 0 ||
                           strncmp(v, "filo", 4) == 0);
        } else if (strncmp(line, "parallel:", 9) == 0) {
            const char *v = line + 9;
            while (*v == ' ' || *v == '\t')
                v++;
            slot->parallel = (v[0] == 'y' || v[0] == 'Y');
        } else {
            const char *s = line;
            const char *name;
            const char *colon;
            const char *task;
            char namebuf[HUSH_ROSTER_NAME_MAX];
            char hex[HUSH_EVENT_PUBKEY_HEX_LEN + 1];
            size_t nlen;
            int grp = 0;
            int has_grp = 0;

            while (*s == ' ' || *s == '\t')
                s++;
            if (*s >= '0' && *s <= '9') {
                while (*s >= '0' && *s <= '9') {
                    grp = grp * 10 + (*s - '0');
                    s++;
                }
                has_grp = 1;
                while (*s == ' ' || *s == '\t')
                    s++;
            }
            if (!has_grp)
                grp = next_auto;

            name = s;
            colon = strchr(name, ':');
            if (colon != NULL && colon > name) {
                nlen = (size_t)(colon - name);
                if (nlen >= sizeof(namebuf))
                    nlen = sizeof(namebuf) - 1;
                memcpy(namebuf, name, nlen);
                namebuf[nlen] = '\0';
                task = colon + 1;
                while (*task == ' ' || *task == '\t')
                    task++;
                if (hush_agent_lookup_hex_by_name(launch, namebuf, hex,
                                                  sizeof(hex))) {
                    hush_agent_copy(tmp_hex[ntmp], sizeof(tmp_hex[0]), hex);
                    hush_agent_copy(tmp_ask[ntmp], sizeof(tmp_ask[0]), task);
                    tmp_group[ntmp] = grp;
                    ntmp++;
                    next_auto = grp + 1;
                }
            }
        }
        if (q == NULL || (end != NULL && q >= end))
            break;
        p = q + 1;
    }

    /* Legacy plan-level `parallel: yes` -> one wave (everything parallel). */
    if (slot->parallel) {
        for (i = 0; i < ntmp; i++)
            tmp_group[i] = 1;
    }

    /* Stable sort by wave number ascending. */
    for (i = 1; i < ntmp; i++) {
        char hx[HUSH_EVENT_PUBKEY_HEX_LEN + 1];
        char tk[HUSH_AGENT_TASK_MAX];
        int tg = tmp_group[i];
        hush_agent_copy(hx, sizeof(hx), tmp_hex[i]);
        hush_agent_copy(tk, sizeof(tk), tmp_ask[i]);
        j = i;
        while (j > 0 && tmp_group[j - 1] > tg) {
            tmp_group[j] = tmp_group[j - 1];
            hush_agent_copy(tmp_hex[j], sizeof(tmp_hex[0]), tmp_hex[j - 1]);
            hush_agent_copy(tmp_ask[j], sizeof(tmp_ask[0]), tmp_ask[j - 1]);
            j--;
        }
        tmp_group[j] = tg;
        hush_agent_copy(tmp_hex[j], sizeof(tmp_hex[0]), hx);
        hush_agent_copy(tmp_ask[j], sizeof(tmp_ask[0]), tk);
    }

    /* Reverse the whole array for lifo (reverses wave execution order). */
    if (slot->order == 1) {
        for (i = 0; i < ntmp / 2; i++) {
            char hx[HUSH_EVENT_PUBKEY_HEX_LEN + 1];
            char tk[HUSH_AGENT_TASK_MAX];
            int tg;
            hush_agent_copy(hx, sizeof(hx), tmp_hex[i]);
            hush_agent_copy(tk, sizeof(tk), tmp_ask[i]);
            tg = tmp_group[i];
            hush_agent_copy(tmp_hex[i], sizeof(tmp_hex[i]),
                            tmp_hex[ntmp - 1 - i]);
            hush_agent_copy(tmp_ask[i], sizeof(tmp_ask[i]),
                            tmp_ask[ntmp - 1 - i]);
            tmp_group[i] = tmp_group[ntmp - 1 - i];
            hush_agent_copy(tmp_hex[ntmp - 1 - i], sizeof(tmp_hex[0]), hx);
            hush_agent_copy(tmp_ask[ntmp - 1 - i], sizeof(tmp_ask[0]), tk);
            tmp_group[ntmp - 1 - i] = tg;
        }
    }

    /* Capture the original worker membership before rebuilding. */
    {
        char old_hex[HUSH_AGENT_FOLLOW_ROBOTS][HUSH_EVENT_PUBKEY_HEX_LEN + 1];
        size_t nold = slot->nnext;
        int max_group = 0;
        for (i = 0; i < nold; i++)
            hush_agent_copy(old_hex[i], sizeof(old_hex[0]), slot->next[i]);

        slot->nnext = 0;
        for (i = 0; i < ntmp; i++) {
            hush_agent_copy(slot->next[slot->nnext], sizeof(slot->next[0]),
                            tmp_hex[i]);
            if (tmp_ask[i][0] != '\0')
                hush_agent_copy(slot->next_ask[slot->nnext],
                                sizeof(slot->next_ask[0]), tmp_ask[i]);
            else
                slot->next_ask[slot->nnext][0] = '\0';
            slot->group[slot->nnext] = tmp_group[i];
            if (tmp_group[i] > max_group)
                max_group = tmp_group[i];
            slot->nnext++;
        }
        /* Append any worker the plan omitted as its own sequential wave. */
        for (i = 0; i < nold; i++) {
            int already = 0;
            for (j = 0; j < slot->nnext; j++) {
                if (strcmp(slot->next[j], old_hex[i]) == 0) {
                    already = 1;
                    break;
                }
            }
            if (already || slot->nnext >= (size_t)HUSH_AGENT_FOLLOW_ROBOTS)
                continue;
            hush_agent_copy(slot->next[slot->nnext], sizeof(slot->next[0]),
                            old_hex[i]);
            slot->next_ask[slot->nnext][0] = '\0';
            slot->group[slot->nnext] = ++max_group;
            slot->nnext++;
        }
        slot->at = 0;
        slot->inflight = 0;
        if (ntmp > 0)
            slot->scoped = 1; /* workers now have per-robot sub-tasks */
    }
}

static size_t hush_agent_collect_hexes(const hush_launch_t *launch,
                                       const hush_event_t *ev,
                                       char hexes[][HUSH_EVENT_PUBKEY_HEX_LEN + 1],
                                       size_t maxn)
{
    hush_agent_robot_t bot;
    const char *p;
    size_t n = 0;
    size_t i;

    assert(launch != NULL);
    assert(ev != NULL);
    p = ev->content;
    while (p != NULL && *p != '\0' && n < maxn) {
        char tok[HUSH_IDENTITY_NPUB_MAX];
        size_t k = 0;

        p = strstr(p, "nostr:");
        if (p == NULL)
            break;
        p += 6;
        while (p[k] != '\0' && !hush_agent_is_space(p[k])
               && k + 1 < sizeof(tok)) {
            tok[k] = p[k];
            k++;
        }
        tok[k] = '\0';
        if (hush_agent_lookup_robot(&bot, launch, tok) &&
            hush_agent_is_work_ok(launch, &bot))
            hush_agent_push_hex(hexes, &n, maxn, bot.hex);
        p += k > 0 ? k : 1;
    }
    for (i = 0; i < ev->tag_count && i < (size_t)HUSH_EVENT_MAX_TAGS; i++) {
        if (strcmp(ev->tags[i][0], "p") != 0)
            continue;
        if (!hush_agent_lookup_robot(&bot, launch, ev->tags[i][1]))
            continue;
        if (!hush_agent_is_work_ok(launch, &bot))
            continue;
        hush_agent_push_hex(hexes, &n, maxn, bot.hex);
    }
    return n;
}

static hush_agent_follow_t *hush_agent_follow_find(const char *root)
{
    size_t i;

    if (root == NULL || root[0] == '\0')
        return NULL;
    for (i = 0; i < (size_t)HUSH_AGENT_FOLLOW_MAX; i++) {
        if (g_follow[i].live && strcmp(g_follow[i].root, root) == 0)
            return &g_follow[i];
    }
    return NULL;
}

static hush_agent_follow_t *hush_agent_follow_take(const char *root)
{
    hush_agent_follow_t *slot;
    size_t i;

    slot = hush_agent_follow_find(root);
    if (slot != NULL)
        return slot;
    for (i = 0; i < (size_t)HUSH_AGENT_FOLLOW_MAX; i++) {
        if (!g_follow[i].live) {
            memset(&g_follow[i], 0, sizeof(g_follow[i]));
            g_follow[i].live = 1;
            hush_agent_copy(g_follow[i].root, sizeof(g_follow[i].root), root);
            return &g_follow[i];
        }
    }
    return NULL;
}

static void hush_agent_follow_push_hex(hush_agent_follow_t *slot,
                                       const char *hex, const char *ask)
{
    size_t i;

    assert(slot != NULL);
    if (hex == NULL || hex[0] == '\0' ||
        slot->nnext >= (size_t)HUSH_AGENT_FOLLOW_ROBOTS)
        return;
    for (i = 0; i < slot->nnext; i++) {
        if (strcmp(slot->next[i], hex) == 0)
            return;
    }
    hush_agent_copy(slot->next[slot->nnext], sizeof(slot->next[0]), hex);
    if (ask != NULL && ask[0] != '\0')
        hush_agent_copy(slot->next_ask[slot->nnext],
                        sizeof(slot->next_ask[0]), ask);
    else
        slot->next_ask[slot->nnext][0] = '\0';
    /* Default to a sequential wave (one task per group); parse_plan overrides
     * these groups with the leader's chosen waves. */
    slot->group[slot->nnext] = (int)slot->nnext + 1;
    slot->nnext++;
}

static void hush_agent_follow_remove(hush_agent_follow_t *slot,
                                     const char *hex)
{
    size_t i;
    size_t w;

    assert(slot != NULL);
    if (hex == NULL || hex[0] == '\0')
        return;
    w = 0;
    for (i = 0; i < slot->nnext; i++) {
        if (strcmp(slot->next[i], hex) == 0)
            continue;
        if (w != i) {
            hush_agent_copy(slot->next[w], sizeof(slot->next[0]),
                            slot->next[i]);
            hush_agent_copy(slot->next_ask[w], sizeof(slot->next_ask[0]),
                            slot->next_ask[i]);
            slot->group[w] = slot->group[i];
        }
        w++;
    }
    slot->nnext = w;
}

static int hush_agent_follow_push(const hush_agent_job_in_t *job,
                                  const hush_agent_mentions_t *mentions,
                                  size_t start)
{
    hush_agent_follow_t *slot;
    char root[HUSH_EVENT_ID_HEX_LEN + 1];
    size_t i;

    assert(job != NULL);
    assert(mentions != NULL);
    hush_agent_event_root(root, sizeof(root), job->parent);
    slot = hush_agent_follow_take(root);
    if (slot == NULL) {
        hush_agent_note_follow_busy(job);
        return 0;
    }
    hush_agent_event_channel(slot->channel, sizeof(slot->channel), job->parent);
    hush_agent_copy(slot->human_pub, sizeof(slot->human_pub), job->parent->pubkey);
    hush_agent_copy(slot->ask, sizeof(slot->ask), job->parent->content);
    if (job->scoped)
        slot->scoped = 1;
    slot->mode = job->mode;
    for (i = start; i < mentions->count &&
                    i < (size_t)HUSH_AGENT_FOLLOW_ROBOTS; i++) {
        const char *ask = mentions->assigns[i].has_ask
            ? mentions->assigns[i].ask : NULL;

        hush_agent_follow_push_hex(slot, mentions->assigns[i].hex, ask);
    }
    return 1;
}

void hush_agent_release_line(hush_store_t *store, hush_agent_job_t *job)
{
    hush_wake_in_t in;
    time_t now;

    assert(job != NULL);
    if (job->kind == HUSH_AGENT_KIND_FIXUP)
        return;
    now = time(NULL);
    memset(&in, 0, sizeof(in));
    in.store = store;
    in.robot_hex = job->robot_pub;
    in.root_hex = job->parent_id;
    in.trigger_id = job->trigger_id;
    in.channel = job->channel;
    in.now = now;
    (void)hush_wake_done(&in);
    if (store != NULL && job->robot_pub[0] != '\0' && job->parent_id[0] != '\0')
        (void)hush_presence_clear(store, job->robot_pub, job->parent_id,
                                  job->channel, now);
}

void hush_agent_presence_put(hush_store_t *store, hush_agent_job_t *job,
                                    const char *slug)
{
    hush_presence_in_t in;

    assert(job != NULL);
    assert(slug != NULL);
    if (store == NULL || job->robot_pub[0] == '\0' || job->parent_id[0] == '\0')
        return;
    if (job->kind == HUSH_AGENT_KIND_FIXUP)
        return;
    memset(&in, 0, sizeof(in));
    in.pubkey = job->robot_pub;
    in.role = job->robot_role;
    in.slug = slug;
    in.channel = job->channel;
    in.root = job->parent_id;
    in.now = time(NULL);
    if (hush_presence_publish(store, &in) == HUSH_OK)
        hush_agent_copy(job->presence_slug, sizeof(job->presence_slug), slug);
}

void hush_agent_nudge_stuck(hush_store_t *store, hush_agent_job_t *job)
{
    hush_event_t ev;
    char line[HUSH_EVENT_MAX_CONTENT];

    assert(job != NULL);
    if (store == NULL || job->launch == NULL)
        return;
    if (!job->launch->payne_enabled)
        return;
    if (strcmp(job->robot_pub, job->launch->payne.pubkey_hex) == 0)
        return;
    if (snprintf(line, sizeof(line),
                 "Stuck: %s needs unstuck or disable. %s",
                 job->robot_name[0] ? job->robot_name : "robot",
                 job->ask[0] ? job->ask : "") >= (int)sizeof(line))
        hush_agent_copy(line, sizeof(line), "Stuck. Unstuck or disable.");
    memset(&ev, 0, sizeof(ev));
    hush_agent_copy(ev.pubkey, sizeof(ev.pubkey), job->robot_pub);
    ev.kind = (uint32_t)HUSH_AGENT_KIND_NOTE;
    ev.created_at = (int64_t)time(NULL);
    hush_agent_copy(ev.content, sizeof(ev.content), line);
    ev.tag_count = 3;
    memcpy(ev.tags[0][0], "h", 2);
    hush_agent_copy(ev.tags[0][1], sizeof(ev.tags[0][1]), job->channel);
    memcpy(ev.tags[1][0], "e", 2);
    hush_agent_copy(ev.tags[1][1], sizeof(ev.tags[1][1]), job->parent_id);
    memcpy(ev.tags[2][0], "p", 2);
    hush_agent_copy(ev.tags[2][1], sizeof(ev.tags[2][1]),
                    job->launch->payne.pubkey_hex);
    hush_agent_mention(store, (hush_launch_t *)job->launch, &ev,
                       job->launch->payne.npub);
}

int hush_agent_job_enabled(const hush_agent_job_t *job)
{
    hush_agent_robot_t bot;

    assert(job != NULL);
    if (job->launch == NULL)
        return 1;
    return hush_agent_lookup_robot(&bot, job->launch, job->robot_pub);
}

static int hush_agent_begin_work(const hush_agent_job_in_t *in)
{
    char root[HUSH_EVENT_ID_HEX_LEN + 1];
    char channel[HUSH_EVENT_MAX_TAG_LEN + 1];
    hush_agent_job_in_t job;

    assert(in != NULL);
    assert(in->store != NULL);
    assert(in->bot != NULL);
    assert(in->parent != NULL);
    if (hush_agent_turns_full(in->store, in->launch, in->parent)) {
        hush_agent_nudge_chaperon(in->store, in->launch, in->parent);
        return 0;
    }
    hush_agent_on_deck(in->store, in->bot, in->parent,
                       "I am on deck. Standing orders are noted.");
    hush_agent_event_root(root, sizeof(root), in->parent);
    hush_agent_event_channel(channel, sizeof(channel), in->parent);
    hush_agent_emit(HUSH_CEVENT_INTRO, channel, root, in->bot->hex,
                    in->bot->name);
    if (!hush_agent_can_start(in->launch, in->bot)) {
        hush_agent_note_no_runtime(in->store, in->bot, in->parent);
        return 0;
    }
    hush_agent_note_fallback(in->store, in->bot, in->parent);
    job = *in;
    if (hush_agent_start_grok(&job) != HUSH_OK) {
        hush_agent_note_start_failed(in->store, in->bot, in->parent);
        return 0;
    }
    hush_agent_emit(HUSH_CEVENT_JOB_START, channel, root, in->bot->hex,
                    in->ask != NULL ? in->ask : "");
    return 1;
}

static void hush_agent_begin_plan(const hush_agent_job_in_t *in)
{
    hush_agent_job_in_t plan;

    assert(in != NULL);
    plan = *in;
    plan.leader = 1;
    (void)hush_agent_begin_work(&plan);
}

/* Runs a one-shot leader-election pass. The convener (first candidate) hosts
 * the grok call; the output is a single candidate name parsed in finish_job. */
static void hush_agent_begin_elect(
    hush_store_t *store, const hush_launch_t *launch,
    hush_agent_follow_t *slot, const hush_event_t *ev,
    const char cands[][HUSH_EVENT_PUBKEY_HEX_LEN + 1], size_t ncand)
{
    hush_agent_robot_t convener;
    hush_agent_job_in_t in;
    char prompt[HUSH_ROSTER_PROMPT_MAX];
    size_t off;
    size_t i;

    assert(store != NULL);
    assert(launch != NULL);
    assert(slot != NULL);
    assert(ev != NULL);
    if (!hush_agent_lookup_robot(&convener, launch, slot->convener))
        return;

    hush_agent_copy(prompt, sizeof(prompt), HUSH_AGENT_ELECT_PROMPT);
    off = strlen(prompt);
    if (off + 14 < sizeof(prompt)) {
        memcpy(prompt + off, " Candidates:", 12);
        off += 12;
    }
    for (i = 0; i < ncand; i++) {
        hush_agent_robot_t c;
        int m;
        if (!hush_agent_lookup_robot(&c, launch, cands[i]))
            continue;
        m = snprintf(prompt + off, sizeof(prompt) - off, " %s(skills:%d)",
                     c.name != NULL ? c.name : "robot",
                     hush_agent_leadership_score(launch, cands[i]));
        if (m < 0 || (size_t)m >= sizeof(prompt) - off)
            break;
        off += (size_t)m;
    }
    if (slot->ask[0] != '\0' && off + 10 < sizeof(prompt)) {
        char snip[HUSH_AGENT_SNIP_MAX + HUSH_IDENTITY_NPUB_MAX + 1];
        hush_agent_snip_line(snip, sizeof(snip), slot->ask);
        (void)snprintf(prompt + off, sizeof(prompt) - off, " Task: %s", snip);
    }

    memset(&in, 0, sizeof(in));
    in.store = store;
    in.launch = launch;
    in.bot = &convener;
    in.parent = ev;
    in.ask = slot->ask;
    in.mode = slot->mode;
    in.elect = 1;
    in.prompt_override = prompt;
    (void)hush_agent_begin_work(&in);
}

/* Starts the leader's planning pass from the follow slot (used after an
 * election, where we reconstruct the parent note from slot state). */
static void hush_agent_start_plan_from_slot(
    hush_store_t *store, const hush_launch_t *launch,
    hush_agent_follow_t *slot, const char *leader_hex)
{
    hush_agent_robot_t leader;
    hush_event_t parent;
    hush_agent_job_in_t in;
    size_t i;

    assert(store != NULL);
    assert(launch != NULL);
    assert(slot != NULL);
    if (!hush_agent_lookup_robot(&leader, launch, leader_hex))
        return;

    memset(&parent, 0, sizeof(parent));
    hush_agent_copy(parent.id, sizeof(parent.id), slot->root);
    hush_agent_copy(parent.pubkey, sizeof(parent.pubkey), slot->human_pub);
    hush_agent_copy(parent.content, sizeof(parent.content), slot->ask);
    parent.kind = (uint32_t)HUSH_AGENT_KIND_NOTE;
    parent.tag_count = 1;
    memcpy(parent.tags[0][0], "h", 2);
    hush_agent_copy(parent.tags[0][1], sizeof(parent.tags[0][1]),
                    slot->channel);
    for (i = 0; i < slot->nnext && parent.tag_count < HUSH_EVENT_MAX_TAGS;
         i++) {
        hush_agent_robot_t w;
        if (slot->next[i][0] == '\0')
            continue;
        if (!hush_agent_lookup_robot(&w, launch, slot->next[i]))
            continue;
        if (w.npub == NULL || w.npub[0] == '\0')
            continue;
        memcpy(parent.tags[parent.tag_count][0], "p", 2);
        hush_agent_copy(parent.tags[parent.tag_count][1],
                        sizeof(parent.tags[parent.tag_count][1]), w.npub);
        parent.tag_count++;
    }

    memset(&in, 0, sizeof(in));
    in.store = store;
    in.launch = launch;
    in.bot = &leader;
    in.parent = &parent;
    in.ask = slot->ask;
    in.mode = slot->mode;
    in.leader = 1;
    hush_agent_begin_plan(&in);
}

void hush_agent_follow_kick(hush_store_t *store,
                                   const hush_launch_t *launch,
                                   const hush_event_t *ev)
{
    hush_agent_follow_t *slot;
    hush_agent_robot_t bot;
    hush_agent_job_in_t in;
    char root[HUSH_EVENT_ID_HEX_LEN + 1];
    const char *hex;
    int cur_group;

    assert(store != NULL);
    assert(launch != NULL);
    assert(ev != NULL);
    hush_agent_event_root(root, sizeof(root), ev);
    slot = hush_agent_follow_find(root);
    if (slot == NULL)
        return;

    /* A work note finished. If we are mid-wave, one parallel task completed;
     * wait for the rest of the wave before starting the next one. */
    if (slot->inflight > 0) {
        slot->inflight--;
        if (slot->inflight > 0)
            return;
    }
    if (slot->at >= slot->nnext)
        return;

    /* Dispatch the next wave: every task sharing this group number. Tasks in
     * a wave run in parallel; waves run in order. */
    cur_group = slot->group[slot->at];
    {
        int last_wave = hush_agent_follow_last_wave(slot);

        while (slot->at < slot->nnext && slot->group[slot->at] == cur_group) {
            size_t at = slot->at;
            hex = slot->next[at];
            slot->at++;
            if (!hush_agent_lookup_robot(&bot, launch, hex))
                continue;
            if (!hush_agent_is_work_ok(launch, &bot))
                continue;
            hush_agent_emit(HUSH_CEVENT_FOLLOW, slot->channel, root, bot.hex,
                            "follow");
            memset(&in, 0, sizeof(in));
            in.store = store;
            in.launch = launch;
            in.bot = &bot;
            in.parent = ev;
            in.ask = (slot->scoped && slot->next_ask[at][0] != '\0')
                ? slot->next_ask[at]
                : (slot->ask[0] ? slot->ask : ev->content);
            in.scoped = slot->scoped;
            in.mode = slot->mode;
            in.last = last_wave;
            if (hush_agent_begin_work(&in))
                slot->inflight++;
        }
    }
}

static void hush_agent_emit(const char *type, const char *channel,
                            const char *root, const char *actor,
                            const char *note)
{
    hush_cevent_t ev;

    memset(&ev, 0, sizeof(ev));
    hush_agent_copy(ev.type, sizeof(ev.type), type);
    hush_agent_copy(ev.channel, sizeof(ev.channel), channel);
    hush_agent_copy(ev.root, sizeof(ev.root), root);
    hush_agent_copy(ev.actor, sizeof(ev.actor), actor);
    hush_agent_copy(ev.note, sizeof(ev.note), note);
    (void)hush_cevent_emit(&ev);
}

void hush_agent_name_dangling_peer(hush_agent_job_t *job, int scoped)
{
    size_t alen;
    char last;
    const char *nm;
    size_t plen;
    size_t nlen;

    assert(job != NULL);
    if (!scoped || job->n_co_robots <= 0 || job->ask[0] == '\0')
        return;
    alen = strlen(job->ask);
    last = job->ask[alen - 1];
    if (last == '.' || last == '!' || last == '?')
        return;
    nm = job->co_names[0][0] ? job->co_names[0] : "robot";
    plen = strlen(job->prompt);
    nlen = strlen(nm);
    if (plen + 3 + nlen >= sizeof(job->prompt))
        return;
    job->prompt[plen++] = ' ';
    job->prompt[plen++] = '@';
    memcpy(job->prompt + plen, nm, nlen);
    plen += nlen;
    job->prompt[plen] = '\0';
}

void hush_agent_append_assign(char *prompt, size_t promptsz,
                                     const char *ask,
                                     const hush_launch_t *launch,
                                     const char *self_hex)
{
    char human[HUSH_EVENT_MAX_CONTENT + 1];
    char snip[HUSH_AGENT_SNIP_MAX + HUSH_IDENTITY_NPUB_MAX + 1];
    size_t used;
    int n;

    assert(prompt != NULL);
    assert(promptsz > 0);
    if (ask == NULL || ask[0] == '\0')
        return;
    hush_agent_copy(human, sizeof(human), ask);
    hush_agent_humanize_ask(human, sizeof(human), launch, self_hex);
    hush_agent_snip_line(snip, sizeof(snip), human);
    used = strlen(prompt);
    if (used + 8 >= promptsz)
        return;
    n = snprintf(prompt + used, promptsz - used, "%s%s",
                 HUSH_AGENT_ASSIGN, snip);
    if (n < 0)
        prompt[used] = '\0';
}

const hush_launch_channel_t *hush_agent_channel(
    const hush_launch_t *launch, const char *slug)
{
    size_t i;

    if (launch == NULL || slug == NULL || slug[0] == '\0')
        return NULL;
    for (i = 0; i < launch->nchannels && i < (size_t)HUSH_LAUNCH_CHANNELS_MAX;
         i++) {
        if (strcmp(launch->channels[i].slug, slug) == 0)
            return &launch->channels[i];
    }
    return NULL;
}

void hush_agent_reset_follow(const hush_launch_t *launch,
                             const hush_event_t *event)
{
    assert(launch != NULL);
    assert(event != NULL);
    if (!hush_agent_is_human(launch, event->pubkey))
        return;
    char root[HUSH_EVENT_ID_HEX_LEN + 1] = {0};
    hush_agent_event_root(root, sizeof(root), event);
    hush_agent_follow_t *slot = hush_agent_follow_find(root);
    if (slot != NULL && slot->inflight == 0)
        memset(slot, 0, sizeof(*slot));
}

static size_t hush_agent_count_turns(hush_store_t *store,
                                     const hush_launch_t *launch, const char *root)
{
    assert(store != NULL && launch != NULL && root != NULL);
    size_t count = hush_store_count(store);
    assert(count <= (size_t)HUSH_STORE_CAPACITY);
    size_t turns = 0;
    for (size_t i = count; i > 0; --i) {
        hush_event_t event = {0};
        hush_agent_robot_t bot;
        if (hush_store_get(store, i - 1, &event) != HUSH_OK) break;
        if (event.kind != HUSH_AGENT_KIND_NOTE || !hush_agent_event_is_root(&event, root)) continue;
        if (hush_agent_is_human(launch, event.pubkey)) break;
        if (hush_agent_lookup_robot(&bot, launch, event.pubkey) &&
            hush_agent_is_work_note(event.content)) ++turns;
    }
    return turns;
}

int hush_agent_is_work_note(const char *content)
{
    static const char *const skip[] = {
        HUSH_AGENT_INTRO_PREFIX,
        HUSH_AGENT_ACK_LINE,
        HUSH_AGENT_CHAPERON_LINE,
        "Holding.",
        "Robots do not chain",
        "Not on this channel.",
        "Say the ask.",
        "I heard:",
        "This channel is humans talking"
    };
    size_t i;
    size_t n;

    if (content == NULL || content[0] == '\0')
        return 0;
    for (i = 0; i < sizeof(skip) / sizeof(skip[0]); i++) {
        n = strlen(skip[i]);
        if (strncmp(content, skip[i], n) == 0)
            return 0;
    }
    return 1;
}

static int hush_agent_turns_full(hush_store_t *store,
                                 const hush_launch_t *launch,
                                 const hush_event_t *ev)
{
    const hush_launch_channel_t *ch;
    char channel[HUSH_EVENT_MAX_TAG_LEN + 1];
    char root[HUSH_EVENT_ID_HEX_LEN + 1];
    int cap;

    assert(store != NULL);
    assert(ev != NULL);
    if (launch == NULL)
        return 0;
    hush_agent_event_channel(channel, sizeof(channel), ev);
    ch = hush_agent_channel(launch, channel);
    cap = HUSH_LAUNCH_TURNS_DEFAULT;
    if (ch != NULL && ch->max_robot_turns > 0)
        cap = ch->max_robot_turns;
    hush_agent_event_root(root, sizeof(root), ev);
    return hush_agent_count_turns(store, launch, root) >= (size_t)cap;
}

static int hush_agent_lookup_slug(hush_agent_robot_t *out,
                                  const hush_launch_t *launch,
                                  const char *slug)
{
    size_t i;
    const hush_roster_agent_t *agent;

    assert(out != NULL);
    assert(launch != NULL);
    if (slug == NULL || slug[0] == '\0' ||
        strcmp(slug, HUSH_LAUNCH_PAYNE_SLUG) == 0)
        return hush_agent_lookup_robot(out, launch, launch->payne.pubkey_hex);
    for (i = 0; i < launch->roster.nagents; i++) {
        agent = &launch->roster.agents[i];
        if (strcmp(agent->slug, slug) != 0)
            continue;
        return hush_agent_lookup_robot(out, launch, agent->id.pubkey_hex);
    }
    return 0;
}

static void hush_agent_nudge_chaperon(hush_store_t *store,
                                      const hush_launch_t *launch,
                                      const hush_event_t *ev)
{
    const hush_launch_channel_t *ch;
    hush_agent_robot_t bot;
    hush_agent_note_in_t in;
    char channel[HUSH_EVENT_MAX_TAG_LEN + 1];
    char root[HUSH_EVENT_ID_HEX_LEN + 1];
    const char *slug;

    assert(store != NULL);
    assert(ev != NULL);
    hush_agent_event_channel(channel, sizeof(channel), ev);
    hush_agent_event_root(root, sizeof(root), ev);
    ch = hush_agent_channel(launch, channel);
    slug = HUSH_LAUNCH_PAYNE_SLUG;
    if (ch != NULL && ch->chaperon[0] != '\0')
        slug = ch->chaperon;
    hush_agent_emit(HUSH_CEVENT_CHAPERON, channel, root, slug,
                    HUSH_AGENT_CHAPERON_LINE);
    if (launch == NULL || !hush_agent_lookup_slug(&bot, launch, slug))
        return;
    memset(&in, 0, sizeof(in));
    in.pubkey = bot.hex != NULL ? bot.hex : "";
    in.content = HUSH_AGENT_CHAPERON_LINE;
    in.channel = channel;
    in.parent_id = root;
    in.human_pub = ev->pubkey;
    (void)hush_agent_insert_note(store, &in);
}

