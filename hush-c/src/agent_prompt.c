/* agent_prompt.c: prompt and directive builders for the agent core. Owns
 * the worker/leader/election prompts, job note assembly, instruction and
 * guidance appends, and the peer/last-rule prompt rules. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "hush_agent.h"
#include "hush_agent_internal.h"

#define HUSH_AGENT_PROMPT_FALLBACK \
    "You are a robot in the Hush hive. Fulfill the last human ask (only your part) in one note."
#define HUSH_AGENT_ONE_JOKE \
    "If the last human ask is a joke, reply with exactly one joke."
#define HUSH_AGENT_PEER_STANDARD \
    " Inter-robot standard: do not copy the human's mention list and " \
    "do not repeat the original ask. Write only your assignment. " \
    "A non-last robot may add \"your turn, @Name\" after the work. " \
    "Never write npub keys or nostr: tokens. Do not mention yourself."
#define HUSH_AGENT_LAST_RULE \
    "You are last. Stop after your assignment. Do not hand off. "
#define HUSH_AGENT_PEER_LINE " Peers: "
#define HUSH_AGENT_HYGIENE \
    " Fulfill YOUR assignment in this note, not a peer's. " \
    "STOP immediately after your part is done. Do not answer questions or perform actions assigned to a peer. " \
    "If peers are mentioned, they take their turn automatically after you stop. " \
    "Do not mention yourself. Do not hand off by appending a bare mention. " \
    "Never end your note with a bare mention. Include any asked code. " \
    "No preamble-only replies. " \
    HUSH_AGENT_ONE_JOKE HUSH_AGENT_PEER_STANDARD
#define HUSH_AGENT_STRICT_SCOPE \
    " Do ONLY the assignment given to you. Do not perform, answer, or " \
    "complete any part assigned to another robot. Ignore other robots' jobs."
#define HUSH_AGENT_COOPERATE \
    " You are a pair. Divide the labor between you two: each does a distinct, " \
    "non-overlapping part of the ask. Do not duplicate your partner's part. " \
    "Do only your own part and stop."
#define HUSH_AGENT_LEADER_PROMPT \
    " You are the leader. Organize the other robots: assign each other robot " \
    "exactly one sub-task, then choose how to run them. Reply with ONE fenced " \
    "block and nothing else:\n" \
    "```plan\n" \
    "order: fifo\n" \
    "1 Happy: generate a riddle\n" \
    "2 Major: answer it\n" \
    "2 Scout: verify it\n" \
    "3 Builder: write a summary\n" \
    "```\n" \
    "order is fifo or lifo. Prefix each task with an integer wave number. " \
    "Tasks sharing a wave number run in parallel; waves run in order. A task " \
    "with no other task in its wave runs alone. Use parallel only when tasks " \
    "are truly independent. Name each robot by its exact display name."
#define HUSH_AGENT_RULES \
    "Fulfill YOUR assignment as the named robot. Do not mention yourself. " \
    "Include asked code. Address the human by first name. No tools. " \
    HUSH_AGENT_ONE_JOKE HUSH_AGENT_PEER_STANDARD

/* Initializes required job metadata from the initiating event. */
static void hush_agent_init_job(hush_agent_job_t *job, const hush_agent_job_in_t *in);
/* Binds required job identity to the selected robot and original human creator. */
static void hush_agent_bind_job(hush_agent_job_t *job, const hush_agent_job_in_t *in);
/* Collects required job's mentioned peers in tag order, excluding self/humans. */
static void hush_agent_collect_job_peers(hush_agent_job_t *job, const hush_agent_job_in_t *in);
/* Appends one eligible peer to required bounded job storage. */
static void hush_agent_add_job_peer(hush_agent_job_t *job, const hush_agent_job_in_t *in,
                                    const char *key);
/* Fills the required planner directive using bounded peer names. */
static hush_status_t hush_agent_fill_leader(hush_agent_job_t *job);
/* Fills the required worker's scoped instructions using existing bounded renderers. */
static void hush_agent_fill_worker(hush_agent_job_t *job, const hush_agent_job_in_t *in);
/* Selects required job's election, planning, or work directive. */
static hush_status_t hush_agent_fill_directive(hush_agent_job_t *job,
                                               const hush_agent_job_in_t *in);
