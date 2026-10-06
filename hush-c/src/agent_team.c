/* agent_team.c: Chief of Staff team fence, milestone hop, project cwd. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "hush_agent.h"
#include "hush_agent_internal.h"
#include "hush_thread.h"

#define HUSH_AGENT_TEAM_OPEN "```team"
#define HUSH_AGENT_HOP_OPEN "```hop"
#define HUSH_AGENT_FENCE_END "```"
#define HUSH_AGENT_TEAM_BAR " | "
#define HUSH_AGENT_TEAM_ASK \
    "Raise this team? Reply Yes or No in this thread."
#define HUSH_AGENT_TEAM_REFUSED "Team refused."
#define HUSH_AGENT_TEAM_RAISED "Team raised."
#define HUSH_AGENT_TEAM_DECLINED "Team declined."
#define HUSH_AGENT_TEAM_STOPPED "Team raise stopped."
#define HUSH_AGENT_HOP_REFUSED "Hop refused."
#define HUSH_AGENT_CHIEF_RULE \
    " You may end with one ```team fence (Name | provider | prompt on each" \
    " line) or one ```hop fence naming a milestone. A team waits for Yes."

enum {
    HUSH_AGENT_TEAM_SLOTS = 8,
    HUSH_AGENT_HOP_LIST = 8,
    HUSH_AGENT_HOP_BRIEF = 200,
    HUSH_AGENT_TEAM_BODY = 2048
};

typedef struct {
    int used;
    char root[HUSH_EVENT_ID_HEX_LEN + 1];
    hush_agent_team_t team;
} hush_agent_team_slot_t;

static hush_agent_team_slot_t g_team[HUSH_AGENT_TEAM_SLOTS];

/* Copies src into dst. Truncates to dstsz - 1. */
static void hush_agent_team_copy(char *dst, size_t dstsz, const char *src);

/* True when text has room for extra. Appends extra on its own line. */
static void hush_agent_team_append(char *text, size_t textsz, const char *extra);

/* Points at the interior of one fence. 0 when the fence is absent. */
static int hush_agent_fence_span(const char *text, const char *open,
                                 const char **start, const char **end);

/* Deletes one fence from text. 0 when it was absent. */
static int hush_agent_fence_cut(char *text, const char *open);

/* Parses one "Name | provider | prompt" line. */
static hush_status_t hush_agent_team_line(hush_agent_team_member_t *out,
                                         const char *line);

/* True when launch names this pubkey as the hive owner. */
static int hush_agent_team_is_owner(const hush_launch_t *launch,
                                   const char *pubkey);

/* Pending team for root, or NULL. */
static hush_agent_team_slot_t *hush_agent_team_find(const char *root);

/* True when this job is a solo turn by Major. */
static int hush_agent_is_chief(const hush_agent_job_t *job);

/* True when ev is a channel root, not a reply. */
static int hush_agent_is_channel_root(const hush_event_t *ev);

/* True when ev is in channel. */
static int hush_agent_in_channel(const hush_event_t *ev, const char *channel);

/* Copies a one-line brief snip. */
static void hush_agent_brief_line(char *out, size_t outsz, const char *root);

/* First active milestone in channel with this desk name. */
static hush_status_t hush_agent_find_milestone(char *root_out, size_t rootsz,
                                              const hush_store_t *store,
                                              const char *channel,
                                              const char *name);

/* Copies the single line inside a ```hop fence. */
static hush_status_t hush_agent_hop_name(char *out, size_t outsz,
                                        const char *text);

/* Posts one line from Major into root. */
static void hush_agent_team_say(hush_store_t *store, const hush_launch_t *launch,
                               const hush_event_t *ev, const char *root,
                               const char *line);

/* Raises every member. Rolls back any member already raised on failure. */
static hush_status_t hush_agent_team_raise(hush_store_t *store,
                                          hush_launch_t *launch,
                                          const hush_agent_team_t *team);

/* True when path is an absolute directory without a parent segment. */
static int hush_agent_cwd_is_dir(const char *path);

