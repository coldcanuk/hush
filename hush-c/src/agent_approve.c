/* agent_approve.c: the owner approves each robot turn (#279).
 * Under "Approve every action", hush_agent_begin_work hands every turn here
 * instead of starting the robot's runtime. The turn waits in memory, the
 * chaperon asks in the thread, and only the hive owner's typed Yes or No
 * answers it. Yes runs the turn through hush_agent_begin_work; No posts one
 * line and starts nothing. A relay restart forgets every waiting turn. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "hush_agent.h"
#include "hush_agent_internal.h"

/* M1 R2: one Yes/No ask the relay posted that no answer settled yet. An
 * empty id marks a free slot; root is set whenever id is. */
typedef struct {
    char root[HUSH_EVENT_ID_HEX_LEN + 1];
    char id[HUSH_EVENT_ID_HEX_LEN + 1];
} hush_agent_ask_t;

static hush_agent_held_t g_held[HUSH_AGENT_APPROVAL_MAX];
static unsigned long g_held_seq;
/* The turn being answered, copied out so its slot is free while it runs. */
static hush_agent_held_t g_taken;
/* M1 R2: ids of the asks the owner answered (a ring, oldest overwritten). */
static char g_answered[HUSH_AGENT_ANSWERED_MAX][HUSH_EVENT_ID_HEX_LEN + 1];
static size_t g_answered_next;
/* M1 R2: asks waiting for an answer, in posting order (a ring). */
static hush_agent_ask_t g_asks[HUSH_AGENT_ASKS_MAX];
static size_t g_asks_next;

static hush_agent_held_t *hush_agent_held_free(void);
static size_t hush_agent_held_guests(void);
static hush_agent_held_t *hush_agent_held_oldest(const char *root);
static void hush_agent_held_fill(hush_agent_held_t *held, const hush_agent_job_in_t *in);
static void hush_agent_held_point(hush_agent_held_t *held, hush_store_t *store,
                                  const hush_launch_t *launch, const hush_agent_robot_t *bot);
static void hush_agent_approval_say(hush_store_t *store, const hush_launch_t *launch,
                                    const hush_agent_held_t *held, const char *fmt);
static void hush_agent_approval_run(hush_store_t *store, const hush_launch_t *launch);
static void hush_agent_approval_guest(const hush_agent_job_in_t *in);
static void hush_agent_approval_decline(hush_store_t *store, const hush_launch_t *launch);
static void hush_agent_approval_next(hush_store_t *store, const hush_launch_t *launch);
/* True when ask (required) is a recorded answered ask id. */
static int hush_agent_answered_has(const char *ask);
/* Adds ask (required, non-empty) to the answered ring once. */
static void hush_agent_answered_put(const char *ask);
/* Writes ev's last "e" tag value to out, or "" when ev has none. */
static void hush_agent_named_ask(char *out, size_t outsz, const hush_event_t *ev);
/* Moves the oldest ask waiting in root (required) to out and frees its
 * slot; writes "" when none waits there. */
static void hush_agent_ask_take(char *out, size_t outsz, const char *root);
/* True when content (required) is a Yes/No ask line: approval, loop, team. */
static int hush_agent_is_yes_no_ask(const char *content);
/* True when content (required) ends with the team ask, spaces aside. */
static int hush_agent_is_team_ask(const char *content);

void hush_agent_approval_init(void)
{
    memset(g_held, 0, sizeof(g_held));
    memset(&g_taken, 0, sizeof(g_taken));
    g_held_seq = 0;
    memset(g_answered, 0, sizeof(g_answered));
    g_answered_next = 0;
    memset(g_asks, 0, sizeof(g_asks));
    g_asks_next = 0;
}

int hush_agent_approval_needed(const hush_agent_job_in_t *in)
{
    assert(in != NULL);
    if (in->launch == NULL || in->approved)
        return 0;
    /* #336: only an explicit auto-approve runs unasked. */
    return in->launch->roster.profile.approval != HUSH_ROSTER_APPROVAL_AUTO;
}

