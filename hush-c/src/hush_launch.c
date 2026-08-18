/* hush_launch.c: owns first-launch session, vibe, channels, projects, Payne. */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "hush_event.h"
#include "hush_launch.h"
#include "hush_pass.h"

enum {
    HUSH_LAUNCH_KIND_META = 0,
    HUSH_LAUNCH_KIND_NOTE = 1,
    HUSH_LAUNCH_KIND_REPO = 30617,
    HUSH_LAUNCH_SLUG_FALLBACK = 'x',
    HUSH_LAUNCH_ID_WIDTH = 16,
    HUSH_LAUNCH_CMD_MAX = 768,
    HUSH_LAUNCH_FILE_MAX = 32768,
    HUSH_LAUNCH_KEY_MAX = 48,
    HUSH_LAUNCH_COUNT_MAX = 8,
    HUSH_LAUNCH_UUID_RAW = 16
};

#define HUSH_LAUNCH_DEFAULT_VIBE "local hive"
#define HUSH_LAUNCH_CHAN_GENERAL "general"
#define HUSH_LAUNCH_CHAN_WELCOME "welcome"
#define HUSH_LAUNCH_CHAN_AGENTS "agents"
#define HUSH_LAUNCH_VIBE_FILE "vibe.json"
#define HUSH_LAUNCH_VIBE_VERSION "1"
#define HUSH_LAUNCH_ENV_CONFIG "HUSH_CONFIG_DIR"

/* Copies text, trimmed, into dst. Empty becomes fallback. */
static void hush_launch_copy_name(char *dst, size_t dstsz,
                                  const char *text, const char *fallback);

/* Writes a lowercase slug of name into dst. */
static void hush_launch_slugify(char *dst, size_t dstsz, const char *name);

/* True when slug already exists in the channel table. */
static int hush_launch_has_channel(const hush_launch_t *launch, const char *slug);

/* Returns the channel with slug, or NULL. */
static hush_launch_channel_t *hush_launch_find_channel(hush_launch_t *launch,
                                                       const char *slug);

/* True when group_id names an existing group. */
static int hush_launch_has_group_id(const hush_launch_t *launch,
                                    const char *group_id);

/* True when slug is Payne or a raised robot. */
static int hush_launch_has_robot(const hush_launch_t *launch, const char *slug);

/* Writes 32 hex chars from /dev/urandom. */
static hush_status_t hush_launch_make_uuid(char *out, size_t outsz);

/* Assigns a UUID when ch->id is empty. */
static hush_status_t hush_launch_ensure_channel_id(hush_launch_channel_t *ch);

/* Assigns missing channel UUIDs and saves when any were filled. */
static hush_status_t hush_launch_ensure_ids(hush_launch_t *launch);

/* Appends a channel. Fails HUSH_ERR_FULL. */
static hush_status_t hush_launch_push_channel(hush_launch_t *launch,
                                              const char *name);

/* Seeds general/welcome/agents and Payne. */
static hush_status_t hush_launch_seed_hive(hush_launch_t *launch,
                                           hush_store_t *store);

/* Inserts a kind 0 profile for id. */
static hush_status_t hush_launch_store_profile(hush_store_t *store,
                                               const hush_identity_t *id,
                                               const char *name,
                                               const char *about);

/* Inserts a kind 1 welcome note from Payne. */
static hush_status_t hush_launch_store_welcome(hush_store_t *store,
                                               const hush_identity_t *payne);

/* Inserts a kind 30617 repo announcement. */
static hush_status_t hush_launch_store_repo(hush_store_t *store,
                                            const hush_identity_t *human,
                                            const hush_launch_project_t *proj);

/* Fills a stored event skeleton. */
static void hush_launch_fill_event(hush_event_t *ev, const char *pubkey_hex,
                                   uint32_t kind, const char *content,
                                   const char *channel);

/* Writes a 16-char hex join token. */
static hush_status_t hush_launch_make_token(char *out, size_t outsz);

/* Writes a deterministic hex id from time + seq. */
static void hush_launch_make_id(char *out65);

/* JSON-escapes src into dst. */
static size_t hush_launch_json_escape(const char *src, char *dst, size_t dstsz);

/* Best-effort pass insert. Never fails the caller. */
static void hush_launch_try_save(hush_launch_t *launch, const char *path,
                                 const char *secret);

/* Runs git init at path. Succeeds if .git already exists. */
static hush_status_t hush_launch_git_init(const char *path);

/* Writes the session header object fields. */
static hush_status_t hush_launch_format_head(const hush_launch_t *launch,
                                             uint16_t port,
                                             char *out, size_t outsz,
                                             size_t *off);

/* Appends the channels array body. */
static hush_status_t hush_launch_format_channels(const hush_launch_t *launch,
                                                 char *out, size_t outsz,
                                                 size_t *off);

/* Appends one channel's humans and robots arrays. */
static hush_status_t hush_launch_format_channel_lists(
    const hush_launch_channel_t *ch, char *out, size_t outsz, size_t *off);

/* Appends the groups array after channels. */
static hush_status_t hush_launch_format_groups(const hush_launch_t *launch,
                                               char *out, size_t outsz,
                                               size_t *off);

/* Appends the projects array body. */
static hush_status_t hush_launch_format_projects(const hush_launch_t *launch,
                                                 char *out, size_t outsz,
                                                 size_t *off);

/* Appends roster JSON and closes the session object. */
static hush_status_t hush_launch_format_roster(const hush_launch_t *launch,
                                               char *out, size_t outsz,
                                               size_t *off);

/* Resolves $HUSH_CONFIG_DIR or ~/.config/hush into out. */
static void hush_launch_config_dir(char *out, size_t outsz);

/* Resolves hush/vibe.json into out. Empty on overflow. */
static void hush_launch_vibe_path(char *out, size_t outsz);

/* mkdir 0700 the config dir. */
static hush_status_t hush_launch_ensure_config_dir(void);

/* Reads vibe.json into buf. Missing file is HUSH_ERR_NOT_FOUND. */
static hush_status_t hush_launch_read_vibe_file(char *buf, size_t bufsz);

/* Writes buf to vibe.json via a sibling tmp + rename. */
static hush_status_t hush_launch_write_vibe_file(const char *buf);

/* Writes every byte of buf to fd. Closes fd. Unlinks tmp on failure. */
static hush_status_t hush_launch_flush_tmp(int fd, const char *tmp,
                                           const char *buf);

/* Copies a "key":"value" field. Returns 1 when present and non-empty. */
static int hush_launch_json_string(const char *json, const char *key,
                                   char *out, size_t outsz);

/* Parses a decimal count field. Caps at maxv. */
static size_t hush_launch_json_count(const char *json, const char *key,
                                     size_t maxv);

/* Formats an indexed persist key into out. */
static void hush_launch_index_key(char *out, size_t outsz,
                                  const char *stem, size_t idx);

/* Appends "key":"escaped" plus a trailing comma. */
static hush_status_t hush_launch_put_field(char *out, size_t outsz, size_t *off,
                                           const char *key, const char *val);

/* Appends the vibe head fields. */
static hush_status_t hush_launch_put_vibe_head(const hush_launch_t *launch,
                                               char *out, size_t outsz,
                                               size_t *off);

/* Appends channel_* indexed fields. */
static hush_status_t hush_launch_put_channels(const hush_launch_t *launch,
                                              char *out, size_t outsz,
                                              size_t *off);

/* Appends one channel's human_* / robot_* persist fields. */
static hush_status_t hush_launch_put_channel_lists(
    const hush_launch_channel_t *ch, size_t idx,
    char *out, size_t outsz, size_t *off);

/* Appends group_* indexed fields. */
static hush_status_t hush_launch_put_groups(const hush_launch_t *launch,
                                            char *out, size_t outsz,
                                            size_t *off);

/* Appends project_* indexed fields. */
static hush_status_t hush_launch_put_projects(const hush_launch_t *launch,
                                              char *out, size_t outsz,
                                              size_t *off);

/* Appends agent_* indexed fields. */
static hush_status_t hush_launch_put_agents(const hush_launch_t *launch,
                                            char *out, size_t outsz,
                                            size_t *off);

/* Appends member_* fields and closes the JSON object. */
static hush_status_t hush_launch_put_members(const hush_launch_t *launch,
                                             char *out, size_t outsz,
                                             size_t *off);

/* Appends profile + agent_* + member_* fields and closes the object. */
static hush_status_t hush_launch_put_roster(const hush_launch_t *launch,
                                            char *out, size_t outsz,
                                            size_t *off);

/* Restores one agent nsec from pass, or generates a fresh key. */
static hush_status_t hush_launch_restore_agent_id(hush_roster_agent_t *agent);

/* Fills launch vibe fields from json. Requires vibe_name. */
static hush_status_t hush_launch_take_vibe_head(hush_launch_t *launch,
                                                const char *json);

/* Fills channels from json. */
static hush_status_t hush_launch_take_channels(hush_launch_t *launch,
                                               const char *json);

/* Fills one restored channel's humans and robots. */
static void hush_launch_take_channel_lists(hush_launch_channel_t *ch,
                                           const char *json, size_t idx);

/* Fills groups from json. */
static hush_status_t hush_launch_take_groups(hush_launch_t *launch,
                                             const char *json);

/* Fills projects from json. */
static hush_status_t hush_launch_take_projects(hush_launch_t *launch,
                                               const char *json);

/* Fills profile, agents, members from json. */
static hush_status_t hush_launch_take_roster(hush_launch_t *launch,
                                             const char *json);

