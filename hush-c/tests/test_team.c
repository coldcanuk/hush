/* tests/test_team.c: Chief of Staff team fence, hop, and project cwd. */

#define _POSIX_C_SOURCE 200809L

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hush_agent.h"
#include "hush_agent_internal.h"
#include "hush_event.h"
#include "hush_launch.h"
#include "hush_store.h"
#include "hush_thread.h"

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

static void id_hex(char out[HUSH_EVENT_ID_HEX_LEN + 1], unsigned n)
{
    int written = snprintf(out, HUSH_EVENT_ID_HEX_LEN + 1, "%064x", n);

    if (written != HUSH_EVENT_ID_HEX_LEN)
        abort();
}

static void test_parse(void)
{
    hush_agent_team_t team;
    const char *ok =
        "plan\n```team\n"
        "Mail | grok-build | Answer the inbox\n"
        "Scout | codex | Read the brief\n"
        "```\n";
    const char *bad =
        "```team\nMail | not-a-provider | Answer\n```\n";
    const char *plain = "no fence here";

    expect(hush_agent_team_parse(&team, ok) == HUSH_OK, "parse team");
    expect(team.count == 2, "two members");
    expect(strcmp(team.member[0].name, "Mail") == 0, "first name");
    expect(strcmp(team.member[1].provider, "codex") == 0, "second provider");
    expect(hush_agent_team_parse(&team, bad) == HUSH_ERR_PARSE, "bad provider");
    expect(team.count == 0, "bad parse keeps nothing");
    expect(hush_agent_team_parse(&team, plain) == HUSH_ERR_NOT_FOUND, "no fence");
}

static void test_project_clock(void)
{
    hush_agent_job_t job;
    time_t t0 = 1000000;

    memset(&job, 0, sizeof(job));
    job.started = t0;
    expect(hush_agent_job_timed_out(&job, t0 + HUSH_AGENT_TIMEOUT_S - 1) == 0,
           "chat inside 90");
    expect(hush_agent_job_timed_out(&job, t0 + HUSH_AGENT_TIMEOUT_S) == 1,
           "chat ends at 90");
    job.project_tools = 1;
    expect(hush_agent_job_timed_out(&job, t0 + HUSH_AGENT_TIMEOUT_S) == 0,
           "project still inside at 90");
    expect(hush_agent_job_timed_out(&job,
               t0 + (time_t)HUSH_AGENT_PROJECT_TIMEOUT_S - 1) == 0,
           "project still inside at 299");
    expect(hush_agent_job_timed_out(&job,
               t0 + (time_t)HUSH_AGENT_PROJECT_TIMEOUT_S) == 1,
           "project ends at 300");
    expect(strcmp(hush_agent_grok_turn_budget(0), "2") == 0, "chat turns");
    expect(strcmp(hush_agent_grok_turn_budget(1), "8") == 0, "project turns");
    expect(hush_agent_budget_seconds(0) == HUSH_AGENT_TIMEOUT_S, "chat clock");
    expect(hush_agent_budget_seconds(1) == HUSH_AGENT_PROJECT_TIMEOUT_S,
           "project clock");
    job.project_tools = 0;
    job.pid = 1;
    expect(hush_agent_child_is_working(&job, t0 + HUSH_PRESENCE_STALL_S) == 1,
           "live child ahead of the deadline is working");
    expect((hush_agent_child_is_working(&job, t0 + HUSH_PRESENCE_STALL_S) == 0)
               == 0,
           "stall stays off while the pid is live and the deadline is ahead");
    expect(hush_agent_child_is_working(&job, t0 + HUSH_AGENT_TIMEOUT_S) == 0,
           "timed out child is not working");
    job.pid = 0;
    expect(hush_agent_child_is_working(&job, t0 + HUSH_PRESENCE_STALL_S) == 0,
           "reaped child is not working");
}

static int argv_has(char **argv, const char *word)
{
    size_t i;

    for (i = 0; argv[i] != NULL; i++) {
        if (strcmp(argv[i], word) == 0)
            return 1;
    }
    return 0;
}