int hush_agent_approval_hold(const hush_agent_job_in_t *in)
{
    hush_agent_held_t *held = NULL;
    int guest = 0;

    assert(in != NULL && in->store != NULL && in->bot != NULL && in->parent != NULL);
    guest = !hush_agent_owner_asked(in);
    held = hush_agent_held_free();
    /* Other people share HUSH_AGENT_APPROVAL_GUEST_MAX entries, so the owner
     * always keeps the rest for their own turns. A refused guest is told so
     * in their own terms; the owner is told to answer one. */
    if (held == NULL || (guest && hush_agent_held_guests() >= HUSH_AGENT_APPROVAL_GUEST_MAX)) {
        if (guest)
            hush_agent_approval_guest(in);
        else
            hush_agent_chaperon_say(in->store, in->launch, in->parent,
                                    HUSH_AGENT_APPROVAL_FULL_LINE);
        return HUSH_AGENT_WORK_HALT;
    }
    hush_agent_held_fill(held, in);
    held->guest = guest;
    hush_agent_approval_say(in->store, in->launch, held,
                            in->elect ? HUSH_AGENT_APPROVAL_ELECT_FMT
                                      : HUSH_AGENT_APPROVAL_ASK_FMT);
    return HUSH_AGENT_WORK_HELD;
}

size_t hush_agent_approval_void(const char *root)
{
    size_t counted = 0;
    size_t i = 0;

    if (root == NULL || root[0] == '\0')
        return 0;
    for (i = 0; i < (size_t)HUSH_AGENT_APPROVAL_MAX; i++) {
        if (!g_held[i].used || strcmp(g_held[i].root, root) != 0)
            continue;
        if (g_held[i].in.follow)
            counted++;
        memset(&g_held[i], 0, sizeof(g_held[i]));
    }
    return counted;
}

int hush_agent_approval_answer(hush_store_t *store, const hush_launch_t *launch,
                               const hush_event_t *ev)
{
    char root[HUSH_EVENT_ID_HEX_LEN + 1] = {0};
    hush_agent_held_t *held = NULL;
    hush_agent_loop_answer_t answer = HUSH_AGENT_LOOP_ANSWER_NONE;

    /* Only the hive owner approves; robots and other humans cannot. */
    if (store == NULL || launch == NULL || ev == NULL ||
        !hush_agent_is_human(launch, ev->pubkey))
        return 0;
    hush_agent_event_root(root, sizeof(root), ev);
    held = hush_agent_held_oldest(root);
    if (held == NULL)
        return 0;
    answer = hush_agent_loop_parse_answer(ev->content);
    if (answer == HUSH_AGENT_LOOP_ANSWER_NONE)
        return 0;
    g_taken = *held;
    memset(held, 0, sizeof(*held));
    if (answer == HUSH_AGENT_LOOP_ANSWER_YES)
        hush_agent_approval_run(store, launch);
    else
        hush_agent_approval_decline(store, launch);
    return 1;
}

void hush_agent_ask_note(const hush_event_t *ev)
{
    char root[HUSH_EVENT_ID_HEX_LEN + 1] = {0};
    hush_agent_ask_t *slot = NULL;

    if (ev == NULL || !hush_agent_is_yes_no_ask(ev->content))
        return;
    hush_agent_event_root(root, sizeof(root), ev);
    if (strcmp(root, ev->id) == 0)
        return;
    slot = &g_asks[g_asks_next];
    hush_agent_copy(slot->root, sizeof(slot->root), root);
    hush_agent_copy(slot->id, sizeof(slot->id), ev->id);
    g_asks_next = (g_asks_next + 1) % (size_t)HUSH_AGENT_ASKS_MAX;
    assert(slot->id[0] != '\0');
}

void hush_agent_ask_void(const char *root)
{
    size_t i = 0;

    if (root == NULL || root[0] == '\0')
        return;
    for (i = 0; i < (size_t)HUSH_AGENT_ASKS_MAX; i++) {
        if (strcmp(g_asks[i].root, root) == 0)
            memset(&g_asks[i], 0, sizeof(g_asks[i]));
    }
    assert(g_asks_next < (size_t)HUSH_AGENT_ASKS_MAX);
}