/* Builds required job's current request with recent conversation/file context. */
static void hush_agent_fill_job_note(hush_agent_job_t *job, const hush_agent_job_in_t *in);
static void hush_agent_fill_prompt(char *out, size_t outsz,
                                   const hush_agent_robot_t *bot,
                                   const char *human);
static void hush_agent_fill_rules(char *out, size_t outsz, const char *human);
/* Appends a named instruction to required job storage; FULL on truncation. */
static hush_status_t hush_agent_add_instruction(hush_agent_job_t *job, const char *label,
                                                const char *text);
/* Appends " Peers: @Name …" onto job->prompt. No-op when none. */
static void hush_agent_append_peers(hush_agent_job_t *job);
/* Prepends LAST_RULE to prompt (and appends to rules) when job->last. */
static void hush_agent_append_last(hush_agent_job_t *job);

static void hush_agent_fill_prompt(char *out, size_t outsz,
                                  const hush_agent_robot_t *bot,
                                  const char *human)
{
    const char *base;
    const char *who;
    const char *self;
    int n;

    assert(out != NULL);
    assert(outsz > 0);
    assert(bot != NULL);
    base = (bot->prompt != NULL && bot->prompt[0] != '\0')
        ? bot->prompt : HUSH_AGENT_PROMPT_FALLBACK;
    who = (human != NULL && human[0] != '\0') ? human : HUSH_AGENT_HUMAN_FALLBACK;
    self = (bot->name != NULL && bot->name[0] != '\0') ? bot->name : NULL;
    if (self != NULL)
        n = snprintf(out, outsz, "%s You are %s. You are speaking to %s.%s",
                     base, self, who, HUSH_AGENT_HYGIENE);
    else
        n = snprintf(out, outsz, "%s You are speaking to %s.%s",
                     base, who, HUSH_AGENT_HYGIENE);
    if (n < 0 || (size_t)n >= outsz)
        hush_agent_copy(out, outsz, HUSH_AGENT_PROMPT_FALLBACK);
}

static void hush_agent_fill_rules(char *out, size_t outsz, const char *human)
{
    const char *who;
    int n;

    assert(out != NULL);
    assert(outsz > 0);
    who = (human != NULL && human[0] != '\0') ? human : HUSH_AGENT_HUMAN_FALLBACK;
    n = snprintf(out, outsz, "%s Speak to %s.", HUSH_AGENT_RULES, who);
    if (n < 0 || (size_t)n >= outsz)
        hush_agent_copy(out, outsz, HUSH_AGENT_RULES);
}

hush_status_t hush_agent_fill_job(hush_agent_job_t *job,
                                         const hush_agent_job_in_t *in)
{
    assert(job != NULL && in != NULL);
    assert(in->bot != NULL && in->parent != NULL);
    hush_agent_init_job(job, in);
    hush_agent_bind_job(job, in);
    hush_agent_collect_job_peers(job, in);
    HUSH_TRY(hush_agent_fill_directive(job, in));
    hush_agent_fill_rules(job->rules, sizeof(job->rules), job->human_name);
    hush_agent_append_last(job);
    hush_agent_prepare_cwd(job->cwd, sizeof(job->cwd));
    hush_agent_fill_job_note(job, in);
    return HUSH_OK;
}

static void hush_agent_init_job(hush_agent_job_t *job, const hush_agent_job_in_t *in)
{
    assert(job != NULL && in != NULL);
    const hush_event_t *parent = in->parent;
    memset(job, 0, sizeof(*job));
    job->fd = HUSH_AGENT_FD_NONE;
    job->busy = 1;
    job->kind = HUSH_AGENT_KIND_NOTE_JOB;
    job->started = time(NULL);
    job->launch = in->launch;
    job->last = in->last;
    hush_agent_event_root(job->parent_id, sizeof(job->parent_id), parent);
    hush_agent_copy(job->trigger_id, sizeof(job->trigger_id),
                    parent->id[0] ? parent->id : job->parent_id);
    hush_agent_event_channel(job->channel, sizeof(job->channel), parent);
    hush_agent_copy(job->ask, sizeof(job->ask),
                    in->ask != NULL && in->ask[0] ? in->ask : parent->content);
    hush_agent_make_token(job->token, sizeof(job->token));
}

