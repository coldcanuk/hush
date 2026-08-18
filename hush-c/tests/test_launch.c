/* tests/test_launch.c: first-launch identity → vibe → channel → project. */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hush_launch.h"
#include "hush_pass.h"
#include "hush_store.h"

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
    static hush_launch_t launch;
    static char json[HUSH_LAUNCH_JSON_MAX];
    hush_store_t *store = NULL;
    const char *gitdir = "/tmp/hush-launch-proj";
    size_t n = 0;

    if (setenv("HUSH_FAKE_PASS_DIR", "/tmp/hush-launch-pass-store", 1) != 0)
        return 1;
    /* Isolated from tests/test_pass.c, which uses /tmp/hush-unit-pass-<pid>. */
    hush_pass_set_helper("tests/fake-pass.sh");
    hush_launch_init(&launch);
    expect(!hush_launch_is_ready(&launch), "cold not ready");
    expect(hush_store_create(&store) == HUSH_OK, "store");
    expect(hush_launch_create_identity(&launch) == HUSH_OK, "create");
    expect(launch.logged_in, "logged in");
    expect(!launch.backup_acked, "needs backup");
    expect(hush_launch_format_session(&launch, 10555, json, sizeof(json),
                                      &n) == HUSH_OK,
           "session after create");
    expect(strstr(json, "\"nsec\":\"nsec1") != NULL, "nsec once");
    expect(hush_launch_ack_backup(&launch, 1) == HUSH_OK, "ack");
    expect(launch.pass_saved, "saved to pass");
    expect(hush_pass_has(HUSH_PASS_IDENTITY_NSEC), "identity in store");
    expect(hush_launch_format_session(&launch, 10555, json, sizeof(json),
                                      &n) == HUSH_OK,
           "session after ack");
    expect(strstr(json, "\"nsec\":\"\"") != NULL, "nsec cleared");
    expect(hush_launch_create_vibe(&launch, store, "HQ",
                                   "primary endpoint") == HUSH_OK,
           "vibe");
    expect(hush_launch_is_ready(&launch), "ready");
    expect(launch.vibe_public == 1, "vibe public default");
    expect(launch.vibe_token[0] != '\0', "join token");
    expect(hush_launch_set_vibe_visibility(&launch, 0) == HUSH_OK, "private");
    expect(launch.vibe_public == 0, "vibe private");
    expect(hush_launch_format_session(&launch, 10555, json, sizeof(json),
                                      &n) == HUSH_OK,
           "session private");
    expect(strstr(json, "\"visibility\":\"private\"") != NULL, "vis private");
    expect(hush_launch_set_vibe_visibility(&launch, 1) == HUSH_OK, "public");
    expect(launch.nchannels == 3, "starter channels");
    expect(strncmp(launch.payne.npub, "npub1", 5) == 0, "payne");
    expect(hush_launch_add_channel(&launch, "incidents") == HUSH_OK, "channel");
    expect(launch.nchannels == 4, "four channels");
    expect(hush_launch_add_project(&launch, store, "alpha", gitdir, 1) == HUSH_OK,
           "project");
    expect(launch.nprojects == 1, "one project");
    expect(hush_launch_format_session(&launch, 10555, json, sizeof(json),
                                      &n) == HUSH_OK,
           "final session");
    expect(strstr(json, "Sgt Major Payne") != NULL, "payne name");
    expect(strstr(json, "\"slug\":\"incidents\"") != NULL, "incidents");
    expect(strstr(json, "\"slug\":\"alpha\"") != NULL, "alpha");
    expect(hush_launch_import_identity(
               &launch,
               "nsec1vl029mgpspedva04g90vltkh6fvh240zqtv9k0t9af8935ke9laqsnlfe5") ==
               HUSH_OK,
           "import");
    expect(!launch.backup_acked, "import still needs backup");
    expect(strcmp(launch.human.npub,
                  "npub10elfcs4fr0l0r8af98jlmgdh9c8tcxjvz9qkw038js35mp4dma8qzvjptg") ==
               0,
           "imported npub");
    expect(hush_launch_ack_backup(&launch, 0) == HUSH_OK, "import ack opt-out");
    expect(!launch.pass_saved || launch.save_pass == 0, "opt-out skips pass");
    {
        hush_roster_profile_t profile;

        memset(&profile, 0, sizeof(profile));
        memcpy(profile.first_name, "Ada", 4);
        memcpy(profile.theme, "dracula", 8);
        expect(hush_launch_set_profile(&launch, &profile) == HUSH_OK, "profile");
        expect(hush_launch_format_session(&launch, 10555, json, sizeof(json),
                                          &n) == HUSH_OK,
               "session profile");
        expect(strstr(json, "\"first_name\":\"Ada\"") != NULL, "first in session");
        expect(strstr(json, "\"theme\":\"dracula\"") != NULL, "theme in session");
    }
    expect(hush_launch_logout(&launch) == HUSH_OK, "logout");
    expect(!launch.logged_in, "logged out");
    expect(!hush_launch_is_ready(&launch), "logout not ready");
    hush_store_destroy(store);
    hush_pass_set_helper(NULL);
    if (g_fail)
        return 1;
    printf("test_launch ok\n");
    return 0;
}
