/* tests/test_profile_patch.c: a partial profile post keeps the other fields. */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hush_launch.h"
#include "hush_store.h"

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

static void fill(hush_roster_profile_t *profile)
{
    memset(profile, 0, sizeof(*profile));
    snprintf(profile->first_name, sizeof(profile->first_name), "Ada");
    snprintf(profile->last_name, sizeof(profile->last_name), "Lovelace");
    snprintf(profile->email, sizeof(profile->email), "ada@example");
    snprintf(profile->organization, sizeof(profile->organization), "Analytical");
    snprintf(profile->theme, sizeof(profile->theme), "dark");
}

static unsigned all_names(void)
{
    return HUSH_PROFILE_FIRST | HUSH_PROFILE_LAST | HUSH_PROFILE_EMAIL |
        HUSH_PROFILE_ORG | HUSH_PROFILE_THEME;
}

int main(void)
{
    char home[] = "/tmp/hush-profile-XXXXXX";
    hush_launch_t launch;
    hush_launch_t again;
    hush_store_t *store = NULL;
    hush_roster_profile_t profile;
    const hush_roster_profile_t *saved;

    if (mkdtemp(home) == NULL)
        return 1;
    if (setenv("HUSH_HOME", home, 1) != 0)
        return 1;
    hush_launch_init(&launch);
    expect(hush_store_create(&store) == HUSH_OK, "store");
    expect(hush_launch_create_identity(&launch) == HUSH_OK, "identity");
    expect(hush_launch_ack_backup(&launch, 0) == HUSH_OK, "ack");
    expect(hush_launch_create_vibe(&launch, store, "HQ", "x") == HUSH_OK, "vibe");
    fill(&profile);
    expect(hush_launch_patch_profile(&launch, &profile, all_names(), 0) == HUSH_OK,
           "full save");
    memset(&profile, 0, sizeof(profile));
    snprintf(profile.theme, sizeof(profile.theme), "field-office");
    expect(hush_launch_patch_profile(&launch, &profile, HUSH_PROFILE_THEME, 0) ==
               HUSH_OK, "theme only");
    saved = &launch.roster.profile;
    expect(strcmp(saved->first_name, "Ada") == 0, "first name kept");
    expect(strcmp(saved->last_name, "Lovelace") == 0, "last name kept");
    expect(strcmp(saved->email, "ada@example") == 0, "email kept");
    expect(strcmp(saved->organization, "Analytical") == 0, "org kept");
    expect(strcmp(saved->theme, "field-office") == 0, "theme updated");
    expect(hush_launch_patch_profile(&launch, &profile, HUSH_PROFILE_DEVLOG, 1) ==
               HUSH_OK, "dev log on");
    expect(launch.dev_log_enabled == 1, "dev log set");
    expect(strcmp(saved->first_name, "Ada") == 0, "dev log keeps the name");
    expect(hush_launch_patch_profile(&launch, &profile, HUSH_PROFILE_DEVLOG, 0) ==
               HUSH_OK, "dev log off");
    expect(launch.dev_log_enabled == 0, "dev log cleared");
    memset(&profile, 0, sizeof(profile));
    expect(hush_launch_patch_profile(&launch, &profile, HUSH_PROFILE_FIRST, 0) ==
               HUSH_OK, "clear first");
    expect(saved->first_name[0] == '\0', "first name cleared");
    expect(strcmp(saved->last_name, "Lovelace") == 0, "last name still kept");
    snprintf(profile.first_name, sizeof(profile.first_name), "Ada");
    expect(hush_launch_patch_profile(&launch, &profile, HUSH_PROFILE_FIRST, 0) ==
               HUSH_OK, "restore first");
    snprintf(profile.theme, sizeof(profile.theme), "nope");
    expect(hush_launch_patch_profile(&launch, &profile, HUSH_PROFILE_THEME, 0) ==
               HUSH_ERR_PARSE, "bad theme");
    expect(strcmp(saved->theme, "field-office") == 0, "bad theme changes nothing");
    expect(strcmp(saved->first_name, "Ada") == 0, "bad theme keeps the name");
    expect(hush_launch_patch_profile(&launch, &profile, 0, 0) == HUSH_ERR_PARSE,
           "empty mask");
    hush_launch_init(&again);
    expect(hush_launch_restore_vibe(&again) == HUSH_OK, "reload");
    expect(strcmp(again.roster.profile.first_name, "Ada") == 0, "restart first");
    expect(strcmp(again.roster.profile.last_name, "Lovelace") == 0, "restart last");
    expect(strcmp(again.roster.profile.email, "ada@example") == 0, "restart email");
    expect(strcmp(again.roster.profile.organization, "Analytical") == 0,
           "restart org");
    expect(strcmp(again.roster.profile.theme, "field-office") == 0, "restart theme");
    expect(again.dev_log_enabled == 0, "restart dev log");
    hush_store_destroy(store);
    if (g_fail)
        return 1;
    printf("test_profile_patch ok\n");
    return 0;
}