/* Restores one agent slot from persist fields + optional pass nsec. */
static hush_status_t hush_launch_take_agent(hush_launch_t *launch,
                                            const char *json, size_t idx);

/* Restores members from persist fields. */
static hush_status_t hush_launch_take_members(hush_launch_t *launch,
                                              const char *json);

/* Restores Payne from pass or generates a fresh key. */
static hush_status_t hush_launch_restore_payne(hush_launch_t *launch);

void hush_launch_init(hush_launch_t *launch)
{
    if (launch == NULL)
        return;
    memset(launch, 0, sizeof(*launch));
    launch->vibe_public = 1;
    hush_roster_init(&launch->roster);
}

hush_status_t hush_launch_create_identity(hush_launch_t *launch)
{
    if (launch == NULL)
        return HUSH_ERR_ARG;
    hush_identity_clear(&launch->human);
    launch->logged_in = 0;
    launch->backup_acked = 0;
    if (hush_identity_generate(&launch->human) != HUSH_OK)
        return HUSH_ERR_CRYPTO;
    launch->logged_in = 1;
    return HUSH_OK;
}

hush_status_t hush_launch_import_identity(hush_launch_t *launch,
                                          const char *secret)
{
    hush_status_t st;

    if (launch == NULL || secret == NULL)
        return HUSH_ERR_ARG;
    hush_identity_clear(&launch->human);
    launch->logged_in = 0;
    launch->backup_acked = 0;
    launch->pass_saved = 0;
    launch->pass_error[0] = '\0';
    st = hush_identity_import(&launch->human, secret);
    if (st != HUSH_OK)
        return st;
    launch->logged_in = 1;
    return HUSH_OK;
}

hush_status_t hush_launch_ack_backup(hush_launch_t *launch, int save_pass)
{
    if (launch == NULL)
        return HUSH_ERR_ARG;
    if (!launch->logged_in)
        return HUSH_ERR_ARG;
    launch->save_pass = save_pass ? 1 : 0;
    launch->pass_saved = 0;
    launch->pass_error[0] = '\0';
    if (launch->save_pass)
        hush_launch_try_save(launch, HUSH_PASS_IDENTITY_NSEC,
                             launch->human.nsec);
    launch->backup_acked = 1;
    return HUSH_OK;
}

hush_status_t hush_launch_restore_identity(hush_launch_t *launch)
{
    char secret[HUSH_PASS_SECRET_MAX];

    if (launch == NULL)
        return HUSH_ERR_ARG;
    if (launch->logged_in)
        return HUSH_OK;
    if (!hush_pass_has(HUSH_PASS_IDENTITY_NSEC))
        return HUSH_OK;
    if (hush_pass_get(secret, sizeof(secret), HUSH_PASS_IDENTITY_NSEC) != HUSH_OK)
        return HUSH_OK;
    if (hush_identity_import(&launch->human, secret) != HUSH_OK) {
        hush_identity_clear(&launch->human);
        return HUSH_OK;
    }
    launch->logged_in = 1;
    launch->backup_acked = 1;
    launch->save_pass = 1;
    launch->pass_saved = 1;
    launch->pass_error[0] = '\0';
    return HUSH_OK;
}

hush_status_t hush_launch_save_vibe(const hush_launch_t *launch)
{
    char json[HUSH_LAUNCH_FILE_MAX];
    size_t off = 0;

    if (launch == NULL)
        return HUSH_ERR_ARG;
    if (!launch->has_vibe)
        return HUSH_OK;
    json[0] = '{';
    json[1] = '\0';
    off = 1;
    HUSH_TRY(hush_launch_put_vibe_head(launch, json, sizeof(json), &off));
    HUSH_TRY(hush_launch_put_channels(launch, json, sizeof(json), &off));
    HUSH_TRY(hush_launch_put_groups(launch, json, sizeof(json), &off));
    HUSH_TRY(hush_launch_put_projects(launch, json, sizeof(json), &off));
    HUSH_TRY(hush_launch_put_roster(launch, json, sizeof(json), &off));
    return hush_launch_write_vibe_file(json);
}

hush_status_t hush_launch_restore_vibe(hush_launch_t *launch)
{
    char json[HUSH_LAUNCH_FILE_MAX];
    hush_status_t st;

    if (launch == NULL)
        return HUSH_ERR_ARG;
    if (launch->has_vibe)
        return HUSH_OK;
    st = hush_launch_read_vibe_file(json, sizeof(json));
    if (st == HUSH_ERR_NOT_FOUND)
        return HUSH_OK;
    if (st != HUSH_OK)
        return HUSH_OK;
    if (hush_launch_take_vibe_head(launch, json) != HUSH_OK)
        return HUSH_OK;
    (void)hush_launch_take_channels(launch, json);
    (void)hush_launch_take_groups(launch, json);
    (void)hush_launch_take_projects(launch, json);
    (void)hush_launch_take_roster(launch, json);
    if (hush_launch_restore_payne(launch) != HUSH_OK)
        return HUSH_OK;
    launch->has_vibe = 1;
    (void)hush_launch_ensure_ids(launch);
    return HUSH_OK;
}

hush_status_t hush_launch_logout(hush_launch_t *launch)
{
    if (launch == NULL)
        return HUSH_ERR_ARG;
    hush_identity_clear(&launch->human);
    launch->logged_in = 0;
    launch->backup_acked = 0;
    launch->save_pass = 0;
    launch->pass_saved = 0;
    launch->pass_error[0] = '\0';
    return HUSH_OK;
}

hush_status_t hush_launch_set_profile(hush_launch_t *launch,
                                      const hush_roster_profile_t *in)
{
    if (launch == NULL || in == NULL)
        return HUSH_ERR_ARG;
    if (!launch->logged_in)
        return HUSH_ERR_ARG;
    HUSH_TRY(hush_roster_set_profile(&launch->roster, in));
    return hush_launch_save_vibe(launch);
}

hush_status_t hush_launch_add_member(hush_launch_t *launch,
                                     const char *key,
                                     const char *name)
{
    if (launch == NULL || key == NULL)
        return HUSH_ERR_ARG;
    if (!launch->has_vibe)
        return HUSH_ERR_ARG;
    HUSH_TRY(hush_roster_add_member(&launch->roster, key, name));
    return hush_launch_save_vibe(launch);
}

hush_status_t hush_launch_add_agent(hush_launch_t *launch,
                                    hush_store_t *store,
                                    const hush_roster_agent_in_t *in,
                                    int save_pass)
{
    if (launch == NULL || store == NULL || in == NULL)
        return HUSH_ERR_ARG;
    if (!launch->has_vibe || !launch->logged_in)
        return HUSH_ERR_ARG;
    HUSH_TRY(hush_roster_add_agent(&launch->roster, store, in, save_pass));
    return hush_launch_save_vibe(launch);
}

hush_status_t hush_launch_remove_agent(hush_launch_t *launch, const char *slug)
{
    if (launch == NULL || slug == NULL)
        return HUSH_ERR_ARG;
    if (!launch->has_vibe || !launch->logged_in)
        return HUSH_ERR_ARG;
    HUSH_TRY(hush_roster_remove_agent(&launch->roster, slug));
    return hush_launch_save_vibe(launch);
}

hush_status_t hush_launch_create_vibe(hush_launch_t *launch,
                                      hush_store_t *store,
                                      const char *name,
                                      const char *about)
{
    if (launch == NULL || store == NULL)
        return HUSH_ERR_ARG;
    if (!launch->logged_in)
        return HUSH_ERR_ARG;
    hush_launch_copy_name(launch->vibe_name, sizeof(launch->vibe_name),
                          name, HUSH_LAUNCH_DEFAULT_VIBE);
    hush_launch_copy_name(launch->vibe_about, sizeof(launch->vibe_about),
                          about, "Primary Hush endpoint.");
    launch->vibe_public = 1;
    if (hush_launch_make_token(launch->vibe_token,
                               sizeof(launch->vibe_token)) != HUSH_OK)
        return HUSH_ERR_IO;
    launch->nchannels = 0;
    launch->nprojects = 0;
    hush_identity_clear(&launch->payne);
    if (hush_launch_seed_hive(launch, store) != HUSH_OK)
        return HUSH_ERR_CRYPTO;
    launch->has_vibe = 1;
    return hush_launch_save_vibe(launch);
}

hush_status_t hush_launch_set_vibe_visibility(hush_launch_t *launch,
                                              int is_public)
{
    if (launch == NULL)
        return HUSH_ERR_ARG;
    if (!launch->has_vibe)
        return HUSH_ERR_ARG;
    launch->vibe_public = is_public ? 1 : 0;
    if (!launch->vibe_public && launch->vibe_token[0] == '\0') {
        if (hush_launch_make_token(launch->vibe_token,
                                   sizeof(launch->vibe_token)) != HUSH_OK)
            return HUSH_ERR_IO;
    }
    return hush_launch_save_vibe(launch);
}

hush_status_t hush_launch_add_channel(hush_launch_t *launch, const char *name)
{
    char slug[HUSH_LAUNCH_NAME_MAX];

    if (launch == NULL || name == NULL)
        return HUSH_ERR_ARG;
    if (!launch->has_vibe)
        return HUSH_ERR_ARG;
    hush_launch_slugify(slug, sizeof(slug), name);
    if (slug[0] == '\0')
        return HUSH_ERR_PARSE;
    if (hush_launch_has_channel(launch, slug))
        return HUSH_OK;
    HUSH_TRY(hush_launch_push_channel(launch, name));
    return hush_launch_save_vibe(launch);
}

