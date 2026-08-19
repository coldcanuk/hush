/* hush_agent.c: owns mention replies for raised robots. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "hush_agent.h"
#include "hush_provider.h"
#include "hush_relay.h"
#include "hush_roster.h"

enum {
    HUSH_AGENT_JOBS_MAX = 4,
    HUSH_AGENT_TIMEOUT_S = 90,
    HUSH_AGENT_KIND_NOTE = 1,
    HUSH_AGENT_ID_WIDTH = 16,
    HUSH_AGENT_ARGV_MAX = 12,
    HUSH_AGENT_FD_NONE = -1
};

#define HUSH_AGENT_GROK_BIN "grok"
#define HUSH_AGENT_DEVNULL "/dev/null"
#define HUSH_AGENT_CHAN_FALLBACK "general"
#define HUSH_AGENT_PROMPT_FALLBACK \
    "You are a robot in the Hush hive. Reply in one short note."

typedef struct {
    int busy;
    pid_t pid;
    int fd;
    time_t started;
    char parent_id[HUSH_EVENT_ID_HEX_LEN + 1];
    char channel[HUSH_EVENT_MAX_TAG_LEN + 1];
    char human_pub[HUSH_EVENT_PUBKEY_HEX_LEN + 1];
    char robot_pub[HUSH_EVENT_PUBKEY_HEX_LEN + 1];
    char robot_name[HUSH_ROSTER_NAME_MAX];
    char prompt[HUSH_ROSTER_PROMPT_MAX];
    char note[HUSH_EVENT_MAX_CONTENT + 1];
    char out[HUSH_EVENT_MAX_CONTENT + 1];
    size_t out_n;
} hush_agent_job_t;

typedef struct {
    const char *name;
    const char *npub;
    const char *hex;
    const char *provider;
    const char *prompt;
} hush_agent_robot_t;

typedef struct {
    const char *pubkey;
    const char *content;
    const char *channel;
    const char *parent_id;
    const char *human_pub;
} hush_agent_note_in_t;

static hush_agent_job_t g_jobs[HUSH_AGENT_JOBS_MAX];
static unsigned g_id_seq;

static void hush_agent_copy(char *dst, size_t dstsz, const char *src);
static void hush_agent_trim(char *text);
static void hush_agent_make_id(char *out65);
static hush_agent_job_t *hush_agent_find_slot(void);
static int hush_agent_key_matches(const char *mention, const char *npub,
                                  const char *hex);
static int hush_agent_is_human(const hush_launch_t *launch,
                               const char *mention);
static int hush_agent_lookup_robot(hush_agent_robot_t *out,
                                   const hush_launch_t *launch,
                                   const char *mention);
static void hush_agent_event_channel(char *out, size_t outsz,
                                     const hush_event_t *ev);
static void hush_agent_fill_note(hush_event_t *ev, const hush_agent_note_in_t *in);
static hush_status_t hush_agent_insert_note(hush_store_t *store,
                                            const hush_agent_note_in_t *in);
static void hush_agent_on_deck(hush_store_t *store, const hush_agent_robot_t *bot,
                               const hush_event_t *parent, const char *why);
static int hush_agent_grok_ready(void);
static hush_status_t hush_agent_start_grok(const hush_agent_robot_t *bot,
                                           const hush_event_t *parent);
static void hush_agent_fill_job(hush_agent_job_t *job,
                                const hush_agent_robot_t *bot,
                                const hush_event_t *parent);
static void hush_agent_exec_grok(int write_fd, const hush_agent_job_t *job);
static void hush_agent_close_job(hush_agent_job_t *job);
static void hush_agent_kill_job(hush_agent_job_t *job);
static void hush_agent_finish_job(hush_store_t *store, hush_agent_job_t *job,
                                  int ok);
static void hush_agent_read_job(hush_agent_job_t *job);
static void hush_agent_handle_mention(hush_store_t *store,
                                      const hush_launch_t *launch,
                                      const hush_event_t *ev,
                                      const char *mention);
static int hush_agent_job_timed_out(const hush_agent_job_t *job, time_t now);

void hush_agent_init(void)
{
    size_t i;

    for (i = 0; i < (size_t)HUSH_AGENT_JOBS_MAX; i++) {
        memset(&g_jobs[i], 0, sizeof(g_jobs[i]));
        g_jobs[i].fd = HUSH_AGENT_FD_NONE;
    }
}

void hush_agent_shutdown(void)
{
    size_t i;

    for (i = 0; i < (size_t)HUSH_AGENT_JOBS_MAX; i++) {
        if (!g_jobs[i].busy)
            continue;
        hush_agent_kill_job(&g_jobs[i]);
        hush_agent_close_job(&g_jobs[i]);
    }
}

void hush_agent_consider(hush_store_t *store, hush_launch_t *launch,
                         const hush_event_t *ev)
{
    size_t i;

    if (store == NULL || launch == NULL || ev == NULL)
        return;
    if (ev->kind != (uint32_t)HUSH_AGENT_KIND_NOTE)
        return;
    for (i = 0; i < ev->tag_count && i < (size_t)HUSH_EVENT_MAX_TAGS; i++) {
        if (strcmp(ev->tags[i][0], "p") != 0)
            continue;
        hush_agent_handle_mention(store, launch, ev, ev->tags[i][1]);
    }
}

void hush_agent_poll(hush_store_t *store)
{
    size_t i;
    time_t now;
    int status;

    now = time(NULL);
    for (i = 0; i < (size_t)HUSH_AGENT_JOBS_MAX; i++) {
        if (!g_jobs[i].busy)
            continue;
        hush_agent_read_job(&g_jobs[i]);
        if (g_jobs[i].pid > 0)
            (void)waitpid(g_jobs[i].pid, &status, WNOHANG);
        if (hush_agent_job_timed_out(&g_jobs[i], now)) {
            hush_agent_kill_job(&g_jobs[i]);
            hush_agent_finish_job(store, &g_jobs[i], 0);
            continue;
        }
        if (g_jobs[i].fd == HUSH_AGENT_FD_NONE)
            hush_agent_finish_job(store, &g_jobs[i], 1);
    }
}

static void hush_agent_copy(char *dst, size_t dstsz, const char *src)
{
    size_t n;

    assert(dst != NULL);
    assert(dstsz > 0);
    if (src == NULL)
        src = "";
    n = strlen(src);
    if (n >= dstsz)
        n = dstsz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void hush_agent_trim(char *text)
{
    size_t n;

    assert(text != NULL);
    n = strlen(text);
    while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r' ||
                     text[n - 1] == ' ' || text[n - 1] == '\t')) {
        text[n - 1] = '\0';
        n--;
    }
}

static void hush_agent_make_id(char *out65)
{
    time_t now;

    assert(out65 != NULL);
    now = time(NULL);
    g_id_seq++;
    (void)snprintf(out65, HUSH_EVENT_ID_HEX_LEN + 1,
                   "%0*llx%0*x%0*x%0*x",
                   HUSH_AGENT_ID_WIDTH, (unsigned long long)now,
                   HUSH_AGENT_ID_WIDTH, g_id_seq,
                   HUSH_AGENT_ID_WIDTH, g_id_seq ^ 0x51ed270bu,
                   HUSH_AGENT_ID_WIDTH, g_id_seq * 7u);
}

static hush_agent_job_t *hush_agent_find_slot(void)
{
    size_t i;

    for (i = 0; i < (size_t)HUSH_AGENT_JOBS_MAX; i++) {
        if (!g_jobs[i].busy)
            return &g_jobs[i];
    }
    return NULL;
}

static int hush_agent_key_matches(const char *mention, const char *npub,
                                  const char *hex)
{
    if (mention == NULL || mention[0] == '\0')
        return 0;
    if (npub != NULL && npub[0] != '\0' && strcmp(mention, npub) == 0)
        return 1;
    if (hex != NULL && hex[0] != '\0' && strcmp(mention, hex) == 0)
        return 1;
    return 0;
}

static int hush_agent_is_human(const hush_launch_t *launch,
                               const char *mention)
{
    assert(launch != NULL);
    if (!launch->logged_in)
        return 0;
    return hush_agent_key_matches(mention, launch->human.npub,
                                  launch->human.pubkey_hex);
}

static int hush_agent_lookup_robot(hush_agent_robot_t *out,
                                   const hush_launch_t *launch,
                                   const char *mention)
{
    size_t i;
    const hush_roster_agent_t *agent;

    assert(out != NULL);
    assert(launch != NULL);
    memset(out, 0, sizeof(*out));
    if (launch->has_vibe &&
        hush_agent_key_matches(mention, launch->payne.npub,
                               launch->payne.pubkey_hex)) {
        out->name = HUSH_LAUNCH_PAYNE_NAME;
        out->npub = launch->payne.npub;
        out->hex = launch->payne.pubkey_hex;
        out->provider = HUSH_ROSTER_PROVIDER_GOOSE;
        out->prompt = HUSH_LAUNCH_PAYNE_ABOUT;
        return 1;
    }
    for (i = 0; i < launch->roster.nagents; i++) {
        agent = &launch->roster.agents[i];
        if (!hush_agent_key_matches(mention, agent->id.npub,
                                    agent->id.pubkey_hex))
            continue;
        out->name = agent->name;
        out->npub = agent->id.npub;
        out->hex = agent->id.pubkey_hex;
        out->provider = agent->provider;
        out->prompt = agent->prompt;
        return 1;
    }
    return 0;
}

static void hush_agent_event_channel(char *out, size_t outsz,
                                     const hush_event_t *ev)
{
    size_t i;

    assert(out != NULL);
    assert(ev != NULL);
    hush_agent_copy(out, outsz, HUSH_AGENT_CHAN_FALLBACK);
    for (i = 0; i < ev->tag_count; i++) {
        if (strcmp(ev->tags[i][0], "h") == 0 && ev->tags[i][1][0] != '\0') {
            hush_agent_copy(out, outsz, ev->tags[i][1]);
            return;
        }
    }
}

static void hush_agent_fill_note(hush_event_t *ev, const hush_agent_note_in_t *in)
{
    assert(ev != NULL);
    assert(in != NULL);
    assert(in->pubkey != NULL);
    assert(in->content != NULL);
    assert(in->channel != NULL);
    memset(ev, 0, sizeof(*ev));
    hush_agent_make_id(ev->id);
    hush_agent_copy(ev->pubkey, sizeof(ev->pubkey), in->pubkey);
    ev->kind = (uint32_t)HUSH_AGENT_KIND_NOTE;
    ev->created_at = (int64_t)time(NULL);
    hush_agent_copy(ev->content, sizeof(ev->content), in->content);
    ev->tag_count = 1;
    memcpy(ev->tags[0][0], "h", 2);
    hush_agent_copy(ev->tags[0][1], sizeof(ev->tags[0][1]), in->channel);
    if (in->parent_id != NULL && in->parent_id[0] != '\0' &&
        ev->tag_count < (size_t)HUSH_EVENT_MAX_TAGS) {
        memcpy(ev->tags[ev->tag_count][0], "e", 2);
        hush_agent_copy(ev->tags[ev->tag_count][1],
                        sizeof(ev->tags[ev->tag_count][1]), in->parent_id);
        ev->tag_count++;
    }
    if (in->human_pub != NULL && in->human_pub[0] != '\0' &&
        ev->tag_count < (size_t)HUSH_EVENT_MAX_TAGS) {
        memcpy(ev->tags[ev->tag_count][0], "p", 2);
        hush_agent_copy(ev->tags[ev->tag_count][1],
                        sizeof(ev->tags[ev->tag_count][1]), in->human_pub);
        ev->tag_count++;
    }
}

static hush_status_t hush_agent_insert_note(hush_store_t *store,
                                            const hush_agent_note_in_t *in)
{
    hush_event_t ev;

    assert(store != NULL);
    hush_agent_fill_note(&ev, in);
    return hush_store_insert(store, &ev);
}

static void hush_agent_on_deck(hush_store_t *store, const hush_agent_robot_t *bot,
                               const hush_event_t *parent, const char *why)
{
    char content[HUSH_EVENT_MAX_CONTENT];
    char channel[HUSH_EVENT_MAX_TAG_LEN + 1];
    const char *name;
    const char *line;

    assert(store != NULL);
    assert(bot != NULL);
    assert(parent != NULL);
    name = (bot->name != NULL && bot->name[0] != '\0') ? bot->name : "robot";
    line = (why != NULL && why[0] != '\0') ? why
        : "I am on deck. Standing orders are noted.";
    if (snprintf(content, sizeof(content),
                 "At ease. %s — %s", line, name) >= (int)sizeof(content))
        hush_agent_copy(content, sizeof(content), line);
    hush_agent_event_channel(channel, sizeof(channel), parent);
    {
        hush_agent_note_in_t in = {
            .pubkey = bot->hex != NULL ? bot->hex : "",
            .content = content,
            .channel = channel,
            .parent_id = parent->id,
            .human_pub = parent->pubkey
        };

        (void)hush_agent_insert_note(store, &in);
    }
}

static int hush_agent_grok_ready(void)
{
    hush_provider_status_t st;

    if (hush_provider_status(&st, HUSH_ROSTER_PROVIDER_GROK_BUILD) != HUSH_OK)
        return 0;
    return st.has_home && st.has_binary;
}

static void hush_agent_fill_job(hush_agent_job_t *job,
                                const hush_agent_robot_t *bot,
                                const hush_event_t *parent)
{
    assert(job != NULL);
    assert(bot != NULL);
    assert(parent != NULL);
    memset(job, 0, sizeof(*job));
    job->fd = HUSH_AGENT_FD_NONE;
    job->busy = 1;
    job->started = time(NULL);
    hush_agent_copy(job->parent_id, sizeof(job->parent_id), parent->id);
    hush_agent_event_channel(job->channel, sizeof(job->channel), parent);
    hush_agent_copy(job->human_pub, sizeof(job->human_pub), parent->pubkey);
    hush_agent_copy(job->robot_pub, sizeof(job->robot_pub),
                    bot->hex != NULL ? bot->hex : "");
    hush_agent_copy(job->robot_name, sizeof(job->robot_name),
                    bot->name != NULL ? bot->name : "robot");
    if (bot->prompt != NULL && bot->prompt[0] != '\0')
        hush_agent_copy(job->prompt, sizeof(job->prompt), bot->prompt);
    else
        hush_agent_copy(job->prompt, sizeof(job->prompt),
                        HUSH_AGENT_PROMPT_FALLBACK);
    hush_agent_copy(job->note, sizeof(job->note), parent->content);
}

static void hush_agent_exec_grok(int write_fd, const hush_agent_job_t *job)
{
    char *argv[HUSH_AGENT_ARGV_MAX];
    int dn;

    assert(job != NULL);
    if (write_fd >= 0)
        (void)dup2(write_fd, STDOUT_FILENO);
    if (write_fd >= 0)
        close(write_fd);
    dn = open(HUSH_AGENT_DEVNULL, O_WRONLY);
    if (dn >= 0) {
        (void)dup2(dn, STDERR_FILENO);
        close(dn);
    }
    argv[0] = (char *)HUSH_AGENT_GROK_BIN;
    argv[1] = (char *)"-p";
    argv[2] = (char *)job->note;
    argv[3] = (char *)"--system-prompt-override";
    argv[4] = (char *)job->prompt;
    argv[5] = (char *)"--output-format";
    argv[6] = (char *)"plain";
    argv[7] = (char *)"--always-approve";
    argv[8] = (char *)"--no-plan";
    argv[9] = NULL;
    execvp(HUSH_AGENT_GROK_BIN, argv);
    _exit(127);
}

static hush_status_t hush_agent_start_grok(const hush_agent_robot_t *bot,
                                           const hush_event_t *parent)
{
    hush_agent_job_t *job;
    int fds[2];
    pid_t pid;
    int flags;

    assert(bot != NULL);
    assert(parent != NULL);
    job = hush_agent_find_slot();
    if (job == NULL)
        return HUSH_ERR_FULL;
    hush_agent_fill_job(job, bot, parent);
    if (pipe(fds) != 0) {
        job->busy = 0;
        return HUSH_ERR_IO;
    }
    pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        job->busy = 0;
        return HUSH_ERR_IO;
    }
    if (pid == 0) {
        close(fds[0]);
        hush_agent_exec_grok(fds[1], job);
    }
    close(fds[1]);
    flags = fcntl(fds[0], F_GETFL, 0);
    if (flags >= 0)
        (void)fcntl(fds[0], F_SETFL, flags | O_NONBLOCK);
    job->pid = pid;
    job->fd = fds[0];
    hush_relay_track_child(pid);
    return HUSH_OK;
}

static void hush_agent_close_job(hush_agent_job_t *job)
{
    assert(job != NULL);
    if (job->fd >= 0)
        close(job->fd);
    job->fd = HUSH_AGENT_FD_NONE;
    job->pid = 0;
    job->busy = 0;
}

static void hush_agent_kill_job(hush_agent_job_t *job)
{
    int status;

    assert(job != NULL);
    if (job->pid > 1) {
        (void)kill(job->pid, SIGTERM);
        (void)waitpid(job->pid, &status, WNOHANG);
    }
}

static void hush_agent_finish_job(hush_store_t *store, hush_agent_job_t *job,
                                  int ok)
{
    hush_agent_robot_t bot;
    hush_event_t parent;

    assert(job != NULL);
    hush_agent_trim(job->out);
    if (store != NULL && ok && job->out[0] != '\0') {
        hush_agent_note_in_t in = {
            .pubkey = job->robot_pub,
            .content = job->out,
            .channel = job->channel,
            .parent_id = job->parent_id,
            .human_pub = job->human_pub
        };

        (void)hush_agent_insert_note(store, &in);
        hush_agent_close_job(job);
        return;
    }
    if (store != NULL) {
        memset(&bot, 0, sizeof(bot));
        memset(&parent, 0, sizeof(parent));
        bot.name = job->robot_name;
        bot.hex = job->robot_pub;
        hush_agent_copy(parent.id, sizeof(parent.id), job->parent_id);
        hush_agent_copy(parent.pubkey, sizeof(parent.pubkey), job->human_pub);
        parent.tag_count = 1;
        memcpy(parent.tags[0][0], "h", 2);
        hush_agent_copy(parent.tags[0][1], sizeof(parent.tags[0][1]),
                        job->channel);
        hush_agent_on_deck(store, &bot, &parent,
                           ok ? "Grok Build returned nothing."
                              : "Grok Build did not answer in time.");
    }
    hush_agent_close_job(job);
}

static void hush_agent_read_job(hush_agent_job_t *job)
{
    char buf[256];
    ssize_t n;
    size_t room;

    assert(job != NULL);
    if (job->fd < 0)
        return;
    for (;;) {
        room = sizeof(job->out) - 1 - job->out_n;
        if (room == 0)
            break;
        n = read(job->fd, buf, sizeof(buf) < room ? sizeof(buf) : room);
        if (n > 0) {
            memcpy(job->out + job->out_n, buf, (size_t)n);
            job->out_n += (size_t)n;
            job->out[job->out_n] = '\0';
            continue;
        }
        if (n == 0) {
            close(job->fd);
            job->fd = HUSH_AGENT_FD_NONE;
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;
        close(job->fd);
        job->fd = HUSH_AGENT_FD_NONE;
        return;
    }
}

static int hush_agent_job_timed_out(const hush_agent_job_t *job, time_t now)
{
    assert(job != NULL);
    if (job->started <= 0)
        return 0;
    return now >= job->started + (time_t)HUSH_AGENT_TIMEOUT_S;
}

static void hush_agent_handle_mention(hush_store_t *store,
                                      const hush_launch_t *launch,
                                      const hush_event_t *ev,
                                      const char *mention)
{
    hush_agent_robot_t bot;

    assert(store != NULL);
    assert(launch != NULL);
    assert(ev != NULL);
    if (mention == NULL || mention[0] == '\0')
        return;
    if (hush_agent_is_human(launch, mention))
        return;
    if (!hush_agent_lookup_robot(&bot, launch, mention))
        return;
    if (strcmp(bot.provider, HUSH_ROSTER_PROVIDER_GROK_BUILD) == 0 &&
        hush_agent_grok_ready()) {
        if (hush_agent_start_grok(&bot, ev) == HUSH_OK)
            return;
    }
    hush_agent_on_deck(store, &bot, ev, "I am on deck. Standing orders are noted.");
}