void hush_agent_answered_mark(const hush_event_t *ev)
{
    char root[HUSH_EVENT_ID_HEX_LEN + 1] = {0};
    char ask[HUSH_EVENT_ID_HEX_LEN + 1] = {0};

    if (ev == NULL)
        return;
    hush_agent_event_root(root, sizeof(root), ev);
    /* The answer handlers settle the oldest wait in the conversation, so
     * that is the ask this answer settled, whichever ask the stamp named. */
    hush_agent_ask_take(ask, sizeof(ask), root);
    if (ask[0] == '\0')
        hush_agent_named_ask(ask, sizeof(ask), ev);
    /* A conversation root is never filed: a later plain Yes there must still
     * reach the robot (CoS 10:03). */
    if (ask[0] == '\0' || strcmp(ask, root) == 0)
        return;
    hush_agent_answered_put(ask);
}

int hush_agent_answered_repeat(const hush_launch_t *launch, const hush_event_t *ev)
{
    char ask[HUSH_EVENT_ID_HEX_LEN + 1] = {0};

    if (launch == NULL || ev == NULL || !hush_agent_is_human(launch, ev->pubkey))
        return 0;
    if (hush_agent_loop_parse_answer(ev->content) == HUSH_AGENT_LOOP_ANSWER_NONE)
        return 0;
    /* The "last pending ask" route of the spec never matches here: a waiting
     * ask is by definition not yet in the answered ring. */
    hush_agent_named_ask(ask, sizeof(ask), ev);
    return ask[0] != '\0' && hush_agent_answered_has(ask);
}

int hush_agent_is_approval_line(const char *content)
{
    static const char *const heads[] = {
        HUSH_AGENT_APPROVAL_ASK_HEAD,
        HUSH_AGENT_APPROVAL_NO_HEAD,
        HUSH_AGENT_APPROVAL_FULL_LINE,
        HUSH_AGENT_APPROVAL_GUEST_HEAD
    };
    size_t i = 0;

    if (content == NULL)
        return 0;
    for (i = 0; i < sizeof(heads) / sizeof(heads[0]); i++) {
        if (strncmp(content, heads[i], strlen(heads[i])) == 0)
            return 1;
    }
    return 0;
}

/* True when the hive owner asked for this turn: the note it answers is the
 * owner's, or a robot's note in a thread the owner opened (follow waves,
 * loop turns, the plan pass). Any other human's note, or a robot's note in
 * their thread, is a guest's ask. */
int hush_agent_owner_asked(const hush_agent_job_in_t *in)
{
    char root[HUSH_EVENT_ID_HEX_LEN + 1] = {0};
    hush_event_t opening = {0};
    hush_agent_robot_t bot = {0};

    assert(in != NULL && in->parent != NULL && in->store != NULL);
    if (hush_agent_is_human(in->launch, in->parent->pubkey))
        return 1;
    if (!hush_agent_lookup_robot(&bot, in->launch, in->parent->pubkey))
        return 0;
    hush_agent_event_root(root, sizeof(root), in->parent);
    return hush_store_find(in->store, &opening, root) == HUSH_OK &&
           hush_agent_is_human(in->launch, opening.pubkey);
}

static size_t hush_agent_held_guests(void)
{
    size_t n = 0;
    size_t i = 0;

    for (i = 0; i < (size_t)HUSH_AGENT_APPROVAL_MAX; i++) {
        if (g_held[i].used && g_held[i].guest)
            n++;
    }
    return n;
}

static hush_agent_held_t *hush_agent_held_free(void)
{
    size_t i = 0;

    for (i = 0; i < (size_t)HUSH_AGENT_APPROVAL_MAX; i++) {
        if (!g_held[i].used)
            return &g_held[i];
    }
    return NULL;
}