hush_status_t hush_agent_team_parse(hush_agent_team_t *out, const char *text)
{
    const char *body;
    const char *end;
    char block[HUSH_AGENT_TEAM_BODY];
    char *cursor;
    char *line;

    if (out == NULL)
        return HUSH_ERR_ARG;
    memset(out, 0, sizeof(*out));
    if (!hush_agent_fence_span(text, HUSH_AGENT_TEAM_OPEN, &body, &end))
        return HUSH_ERR_NOT_FOUND;
    if ((size_t)(end - body) >= sizeof(block))
        return HUSH_ERR_PARSE;
    memcpy(block, body, (size_t)(end - body));
    block[end - body] = '\0';
    cursor = block;
    while (*cursor != '\0') {
        char *next;

        if (out->count >= (size_t)HUSH_AGENT_TEAM_MAX) {
            memset(out, 0, sizeof(*out));
            return HUSH_ERR_PARSE;
        }
        next = strchr(cursor, '\n');
        if (next != NULL)
            *next = '\0';
        line = cursor;
        if (line[0] != '\0' && line[0] != '\r') {
            if (hush_agent_team_line(&out->member[out->count], line) != HUSH_OK) {
                memset(out, 0, sizeof(*out));
                return HUSH_ERR_PARSE;
            }
            out->count++;
        }
        if (next == NULL)
            break;
        cursor = next + 1;
    }
    if (out->count == 0)
        return HUSH_ERR_PARSE;
    return HUSH_OK;
}

hush_status_t hush_agent_team_check(const hush_launch_t *launch,
                                    const hush_agent_team_t *team)
{
    size_t i;
    size_t j;

    if (launch == NULL || team == NULL || team->count == 0)
        return HUSH_ERR_ARG;
    if (launch->roster.nagents + team->count > (size_t)HUSH_ROSTER_AGENTS_MAX)
        return HUSH_ERR_FULL;
    for (i = 0; i < team->count; ++i) {
        const hush_agent_team_member_t *member = &team->member[i];

        if (!hush_roster_is_provider(member->provider))
            return HUSH_ERR_PARSE;
        if (hush_roster_is_name_clash(member->name, hush_launch_payne_name(launch)))
            return HUSH_ERR_PARSE;
        if (hush_roster_name_holder(&launch->roster, member->name, NULL) != NULL)
            return HUSH_ERR_PARSE;
        for (j = 0; j < i; ++j) {
            if (hush_roster_is_name_clash(member->name, team->member[j].name))
                return HUSH_ERR_PARSE;
        }
    }
    return HUSH_OK;
}

int hush_agent_team_offer(const char *root, const hush_agent_team_t *team)
{
    hush_agent_team_slot_t *slot;
    size_t i;

    if (root == NULL || root[0] == '\0' || team == NULL || team->count == 0)
        return 0;
    slot = hush_agent_team_find(root);
    if (slot == NULL) {
        for (i = 0; i < (size_t)HUSH_AGENT_TEAM_SLOTS; ++i) {
            if (!g_team[i].used) {
                slot = &g_team[i];
                break;
            }
        }
    }
    if (slot == NULL)
        return 0;
    memset(slot, 0, sizeof(*slot));
    slot->used = 1;
    hush_agent_team_copy(slot->root, sizeof(slot->root), root);
    slot->team = *team;
    return 1;
}