static void test_worker_cwd(void)
{
    hush_agent_job_t job;
    char *argv[HUSH_AGENT_ARGV_MAX];
    char saved[PATH_MAX];
    char now[PATH_MAX];
    char dir[] = "/tmp/hush-cwd-XXXXXX";
    int n;

    expect(getcwd(saved, sizeof(saved)) != NULL, "save cwd");
    expect(hush_agent_enter_cwd(NULL) == 0, "null cwd refused");
    expect(hush_agent_enter_cwd("") == 0, "empty cwd refused");
    expect(getcwd(now, sizeof(now)) != NULL && strcmp(saved, now) == 0,
           "refuse leaves cwd");
    expect(mkdtemp(dir) != NULL, "job dir");
    expect(hush_agent_enter_cwd(dir) == 1, "enter job cwd");
    expect(getcwd(now, sizeof(now)) != NULL && strcmp(now, dir) == 0,
           "cwd is the job dir");
    expect(chdir(saved) == 0, "restore cwd");
    expect(hush_agent_enter_cwd("/tmp/hush-cwd-missing-desk") == 0,
           "missing cwd refused");

    memset(&job, 0, sizeof(job));
    snprintf(job.cwd, sizeof(job.cwd), "%s", dir);
    n = hush_agent_fill_copilot_argv(&job, argv, HUSH_AGENT_ARGV_MAX, "note");
    expect(n > 0, "copilot argv built");
    expect(argv_has(argv, "--allow-all-tools") == 1, "copilot tools");
    expect(argv_has(argv, "-C") == 1, "copilot -C");
    expect(argv_has(argv, job.cwd) == 1, "copilot cwd");
    expect(argv_has(argv, "--allow-all") == 0, "copilot not allow-all");
    job.cwd[0] = '\0';
    n = hush_agent_fill_copilot_argv(&job, argv, HUSH_AGENT_ARGV_MAX, "note");
    expect(n > 0, "copilot argv without cwd");
    expect(argv_has(argv, "-C") == 0, "no -C when cwd empty");
    expect(argv_has(argv, "--allow-all-tools") == 1, "tools without cwd");
    expect(argv_has(argv, "--allow-all") == 0, "still not allow-all");
}

static void test_denylist(void)
{
    const char *full = hush_agent_tool_denylist(0);
    const char *bound = hush_agent_tool_denylist(1);

    expect(strstr(full, "read_file") != NULL, "full list blocks reads");
    expect(strstr(full, "run_terminal_cmd") != NULL, "full list blocks shell");
    expect(strstr(bound, "read_file") == NULL, "project list allows reads");
    expect(strstr(bound, "search_replace") == NULL, "project list allows edits");
    expect(strstr(bound, "run_terminal_cmd") != NULL, "project list blocks shell");
    expect(strstr(bound, "web_search") != NULL, "project list blocks web");
    expect(strstr(bound, "task") != NULL, "project list blocks task");
}

static int open_hive(hush_launch_t *launch, hush_store_t **store, const char *home)
{
    if (setenv("HUSH_HOME", home, 1) != 0)
        return 0;
    hush_launch_init(launch);
    if (hush_store_create(store) != HUSH_OK)
        return 0;
    if (hush_launch_create_identity(launch) != HUSH_OK)
        return 0;
    if (hush_launch_ack_backup(launch, 0) != HUSH_OK)
        return 0;
    if (hush_launch_create_vibe(launch, *store, "HQ", "x") != HUSH_OK)
        return 0;
    return 1;
}

static void put_root(hush_store_t *store, const hush_launch_t *launch,
                     const char *content, char *id_out)
{
    hush_agent_note_in_t in;
    hush_event_t ev;

    memset(&in, 0, sizeof(in));
    in.pubkey = launch->human.pubkey_hex;
    in.content = content;
    in.channel = "general";
    hush_agent_fill_note(&ev, &in);
    expect(hush_store_insert(store, &ev) == HUSH_OK, "insert root");
    hush_thread_record(&ev);
    memcpy(id_out, ev.id, HUSH_EVENT_ID_HEX_LEN + 1);
}

static void answer_note(hush_event_t *ev, const char *pubkey, const char *root,
                        const char *word)
{
    memset(ev, 0, sizeof(*ev));
    snprintf(ev->pubkey, sizeof(ev->pubkey), "%s", pubkey);
    snprintf(ev->content, sizeof(ev->content), "%s", word);
    ev->kind = 1;
    ev->tag_count = 2;
    memcpy(ev->tags[0][0], "h", 2);
    memcpy(ev->tags[0][1], "general", 8);
    memcpy(ev->tags[1][0], "e", 2);
    snprintf(ev->tags[1][1], sizeof(ev->tags[1][1]), "%s", root);
    id_hex(ev->id, 42);
}

static void test_yes_no(const char *home)
{
    hush_launch_t launch;
    hush_store_t *store = NULL;
    hush_agent_team_t team;
    hush_event_t ev;
    char root[HUSH_EVENT_ID_HEX_LEN + 1];
    const char *fence =
        "```team\n"
        "Mail | grok-build | Answer the inbox\n"
        "```\n";
    size_t before;

    expect(open_hive(&launch, &store, home), "hive");
    put_root(store, &launch, "open the desk", root);
    expect(hush_agent_team_parse(&team, fence) == HUSH_OK, "yes fence");
    expect(hush_agent_team_check(&launch, &team) == HUSH_OK, "team fits");
    before = launch.roster.nagents;
    expect(hush_agent_team_offer(root, &team) == 1, "offer");
    answer_note(&ev, "abababababababababababababababababababababababababababababababab",
                root, "Yes");
    expect(hush_agent_team_answer(store, &launch, &ev) == 0, "stranger yes ignored");
    expect(launch.roster.nagents == before, "stranger raised nobody");
    answer_note(&ev, launch.human.pubkey_hex, root, "No");
    expect(hush_agent_team_answer(store, &launch, &ev) == 1, "owner no");
    expect(launch.roster.nagents == before, "no raised nobody");
    expect(hush_agent_team_offer(root, &team) == 1, "offer again");
    answer_note(&ev, launch.human.pubkey_hex, root, "Yes");
    expect(hush_agent_team_answer(store, &launch, &ev) == 1, "owner yes");
    expect(launch.roster.nagents == before + 1, "yes raised Mail");
    expect(hush_roster_name_holder(&launch.roster, "Mail", NULL) != NULL, "Mail exists");
    hush_store_destroy(store);
}

