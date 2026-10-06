/* tests/test_intro_quiet.c: #278 stock on-deck line is not a chat note. */

#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hush_agent.h"
#include "hush_agent_internal.h"
#include "hush_event.h"
#include "hush_launch.h"
#include "hush_roster.h"
#include "hush_store.h"

#define QUIET_HEX_A \
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define QUIET_HEX_B \
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
#define QUIET_ROOT_A \
    "1111111111111111111111111111111111111111111111111111111111111111"
#define QUIET_ROOT_B \
    "2222222222222222222222222222222222222222222222222222222222222222"
#define QUIET_HUMAN \
    "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
#define QUIET_CUSTOM "Coach here."
#define QUIET_PADDED "  I am on deck. Standing orders are noted.\n"
#define QUIET_LEGACY \
    "At ease. I am on deck. Standing orders are noted. — Coach"
#define QUIET_DIR_TEMPLATE "/tmp/hush-intro-quiet-XXXXXX"

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

static void fill_parent(hush_event_t *ev, const char *id)
{
    memset(ev, 0, sizeof(*ev));
    memcpy(ev->id, id, HUSH_EVENT_ID_HEX_LEN);
    ev->id[HUSH_EVENT_ID_HEX_LEN] = '\0';
    memcpy(ev->pubkey, QUIET_HUMAN, HUSH_EVENT_PUBKEY_HEX_LEN);
    ev->pubkey[HUSH_EVENT_PUBKEY_HEX_LEN] = '\0';
    ev->kind = 1;
    ev->created_at = 1;
    memcpy(ev->content, "hello", 6);
    memcpy(ev->tags[0][0], "h", 2);
    memcpy(ev->tags[0][1], "general", 8);
    ev->tag_count = 1;
}

static void fill_bot(hush_agent_robot_t *bot, const char *hex, const char *intro,
                    int enabled, const char *slug)
{
    memset(bot, 0, sizeof(*bot));
    bot->name = "Coach";
    bot->hex = hex;
    bot->intro = intro;
    bot->intro_enabled = enabled;
    bot->slug = slug;
}

static size_t count_text(hush_store_t *store, const char *hex, const char *needle)
{
    size_t i;
    size_t hits = 0;

    for (i = 0; i < hush_store_count(store); ++i) {
        hush_event_t ev = {0};

        if (hush_store_get(store, i, &ev) != HUSH_OK)
            break;
        if (hex != NULL && strcmp(ev.pubkey, hex) != 0)
            continue;
        if (strstr(ev.content, needle) != NULL)
            hits++;
    }
    return hits;
}

static void post(hush_store_t *store, hush_agent_robot_t *bot, hush_event_t *parent)
{
    hush_agent_on_deck(store, bot, parent, HUSH_ROSTER_INTRO_DEFAULT);
}

int main(void)
{
    hush_store_t *store = NULL;
    hush_event_t root_a = {0};
    hush_event_t root_b = {0};
    hush_agent_robot_t bot = {0};
    char dir[64];

    memcpy(dir, QUIET_DIR_TEMPLATE, sizeof(QUIET_DIR_TEMPLATE));
    expect(mkdtemp(dir) != NULL, "scratch");
    expect(setenv("HOME", dir, 1) == 0, "home");
    expect(setenv("XDG_CONFIG_HOME", dir, 1) == 0, "xdg");
    expect(setenv("HUSH_HOME", dir, 1) == 0, "hush home");
    hush_agent_init();
    expect(hush_store_create(&store) == HUSH_OK, "store");
    fill_parent(&root_a, QUIET_ROOT_A);
    fill_parent(&root_b, QUIET_ROOT_B);
    expect(hush_store_insert(store, &root_a) == HUSH_OK, "root a");
    expect(hush_store_insert(store, &root_b) == HUSH_OK, "root b");

    fill_bot(&bot, QUIET_HEX_A, HUSH_ROSTER_INTRO_DEFAULT, 1, "coach");
    post(store, &bot, &root_a);
    expect(count_text(store, QUIET_HEX_A, "At ease") == 0, "stock posts no note");
    expect(count_text(store, QUIET_HEX_A, "Standing orders") == 0,
           "stock posts no standing orders");

    fill_bot(&bot, QUIET_HEX_A, QUIET_PADDED, 1, "coach");
    post(store, &bot, &root_a);
    expect(count_text(store, QUIET_HEX_A, "At ease") == 0, "padded default stays quiet");

    fill_bot(&bot, QUIET_HEX_A, QUIET_CUSTOM, 1, "coach");
    post(store, &bot, &root_a);
    expect(count_text(store, QUIET_HEX_A, "At ease. Coach here. — Coach") == 1,
           "custom intro posts once");
    post(store, &bot, &root_a);
    expect(count_text(store, QUIET_HEX_A, "At ease. Coach here. — Coach") == 1,
           "custom intro stays one on the same root");
    post(store, &bot, &root_b);
    expect(count_text(store, QUIET_HEX_A, "At ease. Coach here. — Coach") == 2,
           "custom intro posts once on a new root");

    fill_bot(&bot, QUIET_HEX_B, QUIET_CUSTOM, 0, "coach");
    post(store, &bot, &root_a);
    expect(count_text(store, QUIET_HEX_B, "Coach here") == 0, "switch off posts nothing");

    fill_bot(&bot, QUIET_HEX_B, QUIET_CUSTOM, 1, HUSH_LAUNCH_PAYNE_SLUG);
    post(store, &bot, &root_a);
    expect(count_text(store, QUIET_HEX_B, "Coach here") == 0, "major posts no intro");

    expect(!hush_agent_is_work_note(QUIET_LEGACY), "legacy at-ease note is not work");
    if (g_fail)
        return 1;
    printf("test_intro_quiet ok\n");
    return 0;
}
