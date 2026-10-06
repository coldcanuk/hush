/* agent_approve.c: the owner approves each robot turn (#279).
 * Under "Approve every action", hush_agent_begin_work hands every turn here
 * instead of starting the robot's runtime. The turn waits in memory, the
 * chaperon asks in the thread, and only the hive owner's typed Yes or No
 * answers it. Yes runs the turn through hush_agent_begin_work; No posts one
 * line and starts nothing. A relay restart forgets every waiting turn. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "hush_agent.h"
#include "hush_agent_internal.h"

static hush_agent_held_t g_held[HUSH_AGENT_APPROVAL_MAX];
static unsigned long g_held_seq;
/* The turn being answered, copied out so its slot is free while it runs. */
static hush_agent_held_t g_taken;

static hush_agent_held_t *hush_agent_held_free(void);
static hush_agent_held_t *hush_agent_held_oldest(const char *root);
static void hush_agent_held_fill(hush_agent_held_t *held, const hush_agent_job_in_t *in);
static void hush_agent_held_point(hush_agent_held_t *held, hush_store_t *store,
                                  const hush_launch_t *launch, const hush_agent_robot_t *bot);
static void hush_agent_approval_say(hush_store_t *store, const hush_launch_t *launch,
                                    const hush_agent_held_t *held, const char *fmt);
static void hush_agent_approval_run(hush_store_t *store, const hush_launch_t *launch);
static void hush_agent_approval_decline(hush_store_t *store, const hush_launch_t *launch);

void hush_agent_approval_init(void)
{
    memset(g_held, 0, sizeof(g_held));
    memset(&g_taken, 0, sizeof(g_taken));
    g_held_seq = 0;
}

int hush_agent_approval_needed(const hush_agent_job_in_t *in)
{
    assert(in != NULL);
    if (in->launch == NULL || in->approved)
        return 0;
    return in->launch->roster.profile.approval == HUSH_ROSTER_APPROVAL_EVERY;
}

int hush_agent_approval_hold(const hush_agent_job_in_t *in)
{
    hush_agent_held_t *held = NULL;

    assert(in != NULL && in->store != NULL && in->bot != NULL && in->parent != NULL);
    held = hush_agent_held_free();
    if (held == NULL) {
        hush_agent_chaperon_say(in->store, in->launch, in->parent,
                                HUSH_AGENT_APPROVAL_FULL_LINE);
        return HUSH_AGENT_WORK_NONE;
    }
    hush_agent_held_fill(held, in);
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

static void hush_agent_approval_run(hush_store_t *store, const hush_launch_t *launch)
{
    hush_agent_robot_t bot = {0};

    assert(store != NULL && launch != NULL);
    /* A robot removed while its turn waited has nothing left to run. */
    if (!hush_agent_lookup_robot(&bot, launch, g_taken.hex)) {
        hush_agent_follow_release(&g_taken.parent, g_taken.in.follow, 0);
        return;
    }
    hush_agent_held_point(&g_taken, store, launch, &bot);
    (void)hush_agent_begin_approved(&g_taken.in);
}

static void hush_agent_approval_decline(hush_store_t *store, const hush_launch_t *launch)
{
    assert(store != NULL);
    hush_agent_approval_say(store, launch, &g_taken, HUSH_AGENT_APPROVAL_NO_FMT);
    hush_agent_follow_release(&g_taken.parent, g_taken.in.follow, 1);
}