int hush_agent_team_answer(hush_store_t *store, hush_launch_t *launch,
                           const hush_event_t *ev)
{
    char root[HUSH_EVENT_ID_HEX_LEN + 1];
    hush_agent_team_slot_t *slot;
    hush_agent_loop_answer_t answer;
    hush_status_t st;

    if (store == NULL || launch == NULL || ev == NULL)
        return 0;
    answer = hush_agent_loop_parse_answer(ev->content);
    if (answer == HUSH_AGENT_LOOP_ANSWER_NONE)
        return 0;
    if (!hush_agent_team_is_owner(launch, ev->pubkey))
        return 0;
    hush_agent_event_root(root, sizeof(root), ev);
    slot = hush_agent_team_find(root);
    if (slot == NULL)
        return 0;
    if (answer == HUSH_AGENT_LOOP_ANSWER_NO) {
        slot->used = 0;
        hush_agent_team_say(store, launch, ev, root, HUSH_AGENT_TEAM_DECLINED);
        return 1;
    }
    st = hush_agent_team_check(launch, &slot->team);
    if (st != HUSH_OK) {
        slot->used = 0;
        hush_agent_team_say(store, launch, ev, root, HUSH_AGENT_TEAM_REFUSED);
        return 1;
    }
    st = hush_agent_team_raise(store, launch, &slot->team);
    slot->used = 0;
    hush_agent_team_say(store, launch, ev, root,
                        st == HUSH_OK ? HUSH_AGENT_TEAM_RAISED
                                      : HUSH_AGENT_TEAM_STOPPED);
    return 1;
}

void hush_agent_chief_prepare(hush_agent_job_t *job, const hush_store_t *store,
                              char *hop_root, size_t hopsz)
{
    hush_agent_team_t team;
    char hop_name[HUSH_THREAD_DESK_TEXT_MAX + 1];
    hush_status_t st;

    assert(hop_root != NULL && hopsz > 0);
    hop_root[0] = '\0';
    if (!hush_agent_is_chief(job))
        return;
    st = hush_agent_team_parse(&team, job->out);
    if (st == HUSH_OK &&
        hush_agent_team_check(job->launch, &team) == HUSH_OK &&
        hush_agent_team_offer(job->parent_id, &team)) {
        hush_agent_fence_cut(job->out, HUSH_AGENT_TEAM_OPEN);
        hush_agent_team_append(job->out, sizeof(job->out), HUSH_AGENT_TEAM_ASK);
    } else if (st == HUSH_ERR_PARSE) {
        hush_agent_fence_cut(job->out, HUSH_AGENT_TEAM_OPEN);
        hush_agent_team_append(job->out, sizeof(job->out), HUSH_AGENT_TEAM_REFUSED);
    }
    st = hush_agent_hop_name(hop_name, sizeof(hop_name), job->out);
    if (st == HUSH_ERR_NOT_FOUND)
        return;
    hush_agent_fence_cut(job->out, HUSH_AGENT_HOP_OPEN);
    if (st != HUSH_OK ||
        hush_agent_find_milestone(hop_root, hopsz, store, job->channel,
                                  hop_name) != HUSH_OK) {
        hop_root[0] = '\0';
        hush_agent_team_append(job->out, sizeof(job->out), HUSH_AGENT_HOP_REFUSED);
    }
}

void hush_agent_chief_equip(hush_agent_job_t *job, const hush_store_t *store)
{
    char index[HUSH_AGENT_NOTE_MAX];
    size_t used;
    int n;

    assert(job != NULL);
    if (!hush_agent_is_chief(job) || store == NULL)
        return;
    used = strlen(job->prompt);
    n = snprintf(job->prompt + used, sizeof(job->prompt) - used, "%s",
                 HUSH_AGENT_CHIEF_RULE);
    if (n < 0 || (size_t)n >= sizeof(job->prompt) - used)
        job->prompt[used] = '\0';
    index[0] = '\0';
    hush_agent_team_append(index, sizeof(index), "Milestones:");
    {
        size_t i;
        size_t listed = 0;

        for (i = 0; i < hush_store_count(store) && i < (size_t)HUSH_STORE_CAPACITY; ++i) {
            hush_event_t ev;
            hush_thread_desk_t desk;
            char line[HUSH_THREAD_DESK_TEXT_MAX * 2 + HUSH_AGENT_HOP_BRIEF + 8];
            char brief[HUSH_AGENT_HOP_BRIEF + 1];

            if (listed >= (size_t)HUSH_AGENT_HOP_LIST)
                break;
            if (hush_store_get(store, i, &ev) != HUSH_OK)
                continue;
            if (!hush_agent_is_channel_root(&ev))
                continue;
            if (!hush_agent_in_channel(&ev, job->channel))
                continue;
            if (hush_thread_desk_get(ev.id, &desk) != HUSH_OK)
                continue;
            if (desk.archived || desk.name[0] == '\0')
                continue;
            hush_agent_brief_line(brief, sizeof(brief), ev.id);
            n = snprintf(line, sizeof(line), "%s | %s | %s",
                         desk.name, desk.category, brief);
            if (n > 0 && (size_t)n < sizeof(line))
                hush_agent_team_append(index, sizeof(index), line);
            listed++;
        }
    }
    if (strstr(index, " | ") != NULL)
        hush_agent_team_append(job->note, sizeof(job->note), index);
}