hush_status_t hush_launch_remove_channel(hush_launch_t *launch, const char *slug)
{
    size_t i;

    if (launch == NULL || slug == NULL || slug[0] == '\0')
        return HUSH_ERR_ARG;
    if (!launch->has_vibe)
        return HUSH_ERR_ARG;
    if (launch->nchannels <= 1)
        return HUSH_ERR_DENIED;
    for (i = 0; i < launch->nchannels; ++i) {
        if (strcmp(launch->channels[i].slug, slug) != 0)
            continue;
        if (i + 1 < launch->nchannels)
            memmove(&launch->channels[i], &launch->channels[i + 1],
                    (launch->nchannels - i - 1) * sizeof(launch->channels[0]));
        launch->nchannels--;
        memset(&launch->channels[launch->nchannels], 0,
               sizeof(launch->channels[0]));
        return hush_launch_save_vibe(launch);
    }
    return HUSH_ERR_NOT_FOUND;
}

hush_status_t hush_launch_add_group(hush_launch_t *launch, const char *name)
{
    hush_launch_group_t *group;

    if (launch == NULL || name == NULL)
        return HUSH_ERR_ARG;
    if (!launch->has_vibe)
        return HUSH_ERR_ARG;
    if (launch->ngroups >= (size_t)HUSH_LAUNCH_GROUPS_MAX)
        return HUSH_ERR_FULL;
    group = &launch->groups[launch->ngroups];
    memset(group, 0, sizeof(*group));
    hush_launch_copy_name(group->name, sizeof(group->name), name, "group");
    if (hush_launch_make_uuid(group->id, sizeof(group->id)) != HUSH_OK)
        return HUSH_ERR_IO;
    launch->ngroups++;
    return hush_launch_save_vibe(launch);
}

hush_status_t hush_launch_set_channel_group(hush_launch_t *launch,
                                            const char *slug,
                                            const char *group_id)
{
    hush_launch_channel_t *ch;

    if (launch == NULL || slug == NULL)
        return HUSH_ERR_ARG;
    if (!launch->has_vibe)
        return HUSH_ERR_ARG;
    ch = hush_launch_find_channel(launch, slug);
    if (ch == NULL)
        return HUSH_ERR_NOT_FOUND;
    if (group_id == NULL || group_id[0] == '\0') {
        ch->group_id[0] = '\0';
        return hush_launch_save_vibe(launch);
    }
    if (!hush_launch_has_group_id(launch, group_id))
        return HUSH_ERR_NOT_FOUND;
    hush_launch_copy_name(ch->group_id, sizeof(ch->group_id), group_id, "");
    return hush_launch_save_vibe(launch);
}

hush_status_t hush_launch_set_channel_roster(hush_launch_t *launch,
                                             const char *slug,
                                             const char *const *humans,
                                             size_t nhumans,
                                             const char *const *robots,
                                             size_t nrobots)
{
    hush_launch_channel_t *ch;
    size_t i;

    if (launch == NULL || slug == NULL)
        return HUSH_ERR_ARG;
    if (!launch->has_vibe)
        return HUSH_ERR_ARG;
    if (nhumans > (size_t)HUSH_LAUNCH_CHAN_HUMANS_MAX)
        return HUSH_ERR_FULL;
    if (nrobots > (size_t)HUSH_LAUNCH_CHAN_ROBOTS_MAX)
        return HUSH_ERR_FULL;
    if (nhumans > 0 && humans == NULL)
        return HUSH_ERR_ARG;
    if (nrobots > 0 && robots == NULL)
        return HUSH_ERR_ARG;
    ch = hush_launch_find_channel(launch, slug);
    if (ch == NULL)
        return HUSH_ERR_NOT_FOUND;
    ch->nhumans = 0;
    ch->nrobots = 0;
    memset(ch->humans, 0, sizeof(ch->humans));
    memset(ch->robots, 0, sizeof(ch->robots));
    for (i = 0; i < nhumans; ++i) {
        if (humans[i] == NULL || humans[i][0] == '\0')
            return HUSH_ERR_PARSE;
        hush_launch_copy_name(ch->humans[ch->nhumans],
                              sizeof(ch->humans[0]), humans[i], "");
        ch->nhumans++;
    }
    for (i = 0; i < nrobots; ++i) {
        if (robots[i] == NULL || robots[i][0] == '\0')
            return HUSH_ERR_PARSE;
        if (!hush_launch_has_robot(launch, robots[i]))
            return HUSH_ERR_NOT_FOUND;
        hush_launch_copy_name(ch->robots[ch->nrobots],
                              sizeof(ch->robots[0]), robots[i], "");
        ch->nrobots++;
    }
    return hush_launch_save_vibe(launch);
}

hush_status_t hush_launch_add_project(hush_launch_t *launch,
                                      hush_store_t *store,
                                      const char *name,
                                      const char *path,
                                      int init_git)
{
    hush_launch_project_t *proj;

    if (launch == NULL || store == NULL || name == NULL)
        return HUSH_ERR_ARG;
    if (!launch->has_vibe)
        return HUSH_ERR_ARG;
    if (launch->nprojects >= (size_t)HUSH_LAUNCH_PROJECTS_MAX)
        return HUSH_ERR_FULL;
    proj = &launch->projects[launch->nprojects];
    memset(proj, 0, sizeof(*proj));
    hush_launch_copy_name(proj->name, sizeof(proj->name), name, "project");
    hush_launch_slugify(proj->slug, sizeof(proj->slug), proj->name);
    if (path != NULL && path[0] != '\0')
        hush_launch_copy_name(proj->path, sizeof(proj->path), path, "");
    if (init_git && proj->path[0] != '\0') {
        if (hush_launch_git_init(proj->path) != HUSH_OK)
            return HUSH_ERR_IO;
    }
    if (hush_launch_store_repo(store, &launch->human, proj) != HUSH_OK)
        return HUSH_ERR_FULL;
    launch->nprojects++;
    return hush_launch_save_vibe(launch);
}

hush_status_t hush_launch_format_session(const hush_launch_t *launch,
                                         uint16_t port,
                                         char *out, size_t outsz,
                                         size_t *out_len)
{
    size_t off = 0;
    hush_status_t st;

    if (launch == NULL || out == NULL || outsz == 0)
        return HUSH_ERR_ARG;
    st = hush_launch_format_head(launch, port, out, outsz, &off);
    if (st != HUSH_OK)
        return st;
    st = hush_launch_format_channels(launch, out, outsz, &off);
    if (st != HUSH_OK)
        return st;
    st = hush_launch_format_groups(launch, out, outsz, &off);
    if (st != HUSH_OK)
        return st;
    st = hush_launch_format_projects(launch, out, outsz, &off);
    if (st != HUSH_OK)
        return st;
    st = hush_launch_format_roster(launch, out, outsz, &off);
    if (st != HUSH_OK)
        return st;
    if (out_len != NULL)
        *out_len = off;
    return HUSH_OK;
}

int hush_launch_is_ready(const hush_launch_t *launch)
{
    if (launch == NULL)
        return 0;
    return launch->logged_in && launch->backup_acked && launch->has_vibe;
}

static void hush_launch_copy_name(char *dst, size_t dstsz,
                                  const char *text, const char *fallback)
{
    size_t i = 0;

    assert(dst != NULL);
    assert(dstsz > 0);
    assert(fallback != NULL);
    if (text != NULL) {
        while (text[i] != '\0' && isspace((unsigned char)text[i]))
            text++;
        while (text[i] != '\0' && i + 1 < dstsz) {
            dst[i] = text[i];
            i++;
        }
        while (i > 0 && isspace((unsigned char)dst[i - 1]))
            i--;
    }
    dst[i] = '\0';
    if (dst[0] == '\0') {
        strncpy(dst, fallback, dstsz - 1);
        dst[dstsz - 1] = '\0';
    }
}

static void hush_launch_slugify(char *dst, size_t dstsz, const char *name)
{
    size_t i = 0;
    size_t o = 0;
    int dash = 0;

    assert(dst != NULL);
    assert(dstsz > 0);
    if (name == NULL)
        name = "";
    while (name[i] != '\0' && o + 1 < dstsz) {
        unsigned char c = (unsigned char)name[i++];

        if (isalnum(c)) {
            dst[o++] = (char)tolower(c);
            dash = 0;
        } else if (!dash && o > 0) {
            dst[o++] = '-';
            dash = 1;
        }
    }
    if (o > 0 && dst[o - 1] == '-')
        o--;
    dst[o] = '\0';
    if (dst[0] == '\0' && dstsz > 1) {
        dst[0] = (char)HUSH_LAUNCH_SLUG_FALLBACK;
        dst[1] = '\0';
    }
}

static int hush_launch_has_channel(const hush_launch_t *launch, const char *slug)
{
    size_t i;

    assert(launch != NULL);
    assert(slug != NULL);
    for (i = 0; i < launch->nchannels; ++i) {
        if (strcmp(launch->channels[i].slug, slug) == 0)
            return 1;
    }
    return 0;
}

static hush_launch_channel_t *hush_launch_find_channel(hush_launch_t *launch,
                                                       const char *slug)
{
    size_t i;

    assert(launch != NULL);
    assert(slug != NULL);
    for (i = 0; i < launch->nchannels; ++i) {
        if (strcmp(launch->channels[i].slug, slug) == 0)
            return &launch->channels[i];
    }
    return NULL;
}

static int hush_launch_has_group_id(const hush_launch_t *launch,
                                    const char *group_id)
{
    size_t i;

    assert(launch != NULL);
    if (group_id == NULL || group_id[0] == '\0')
        return 0;
    for (i = 0; i < launch->ngroups; ++i) {
        if (strcmp(launch->groups[i].id, group_id) == 0)
            return 1;
    }
    return 0;
}

