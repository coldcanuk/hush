/* tests/test_cevent.c: bounded in-hive channel event ring. */

#include <stdio.h>
#include <string.h>

#include "hush_cevent.h"

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

int main(void)
{
    static char json[HUSH_CEVENT_JSON_MAX];
    hush_cevent_t ev;
    size_t n = 0;
    size_t i;

    hush_cevent_init();
    expect(hush_cevent_emit(NULL) == HUSH_ERR_ARG, "null emit");
    memset(&ev, 0, sizeof(ev));
    expect(hush_cevent_emit(&ev) == HUSH_ERR_ARG, "empty type");
    memcpy(ev.type, HUSH_CEVENT_MENTION, sizeof(HUSH_CEVENT_MENTION));
    memcpy(ev.channel, "general", 8);
    memcpy(ev.root, "aa", 3);
    memcpy(ev.actor, "bb", 3);
    memcpy(ev.note, "hello", 6);
    expect(hush_cevent_emit(&ev) == HUSH_OK, "emit mention");
    memcpy(ev.type, HUSH_CEVENT_INTRO, sizeof(HUSH_CEVENT_INTRO));
    expect(hush_cevent_emit(&ev) == HUSH_OK, "emit intro");
    expect(hush_cevent_format_json(json, sizeof(json), &n) == HUSH_OK, "json");
    expect(strstr(json, "\"ok\":true") != NULL, "ok");
    expect(strstr(json, "\"type\":\"mention\"") != NULL, "mention type");
    expect(strstr(json, "\"type\":\"intro\"") != NULL, "intro type");
    expect(strstr(json, "\"seq\":1") != NULL, "seq 1");
    expect(strstr(json, "\"seq\":2") != NULL, "seq 2");
    expect(strstr(json, "\"drops\":0") != NULL, "no drops yet");
    expect(strstr(json, "\"last_seq\":2") != NULL, "last_seq exposed");
    expect(hush_cevent_drops() == 0, "drops zero");
    expect(hush_cevent_last_seq() == 2, "last_seq watermark 2");

    /* Delta cursor: since=1 returns only the intro (seq 2). */
    n = 0;
    expect(hush_cevent_format_json_since(json, sizeof(json), &n, 1) == HUSH_OK,
           "since json");
    expect(strstr(json, "\"type\":\"mention\"") == NULL, "since hides mention");
    expect(strstr(json, "\"type\":\"intro\"") != NULL, "since keeps intro");
    expect(strstr(json, "\"last_seq\":2") != NULL, "since exposes last_seq");

    /* since=2 returns no events but still reports the watermark. */
    n = 0;
    expect(hush_cevent_format_json_since(json, sizeof(json), &n, 2) == HUSH_OK,
           "since at cursor json");
    expect(strstr(json, "\"type\":\"intro\"") == NULL, "since 2 hides intro");
    expect(strstr(json, "\"last_seq\":2") != NULL, "since 2 keeps watermark");
    hush_cevent_init();
    memcpy(ev.type, HUSH_CEVENT_FOLLOW, sizeof(HUSH_CEVENT_FOLLOW));
    for (i = 0; i < (size_t)HUSH_CEVENT_MAX + 2; i++)
        expect(hush_cevent_emit(&ev) == HUSH_OK, "wrap emit");
    expect(hush_cevent_format_json(json, sizeof(json), &n) == HUSH_OK,
           "wrap json");
    expect(strstr(json, "\"seq\":3") != NULL, "kept seq after wrap");
    expect(strstr(json, "\"drops\":2") != NULL, "drops recorded in json");
    expect(hush_cevent_drops() == 2, "two drops after wrap");

    /* Ack watermark: a wrap that evicts only acked events is not a loss. */
    hush_cevent_init();
    memcpy(ev.type, HUSH_CEVENT_CHAPERON, sizeof(HUSH_CEVENT_CHAPERON));
    for (i = 0; i < (size_t)HUSH_CEVENT_MAX; i++)
        expect(hush_cevent_emit(&ev) == HUSH_OK, "fill emit");
    expect(hush_cevent_last_seq() == (uint32_t)HUSH_CEVENT_MAX,
           "full ring watermark");
    hush_cevent_ack(hush_cevent_last_seq());
    for (i = 0; i < 2; i++)
        expect(hush_cevent_emit(&ev) == HUSH_OK, "overflow after ack");
    expect(hush_cevent_drops() == 0, "acked eviction is not a drop");
    hush_cevent_init();
    if (g_fail)
        return 1;
    printf("test_cevent ok\n");
    return 0;
}