static void hush_agent_bind_job(hush_agent_job_t *job, const hush_agent_job_in_t *in)
{
    assert(job != NULL && in != NULL);
    const hush_agent_robot_t *robot = in->bot;
    hush_event_t original = {0};
    const char *human = in->parent->pubkey;
    if (hush_store_find(in->store, &original, job->parent_id) == HUSH_OK)
        human = original.pubkey;
    hush_agent_copy(job->human_pub, sizeof(job->human_pub), human);
    hush_agent_copy(job->robot_pub, sizeof(job->robot_pub), robot->hex);
    hush_agent_copy(job->robot_name, sizeof(job->robot_name), robot->name);
    hush_agent_copy(job->robot_role, sizeof(job->robot_role), robot->role);
    hush_agent_copy(job->provider, sizeof(job->provider), hush_agent_pick_provider(robot));
    hush_agent_human_name(job->human_name, sizeof(job->human_name), in->launch);
}

static void hush_agent_collect_job_peers(hush_agent_job_t *job, const hush_agent_job_in_t *in)
{
    assert(job != NULL && in != NULL);
    const hush_event_t *parent = in->parent;
    for (size_t i = 0; i < parent->tag_count && i < (size_t)HUSH_EVENT_MAX_TAGS; ++i) {
        if (strcmp(parent->tags[i][0], "p") == 0)
            hush_agent_add_job_peer(job, in, parent->tags[i][1]);
    }
}

static void hush_agent_add_job_peer(hush_agent_job_t *job, const hush_agent_job_in_t *in,
                                    const char *key)
{
    assert(job != NULL && in != NULL && key != NULL);
    if ((size_t)job->n_co_robots >= sizeof(job->co_names) / sizeof(job->co_names[0]))
        return;
    if (hush_agent_key_matches(key, in->bot->npub, in->bot->hex) ||
        hush_agent_is_human(in->launch, key))
        return;
    hush_agent_robot_t peer = {0};
    if (!hush_agent_lookup_robot(&peer, in->launch, key))
        return;
    hush_agent_copy(job->co_npubs[job->n_co_robots], sizeof(job->co_npubs[0]), key);
    hush_agent_copy(job->co_names[job->n_co_robots], sizeof(job->co_names[0]), peer.name);
    ++job->n_co_robots;
}

static hush_status_t hush_agent_fill_leader(hush_agent_job_t *job)
{
    assert(job != NULL);
    job->kind = HUSH_AGENT_KIND_PLAN;
    hush_agent_copy(job->prompt, sizeof(job->prompt), HUSH_AGENT_LEADER_PROMPT);
    for (size_t i = 0; i < (size_t)job->n_co_robots && i < (size_t)HUSH_AGENT_FOLLOW_ROBOTS; ++i)
        HUSH_TRY(hush_agent_add_instruction(job, "Other robot", job->co_names[i]));
    return hush_agent_add_instruction(job, "The ask", job->ask);
}

static void hush_agent_fill_worker(hush_agent_job_t *job, const hush_agent_job_in_t *in)
{
    assert(job != NULL && in != NULL);
    hush_agent_fill_prompt(job->prompt, sizeof(job->prompt), in->bot, job->human_name);
    hush_agent_append_assign(job->prompt, sizeof(job->prompt), job->ask,
                             in->launch, in->bot->hex);
    hush_agent_name_dangling_peer(job, in->scoped);
    if (in->scoped && strlen(job->prompt) + strlen(HUSH_AGENT_STRICT_SCOPE) < sizeof(job->prompt))
        strcat(job->prompt, HUSH_AGENT_STRICT_SCOPE);
    if (!in->scoped && in->mode == HUSH_AGENT_MODE_BROADCAST && job->n_co_robots == 1 &&
        strlen(job->prompt) + strlen(HUSH_AGENT_COOPERATE) < sizeof(job->prompt))
        strcat(job->prompt, HUSH_AGENT_COOPERATE);
    hush_agent_append_peers(job);
}

static hush_status_t hush_agent_fill_directive(hush_agent_job_t *job,
                                               const hush_agent_job_in_t *in)
{
    assert(job != NULL && in != NULL);
    if (in->elect) {
        job->kind = HUSH_AGENT_KIND_ELECT;
        hush_agent_copy(job->prompt, sizeof(job->prompt),
                        in->prompt_override != NULL ? in->prompt_override : HUSH_AGENT_ELECT_PROMPT);
        return HUSH_OK;
    }
    if (in->leader)
        return hush_agent_fill_leader(job);
    hush_agent_fill_worker(job, in);
    return HUSH_OK;
}

