/* tests/test_thread.c: transcript and brief round-trips with isolation. */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hush_event.h"
#include "hush_thread.h"

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

/* Writes a 64-char lowercase hex id derived from n. */
static void id_for(char out[HUSH_EVENT_ID_HEX_LEN + 1], unsigned n)
{
    int written = snprintf(out, HUSH_EVENT_ID_HEX_LEN + 1, "%064x", n);

    if (written != HUSH_EVENT_ID_HEX_LEN)
        abort();
}

/* Builds one kind-1 note with the given id and content. */
static void make_note(hush_event_t *ev, const char *id, const char *content)
{
    memset(ev, 0, sizeof(*ev));
    (void)snprintf(ev->id, sizeof(ev->id), "%s", id);
    memset(ev->pubkey, 'a', HUSH_EVENT_PUBKEY_HEX_LEN);
    ev->kind = 1;
    ev->created_at = 1720000000;
    (void)snprintf(ev->content, sizeof(ev->content), "%s", content);
}

/* Adds a single ["e", root] tag so the note joins root's transcript. */
static void add_root_tag(hush_event_t *ev, const char *root)
{
    if (ev->tag_count != 0)
        abort();
    (void)snprintf(ev->tags[0][0], HUSH_EVENT_MAX_TAG_LEN + 1, "%s", "e");
    (void)snprintf(ev->tags[0][1], HUSH_EVENT_MAX_TAG_LEN + 1, "%s", root);
    ev->tag_count = 1;
}

/* Writes the transcript path for root into out. */
static void log_path(char *out, size_t outsz, const char *home,
                     const char *root)
{
    int n = snprintf(out, outsz, "%s/threads/%s.log", home, root);

    if (n <= 0 || (size_t)n >= outsz)
        abort();
}

/* Records turns 2..2+count-1 onto root and returns the last note. */
static void record_series(const char *root, unsigned count)
{
    hush_event_t ev;
    char id[HUSH_EVENT_ID_HEX_LEN + 1];
    char content[32];
    unsigned i;

    for (i = 0; i < count; ++i) {
        id_for(id, i + 2);
        (void)snprintf(content, sizeof(content), "t%u", i + 2);
        make_note(&ev, id, content);
        add_root_tag(&ev, root);
        hush_thread_record(&ev);
    }
}

static void test_root_and_reply(void)
{
    hush_event_t ev;
    hush_thread_turn_t turns[8];
    char root_note[HUSH_EVENT_ID_HEX_LEN + 1];
    char id[HUSH_EVENT_ID_HEX_LEN + 1];
    size_t count;

    id_for(root_note, 1);
    make_note(&ev, root_note, "root note");
    hush_thread_record(&ev);
    expect(hush_thread_count(root_note) == 1, "root note recorded");
    id_for(id, 2);
    make_note(&ev, id, "reply one");
    add_root_tag(&ev, root_note);
    hush_thread_record(&ev);
    id_for(id, 3);
    make_note(&ev, id, "reply two");
    add_root_tag(&ev, root_note);
    hush_thread_record(&ev);
    expect(hush_thread_count(root_note) == 3, "replies join the root");
    count = hush_thread_read(root_note, turns, 8);
    expect(count == 3, "read returns every turn");
    expect(strcmp(turns[0].content, "root note") == 0, "oldest first");
    expect(strcmp(turns[2].content, "reply two") == 0, "newest last");
    expect(turns[0].created_at == 1720000000, "timestamp round-trips");
    expect(strcmp(turns[1].pubkey, ev.pubkey) == 0, "pubkey round-trips");
    expect(count <= (size_t)HUSH_THREAD_TURNS_MAX, "read stays under cap");
}

static void test_newest_cap(const char *root)
{
    hush_thread_turn_t turns[HUSH_THREAD_TURNS_MAX];
    size_t count;

    record_series(root, (unsigned)HUSH_THREAD_TURNS_MAX + 1);
    count = hush_thread_read(root, turns, HUSH_THREAD_TURNS_MAX);
    expect(count == (size_t)HUSH_THREAD_TURNS_MAX, "cap honoured");
    expect(strcmp(turns[0].content, "t3") == 0, "drops the oldest turn");
    expect(strcmp(turns[count - 1].content, "t34") == 0, "keeps the newest");
}

static void test_escaping(void)
{
    hush_event_t ev;
    hush_thread_turn_t turns[2];
    char tricky[] = "quote \" back \\ slash\ttab\nline";
    char root_note[HUSH_EVENT_ID_HEX_LEN + 1];

    id_for(root_note, 40);
    make_note(&ev, root_note, tricky);
    hush_thread_record(&ev);
    expect(hush_thread_read(root_note, turns, 2) == 1, "tricky note stored");
    expect(strcmp(turns[0].content, tricky) == 0, "escaping round-trips");
}