static int hush_launch_has_robot(const hush_launch_t *launch, const char *slug)
{
    size_t i;

    assert(launch != NULL);
    assert(slug != NULL);
    if (strcmp(slug, HUSH_LAUNCH_PAYNE_SLUG) == 0)
        return 1;
    for (i = 0; i < launch->roster.nagents; ++i) {
        if (strcmp(launch->roster.agents[i].slug, slug) == 0)
            return 1;
    }
    return 0;
}

static hush_status_t hush_launch_make_uuid(char *out, size_t outsz)
{
    unsigned char raw[HUSH_LAUNCH_UUID_RAW];
    static const char hex[] = "0123456789abcdef";
    int fd;
    ssize_t n;
    size_t i;

    assert(out != NULL);
    if (outsz < (size_t)HUSH_LAUNCH_ID_HEX + 1)
        return HUSH_ERR_ARG;
    fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0)
        return HUSH_ERR_IO;
    n = read(fd, raw, sizeof(raw));
    close(fd);
    if (n != (ssize_t)sizeof(raw))
        return HUSH_ERR_IO;
    for (i = 0; i < sizeof(raw); ++i) {
        out[i * 2] = hex[(raw[i] >> 4) & 0x0f];
        out[i * 2 + 1] = hex[raw[i] & 0x0f];
    }
    out[HUSH_LAUNCH_ID_HEX] = '\0';
    return HUSH_OK;
}

static hush_status_t hush_launch_ensure_channel_id(hush_launch_channel_t *ch)
{
    assert(ch != NULL);
    if (ch->id[0] != '\0')
        return HUSH_OK;
    return hush_launch_make_uuid(ch->id, sizeof(ch->id));
}

static hush_status_t hush_launch_ensure_ids(hush_launch_t *launch)
{
    size_t i;
    int dirty = 0;

    assert(launch != NULL);
    for (i = 0; i < launch->nchannels; ++i) {
        if (launch->channels[i].id[0] != '\0')
            continue;
        if (hush_launch_ensure_channel_id(&launch->channels[i]) != HUSH_OK)
            return HUSH_ERR_IO;
        dirty = 1;
    }
    if (dirty)
        return hush_launch_save_vibe(launch);
    return HUSH_OK;
}

static hush_status_t hush_launch_push_channel(hush_launch_t *launch,
                                              const char *name)
{
    hush_launch_channel_t *ch;

    assert(launch != NULL);
    assert(name != NULL);
    if (launch->nchannels >= (size_t)HUSH_LAUNCH_CHANNELS_MAX)
        return HUSH_ERR_FULL;
    ch = &launch->channels[launch->nchannels];
    memset(ch, 0, sizeof(*ch));
    hush_launch_copy_name(ch->name, sizeof(ch->name), name, "channel");
    hush_launch_slugify(ch->slug, sizeof(ch->slug), ch->name);
    if (hush_launch_ensure_channel_id(ch) != HUSH_OK)
        return HUSH_ERR_IO;
    launch->nchannels++;
    return HUSH_OK;
}

static hush_status_t hush_launch_seed_hive(hush_launch_t *launch,
                                           hush_store_t *store)
{
    assert(launch != NULL);
    assert(store != NULL);
    if (hush_identity_generate(&launch->payne) != HUSH_OK)
        return HUSH_ERR_CRYPTO;
    if (launch->save_pass)
        hush_launch_try_save(launch, HUSH_PASS_PAYNE_NSEC, launch->payne.nsec);
    if (hush_launch_push_channel(launch, HUSH_LAUNCH_CHAN_GENERAL) != HUSH_OK)
        return HUSH_ERR_FULL;
    if (hush_launch_push_channel(launch, HUSH_LAUNCH_CHAN_WELCOME) != HUSH_OK)
        return HUSH_ERR_FULL;
    if (hush_launch_push_channel(launch, HUSH_LAUNCH_CHAN_AGENTS) != HUSH_OK)
        return HUSH_ERR_FULL;
    if (hush_launch_store_profile(store, &launch->human, "you",
                                  "hive operator") != HUSH_OK)
        return HUSH_ERR_FULL;
    if (hush_launch_store_profile(store, &launch->payne,
                                  HUSH_LAUNCH_PAYNE_NAME,
                                  HUSH_LAUNCH_PAYNE_ABOUT) != HUSH_OK)
        return HUSH_ERR_FULL;
    return hush_launch_store_welcome(store, &launch->payne);
}

static hush_status_t hush_launch_store_profile(hush_store_t *store,
                                               const hush_identity_t *id,
                                               const char *name,
                                               const char *about)
{
    hush_event_t ev;
    char content[HUSH_EVENT_MAX_CONTENT];
    char esc_name[HUSH_LAUNCH_NAME_MAX * 2];
    char esc_about[HUSH_LAUNCH_ABOUT_MAX * 2];

    assert(store != NULL);
    assert(id != NULL);
    assert(name != NULL);
    assert(about != NULL);
    hush_launch_json_escape(name, esc_name, sizeof(esc_name));
    hush_launch_json_escape(about, esc_about, sizeof(esc_about));
    if (snprintf(content, sizeof(content),
                 "{\"name\":\"%s\",\"about\":\"%s\"}",
                 esc_name, esc_about) >= (int)sizeof(content))
        return HUSH_ERR_FULL;
    hush_launch_fill_event(&ev, id->pubkey_hex, HUSH_LAUNCH_KIND_META,
                           content, "");
    return hush_store_insert(store, &ev);
}

static hush_status_t hush_launch_store_welcome(hush_store_t *store,
                                               const hush_identity_t *payne)
{
    hush_event_t ev;

    assert(store != NULL);
    assert(payne != NULL);
    hush_launch_fill_event(&ev, payne->pubkey_hex, HUSH_LAUNCH_KIND_NOTE,
                           "At ease. I'm Sgt Major Payne. Tell me what you "
                           "want built and I'll find — or raise — the right "
                           "robot for the job.",
                           HUSH_LAUNCH_CHAN_WELCOME);
    return hush_store_insert(store, &ev);
}

static hush_status_t hush_launch_store_repo(hush_store_t *store,
                                            const hush_identity_t *human,
                                            const hush_launch_project_t *proj)
{
    hush_event_t ev;
    char content[HUSH_EVENT_MAX_CONTENT];

    assert(store != NULL);
    assert(human != NULL);
    assert(proj != NULL);
    if (snprintf(content, sizeof(content),
                 "{\"d\":\"%s\",\"name\":\"%s\",\"clone\":\"%s\"}",
                 proj->slug, proj->name, proj->path) >= (int)sizeof(content))
        return HUSH_ERR_FULL;
    hush_launch_fill_event(&ev, human->pubkey_hex, HUSH_LAUNCH_KIND_REPO,
                           content, "");
    ev.tag_count = 1;
    memcpy(ev.tags[0][0], "d", 2);
    memcpy(ev.tags[0][1], proj->slug, strlen(proj->slug) + 1);
    return hush_store_insert(store, &ev);
}

static void hush_launch_fill_event(hush_event_t *ev, const char *pubkey_hex,
                                   uint32_t kind, const char *content,
                                   const char *channel)
{
    assert(ev != NULL);
    assert(pubkey_hex != NULL);
    assert(content != NULL);
    memset(ev, 0, sizeof(*ev));
    hush_launch_make_id(ev->id);
    memcpy(ev->pubkey, pubkey_hex, HUSH_IDENTITY_HEX_LEN + 1);
    ev->kind = kind;
    ev->created_at = (int64_t)time(NULL);
    memcpy(ev->content, content, strlen(content) + 1);
    if (channel != NULL && channel[0] != '\0') {
        ev->tag_count = 1;
        memcpy(ev->tags[0][0], "h", 2);
        memcpy(ev->tags[0][1], channel, strlen(channel) + 1);
    }
}

static void hush_launch_make_id(char *out65)
{
    static unsigned seq;
    time_t now;

    assert(out65 != NULL);
    now = time(NULL);
    seq++;
    (void)snprintf(out65, HUSH_EVENT_ID_HEX_LEN + 1,
                   "%0*llx%0*x%0*x%0*x",
                   HUSH_LAUNCH_ID_WIDTH, (unsigned long long)now,
                   HUSH_LAUNCH_ID_WIDTH, seq,
                   HUSH_LAUNCH_ID_WIDTH, seq ^ 0x9e3779b9u,
                   HUSH_LAUNCH_ID_WIDTH, seq * 3u);
}