static void hush_agent_fill_job_note(hush_agent_job_t *job, const hush_agent_job_in_t *in)
{
    assert(job != NULL && in != NULL);
    hush_agent_thread_walk_t names = {.human = job->human_name, .robot = job->robot_name};
    hush_agent_fill_thread(job->note, sizeof(job->note), in->store, in->launch, in->parent, &names);
    if (job->note[0] == '\0')
        hush_agent_copy(job->note, sizeof(job->note), in->parent->content);
    hush_agent_humanize_ask(job->note, sizeof(job->note), in->launch, in->bot->hex);
    hush_agent_append_context(job->note, sizeof(job->note), in->bot);
}

static hush_status_t hush_agent_add_instruction(hush_agent_job_t *job, const char *label,
                                                const char *text)
{
    assert(job != NULL);
    assert(label != NULL && text != NULL);
    size_t used = strlen(job->prompt);
    int written = snprintf(job->prompt + used, sizeof(job->prompt) - used,
                           "\n%s: %s\n", label, text);
    if (written < 0 || (size_t)written >= sizeof(job->prompt) - used)
        return HUSH_ERR_FULL;
    return HUSH_OK;
}

hush_status_t hush_agent_add_guidance(hush_agent_job_t *job,
                                             const hush_agent_robot_t *robot)
{
    assert(job != NULL);
    assert(robot != NULL && robot->nskills <= (size_t)HUSH_SKILL_EQUIP_MAX);
    if (job->kind != HUSH_AGENT_KIND_NOTE_JOB && robot->prompt != NULL)
        HUSH_TRY(hush_agent_add_instruction(job, "Robot system prompt", robot->prompt));
    const hush_launch_channel_t *channel = hush_agent_channel(job->launch, job->channel);
    if (channel != NULL) {
        HUSH_TRY(hush_agent_add_instruction(job, "Room system prompt", channel->system_prompt));
        if (channel->about[0] != '\0')
            HUSH_TRY(hush_agent_add_instruction(job, "Channel topic", channel->about));
    }
    for (size_t i = 0; i < robot->nskills; ++i) {
        char instructions[HUSH_SKILL_BODY_MAX] = {0};
        HUSH_TRY(hush_skill_read_instructions(instructions, sizeof(instructions), robot->skills[i]));
        HUSH_TRY(hush_agent_add_instruction(job, robot->skills[i], instructions));
    }
    return HUSH_OK;
}

static void hush_agent_append_peers(hush_agent_job_t *job)
{
    size_t off;
    size_t glen;
    int c;

    assert(job != NULL);
    if (job->n_co_robots <= 0)
        return;
    off = strlen(job->prompt);
    glen = strlen(HUSH_AGENT_PEER_LINE);
    if (off + glen >= sizeof(job->prompt))
        return;
    memcpy(job->prompt + off, HUSH_AGENT_PEER_LINE, glen);
    off += glen;
    for (c = 0; c < job->n_co_robots; c++) {
        const char *nm = job->co_names[c][0] ? job->co_names[c] : "robot";
        int nwritten;

        nwritten = snprintf(job->prompt + off, sizeof(job->prompt) - off,
                            "%s@%s", c > 0 ? " " : "", nm);
        if (nwritten < 0 || (size_t)nwritten >= sizeof(job->prompt) - off)
            break;
        off += (size_t)nwritten;
    }
    if (off < sizeof(job->prompt))
        job->prompt[off] = '\0';
}

static void hush_agent_append_last(hush_agent_job_t *job)
{
    assert(job != NULL);
    if (!job->last)
        return;
    size_t rule_len = strlen(HUSH_AGENT_LAST_RULE);
    size_t prompt_len = strlen(job->prompt);
    if (rule_len + prompt_len >= sizeof(job->prompt))
        return;
    memmove(job->prompt + rule_len, job->prompt, prompt_len + 1);
    memcpy(job->prompt, HUSH_AGENT_LAST_RULE, rule_len);
    size_t rules_len = strlen(job->rules);
    if (rules_len + rule_len < sizeof(job->rules))
        memcpy(job->rules + rules_len, HUSH_AGENT_LAST_RULE, rule_len + 1);
}