/* First come, first answered: one Yes or No settles one waiting turn. */
static hush_agent_held_t *hush_agent_held_oldest(const char *root)
{
    hush_agent_held_t *best = NULL;
    size_t i = 0;

    assert(root != NULL);
    for (i = 0; i < (size_t)HUSH_AGENT_APPROVAL_MAX; i++) {
        if (!g_held[i].used || strcmp(g_held[i].root, root) != 0)
            continue;
        if (best == NULL || g_held[i].seq < best->seq)
            best = &g_held[i];
    }
    return best;
}

static void hush_agent_held_fill(hush_agent_held_t *held, const hush_agent_job_in_t *in)
{
    assert(held != NULL && in != NULL);
    memset(held, 0, sizeof(*held));
    held->used = 1;
    held->seq = ++g_held_seq;
    held->in = *in;
    held->parent = *in->parent;
    hush_agent_event_root(held->root, sizeof(held->root), in->parent);
    hush_agent_copy(held->hex, sizeof(held->hex), in->bot->hex);
    hush_agent_copy(held->name, sizeof(held->name), in->bot->name);
    hush_agent_copy(held->ask, sizeof(held->ask), in->ask);
    hush_agent_copy(held->loop_note, sizeof(held->loop_note), in->loop_note);
    hush_agent_copy(held->prompt_override, sizeof(held->prompt_override),
                    in->prompt_override);
    hush_agent_copy(held->trigger, sizeof(held->trigger), in->trigger);
}

/* Re-points the copied input at the owned copies and the live robot. */
static void hush_agent_held_point(hush_agent_held_t *held, hush_store_t *store,
                                  const hush_launch_t *launch, const hush_agent_robot_t *bot)
{
    assert(held != NULL && store != NULL && launch != NULL && bot != NULL);
    held->in.store = store;
    held->in.launch = launch;
    held->in.bot = bot;
    held->in.parent = &held->parent;
    held->in.ask = held->in.ask != NULL ? held->ask : NULL;
    held->in.loop_note = held->in.loop_note != NULL ? held->loop_note : NULL;
    held->in.prompt_override =
        held->in.prompt_override != NULL ? held->prompt_override : NULL;
    held->in.trigger = held->in.trigger != NULL ? held->trigger : NULL;
}

static void hush_agent_approval_say(hush_store_t *store, const hush_launch_t *launch,
                                    const hush_agent_held_t *held, const char *fmt)
{
    char line[HUSH_AGENT_APPROVAL_LINE_MAX] = {0};

    assert(store != NULL && held != NULL && fmt != NULL);
    (void)snprintf(line, sizeof(line), fmt, held->name[0] != '\0' ? held->name : "robot");
    hush_agent_chaperon_say(store, launch, &held->parent, line);
}

/* #313: tells a refused guest which robot's turn did not start. */
static void hush_agent_approval_guest(const hush_agent_job_in_t *in)
{
    char line[HUSH_AGENT_APPROVAL_LINE_MAX] = {0};
    const char *name = NULL;

    assert(in != NULL && in->bot != NULL);
    name = (in->bot->name != NULL && in->bot->name[0] != '\0') ? in->bot->name : "robot";
    (void)snprintf(line, sizeof(line), HUSH_AGENT_APPROVAL_GUEST_FMT, name);
    hush_agent_chaperon_say(in->store, in->launch, in->parent, line);
}

static void hush_agent_approval_run(hush_store_t *store, const hush_launch_t *launch)
{
    hush_agent_robot_t bot = {0};

    assert(store != NULL && launch != NULL);
    /* A robot removed while its turn waited has nothing left to run. */
    if (!hush_agent_lookup_robot(&bot, launch, g_taken.hex)) {
        hush_agent_follow_release(&g_taken.parent, g_taken.in.follow, 0);
        hush_agent_approval_next(store, launch);
        return;
    }
    hush_agent_held_point(&g_taken, store, launch, &bot);
    /* A Yes that starts no job (no runtime ready) posts no robot reply, so
     * nothing else would ask the robots still queued behind it (#312). */
    if (hush_agent_begin_approved(&g_taken.in) == HUSH_AGENT_WORK_NONE)
        hush_agent_approval_next(store, launch);
}