static void test_hop_and_cwd(const char *home)
{
    hush_launch_t launch;
    hush_store_t *store = NULL;
    hush_thread_desk_t desk;
    hush_agent_job_t job;
    char root[HUSH_EVENT_ID_HEX_LEN + 1];
    char other[HUSH_EVENT_ID_HEX_LEN + 1];
    char found[HUSH_EVENT_ID_HEX_LEN + 1];
    char path[256];
    int n;

    expect(open_hive(&launch, &store, home), "hop hive");
    put_root(store, &launch, "SECRET-TURN body", root);
    memset(&desk, 0, sizeof(desk));
    snprintf(desk.name, sizeof(desk.name), "Mail desk");
    snprintf(desk.category, sizeof(desk.category), "ops");
    expect(hush_thread_desk_set(root, &desk) == HUSH_OK, "name milestone");
    hush_thread_brief_set(root, "visible brief");
    memset(&job, 0, sizeof(job));
    snprintf(job.channel, sizeof(job.channel), "general");
    job.launch = &launch;
    job.kind = HUSH_AGENT_KIND_NOTE_JOB;
    snprintf(job.robot_pub, sizeof(job.robot_pub), "%s", launch.payne.pubkey_hex);
    hush_agent_chief_equip(&job, store);
    expect(strstr(job.note, "Mail desk") != NULL, "index names the milestone");
    expect(strstr(job.note, "visible brief") != NULL, "index carries the brief");
    expect(strstr(job.note, "SECRET-TURN") == NULL, "index omits the turn");

    put_root(store, &launch, "other room", other);
    memset(&desk, 0, sizeof(desk));
    snprintf(desk.name, sizeof(desk.name), "Other");
    desk.archived = 1;
    expect(hush_thread_desk_set(other, &desk) == HUSH_OK, "archive other");
    snprintf(job.out, sizeof(job.out), "```hop\nOther\n```\n");
    hush_agent_chief_prepare(&job, store, found, sizeof(found));
    expect(found[0] == '\0', "archived hop refused");
    expect(strstr(job.out, "Hop refused.") != NULL, "hop refusal is posted");

    snprintf(job.out, sizeof(job.out), "going\n```hop\nMail desk\n```\n");
    hush_agent_chief_prepare(&job, store, found, sizeof(found));
    expect(strcmp(found, root) == 0, "hop finds Mail desk");
    expect(strstr(job.out, "```hop") == NULL, "hop fence stripped");
    expect(strstr(job.out, "going") != NULL, "hop keeps the note");

    n = snprintf(path, sizeof(path), "%s/proj", home);
    expect(n > 0 && (size_t)n < sizeof(path), "project path fits");
    expect(mkdir(path, 0700) == 0, "project dir");
    expect(hush_launch_add_project(&launch, store, "Mail", path, 0) == HUSH_OK,
           "record project");
    memset(&desk, 0, sizeof(desk));
    snprintf(desk.name, sizeof(desk.name), "Mail desk");
    snprintf(desk.project, sizeof(desk.project), "%s", launch.projects[0].slug);
    expect(hush_thread_desk_set(root, &desk) == HUSH_OK, "bind project");
    memset(&job, 0, sizeof(job));
    job.launch = &launch;
    snprintf(job.parent_id, sizeof(job.parent_id), "%s", root);
    snprintf(job.cwd, sizeof(job.cwd), "/tmp");
    hush_agent_apply_project_cwd(&job);
    expect(job.project_tools == 1, "bound milestone unlocks file tools");
    expect(strcmp(job.cwd, path) == 0, "cwd is the project");
    desk.project[0] = '\0';
    expect(hush_thread_desk_set(root, &desk) == HUSH_OK, "clear project");
    snprintf(job.cwd, sizeof(job.cwd), "/tmp");
    hush_agent_apply_project_cwd(&job);
    expect(job.project_tools == 0, "unbound milestone stays private");
    expect(strcmp(job.cwd, "/tmp") == 0, "unbound cwd unchanged");
    hush_store_destroy(store);
}

int main(void)
{
    char home[] = "/tmp/hush-team-XXXXXX";

    if (mkdtemp(home) == NULL)
        return 1;
    test_parse();
    test_project_clock();
    test_worker_cwd();
    test_denylist();
    test_yes_no(home);
    {
        char hop_home[] = "/tmp/hush-team-hop-XXXXXX";

        if (mkdtemp(hop_home) == NULL)
            return 1;
        test_hop_and_cwd(hop_home);
    }
    if (g_fail)
        return 1;
    printf("test_team ok\n");
    return 0;
}