static void test_truncation(void)
{
    hush_event_t ev;
    hush_thread_turn_t turn;
    char root_note[HUSH_EVENT_ID_HEX_LEN + 1];
    char long_content[HUSH_EVENT_MAX_CONTENT + 1];

    id_for(root_note, 50);
    memset(long_content, 'x', sizeof(long_content) - 1);
    long_content[HUSH_EVENT_MAX_CONTENT] = '\0';
    make_note(&ev, root_note, long_content);
    hush_thread_record(&ev);
    expect(hush_thread_read(root_note, &turn, 1) == 1, "long note stored");
    expect(strlen(turn.content) == (size_t)HUSH_THREAD_CONTENT_MAX,
           "content capped at the transcript limit");
}

static void test_brief(const char *root)
{
    char brief[HUSH_THREAD_BRIEF_MAX + 1];
    char long_brief[64];

    hush_thread_brief_get(root, brief, sizeof(brief));
    expect(brief[0] == '\0', "brief starts empty");
    hush_thread_brief_set(root, "shipping the relay\nthread memory");
    hush_thread_brief_get(root, brief, sizeof(brief));
    expect(strcmp(brief, "shipping the relay\nthread memory") == 0,
           "brief round-trips");
    memset(long_brief, 'b', sizeof(long_brief) - 1);
    long_brief[sizeof(long_brief) - 1] = '\0';
    hush_thread_brief_set(root, long_brief);
    hush_thread_brief_get(root, brief, sizeof(brief));
    expect(strcmp(brief, long_brief) == 0, "brief replaces");
    hush_thread_brief_set(root, "");
    hush_thread_brief_get(root, brief, sizeof(brief));
    expect(brief[0] == '\0', "empty brief clears");
}

static void test_bad_roots(void)
{
    hush_thread_turn_t turn;
    char brief[16];
    char fresh[HUSH_EVENT_ID_HEX_LEN + 1];

    id_for(fresh, 900);

    expect(hush_thread_count("../escape") == 0, "traversal rejected");
    expect(hush_thread_count("short") == 0, "short root rejected");
    expect(hush_thread_count("00000000000000000000000000000000000000000000000000000000000000AB")
               == 0,
           "uppercase root rejected");
    expect(hush_thread_count(NULL) == 0, "NULL root rejected");
    expect(hush_thread_read("../escape", &turn, 1) == 0, "read traversal");
    hush_thread_brief_get("../escape", brief, sizeof(brief));
    expect(brief[0] == '\0', "brief traversal empty");
    hush_thread_brief_set("../escape", "nope");
    expect(hush_thread_count("../escape") == 0, "bad set is a no-op");
    expect(hush_thread_count(fresh) == 0, "unknown root is empty");
}

static void test_ignored(void)
{
    hush_event_t ev;
    char id[HUSH_EVENT_ID_HEX_LEN + 1];
    char fresh[HUSH_EVENT_ID_HEX_LEN + 1];

    id_for(fresh, 901);

    id_for(id, 60);
    make_note(&ev, id, "not a note");
    ev.kind = 2;
    hush_thread_record(&ev);
    expect(hush_thread_count(id) == 0, "non-notes ignored");
    ev.kind = 1;
    ev.id[0] = '\0';
    hush_thread_record(&ev);
    hush_thread_record(NULL);
    expect(hush_thread_count(fresh) == 0, "malformed events ignored");
}

static void test_privacy_and_symlink(const char *home)
{
    hush_event_t ev;
    char dir[512];
    char path[512];
    char decoy[512];
    char id[HUSH_EVENT_ID_HEX_LEN + 1];
    struct stat st;
    FILE *fp;

    id_for(id, 70);
    make_note(&ev, id, "private");
    hush_thread_record(&ev);
    (void)snprintf(dir, sizeof(dir), "%s/threads", home);
    expect(stat(dir, &st) == 0 && (st.st_mode & 077) == 0, "dir is private");
    log_path(path, sizeof(path), home, id);
    expect(stat(path, &st) == 0 && (st.st_mode & 077) == 0, "log is private");
    (void)snprintf(decoy, sizeof(decoy), "%s/decoy", home);
    fp = fopen(decoy, "w");
    if (fp != NULL) {
        fprintf(fp, "{\"id\":\"%s\",\"pubkey\":\"%s\",\"at\":1,"
                    "\"content\":\"stolen\"}\n", id, ev.pubkey);
        (void)fclose(fp);
    }
    (void)unlink(path);
    if (symlink(decoy, path) == 0)
        expect(hush_thread_count(id) == 0, "symlinked log refused");
}

int main(void)
{
    char home[] = "/tmp/hush-thread-XXXXXX";
    char root[HUSH_EVENT_ID_HEX_LEN + 1];

    if (mkdtemp(home) == NULL)
        return 1;
    if (setenv("HUSH_HOME", home, 1) != 0)
        return 1;
    id_for(root, 1);
    test_root_and_reply();
    test_newest_cap(root);
    test_escaping();
    test_truncation();
    test_brief(root);
    test_bad_roots();
    test_ignored();
    test_privacy_and_symlink(home);
    if (g_fail)
        return 1;
    printf("test_thread ok\n");
    return 0;
}