static void hush_agent_approval_decline(hush_store_t *store, const hush_launch_t *launch)
{
    assert(store != NULL);
    hush_agent_approval_say(store, launch, &g_taken, HUSH_AGENT_APPROVAL_NO_FMT);
    hush_agent_follow_release(&g_taken.parent, g_taken.in.follow, 1);
    /* No ends this robot's turn and the loop, not the turns of the other
     * robots the mention named: each still gets its own ask (#312). */
    hush_agent_approval_next(store, launch);
}

/* #312: the answered turn ended with no job (a No, or a Yes that started
 * nothing). The robots queued behind an ordinary turn still get their own
 * asks. A team's election or plan pass decides the workers, so when it
 * does not run no worker is asked (G6). */
static void hush_agent_approval_next(hush_store_t *store, const hush_launch_t *launch)
{
    assert(store != NULL && launch != NULL);
    if (g_taken.in.leader || g_taken.in.elect)
        return;
    hush_agent_follow_resume(store, launch, &g_taken.parent);
}

/* True when ask is in the answered ring. */
static int hush_agent_answered_has(const char *ask)
{
    size_t i = 0;

    assert(ask != NULL);
    for (i = 0; i < (size_t)HUSH_AGENT_ANSWERED_MAX; i++) {
        if (strcmp(g_answered[i], ask) == 0)
            return 1;
    }
    return 0;
}

static void hush_agent_answered_put(const char *ask)
{
    assert(ask != NULL && ask[0] != '\0');
    if (hush_agent_answered_has(ask))
        return;
    hush_agent_copy(g_answered[g_answered_next], sizeof(g_answered[g_answered_next]), ask);
    g_answered_next = (g_answered_next + 1) % (size_t)HUSH_AGENT_ANSWERED_MAX;
    assert(hush_agent_answered_has(ask));
}

static void hush_agent_named_ask(char *out, size_t outsz, const hush_event_t *ev)
{
    size_t i = 0;

    assert(out != NULL && outsz > 0 && ev != NULL);
    out[0] = '\0';
    for (i = 0; i < ev->tag_count && i < (size_t)HUSH_EVENT_MAX_TAGS; i++) {
        if (strcmp(ev->tags[i][0], "e") == 0 && ev->tags[i][1][0] != '\0')
            hush_agent_copy(out, outsz, ev->tags[i][1]);
    }
}

/* Walks the ring from its oldest slot, so the first match is the oldest. */
static void hush_agent_ask_take(char *out, size_t outsz, const char *root)
{
    size_t i = 0;

    assert(out != NULL && outsz > 0 && root != NULL);
    out[0] = '\0';
    for (i = 0; i < (size_t)HUSH_AGENT_ASKS_MAX; i++) {
        hush_agent_ask_t *slot = &g_asks[(g_asks_next + i) % (size_t)HUSH_AGENT_ASKS_MAX];

        if (slot->id[0] == '\0' || strcmp(slot->root, root) != 0)
            continue;
        hush_agent_copy(out, outsz, slot->id);
        memset(slot, 0, sizeof(*slot));
        return;
    }
}

static int hush_agent_is_yes_no_ask(const char *content)
{
    const size_t head = strlen(HUSH_AGENT_APPROVAL_ASK_HEAD);

    assert(content != NULL);
    return strncmp(content, HUSH_AGENT_APPROVAL_ASK_HEAD, head) == 0 ||
           strcmp(content, HUSH_AGENT_LOOP_ASK_LINE) == 0 ||
           hush_agent_is_team_ask(content);
}

static int hush_agent_is_team_ask(const char *content)
{
    const size_t ask = strlen(HUSH_AGENT_TEAM_ASK);
    size_t len = 0;

    assert(content != NULL);
    len = strnlen(content, (size_t)HUSH_EVENT_MAX_CONTENT);
    while (len > 0 && isspace((unsigned char)content[len - 1]))
        len--;
    return len >= ask && strncmp(content + len - ask, HUSH_AGENT_TEAM_ASK, ask) == 0;
}