void hush_agent_apply_project_cwd(hush_agent_job_t *job)
{
    hush_thread_desk_t desk;
    size_t i;

    assert(job != NULL);
    job->project_tools = 0;
    if (job->launch == NULL || job->parent_id[0] == '\0')
        return;
    if (hush_thread_desk_get(job->parent_id, &desk) != HUSH_OK)
        return;
    if (desk.project[0] == '\0')
        return;
    for (i = 0; i < job->launch->nprojects && i < (size_t)HUSH_LAUNCH_PROJECTS_MAX; ++i) {
        const hush_launch_project_t *project = &job->launch->projects[i];

        if (strcmp(project->slug, desk.project) != 0)
            continue;
        if (!hush_agent_cwd_is_dir(project->path))
            return;
        hush_agent_team_copy(job->cwd, sizeof(job->cwd), project->path);
        job->project_tools = 1;
        return;
    }
}

static void hush_agent_team_copy(char *dst, size_t dstsz, const char *src)
{
    size_t n;

    assert(dst != NULL && dstsz > 0);
    if (src == NULL)
        src = "";
    n = strlen(src);
    if (n >= dstsz)
        n = dstsz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void hush_agent_team_append(char *text, size_t textsz, const char *extra)
{
    size_t used;
    int n;

    assert(text != NULL && extra != NULL);
    used = strlen(text);
    if (used + 1 >= textsz)
        return;
    n = snprintf(text + used, textsz - used, "%s%s", used > 0 ? "\n" : "", extra);
    if (n < 0 || (size_t)n >= textsz - used)
        text[used] = '\0';
}

static int hush_agent_fence_span(const char *text, const char *open,
                                 const char **start, const char **end)
{
    const char *hit;
    const char *close;

    assert(start != NULL && end != NULL);
    if (text == NULL || open == NULL)
        return 0;
    hit = strstr(text, open);
    if (hit == NULL)
        return 0;
    hit += strlen(open);
    if (*hit == '\n')
        hit++;
    close = strstr(hit, HUSH_AGENT_FENCE_END);
    if (close == NULL)
        return 0;
    *start = hit;
    *end = close;
    return 1;
}

static hush_status_t hush_agent_hop_name(char *out, size_t outsz,
                                        const char *text)
{
    const char *start;
    const char *end;
    size_t n = 0;

    assert(out != NULL && outsz > 0);
    out[0] = '\0';
    if (!hush_agent_fence_span(text, HUSH_AGENT_HOP_OPEN, &start, &end))
        return HUSH_ERR_NOT_FOUND;
    while (start < end && (*start == ' ' || *start == '\t'))
        start++;
    while (start + n < end && start[n] != '\n' && start[n] != '\r')
        n++;
    if (n == 0 || n >= outsz)
        return HUSH_ERR_PARSE;
    memcpy(out, start, n);
    out[n] = '\0';
    return HUSH_OK;
}

static int hush_agent_fence_cut(char *text, const char *open)
{
    char *hit;
    char *close;

    assert(text != NULL && open != NULL);
    hit = strstr(text, open);
    if (hit == NULL)
        return 0;
    close = strstr(hit + strlen(open), HUSH_AGENT_FENCE_END);
    if (close == NULL)
        return 0;
    close += strlen(HUSH_AGENT_FENCE_END);
    if (*close == '\n')
        close++;
    memmove(hit, close, strlen(close) + 1);
    return 1;
}

static hush_status_t hush_agent_team_line(hush_agent_team_member_t *out,
                                         const char *line)
{
    const char *bar;
    const char *bar2;
    size_t nlen;
    size_t plen;
    size_t tlen;

    assert(out != NULL && line != NULL);
    bar = strstr(line, HUSH_AGENT_TEAM_BAR);
    if (bar == NULL)
        return HUSH_ERR_PARSE;
    bar2 = strstr(bar + strlen(HUSH_AGENT_TEAM_BAR), HUSH_AGENT_TEAM_BAR);
    if (bar2 == NULL)
        return HUSH_ERR_PARSE;
    nlen = (size_t)(bar - line);
    plen = (size_t)(bar2 - (bar + strlen(HUSH_AGENT_TEAM_BAR)));
    tlen = strlen(bar2 + strlen(HUSH_AGENT_TEAM_BAR));
    if (tlen > 0 && line[strlen(line) - 1] == '\r')
        tlen--;
    if (nlen == 0 || nlen >= sizeof(out->name))
        return HUSH_ERR_PARSE;
    if (plen == 0 || plen >= sizeof(out->provider))
        return HUSH_ERR_PARSE;
    if (tlen == 0 || tlen >= sizeof(out->prompt))
        return HUSH_ERR_PARSE;
    memset(out, 0, sizeof(*out));
    memcpy(out->name, line, nlen);
    memcpy(out->provider, bar + strlen(HUSH_AGENT_TEAM_BAR), plen);
    memcpy(out->prompt, bar2 + strlen(HUSH_AGENT_TEAM_BAR), tlen);
    if (!hush_roster_name_prints(out->name))
        return HUSH_ERR_PARSE;
    if (!hush_roster_is_provider(out->provider))
        return HUSH_ERR_PARSE;
    return HUSH_OK;
}

static int hush_agent_team_is_owner(const hush_launch_t *launch,
                                   const char *pubkey)
{
    assert(launch != NULL);
    if (pubkey == NULL || launch->human.pubkey_hex[0] == '\0')
        return 0;
    return strcmp(pubkey, launch->human.pubkey_hex) == 0;
}

static hush_agent_team_slot_t *hush_agent_team_find(const char *root)
{
    size_t i;

    assert(root != NULL);
    for (i = 0; i < (size_t)HUSH_AGENT_TEAM_SLOTS; ++i) {
        if (g_team[i].used && strcmp(g_team[i].root, root) == 0)
            return &g_team[i];
    }
    return NULL;
}

static int hush_agent_is_chief(const hush_agent_job_t *job)
{
    if (job == NULL || job->launch == NULL)
        return 0;
    if (job->kind != HUSH_AGENT_KIND_NOTE_JOB || job->n_co_robots != 0)
        return 0;
    if (job->launch->payne.pubkey_hex[0] == '\0')
        return 0;
    return strcmp(job->robot_pub, job->launch->payne.pubkey_hex) == 0;
}

static int hush_agent_is_channel_root(const hush_event_t *ev)
{
    size_t i;

    assert(ev != NULL);
    if (ev->kind != (uint32_t)HUSH_AGENT_KIND_NOTE)
        return 0;
    for (i = 0; i < ev->tag_count && i < (size_t)HUSH_EVENT_MAX_TAGS; ++i) {
        if (strcmp(ev->tags[i][0], "e") == 0 && ev->tags[i][1][0] != '\0')
            return 0;
    }
    return 1;
}

static int hush_agent_in_channel(const hush_event_t *ev, const char *channel)
{
    char got[HUSH_EVENT_MAX_TAG_LEN + 1];

    assert(ev != NULL);
    if (channel == NULL)
        return 0;
    hush_agent_event_channel(got, sizeof(got), ev);
    return strcmp(got, channel) == 0;
}

static void hush_agent_brief_line(char *out, size_t outsz, const char *root)
{
    char brief[HUSH_THREAD_BRIEF_MAX + 1];
    size_t i;
    size_t n = 0;

    assert(out != NULL && outsz > 0);
    hush_thread_brief_get(root, brief, sizeof(brief));
    for (i = 0; brief[i] != '\0' && n + 1 < outsz && n < (size_t)HUSH_AGENT_HOP_BRIEF; ++i) {
        char ch = brief[i];

        if (ch == '\n' || ch == '\r' || ch == '\t')
            ch = ' ';
        out[n++] = ch;
    }
    out[n] = '\0';
}

static hush_status_t hush_agent_find_milestone(char *root_out, size_t rootsz,
                                              const hush_store_t *store,
                                              const char *channel,
                                              const char *name)
{
    size_t i;

    assert(root_out != NULL && rootsz > 0);
    root_out[0] = '\0';
    if (store == NULL || name == NULL || name[0] == '\0')
        return HUSH_ERR_NOT_FOUND;
    for (i = 0; i < hush_store_count(store) && i < (size_t)HUSH_STORE_CAPACITY; ++i) {
        hush_event_t ev;
        hush_thread_desk_t desk;

        if (hush_store_get(store, i, &ev) != HUSH_OK)
            continue;
        if (!hush_agent_is_channel_root(&ev))
            continue;
        if (!hush_agent_in_channel(&ev, channel))
            continue;
        if (hush_thread_desk_get(ev.id, &desk) != HUSH_OK)
            continue;
        if (desk.archived || strcmp(desk.name, name) != 0)
            continue;
        hush_agent_team_copy(root_out, rootsz, ev.id);
        return HUSH_OK;
    }
    return HUSH_ERR_NOT_FOUND;
}

static void hush_agent_team_say(hush_store_t *store, const hush_launch_t *launch,
                               const hush_event_t *ev, const char *root,
                               const char *line)
{
    char channel[HUSH_EVENT_MAX_TAG_LEN + 1];
    const char *speaker;
    hush_agent_note_in_t note;

    assert(store != NULL && launch != NULL && ev != NULL);
    assert(root != NULL && line != NULL);
    hush_agent_event_channel(channel, sizeof(channel), ev);
    speaker = launch->payne.pubkey_hex[0] != '\0'
        ? launch->payne.pubkey_hex : launch->human.pubkey_hex;
    memset(&note, 0, sizeof(note));
    note.pubkey = speaker;
    note.content = line;
    note.channel = channel;
    note.parent_id = root;
    note.human_pub = launch->human.pubkey_hex;
    (void)hush_agent_insert_note(store, &note);
}

static hush_status_t hush_agent_team_raise(hush_store_t *store,
                                          hush_launch_t *launch,
                                          const hush_agent_team_t *team)
{
    char slugs[HUSH_AGENT_TEAM_MAX][HUSH_ROSTER_NAME_MAX];
    size_t raised = 0;
    size_t i;

    assert(store != NULL && launch != NULL && team != NULL);
    for (i = 0; i < team->count; ++i) {
        hush_roster_agent_in_t in;
        const hush_roster_agent_t *held;
        hush_status_t st;

        memset(&in, 0, sizeof(in));
        hush_agent_team_copy(in.name, sizeof(in.name), team->member[i].name);
        hush_agent_team_copy(in.prompt, sizeof(in.prompt), team->member[i].prompt);
        hush_agent_team_copy(in.provider, sizeof(in.provider),
                             team->member[i].provider);
        st = hush_launch_add_agent(launch, store, &in, 0);
        if (st != HUSH_OK) {
            while (raised > 0) {
                raised--;
                (void)hush_launch_remove_agent(launch, slugs[raised]);
            }
            return st;
        }
        held = hush_roster_name_holder(&launch->roster, in.name, NULL);
        if (held == NULL)
            return HUSH_ERR_IO;
        hush_agent_team_copy(slugs[raised], sizeof(slugs[raised]), held->slug);
        raised++;
    }
    return HUSH_OK;
}

static int hush_agent_cwd_is_dir(const char *path)
{
    struct stat st;

    if (path == NULL || path[0] != '/')
        return 0;
    if (strstr(path, "/../") != NULL || strcmp(path, "..") == 0)
        return 0;
    if (stat(path, &st) != 0)
        return 0;
    return S_ISDIR(st.st_mode);
}
