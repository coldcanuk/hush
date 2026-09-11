/* tests/test_filter.c: filter field matching semantics. */

#include <stdio.h>
#include <string.h>

#include "hush_event.h"
#include "hush_filter.h"

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

static void fill(hush_event_t *ev, uint32_t kind, const char *pub,
                 const char *id, int64_t at)
{
    memset(ev, 0, sizeof(*ev));
    (void)snprintf(ev->id, sizeof(ev->id), "%s", id);
    (void)snprintf(ev->pubkey, sizeof(ev->pubkey), "%s", pub);
    ev->kind = kind;
    ev->created_at = at;
    ev->tag_count = 1;
    memcpy(ev->tags[0][0], "h", 2);
    memcpy(ev->tags[0][1], "general", 8);
}

int main(void)
{
    hush_event_t ev;
    hush_filter_t f;

    memset(&f, 0, sizeof(f));
    f.kinds_len = 1;
    f.kinds[0] = 1;
    fill(&ev, 1, "aa", "11", 100);
    expect(hush_filter_match(&f, &ev), "kind match");
    fill(&ev, 2, "aa", "11", 100);
    expect(!hush_filter_match(&f, &ev), "kind mismatch");

    memset(&f, 0, sizeof(f));
    f.since = 100;
    f.until = 200;
    fill(&ev, 1, "aa", "11", 100);
    expect(hush_filter_match(&f, &ev), "since inclusive");
    fill(&ev, 1, "aa", "11", 200);
    expect(hush_filter_match(&f, &ev), "until inclusive");
    fill(&ev, 1, "aa", "11", 99);
    expect(!hush_filter_match(&f, &ev), "before since");
    fill(&ev, 1, "aa", "11", 201);
    expect(!hush_filter_match(&f, &ev), "after until");

    memset(&f, 0, sizeof(f));
    f.ids_len = 1;
    memcpy(f.ids[0], "11", 3);
    f.authors_len = 1;
    memcpy(f.authors[0], "aa", 3);
    fill(&ev, 1, "aa", "11", 5);
    expect(hush_filter_match(&f, &ev), "ids and authors");
    fill(&ev, 1, "bb", "11", 5);
    expect(!hush_filter_match(&f, &ev), "wrong author");
    fill(&ev, 1, "aa", "12", 5);
    expect(!hush_filter_match(&f, &ev), "wrong id");

    memset(&f, 0, sizeof(f));
    f.tag_count = 1;
    memcpy(f.tag_keys[0], "p", 2);
    f.tag_vals_len[0] = 1;
    memcpy(f.tag_vals[0][0], "cc", 3);
    fill(&ev, 1, "aa", "11", 5);
    ev.tag_count = 2;
    memcpy(ev.tags[1][0], "p", 2);
    memcpy(ev.tags[1][1], "cc", 3);
    expect(hush_filter_match(&f, &ev), "p tag match");
    memcpy(ev.tags[1][1], "dd", 3);
    expect(!hush_filter_match(&f, &ev), "p tag mismatch");

    if (g_fail)
        return 1;
    printf("test_filter ok\n");
    return 0;
}