static hush_status_t hush_launch_format_head(const hush_launch_t *launch,
                                             uint16_t port,
                                             char *out, size_t outsz,
                                             size_t *off)
{
    char esc_vibe[HUSH_LAUNCH_NAME_MAX * 2];
    char esc_about[HUSH_LAUNCH_ABOUT_MAX * 2];
    int n;

    assert(launch != NULL);
    assert(out != NULL);
    assert(off != NULL);
    hush_launch_json_escape(launch->vibe_name, esc_vibe, sizeof(esc_vibe));
    hush_launch_json_escape(launch->vibe_about, esc_about, sizeof(esc_about));
    n = snprintf(out, outsz,
                 "{\"ok\":true,\"logged_in\":%s,\"backup_acked\":%s,"
                 "\"has_vibe\":%s,\"ready\":%s,\"save_pass\":%s,"
                 "\"pass_saved\":%s,\"pass_error\":\"%s\",\"port\":%u,"
                 "\"npub\":\"%s\",\"pubkey\":\"%s\",\"nsec\":\"%s\","
                 "\"vibe\":{\"name\":\"%s\",\"about\":\"%s\","
                 "\"visibility\":\"%s\",\"discoverable\":%s,"
                 "\"join_token\":\"%s\"},"
                 "\"payne\":{\"name\":\"%s\",\"npub\":\"%s\","
                 "\"about\":\"%s\"},\"channels\":[",
                 launch->logged_in ? "true" : "false",
                 launch->backup_acked ? "true" : "false",
                 launch->has_vibe ? "true" : "false",
                 hush_launch_is_ready(launch) ? "true" : "false",
                 launch->save_pass ? "true" : "false",
                 launch->pass_saved ? "true" : "false",
                 launch->pass_error,
                 (unsigned)port,
                 launch->logged_in ? launch->human.npub : "",
                 launch->logged_in ? launch->human.pubkey_hex : "",
                 (launch->logged_in && !launch->backup_acked)
                     ? launch->human.nsec : "",
                 esc_vibe, esc_about,
                 launch->vibe_public ? "public" : "private",
                 launch->vibe_public ? "true" : "false",
                 launch->has_vibe ? launch->vibe_token : "",
                 launch->has_vibe ? HUSH_LAUNCH_PAYNE_NAME : "",
                 launch->has_vibe ? launch->payne.npub : "",
                 launch->has_vibe ? HUSH_LAUNCH_PAYNE_ABOUT : "");
    if (n < 0 || (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    *off = (size_t)n;
    return HUSH_OK;
}

static hush_status_t hush_launch_format_channels(const hush_launch_t *launch,
                                                 char *out, size_t outsz,
                                                 size_t *off)
{
    char esc_name[HUSH_LAUNCH_NAME_MAX * 2];
    size_t i;
    int n;

    assert(launch != NULL);
    assert(out != NULL);
    assert(off != NULL);
    for (i = 0; i < launch->nchannels; ++i) {
        if (*off + 96 >= outsz)
            return HUSH_ERR_FULL;
        hush_launch_json_escape(launch->channels[i].name, esc_name,
                                sizeof(esc_name));
        n = snprintf(out + *off, outsz - *off,
                     "%s{\"name\":\"%s\",\"slug\":\"%s\",\"id\":\"%s\","
                     "\"group_id\":\"%s\"",
                     (i == 0) ? "" : ",",
                     esc_name, launch->channels[i].slug,
                     launch->channels[i].id, launch->channels[i].group_id);
        if (n < 0)
            return HUSH_ERR_FULL;
        *off += (size_t)n;
        HUSH_TRY(hush_launch_format_channel_lists(&launch->channels[i],
                                                  out, outsz, off));
        if (*off + 1 >= outsz)
            return HUSH_ERR_FULL;
        out[(*off)++] = '}';
        out[*off] = '\0';
    }
    return HUSH_OK;
}

static hush_status_t hush_launch_format_channel_lists(
    const hush_launch_channel_t *ch, char *out, size_t outsz, size_t *off)
{
    char esc[HUSH_IDENTITY_NPUB_MAX * 2];
    size_t i;
    int n;

    assert(ch != NULL);
    assert(out != NULL);
    assert(off != NULL);
    n = snprintf(out + *off, outsz - *off, ",\"humans\":[");
    if (n < 0 || *off + (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    *off += (size_t)n;
    for (i = 0; i < ch->nhumans; ++i) {
        hush_launch_json_escape(ch->humans[i], esc, sizeof(esc));
        n = snprintf(out + *off, outsz - *off, "%s\"%s\"",
                     (i == 0) ? "" : ",", esc);
        if (n < 0 || *off + (size_t)n >= outsz)
            return HUSH_ERR_FULL;
        *off += (size_t)n;
    }
    n = snprintf(out + *off, outsz - *off, "],\"robots\":[");
    if (n < 0 || *off + (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    *off += (size_t)n;
    for (i = 0; i < ch->nrobots; ++i) {
        hush_launch_json_escape(ch->robots[i], esc, sizeof(esc));
        n = snprintf(out + *off, outsz - *off, "%s\"%s\"",
                     (i == 0) ? "" : ",", esc);
        if (n < 0 || *off + (size_t)n >= outsz)
            return HUSH_ERR_FULL;
        *off += (size_t)n;
    }
    if (*off + 1 >= outsz)
        return HUSH_ERR_FULL;
    out[(*off)++] = ']';
    out[*off] = '\0';
    return HUSH_OK;
}

static hush_status_t hush_launch_format_groups(const hush_launch_t *launch,
                                               char *out, size_t outsz,
                                               size_t *off)
{
    char esc_name[HUSH_LAUNCH_NAME_MAX * 2];
    size_t i;
    int n;

    assert(launch != NULL);
    assert(out != NULL);
    assert(off != NULL);
    if (*off + 14 >= outsz)
        return HUSH_ERR_FULL;
    memcpy(out + *off, "],\"groups\":[", 12);
    *off += 12;
    for (i = 0; i < launch->ngroups; ++i) {
        hush_launch_json_escape(launch->groups[i].name, esc_name,
                                sizeof(esc_name));
        n = snprintf(out + *off, outsz - *off,
                     "%s{\"name\":\"%s\",\"id\":\"%s\"}",
                     (i == 0) ? "" : ",",
                     esc_name, launch->groups[i].id);
        if (n < 0 || *off + (size_t)n >= outsz)
            return HUSH_ERR_FULL;
        *off += (size_t)n;
    }
    out[*off] = '\0';
    return HUSH_OK;
}

static hush_status_t hush_launch_format_projects(const hush_launch_t *launch,
                                                 char *out, size_t outsz,
                                                 size_t *off)
{
    char esc_name[HUSH_LAUNCH_NAME_MAX * 2];
    char esc_path[HUSH_LAUNCH_PATH_MAX * 2];
    size_t i;
    int n;

    assert(launch != NULL);
    assert(out != NULL);
    assert(off != NULL);
    if (*off + 16 >= outsz)
        return HUSH_ERR_FULL;
    memcpy(out + *off, "],\"projects\":[", 14);
    *off += 14;
    for (i = 0; i < launch->nprojects; ++i) {
        if (*off + 64 >= outsz)
            return HUSH_ERR_FULL;
        hush_launch_json_escape(launch->projects[i].name, esc_name,
                                sizeof(esc_name));
        hush_launch_json_escape(launch->projects[i].path, esc_path,
                                sizeof(esc_path));
        n = snprintf(out + *off, outsz - *off,
                     "%s{\"name\":\"%s\",\"slug\":\"%s\",\"path\":\"%s\"}",
                     (i == 0) ? "" : ",",
                     esc_name, launch->projects[i].slug, esc_path);
        if (n < 0)
            return HUSH_ERR_FULL;
        *off += (size_t)n;
    }
    if (*off + 2 >= outsz)
        return HUSH_ERR_FULL;
    out[(*off)++] = ']';
    out[*off] = '\0';
    return HUSH_OK;
}

static hush_status_t hush_launch_format_roster(const hush_launch_t *launch,
                                               char *out, size_t outsz,
                                               size_t *off)
{
    size_t n = 0;
    size_t room;

    assert(launch != NULL);
    assert(out != NULL);
    assert(off != NULL);
    if (*off + 3 >= outsz)
        return HUSH_ERR_FULL;
    room = outsz - *off;
    if (hush_roster_format_json(&launch->roster, out + *off, room, &n) != HUSH_OK)
        return HUSH_ERR_FULL;
    *off += n;
    if (*off + 2 >= outsz)
        return HUSH_ERR_FULL;
    out[(*off)++] = '}';
    out[(*off)++] = '\n';
    out[*off] = '\0';
    return HUSH_OK;
}

static hush_status_t hush_launch_git_init(const char *path)
{
    char cmd[HUSH_LAUNCH_CMD_MAX];
    char gitdir[HUSH_LAUNCH_PATH_MAX + 8];
    struct stat st;

    assert(path != NULL);
    if (snprintf(gitdir, sizeof(gitdir), "%s/.git", path) >= (int)sizeof(gitdir))
        return HUSH_ERR_ARG;
    if (stat(gitdir, &st) == 0)
        return HUSH_OK;
    if (snprintf(cmd, sizeof(cmd), "mkdir -p '%s' && git init -q '%s'",
                 path, path) >= (int)sizeof(cmd))
        return HUSH_ERR_ARG;
    if (system(cmd) < 0 && stat(gitdir, &st) != 0)
        return HUSH_ERR_IO;
    if (stat(gitdir, &st) == 0)
        return HUSH_OK;
    return HUSH_ERR_IO;
}

static void hush_launch_try_save(hush_launch_t *launch, const char *path,
                                 const char *secret)
{
    assert(launch != NULL);
    assert(path != NULL);
    assert(secret != NULL);
    if (hush_pass_save(path, secret) == HUSH_OK) {
        launch->pass_saved = 1;
        launch->pass_error[0] = '\0';
        return;
    }
    launch->pass_saved = 0;
    hush_pass_last_error(launch->pass_error, sizeof(launch->pass_error));
    if (launch->pass_error[0] == '\0')
        memcpy(launch->pass_error, HUSH_LAUNCH_PASS_FAIL,
               sizeof(HUSH_LAUNCH_PASS_FAIL));
}

static size_t hush_launch_json_escape(const char *src, char *dst, size_t dstsz)
{
    size_t o = 0;

    assert(dst != NULL);
    assert(dstsz > 0);
    if (src == NULL)
        src = "";
    while (*src != '\0' && o + 2 < dstsz) {
        if (*src == '"' || *src == '\\') {
            dst[o++] = '\\';
            dst[o++] = *src++;
        } else if (*src == '\n') {
            dst[o++] = '\\';
            dst[o++] = 'n';
            src++;
        } else {
            dst[o++] = *src++;
        }
    }
    dst[o] = '\0';
    return o;
}

static hush_status_t hush_launch_make_token(char *out, size_t outsz)
{
    unsigned char raw[8];
    static const char hex[] = "0123456789abcdef";
    int fd;
    ssize_t n;
    size_t i;

    assert(out != NULL);
    if (outsz < 17)
        return HUSH_ERR_ARG;
    fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0)
        return HUSH_ERR_IO;
    n = read(fd, raw, sizeof(raw));
    close(fd);
    if (n != (ssize_t)sizeof(raw))
        return HUSH_ERR_IO;
    for (i = 0; i < sizeof(raw); ++i) {
        out[i * 2] = hex[(raw[i] >> 4) & 0x0f];
        out[i * 2 + 1] = hex[raw[i] & 0x0f];
    }
    out[16] = '\0';
    return HUSH_OK;
}

static void hush_launch_config_dir(char *out, size_t outsz)
{
    const char *env;
    const char *xdg;
    const char *home;
    int n;

    assert(out != NULL);
    assert(outsz > 0);
    env = getenv(HUSH_LAUNCH_ENV_CONFIG);
    if (env != NULL && env[0] != '\0') {
        hush_launch_copy_name(out, outsz, env, "");
        return;
    }
    xdg = getenv("XDG_CONFIG_HOME");
    if (xdg != NULL && xdg[0] != '\0') {
        n = snprintf(out, outsz, "%s/hush", xdg);
        if (n > 0 && (size_t)n < outsz)
            return;
    }
    home = getenv("HOME");
    if (home != NULL && home[0] != '\0') {
        n = snprintf(out, outsz, "%s/.config/hush", home);
        if (n > 0 && (size_t)n < outsz)
            return;
    }
    hush_launch_copy_name(out, outsz, "/tmp/hush", "");
}

static void hush_launch_vibe_path(char *out, size_t outsz)
{
    char dir[HUSH_LAUNCH_PATH_MAX];
    int n;

    assert(out != NULL);
    assert(outsz > 0);
    hush_launch_config_dir(dir, sizeof(dir));
    n = snprintf(out, outsz, "%s/%s", dir, HUSH_LAUNCH_VIBE_FILE);
    if (n <= 0 || (size_t)n >= outsz)
        out[0] = '\0';
}

static hush_status_t hush_launch_ensure_config_dir(void)
{
    char dir[HUSH_LAUNCH_PATH_MAX];

    hush_launch_config_dir(dir, sizeof(dir));
    if (dir[0] == '\0')
        return HUSH_ERR_IO;
    if (mkdir(dir, 0700) != 0 && errno != EEXIST)
        return HUSH_ERR_IO;
    return HUSH_OK;
}

static hush_status_t hush_launch_read_vibe_file(char *buf, size_t bufsz)
{
    char path[HUSH_LAUNCH_PATH_MAX];
    FILE *fp;
    size_t n;

    assert(buf != NULL);
    assert(bufsz > 0);
    buf[0] = '\0';
    hush_launch_vibe_path(path, sizeof(path));
    if (path[0] == '\0')
        return HUSH_ERR_IO;
    fp = fopen(path, "r");
    if (fp == NULL)
        return HUSH_ERR_NOT_FOUND;
    n = fread(buf, 1, bufsz - 1, fp);
    buf[n] = '\0';
    fclose(fp);
    return HUSH_OK;
}

static hush_status_t hush_launch_flush_tmp(int fd, const char *tmp,
                                           const char *buf)
{
    size_t len;
    ssize_t wr;

    assert(tmp != NULL);
    assert(buf != NULL);
    len = strlen(buf);
    wr = write(fd, buf, len);
    if (wr != (ssize_t)len || close(fd) != 0) {
        close(fd);
        unlink(tmp);
        return HUSH_ERR_IO;
    }
    return HUSH_OK;
}

static hush_status_t hush_launch_write_vibe_file(const char *buf)
{
    char path[HUSH_LAUNCH_PATH_MAX];
    char tmp[HUSH_LAUNCH_PATH_MAX];
    int fd;
    int n;

    if (buf == NULL)
        return HUSH_ERR_ARG;
    if (hush_launch_ensure_config_dir() != HUSH_OK)
        return HUSH_ERR_IO;
    hush_launch_vibe_path(path, sizeof(path));
    if (path[0] == '\0')
        return HUSH_ERR_IO;
    n = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (n <= 0 || (size_t)n >= sizeof(tmp))
        return HUSH_ERR_IO;
    fd = open(tmp, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (fd < 0)
        return HUSH_ERR_IO;
    if (hush_launch_flush_tmp(fd, tmp, buf) != HUSH_OK)
        return HUSH_ERR_IO;
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return HUSH_ERR_IO;
    }
    return HUSH_OK;
}

static int hush_launch_json_string(const char *json, const char *key,
                                   char *out, size_t outsz)
{
    char quoted[HUSH_LAUNCH_KEY_MAX + 8];
    const char *p;
    size_t i = 0;

    assert(out != NULL);
    assert(outsz > 0);
    out[0] = '\0';
    if (json == NULL || key == NULL)
        return 0;
    if (snprintf(quoted, sizeof(quoted), "\"%s\":\"", key)
        >= (int)sizeof(quoted))
        return 0;
    p = strstr(json, quoted);
    if (p == NULL)
        return 0;
    p += strlen(quoted);
    while (*p != '\0' && *p != '"' && i + 1 < outsz) {
        if (*p == '\\' && p[1] != '\0')
            p++;
        out[i++] = *p++;
    }
    out[i] = '\0';
    return out[0] != '\0';
}

static size_t hush_launch_json_count(const char *json, const char *key,
                                     size_t maxv)
{
    char text[HUSH_LAUNCH_COUNT_MAX];
    unsigned long val;
    char *end = NULL;

    if (!hush_launch_json_string(json, key, text, sizeof(text)))
        return 0;
    val = strtoul(text, &end, 10);
    if (end == text)
        return 0;
    if (val > (unsigned long)maxv)
        return maxv;
    return (size_t)val;
}

static void hush_launch_index_key(char *out, size_t outsz,
                                  const char *stem, size_t idx)
{
    assert(out != NULL);
    assert(outsz > 0);
    assert(stem != NULL);
    if (snprintf(out, outsz, "%s_%zu", stem, idx) >= (int)outsz)
        out[0] = '\0';
}

static hush_status_t hush_launch_put_field(char *out, size_t outsz, size_t *off,
                                           const char *key, const char *val)
{
    char esc[HUSH_ROSTER_PROMPT_MAX * 2];
    int n;

    assert(out != NULL);
    assert(off != NULL);
    assert(key != NULL);
    hush_launch_json_escape(val != NULL ? val : "", esc, sizeof(esc));
    n = snprintf(out + *off, outsz - *off, "\"%s\":\"%s\",", key, esc);
    if (n < 0 || *off + (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    *off += (size_t)n;
    return HUSH_OK;
}

static hush_status_t hush_launch_put_vibe_head(const hush_launch_t *launch,
                                               char *out, size_t outsz,
                                               size_t *off)
{
    char flag[2];

    assert(launch != NULL);
    flag[0] = launch->vibe_public ? '1' : '0';
    flag[1] = '\0';
    HUSH_TRY(hush_launch_put_field(out, outsz, off, "version",
                                   HUSH_LAUNCH_VIBE_VERSION));
    HUSH_TRY(hush_launch_put_field(out, outsz, off, "vibe_name",
                                   launch->vibe_name));
    HUSH_TRY(hush_launch_put_field(out, outsz, off, "vibe_about",
                                   launch->vibe_about));
    HUSH_TRY(hush_launch_put_field(out, outsz, off, "vibe_public", flag));
    return hush_launch_put_field(out, outsz, off, "vibe_token",
                                 launch->vibe_token);
}

static hush_status_t hush_launch_put_channels(const hush_launch_t *launch,
                                              char *out, size_t outsz,
                                              size_t *off)
{
    char key[HUSH_LAUNCH_KEY_MAX];
    char count[HUSH_LAUNCH_COUNT_MAX];
    size_t i;

    assert(launch != NULL);
    if (snprintf(count, sizeof(count), "%zu", launch->nchannels)
        >= (int)sizeof(count))
        return HUSH_ERR_FULL;
    HUSH_TRY(hush_launch_put_field(out, outsz, off, "nchannels", count));
    for (i = 0; i < launch->nchannels; ++i) {
        hush_launch_index_key(key, sizeof(key), "channel_name", i);
        HUSH_TRY(hush_launch_put_field(out, outsz, off, key,
                                       launch->channels[i].name));
        hush_launch_index_key(key, sizeof(key), "channel_slug", i);
        HUSH_TRY(hush_launch_put_field(out, outsz, off, key,
                                       launch->channels[i].slug));
        hush_launch_index_key(key, sizeof(key), "channel_id", i);
        HUSH_TRY(hush_launch_put_field(out, outsz, off, key,
                                       launch->channels[i].id));
        hush_launch_index_key(key, sizeof(key), "channel_group", i);
        HUSH_TRY(hush_launch_put_field(out, outsz, off, key,
                                       launch->channels[i].group_id));
        HUSH_TRY(hush_launch_put_channel_lists(&launch->channels[i], i,
                                              out, outsz, off));
    }
    return HUSH_OK;
}

static hush_status_t hush_launch_put_channel_lists(
    const hush_launch_channel_t *ch, size_t idx,
    char *out, size_t outsz, size_t *off)
{
    char key[HUSH_LAUNCH_KEY_MAX];
    char stem[HUSH_LAUNCH_KEY_MAX];
    char count[HUSH_LAUNCH_COUNT_MAX];
    size_t i;

    assert(ch != NULL);
    if (snprintf(count, sizeof(count), "%zu", ch->nhumans) >= (int)sizeof(count))
        return HUSH_ERR_FULL;
    hush_launch_index_key(key, sizeof(key), "channel_nhumans", idx);
    HUSH_TRY(hush_launch_put_field(out, outsz, off, key, count));
    for (i = 0; i < ch->nhumans; ++i) {
        if (snprintf(stem, sizeof(stem), "channel_%zu_human", idx)
            >= (int)sizeof(stem))
            return HUSH_ERR_FULL;
        hush_launch_index_key(key, sizeof(key), stem, i);
        HUSH_TRY(hush_launch_put_field(out, outsz, off, key, ch->humans[i]));
    }
    if (snprintf(count, sizeof(count), "%zu", ch->nrobots) >= (int)sizeof(count))
        return HUSH_ERR_FULL;
    hush_launch_index_key(key, sizeof(key), "channel_nrobots", idx);
    HUSH_TRY(hush_launch_put_field(out, outsz, off, key, count));
    for (i = 0; i < ch->nrobots; ++i) {
        if (snprintf(stem, sizeof(stem), "channel_%zu_robot", idx)
            >= (int)sizeof(stem))
            return HUSH_ERR_FULL;
        hush_launch_index_key(key, sizeof(key), stem, i);
        HUSH_TRY(hush_launch_put_field(out, outsz, off, key, ch->robots[i]));
    }
    return HUSH_OK;
}

static hush_status_t hush_launch_put_groups(const hush_launch_t *launch,
                                            char *out, size_t outsz,
                                            size_t *off)
{
    char key[HUSH_LAUNCH_KEY_MAX];
    char count[HUSH_LAUNCH_COUNT_MAX];
    size_t i;

    assert(launch != NULL);
    if (snprintf(count, sizeof(count), "%zu", launch->ngroups)
        >= (int)sizeof(count))
        return HUSH_ERR_FULL;
    HUSH_TRY(hush_launch_put_field(out, outsz, off, "ngroups", count));
    for (i = 0; i < launch->ngroups; ++i) {
        hush_launch_index_key(key, sizeof(key), "group_name", i);
        HUSH_TRY(hush_launch_put_field(out, outsz, off, key,
                                       launch->groups[i].name));
        hush_launch_index_key(key, sizeof(key), "group_id", i);
        HUSH_TRY(hush_launch_put_field(out, outsz, off, key,
                                       launch->groups[i].id));
    }
    return HUSH_OK;
}

static hush_status_t hush_launch_put_projects(const hush_launch_t *launch,
                                              char *out, size_t outsz,
                                              size_t *off)
{
    char key[HUSH_LAUNCH_KEY_MAX];
    char count[HUSH_LAUNCH_COUNT_MAX];
    size_t i;

    assert(launch != NULL);
    if (snprintf(count, sizeof(count), "%zu", launch->nprojects)
        >= (int)sizeof(count))
        return HUSH_ERR_FULL;
    HUSH_TRY(hush_launch_put_field(out, outsz, off, "nprojects", count));
    for (i = 0; i < launch->nprojects; ++i) {
        hush_launch_index_key(key, sizeof(key), "project_name", i);
        HUSH_TRY(hush_launch_put_field(out, outsz, off, key,
                                       launch->projects[i].name));
        hush_launch_index_key(key, sizeof(key), "project_slug", i);
        HUSH_TRY(hush_launch_put_field(out, outsz, off, key,
                                       launch->projects[i].slug));
        hush_launch_index_key(key, sizeof(key), "project_path", i);
        HUSH_TRY(hush_launch_put_field(out, outsz, off, key,
                                       launch->projects[i].path));
    }
    return HUSH_OK;
}

static hush_status_t hush_launch_put_agents(const hush_launch_t *launch,
                                            char *out, size_t outsz,
                                            size_t *off)
{
    char key[HUSH_LAUNCH_KEY_MAX];
    char count[HUSH_LAUNCH_COUNT_MAX];
    size_t i;

    assert(launch != NULL);
    if (snprintf(count, sizeof(count), "%zu", launch->roster.nagents)
        >= (int)sizeof(count))
        return HUSH_ERR_FULL;
    HUSH_TRY(hush_launch_put_field(out, outsz, off, "nagents", count));
    for (i = 0; i < launch->roster.nagents; ++i) {
        hush_launch_index_key(key, sizeof(key), "agent_name", i);
        HUSH_TRY(hush_launch_put_field(out, outsz, off, key,
                                       launch->roster.agents[i].name));
        hush_launch_index_key(key, sizeof(key), "agent_slug", i);
        HUSH_TRY(hush_launch_put_field(out, outsz, off, key,
                                       launch->roster.agents[i].slug));
        hush_launch_index_key(key, sizeof(key), "agent_provider", i);
        HUSH_TRY(hush_launch_put_field(out, outsz, off, key,
                                       launch->roster.agents[i].provider));
        hush_launch_index_key(key, sizeof(key), "agent_prompt", i);
        HUSH_TRY(hush_launch_put_field(out, outsz, off, key,
                                       launch->roster.agents[i].prompt));
    }
    return HUSH_OK;
}

static hush_status_t hush_launch_put_members(const hush_launch_t *launch,
                                             char *out, size_t outsz,
                                             size_t *off)
{
    char key[HUSH_LAUNCH_KEY_MAX];
    char count[HUSH_LAUNCH_COUNT_MAX];
    size_t i;

    assert(launch != NULL);
    if (snprintf(count, sizeof(count), "%zu", launch->roster.nmembers)
        >= (int)sizeof(count))
        return HUSH_ERR_FULL;
    HUSH_TRY(hush_launch_put_field(out, outsz, off, "nmembers", count));
    for (i = 0; i < launch->roster.nmembers; ++i) {
        hush_launch_index_key(key, sizeof(key), "member_npub", i);
        HUSH_TRY(hush_launch_put_field(out, outsz, off, key,
                                       launch->roster.members[i].npub));
        hush_launch_index_key(key, sizeof(key), "member_name", i);
        HUSH_TRY(hush_launch_put_field(out, outsz, off, key,
                                       launch->roster.members[i].name));
    }
    if (*off == 0)
        return HUSH_ERR_FULL;
    out[*off - 1] = '}';
    if (*off + 1 >= outsz)
        return HUSH_ERR_FULL;
    out[*off] = '\0';
    return HUSH_OK;
}

static hush_status_t hush_launch_put_roster(const hush_launch_t *launch,
                                            char *out, size_t outsz,
                                            size_t *off)
{
    const hush_roster_profile_t *profile;

    assert(launch != NULL);
    profile = &launch->roster.profile;
    HUSH_TRY(hush_launch_put_field(out, outsz, off, "theme", profile->theme));
    HUSH_TRY(hush_launch_put_field(out, outsz, off, "first_name",
                                   profile->first_name));
    HUSH_TRY(hush_launch_put_field(out, outsz, off, "last_name",
                                   profile->last_name));
    HUSH_TRY(hush_launch_put_field(out, outsz, off, "organization",
                                   profile->organization));
    HUSH_TRY(hush_launch_put_field(out, outsz, off, "picture",
                                   profile->picture));
    HUSH_TRY(hush_launch_put_agents(launch, out, outsz, off));
    return hush_launch_put_members(launch, out, outsz, off);
}

static hush_status_t hush_launch_take_vibe_head(hush_launch_t *launch,
                                                const char *json)
{
    char flag[2];

    assert(launch != NULL);
    assert(json != NULL);
    if (!hush_launch_json_string(json, "vibe_name", launch->vibe_name,
                                 sizeof(launch->vibe_name)))
        return HUSH_ERR_PARSE;
    (void)hush_launch_json_string(json, "vibe_about", launch->vibe_about,
                                  sizeof(launch->vibe_about));
    if (!hush_launch_json_string(json, "vibe_token", launch->vibe_token,
                                 sizeof(launch->vibe_token)))
        return HUSH_ERR_PARSE;
    launch->vibe_public = 1;
    if (hush_launch_json_string(json, "vibe_public", flag, sizeof(flag))
        && flag[0] == '0')
        launch->vibe_public = 0;
    return HUSH_OK;
}

static hush_status_t hush_launch_take_channels(hush_launch_t *launch,
                                               const char *json)
{
    char key[HUSH_LAUNCH_KEY_MAX];
    size_t n;
    size_t i;

    assert(launch != NULL);
    launch->nchannels = 0;
    n = hush_launch_json_count(json, "nchannels",
                               (size_t)HUSH_LAUNCH_CHANNELS_MAX);
    for (i = 0; i < n; ++i) {
        hush_launch_channel_t *ch = &launch->channels[i];

        memset(ch, 0, sizeof(*ch));
        hush_launch_index_key(key, sizeof(key), "channel_name", i);
        (void)hush_launch_json_string(json, key, ch->name, sizeof(ch->name));
        hush_launch_index_key(key, sizeof(key), "channel_slug", i);
        (void)hush_launch_json_string(json, key, ch->slug, sizeof(ch->slug));
        hush_launch_index_key(key, sizeof(key), "channel_id", i);
        (void)hush_launch_json_string(json, key, ch->id, sizeof(ch->id));
        hush_launch_index_key(key, sizeof(key), "channel_group", i);
        (void)hush_launch_json_string(json, key, ch->group_id,
                                      sizeof(ch->group_id));
        hush_launch_take_channel_lists(ch, json, i);
        if (ch->name[0] == '\0')
            continue;
        if (ch->slug[0] == '\0')
            hush_launch_slugify(ch->slug, sizeof(ch->slug), ch->name);
        launch->nchannels++;
    }
    return HUSH_OK;
}

static void hush_launch_take_channel_lists(hush_launch_channel_t *ch,
                                           const char *json, size_t idx)
{
    char key[HUSH_LAUNCH_KEY_MAX];
    char stem[HUSH_LAUNCH_KEY_MAX];
    size_t n;
    size_t i;

    assert(ch != NULL);
    ch->nhumans = 0;
    ch->nrobots = 0;
    hush_launch_index_key(key, sizeof(key), "channel_nhumans", idx);
    n = hush_launch_json_count(json, key, (size_t)HUSH_LAUNCH_CHAN_HUMANS_MAX);
    for (i = 0; i < n; ++i) {
        if (snprintf(stem, sizeof(stem), "channel_%zu_human", idx)
            >= (int)sizeof(stem))
            break;
        hush_launch_index_key(key, sizeof(key), stem, i);
        if (!hush_launch_json_string(json, key, ch->humans[ch->nhumans],
                                     sizeof(ch->humans[0])))
            continue;
        ch->nhumans++;
    }
    hush_launch_index_key(key, sizeof(key), "channel_nrobots", idx);
    n = hush_launch_json_count(json, key, (size_t)HUSH_LAUNCH_CHAN_ROBOTS_MAX);
    for (i = 0; i < n; ++i) {
        if (snprintf(stem, sizeof(stem), "channel_%zu_robot", idx)
            >= (int)sizeof(stem))
            break;
        hush_launch_index_key(key, sizeof(key), stem, i);
        if (!hush_launch_json_string(json, key, ch->robots[ch->nrobots],
                                     sizeof(ch->robots[0])))
            continue;
        ch->nrobots++;
    }
}

static hush_status_t hush_launch_take_groups(hush_launch_t *launch,
                                             const char *json)
{
    char key[HUSH_LAUNCH_KEY_MAX];
    size_t n;
    size_t i;

    assert(launch != NULL);
    launch->ngroups = 0;
    n = hush_launch_json_count(json, "ngroups", (size_t)HUSH_LAUNCH_GROUPS_MAX);
    for (i = 0; i < n; ++i) {
        hush_launch_group_t *group = &launch->groups[i];

        memset(group, 0, sizeof(*group));
        hush_launch_index_key(key, sizeof(key), "group_name", i);
        (void)hush_launch_json_string(json, key, group->name,
                                      sizeof(group->name));
        hush_launch_index_key(key, sizeof(key), "group_id", i);
        (void)hush_launch_json_string(json, key, group->id, sizeof(group->id));
        if (group->name[0] == '\0' || group->id[0] == '\0')
            continue;
        launch->ngroups++;
    }
    return HUSH_OK;
}

static hush_status_t hush_launch_take_projects(hush_launch_t *launch,
                                               const char *json)
{
    char key[HUSH_LAUNCH_KEY_MAX];
    size_t n;
    size_t i;

    assert(launch != NULL);
    launch->nprojects = 0;
    n = hush_launch_json_count(json, "nprojects",
                               (size_t)HUSH_LAUNCH_PROJECTS_MAX);
    for (i = 0; i < n; ++i) {
        hush_launch_project_t *proj = &launch->projects[i];

        memset(proj, 0, sizeof(*proj));
        hush_launch_index_key(key, sizeof(key), "project_name", i);
        (void)hush_launch_json_string(json, key, proj->name, sizeof(proj->name));
        hush_launch_index_key(key, sizeof(key), "project_slug", i);
        (void)hush_launch_json_string(json, key, proj->slug, sizeof(proj->slug));
        hush_launch_index_key(key, sizeof(key), "project_path", i);
        (void)hush_launch_json_string(json, key, proj->path, sizeof(proj->path));
        if (proj->name[0] == '\0')
            continue;
        launch->nprojects++;
    }
    return HUSH_OK;
}

static hush_status_t hush_launch_restore_agent_id(hush_roster_agent_t *agent)
{
    char path[HUSH_PASS_PATH_MAX];
    char secret[HUSH_PASS_SECRET_MAX];

    assert(agent != NULL);
    if (snprintf(path, sizeof(path), "agents/%s/nsec", agent->slug)
        >= (int)sizeof(path))
        return hush_identity_generate(&agent->id);
    if (hush_pass_has(path)
        && hush_pass_get(secret, sizeof(secret), path) == HUSH_OK
        && hush_identity_import(&agent->id, secret) == HUSH_OK)
        return HUSH_OK;
    return hush_identity_generate(&agent->id);
}

static hush_status_t hush_launch_take_agent(hush_launch_t *launch,
                                            const char *json, size_t idx)
{
    hush_roster_agent_t *agent;
    char key[HUSH_LAUNCH_KEY_MAX];

    assert(launch != NULL);
    assert(idx < (size_t)HUSH_ROSTER_AGENTS_MAX);
    agent = &launch->roster.agents[launch->roster.nagents];
    memset(agent, 0, sizeof(*agent));
    hush_launch_index_key(key, sizeof(key), "agent_name", idx);
    (void)hush_launch_json_string(json, key, agent->name, sizeof(agent->name));
    hush_launch_index_key(key, sizeof(key), "agent_slug", idx);
    (void)hush_launch_json_string(json, key, agent->slug, sizeof(agent->slug));
    hush_launch_index_key(key, sizeof(key), "agent_provider", idx);
    (void)hush_launch_json_string(json, key, agent->provider,
                                  sizeof(agent->provider));
    hush_launch_index_key(key, sizeof(key), "agent_prompt", idx);
    (void)hush_launch_json_string(json, key, agent->prompt,
                                  sizeof(agent->prompt));
    if (agent->name[0] == '\0' || agent->slug[0] == '\0')
        return HUSH_OK;
    if (!hush_roster_is_provider(agent->provider))
        hush_launch_copy_name(agent->provider, sizeof(agent->provider),
                              HUSH_ROSTER_PROVIDER_GOOSE, "");
    if (hush_launch_restore_agent_id(agent) != HUSH_OK)
        return HUSH_ERR_CRYPTO;
    launch->roster.nagents++;
    return HUSH_OK;
}

static hush_status_t hush_launch_take_members(hush_launch_t *launch,
                                              const char *json)
{
    char key[HUSH_LAUNCH_KEY_MAX];
    size_t n;
    size_t i;

    assert(launch != NULL);
    n = hush_launch_json_count(json, "nmembers",
                               (size_t)HUSH_ROSTER_MEMBERS_MAX);
    for (i = 0; i < n; ++i) {
        char npub[HUSH_IDENTITY_NPUB_MAX];
        char name[HUSH_ROSTER_NAME_MAX];

        hush_launch_index_key(key, sizeof(key), "member_npub", i);
        if (!hush_launch_json_string(json, key, npub, sizeof(npub)))
            continue;
        hush_launch_index_key(key, sizeof(key), "member_name", i);
        (void)hush_launch_json_string(json, key, name, sizeof(name));
        (void)hush_roster_add_member(&launch->roster, npub, name);
    }
    return HUSH_OK;
}

static hush_status_t hush_launch_take_roster(hush_launch_t *launch,
                                             const char *json)
{
    hush_roster_profile_t profile;
    size_t n;
    size_t i;

    assert(launch != NULL);
    memset(&profile, 0, sizeof(profile));
    (void)hush_launch_json_string(json, "theme", profile.theme,
                                  sizeof(profile.theme));
    (void)hush_launch_json_string(json, "first_name", profile.first_name,
                                  sizeof(profile.first_name));
    (void)hush_launch_json_string(json, "last_name", profile.last_name,
                                  sizeof(profile.last_name));
    (void)hush_launch_json_string(json, "organization", profile.organization,
                                  sizeof(profile.organization));
    (void)hush_launch_json_string(json, "picture", profile.picture,
                                  sizeof(profile.picture));
    if (profile.theme[0] != '\0')
        (void)hush_roster_set_profile(&launch->roster, &profile);
    n = hush_launch_json_count(json, "nagents", (size_t)HUSH_ROSTER_AGENTS_MAX);
    for (i = 0; i < n; ++i)
        HUSH_TRY(hush_launch_take_agent(launch, json, i));
    return hush_launch_take_members(launch, json);
}

static hush_status_t hush_launch_restore_payne(hush_launch_t *launch)
{
    char secret[HUSH_PASS_SECRET_MAX];

    assert(launch != NULL);
    hush_identity_clear(&launch->payne);
    if (hush_pass_has(HUSH_PASS_PAYNE_NSEC)
        && hush_pass_get(secret, sizeof(secret), HUSH_PASS_PAYNE_NSEC) == HUSH_OK
        && hush_identity_import(&launch->payne, secret) == HUSH_OK)
        return HUSH_OK;
    if (hush_identity_generate(&launch->payne) != HUSH_OK)
        return HUSH_ERR_CRYPTO;
    if (launch->save_pass)
        hush_launch_try_save(launch, HUSH_PASS_PAYNE_NSEC, launch->payne.nsec);
    return HUSH_OK;
}
