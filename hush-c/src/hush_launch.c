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
#include "hush_home.h"
#include "hush_json.h"
#include "hush_launch.h"
#include "hush_pass.h"
#include "hush_skill.h"

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

/* True when a raised agent already uses slug. */
static int hush_launch_has_agent_slug(const hush_launch_t *launch,
                                      const char *slug);

/* Copies id into the next loadout slot. Ignores overflow. */
static void hush_launch_push_template_skill(hush_roster_agent_in_t *in,
                                            const char *id);

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

/* Writes logged_in through vibe, then opens the payne object.
 * Returns bytes written, or -1 on overflow. */
static int hush_launch_write_session_open(const hush_launch_t *launch,
                                          uint16_t port,
                                          char *out, size_t outsz);

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

/* Resolves $HUSH_CONFIG_DIR or ~/.hush/config into out. */
static void hush_launch_config_dir(char *out, size_t outsz);

/* Seeds Major name and standing orders when empty. */
static void hush_launch_fill_payne_defaults(hush_launch_t *launch);

/* Copies equipped skill ids onto Payne. */
static hush_status_t hush_launch_copy_payne_skills(hush_launch_t *launch,
                                                   const hush_roster_agent_in_t *in);

/* Persists Payne name, prompt, picture, voice, skills. */
static hush_status_t hush_launch_put_payne_profile(const hush_launch_t *launch,
                                                   char *out, size_t outsz,
                                                   size_t *off);

/* Restores Payne profile fields. */
static void hush_launch_take_payne_profile(hush_launch_t *launch,
                                           const char *json);

/* Persists one agent's picture, voice, and skills. */
static hush_status_t hush_launch_put_agent_extras(const hush_roster_agent_t *agent,
                                                  size_t idx,
                                                  char *out, size_t outsz,
                                                  size_t *off);

/* Restores one agent's picture, voice, and skills. */
static void hush_launch_take_agent_extras(hush_roster_agent_t *agent,
                                          const char *json, size_t idx);

/* Appends Payne skills array and closes the payne object. */
static hush_status_t hush_launch_format_payne_tail(const hush_launch_t *launch,
                                                   char *out, size_t outsz,
                                                   size_t *off);

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

/* Fills one restored channel's policy. Missing keys keep defaults. */
static void hush_launch_take_channel_policy(hush_launch_channel_t *ch,
                                            const char *json, size_t idx);

/* Appends one channel's policy persist fields. */
static hush_status_t hush_launch_put_channel_policy(
    const hush_launch_channel_t *ch, size_t idx,
    char *out, size_t outsz, size_t *off);

/* Writes an integer persist field. */
static hush_status_t hush_launch_put_int_field(char *out, size_t outsz,
                                               size_t *off, const char *key,
                                               int value);

/* Reads an integer persist field. Missing returns fallback. */
static int hush_launch_take_int_field(const char *json, const char *key,
                                      int fallback);

/* Reads channel_<stem>_<idx> as an integer. */
static int hush_launch_take_named_int(const char *json, size_t idx,
                                      const char *stem, int fallback);

/* Restores kind and robot_reply, defaulting unknowns. */
static void hush_launch_take_policy_text(hush_launch_channel_t *ch,
                                         const char *json, size_t idx);

/* Restores talk/burst/jobs/cooldown/hops, defaulting unknowns. */
static void hush_launch_take_policy_nums(hush_launch_channel_t *ch,
                                         const char *json, size_t idx);

/* Appends one channel's policy onto the session object. */
static hush_status_t hush_launch_format_channel_policy(
    const hush_launch_channel_t *ch, char *out, size_t outsz, size_t *off);

/* True when kind is open/humans/robots/mixed. */
static int hush_launch_kind_ok(const char *kind);

/* True when reply is off/mention/confirm. */
static int hush_launch_reply_ok(const char *reply);

/* True when burst_ms is one of the three allowed waits. */
static int hush_launch_burst_ok(int burst_ms);

/* True when max_jobs is 1, 2, or 4. */
static int hush_launch_jobs_ok(int max_jobs);

/* True when cooldown_s is 0, 10, or 30. */
static int hush_launch_cooldown_ok(int cooldown_s);

/* True when max_robot_turns is 0 (default), 1, 2, 4, or 8. */
static int hush_launch_turns_ok(int max_robot_turns);

/* Rejects unknown kind/reply/burst/jobs/cooldown/talk/hops. */
static hush_status_t hush_launch_policy_check(const hush_launch_policy_t *in);

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

/* Writes one Goose slot when the ranked list is empty. */
static void hush_launch_default_payne_providers(hush_launch_t *launch);

/* True when id is already in the ranked list. */
static int hush_launch_has_payne_provider(const hush_launch_t *launch,
                                          const char *id);

/* Appends a valid unused id. Ignores full, unknown, or duplicate. */
static void hush_launch_push_payne_provider(hush_launch_t *launch,
                                            const char *id);

/* Appends payne_provider_N persist fields. */
static hush_status_t hush_launch_put_payne_providers(const hush_launch_t *launch,
                                                     char *out, size_t outsz,
                                                     size_t *off);

/* Restores the ranked list. Missing keys become one Goose slot. */
static void hush_launch_take_payne_providers(hush_launch_t *launch,
                                             const char *json);

/* Appends session payne.provider plus the providers array. */
static hush_status_t hush_launch_format_payne_providers(
    const hush_launch_t *launch, char *out, size_t outsz, size_t *off);

void hush_launch_init(hush_launch_t *launch)
{
    if (launch == NULL)
        return;
    memset(launch, 0, sizeof(*launch));
    launch->vibe_public = 1;
    launch->dev_log_enabled = 0; /* default: disabled (see M3.1) */
    hush_roster_init(&launch->roster);
    hush_launch_default_payne_providers(launch);
    launch->payne_enabled = 1;
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
    HUSH_TRY(hush_launch_put_payne_profile(launch, json, sizeof(json), &off));
    HUSH_TRY(hush_launch_put_payne_providers(launch, json, sizeof(json), &off));
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
    hush_launch_take_payne_profile(launch, json);
    hush_launch_take_payne_providers(launch, json);
    hush_launch_fill_payne_defaults(launch);
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

hush_status_t hush_launch_clone_agent(hush_launch_t *launch,
                                      hush_store_t *store,
                                      const char *slug)
{
    if (launch == NULL || store == NULL || slug == NULL)
        return HUSH_ERR_ARG;
    if (!launch->has_vibe || !launch->logged_in)
        return HUSH_ERR_ARG;
    HUSH_TRY(hush_roster_clone_agent(&launch->roster, store, slug));
    return hush_launch_save_vibe(launch);
}

static int hush_launch_has_agent_slug(const hush_launch_t *launch,
                                      const char *slug)
{
    size_t i;

    assert(launch != NULL);
    assert(slug != NULL);
    for (i = 0; i < launch->roster.nagents; i++) {
        if (strcmp(launch->roster.agents[i].slug, slug) == 0)
            return 1;
    }
    return 0;
}

hush_status_t hush_launch_seed_templates(hush_launch_t *launch,
                                         hush_store_t *store)
{
    hush_roster_agent_in_t in;

    if (launch == NULL || store == NULL)
        return HUSH_ERR_ARG;
    if (!launch->has_vibe)
        return HUSH_ERR_ARG;
    if (!hush_launch_has_agent_slug(launch, "coach")) {
        memset(&in, 0, sizeof(in));
        memcpy(in.name, "Coach", 6);
        memcpy(in.prompt, "Coach hive jobs toward small tested C changes.", 47);
        memcpy(in.provider, HUSH_ROSTER_PROVIDER_GROK_BUILD,
               sizeof(HUSH_ROSTER_PROVIDER_GROK_BUILD));
        memcpy(in.picture, "panel:robots:0", 15);
        in.has_picture = 1;
        hush_launch_push_template_skill(&in, "system:canvas-coach");
        in.has_skills = 1;
        in.locked = 1;
        HUSH_TRY(hush_roster_add_agent(&launch->roster, store, &in, 0));
    }
    if (!hush_launch_has_agent_slug(launch, "auditor")) {
        memset(&in, 0, sizeof(in));
        memcpy(in.name, "Auditor", 8);
        memcpy(in.prompt, "Hunt exploitable bugs. Report only what you can prove.",
               55);
        memcpy(in.provider, HUSH_ROSTER_PROVIDER_GROK_BUILD,
               sizeof(HUSH_ROSTER_PROVIDER_GROK_BUILD));
        memcpy(in.picture, "panel:robots:2", 15);
        in.has_picture = 1;
        hush_launch_push_template_skill(&in, "system:hive-audit");
        in.has_skills = 1;
        in.locked = 1;
        HUSH_TRY(hush_roster_add_agent(&launch->roster, store, &in, 0));
    }
    if (!hush_launch_has_agent_slug(launch, "marshal")) {
        memset(&in, 0, sizeof(in));
        memcpy(in.name, "Marshal", 8);
        memcpy(in.prompt, "Babysit the channel. Enforce rails. Do not take work grok.",
               59);
        memcpy(in.provider, HUSH_ROSTER_PROVIDER_GROK_BUILD,
               sizeof(HUSH_ROSTER_PROVIDER_GROK_BUILD));
        memcpy(in.picture, "panel:angevin:3", 16);
        in.has_picture = 1;
        memcpy(in.role, HUSH_ROSTER_ROLE_CHAPERON,
               sizeof(HUSH_ROSTER_ROLE_CHAPERON));
        in.has_role = 1;
        hush_launch_push_template_skill(&in, "system:topic-leash");
        hush_launch_push_template_skill(&in, "system:no-loop");
        hush_launch_push_template_skill(&in, "system:civility");
        hush_launch_push_template_skill(&in, "system:hop-cap");
        hush_launch_push_template_skill(&in, "system:secret-watch");
        hush_launch_push_template_skill(&in, "system:chaperon-ack");
        in.has_skills = 1;
        in.locked = 1;
        HUSH_TRY(hush_roster_add_agent(&launch->roster, store, &in, 0));
    }
    return hush_launch_save_vibe(launch);
}

hush_status_t hush_launch_update_agent(hush_launch_t *launch, const char *slug,
                                       const hush_roster_agent_in_t *in)
{
    if (launch == NULL || slug == NULL || in == NULL)
        return HUSH_ERR_ARG;
    if (!launch->has_vibe || !launch->logged_in)
        return HUSH_ERR_ARG;
    HUSH_TRY(hush_roster_update_agent(&launch->roster, slug, in));
    return hush_launch_save_vibe(launch);
}

hush_status_t hush_launch_update_payne_profile(hush_launch_t *launch,
                                               const hush_roster_agent_in_t *in)
{
    if (launch == NULL || in == NULL)
        return HUSH_ERR_ARG;
    if (!launch->has_vibe || !launch->logged_in)
        return HUSH_ERR_ARG;
    hush_launch_fill_payne_defaults(launch);
    if (in->has_picture)
        hush_launch_copy_name(launch->payne_picture, sizeof(launch->payne_picture),
                              in->picture, "");
    if (in->has_voice) {
        if (in->voice[0] != '\0' && !hush_skill_is_voice(in->voice))
            return HUSH_ERR_PARSE;
        hush_launch_copy_name(launch->payne_voice, sizeof(launch->payne_voice),
                              in->voice, "");
    }
    if (in->has_enabled)
        launch->payne_enabled = in->enabled ? 1 : 0;
    if (in->has_skills && hush_launch_copy_payne_skills(launch, in) != HUSH_OK)
        return HUSH_ERR_FULL;
    return hush_launch_save_vibe(launch);
}

const char *hush_launch_payne_name(const hush_launch_t *launch)
{
    if (launch == NULL || launch->payne_name[0] == '\0')
        return HUSH_LAUNCH_PAYNE_NAME;
    return launch->payne_name;
}

const char *hush_launch_payne_prompt(const hush_launch_t *launch)
{
    if (launch == NULL || launch->payne_prompt[0] == '\0')
        return HUSH_LAUNCH_PAYNE_ABOUT;
    return launch->payne_prompt;
}

hush_status_t hush_launch_set_payne_providers(hush_launch_t *launch,
                                              const char *const *ids,
                                              size_t nids)
{
    size_t i;

    if (launch == NULL)
        return HUSH_ERR_ARG;
    if (!launch->has_vibe || !launch->logged_in)
        return HUSH_ERR_ARG;
    if (nids > 0 && ids == NULL)
        return HUSH_ERR_ARG;
    launch->npayne_providers = 0;
    memset(launch->payne_providers, 0, sizeof(launch->payne_providers));
    for (i = 0; i < nids && i < (size_t)HUSH_LAUNCH_PAYNE_PROVIDERS_MAX; ++i)
        hush_launch_push_payne_provider(launch, ids[i]);
    hush_launch_default_payne_providers(launch);
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
    launch->payne_name[0] = '\0';
    launch->payne_prompt[0] = '\0';
    launch->payne_picture[0] = '\0';
    launch->payne_voice[0] = '\0';
    launch->npayne_skills = 0;
    launch->payne_enabled = 1;
    hush_launch_fill_payne_defaults(launch);
    if (hush_launch_seed_hive(launch, store) != HUSH_OK)
        return HUSH_ERR_CRYPTO;
    launch->has_vibe = 1;
    HUSH_TRY(hush_launch_seed_templates(launch, store));
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

void hush_launch_policy_default(hush_launch_channel_t *ch)
{
    if (ch == NULL)
        return;
    memcpy(ch->kind, HUSH_LAUNCH_KIND_OPEN, sizeof(HUSH_LAUNCH_KIND_OPEN));
    memcpy(ch->robot_reply, HUSH_LAUNCH_REPLY_MENTION,
           sizeof(HUSH_LAUNCH_REPLY_MENTION));
    ch->robot_talk = 0;
    ch->burst_ms = HUSH_LAUNCH_BURST_MS_DEFAULT;
    ch->max_jobs = HUSH_LAUNCH_MAX_JOBS_DEFAULT;
    ch->cooldown_s = HUSH_LAUNCH_COOLDOWN_S_DEFAULT;
    ch->robot_hops = 0;
    ch->max_robot_turns = HUSH_LAUNCH_TURNS_DEFAULT;
    ch->chaperon[0] = '\0';
}

hush_status_t hush_launch_policy_copy(hush_launch_policy_t *out,
                                      const hush_launch_channel_t *ch)
{
    if (out == NULL || ch == NULL)
        return HUSH_ERR_ARG;
    memset(out, 0, sizeof(*out));
    memcpy(out->kind, ch->kind, sizeof(out->kind));
    memcpy(out->robot_reply, ch->robot_reply, sizeof(out->robot_reply));
    out->robot_talk = ch->robot_talk;
    out->burst_ms = ch->burst_ms;
    out->max_jobs = ch->max_jobs;
    out->cooldown_s = ch->cooldown_s;
    out->robot_hops = ch->robot_hops;
    out->max_robot_turns = ch->max_robot_turns;
    memcpy(out->chaperon, ch->chaperon, sizeof(out->chaperon));
    return HUSH_OK;
}

hush_status_t hush_launch_set_channel_policy(hush_launch_t *launch,
                                             const char *slug,
                                             const hush_launch_policy_t *in)
{
    hush_launch_channel_t *ch;

    if (launch == NULL || slug == NULL || in == NULL)
        return HUSH_ERR_ARG;
    if (!launch->has_vibe)
        return HUSH_ERR_ARG;
    HUSH_TRY(hush_launch_policy_check(in));
    ch = hush_launch_find_channel(launch, slug);
    if (ch == NULL)
        return HUSH_ERR_NOT_FOUND;
    memcpy(ch->kind, in->kind, sizeof(ch->kind));
    memcpy(ch->robot_reply, in->robot_reply, sizeof(ch->robot_reply));
    ch->robot_talk = in->robot_talk;
    ch->burst_ms = in->burst_ms;
    ch->max_jobs = in->max_jobs;
    ch->cooldown_s = in->cooldown_s;
    ch->robot_hops = in->robot_hops;
    ch->max_robot_turns = in->max_robot_turns > 0
        ? in->max_robot_turns : HUSH_LAUNCH_TURNS_DEFAULT;
    hush_launch_copy_name(ch->chaperon, sizeof(ch->chaperon), in->chaperon, "");
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

/* Public: returns the about/topic for a channel slug (used as quick LLM system
 * prompt pointer for pills/topics). Empty string if none or unknown. */
const char *hush_launch_channel_about(const hush_launch_t *launch, const char *slug)
{
    const hush_launch_channel_t *ch;
    size_t i;

    if (launch == NULL || slug == NULL || slug[0] == '\0')
        return "";
    for (i = 0; i < launch->nchannels; ++i) {
        if (strcmp(launch->channels[i].slug, slug) == 0) {
            ch = &launch->channels[i];
            return ch->about[0] ? ch->about : "";
        }
    }
    return "";
}

hush_status_t hush_launch_set_channel_about(hush_launch_t *launch,
                                            const char *slug,
                                            const char *about)
{
    hush_launch_channel_t *ch;

    if (launch == NULL || slug == NULL)
        return HUSH_ERR_ARG;
    if (!launch->has_vibe)
        return HUSH_ERR_ARG;
    ch = hush_launch_find_channel(launch, slug);
    if (ch == NULL)
        return HUSH_ERR_NOT_FOUND;
    hush_launch_copy_name(ch->about, sizeof(ch->about),
                          about != NULL ? about : "", "");
    return hush_launch_save_vibe(launch);
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
    hush_launch_policy_default(ch);
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
                           "At ease. I'm Major. Tell me what you "
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

static void hush_launch_default_payne_providers(hush_launch_t *launch)
{
    assert(launch != NULL);
    if (launch->npayne_providers > 0)
        return;
    memcpy(launch->payne_providers[0], HUSH_ROSTER_PROVIDER_GOOSE,
           sizeof(HUSH_ROSTER_PROVIDER_GOOSE));
    launch->npayne_providers = 1;
}

static int hush_launch_has_payne_provider(const hush_launch_t *launch,
                                          const char *id)
{
    size_t i;

    assert(launch != NULL);
    assert(id != NULL);
    for (i = 0; i < launch->npayne_providers; ++i) {
        if (strcmp(launch->payne_providers[i], id) == 0)
            return 1;
    }
    return 0;
}

static void hush_launch_push_payne_provider(hush_launch_t *launch,
                                            const char *id)
{
    assert(launch != NULL);
    if (id == NULL || id[0] == '\0')
        return;
    if (!hush_roster_is_provider(id))
        return;
    if (hush_launch_has_payne_provider(launch, id))
        return;
    if (launch->npayne_providers >= (size_t)HUSH_LAUNCH_PAYNE_PROVIDERS_MAX)
        return;
    hush_launch_copy_name(launch->payne_providers[launch->npayne_providers],
                          sizeof(launch->payne_providers[0]), id, "");
    launch->npayne_providers++;
}

static hush_status_t hush_launch_put_payne_providers(const hush_launch_t *launch,
                                                     char *out, size_t outsz,
                                                     size_t *off)
{
    char key[HUSH_LAUNCH_KEY_MAX];
    char count[HUSH_LAUNCH_COUNT_MAX];
    size_t i;

    assert(launch != NULL);
    if (snprintf(count, sizeof(count), "%zu", launch->npayne_providers)
        >= (int)sizeof(count))
        return HUSH_ERR_FULL;
    HUSH_TRY(hush_launch_put_field(out, outsz, off, "npayne_providers", count));
    for (i = 0; i < launch->npayne_providers; ++i) {
        hush_launch_index_key(key, sizeof(key), "payne_provider", i);
        HUSH_TRY(hush_launch_put_field(out, outsz, off, key,
                                       launch->payne_providers[i]));
    }
    return HUSH_OK;
}

static void hush_launch_take_payne_providers(hush_launch_t *launch,
                                             const char *json)
{
    char key[HUSH_LAUNCH_KEY_MAX];
    char id[HUSH_ROSTER_PROVIDER_MAX];
    size_t i;
    size_t n;

    assert(launch != NULL);
    assert(json != NULL);
    launch->npayne_providers = 0;
    memset(launch->payne_providers, 0, sizeof(launch->payne_providers));
    n = hush_launch_json_count(json, "npayne_providers",
                               (size_t)HUSH_LAUNCH_PAYNE_PROVIDERS_MAX);
    for (i = 0; i < n; ++i) {
        hush_launch_index_key(key, sizeof(key), "payne_provider", i);
        if (!hush_launch_json_string(json, key, id, sizeof(id)))
            continue;
        hush_launch_push_payne_provider(launch, id);
    }
    hush_launch_default_payne_providers(launch);
}

static hush_status_t hush_launch_format_payne_providers(
    const hush_launch_t *launch, char *out, size_t outsz, size_t *off)
{
    const char *name;
    const char *npub;
    const char *hex;
    const char *about;
    const char *primary;
    size_t i;
    int n;

    assert(launch != NULL);
    assert(out != NULL);
    assert(off != NULL);
    name = launch->has_vibe ? hush_launch_payne_name(launch) : "";
    npub = launch->has_vibe ? launch->payne.npub : "";
    hex = launch->has_vibe ? launch->payne.pubkey_hex : "";
    about = launch->has_vibe ? hush_launch_payne_prompt(launch) : "";
    primary = launch->npayne_providers > 0 ? launch->payne_providers[0] : "";
    n = snprintf(out + *off, outsz - *off,
                 "\"name\":\"%s\",\"npub\":\"%s\",\"pubkey\":\"%s\","
                 "\"about\":\"%s\",\"prompt\":\"%s\",\"picture\":\"%s\","
                 "\"voice\":\"%s\",\"enabled\":%s,\"provider\":\"%s\","
                 "\"providers\":[",
                 name, npub, hex, about, about, launch->payne_picture,
                 launch->payne_voice,
                 launch->payne_enabled ? "true" : "false", primary);
    if (n < 0 || *off + (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    *off += (size_t)n;
    for (i = 0; i < launch->npayne_providers; ++i) {
        n = snprintf(out + *off, outsz - *off, "%s\"%s\"",
                     (i == 0) ? "" : ",", launch->payne_providers[i]);
        if (n < 0 || *off + (size_t)n >= outsz)
            return HUSH_ERR_FULL;
        *off += (size_t)n;
    }
    n = snprintf(out + *off, outsz - *off, "],\"skills\":");
    if (n < 0 || *off + (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    *off += (size_t)n;
    return hush_launch_format_payne_tail(launch, out, outsz, off);
}

static int hush_launch_write_session_open(const hush_launch_t *launch,
                                          uint16_t port,
                                          char *out, size_t outsz)
{
    char esc_vibe[HUSH_LAUNCH_NAME_MAX * 2];
    char esc_about[HUSH_LAUNCH_ABOUT_MAX * 2];
    int n;

    assert(launch != NULL);
    assert(out != NULL);
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
                 "\"dev_log_enabled\":%s,\"payne\":{",
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
                 launch->dev_log_enabled ? "true" : "false");
    if (n < 0 || (size_t)n >= outsz)
        return -1;
    return n;
}

static hush_status_t hush_launch_format_head(const hush_launch_t *launch,
                                             uint16_t port,
                                             char *out, size_t outsz,
                                             size_t *off)
{
    int n;

    assert(launch != NULL);
    assert(out != NULL);
    assert(off != NULL);
    n = hush_launch_write_session_open(launch, port, out, outsz);
    if (n < 0)
        return HUSH_ERR_FULL;
    *off = (size_t)n;
    return hush_launch_format_payne_providers(launch, out, outsz, off);
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
        HUSH_TRY(hush_launch_format_channel_policy(&launch->channels[i],
                                                   out, outsz, off));
        /* Emit optional channel about/topic so it can be used as prompt pointer
         * (pills/topics become quick LLM system context). */
        if (launch->channels[i].about[0] != '\0') {
            char esc_ab[HUSH_LAUNCH_ABOUT_MAX * 2];
            if (*off + 16 >= outsz)
                return HUSH_ERR_FULL;
            hush_launch_json_escape(launch->channels[i].about, esc_ab,
                                    sizeof(esc_ab));
            n = snprintf(out + *off, outsz - *off, ",\"about\":\"%s\"", esc_ab);
            if (n > 0)
                *off += (size_t)n;
        }
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

static hush_status_t hush_launch_format_channel_policy(
    const hush_launch_channel_t *ch, char *out, size_t outsz, size_t *off)
{
    int n;

    assert(ch != NULL);
    assert(out != NULL);
    assert(off != NULL);
    n = snprintf(out + *off, outsz - *off,
                 ",\"kind\":\"%s\",\"robot_reply\":\"%s\",\"robot_talk\":%d,"
                 "\"burst_ms\":%d,\"max_jobs\":%d,\"cooldown_s\":%d,"
                 "\"robot_hops\":%d,\"max_robot_turns\":%d,\"chaperon\":\"%s\"",
                 ch->kind[0] ? ch->kind : HUSH_LAUNCH_KIND_OPEN,
                 ch->robot_reply[0] ? ch->robot_reply
                                    : HUSH_LAUNCH_REPLY_MENTION,
                 ch->robot_talk, ch->burst_ms, ch->max_jobs,
                 ch->cooldown_s, ch->robot_hops,
                 ch->max_robot_turns > 0
                     ? ch->max_robot_turns : HUSH_LAUNCH_TURNS_DEFAULT,
                 ch->chaperon);
    if (n < 0 || *off + (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    *off += (size_t)n;
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
    return hush_json_escape(src, dst, dstsz);
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
    assert(out != NULL);
    assert(outsz > 0);
    hush_home_config_dir(out, outsz);
    if (out[0] == '\0')
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
    return hush_home_ensure();
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
    if (fp == NULL && getenv(HUSH_HOME_ENV_CONFIG) == NULL) {
        char legacy[HUSH_HOME_PATH_MAX];

        hush_home_legacy_config_dir(legacy, sizeof(legacy));
        if (legacy[0] != '\0') {
            if (snprintf(path, sizeof(path), "%s/%s", legacy, HUSH_LAUNCH_VIBE_FILE)
                < (int)sizeof(path))
                fp = fopen(path, "r");
        }
    }
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
    flag[0] = launch->dev_log_enabled ? '1' : '0';
    HUSH_TRY(hush_launch_put_field(out, outsz, off, "dev_log_enabled", flag));
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
        /* Persist channel about/topic so it can be used as a quick prompt pointer
         * for robots on this channel (pills/topics). */
        hush_launch_index_key(key, sizeof(key), "channel_about", i);
        HUSH_TRY(hush_launch_put_field(out, outsz, off, key,
                                       launch->channels[i].about));
        HUSH_TRY(hush_launch_put_channel_lists(&launch->channels[i], i,
                                              out, outsz, off));
        HUSH_TRY(hush_launch_put_channel_policy(&launch->channels[i], i,
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

static hush_status_t hush_launch_put_int_field(char *out, size_t outsz,
                                               size_t *off, const char *key,
                                               int value)
{
    char text[HUSH_LAUNCH_COUNT_MAX];

    if (snprintf(text, sizeof(text), "%d", value) >= (int)sizeof(text))
        return HUSH_ERR_FULL;
    return hush_launch_put_field(out, outsz, off, key, text);
}

static hush_status_t hush_launch_put_channel_policy(
    const hush_launch_channel_t *ch, size_t idx,
    char *out, size_t outsz, size_t *off)
{
    char key[HUSH_LAUNCH_KEY_MAX];

    assert(ch != NULL);
    hush_launch_index_key(key, sizeof(key), "channel_kind", idx);
    HUSH_TRY(hush_launch_put_field(out, outsz, off, key, ch->kind));
    hush_launch_index_key(key, sizeof(key), "channel_robot_reply", idx);
    HUSH_TRY(hush_launch_put_field(out, outsz, off, key, ch->robot_reply));
    hush_launch_index_key(key, sizeof(key), "channel_robot_talk", idx);
    HUSH_TRY(hush_launch_put_int_field(out, outsz, off, key, ch->robot_talk));
    hush_launch_index_key(key, sizeof(key), "channel_burst_ms", idx);
    HUSH_TRY(hush_launch_put_int_field(out, outsz, off, key, ch->burst_ms));
    hush_launch_index_key(key, sizeof(key), "channel_max_jobs", idx);
    HUSH_TRY(hush_launch_put_int_field(out, outsz, off, key, ch->max_jobs));
    hush_launch_index_key(key, sizeof(key), "channel_cooldown_s", idx);
    HUSH_TRY(hush_launch_put_int_field(out, outsz, off, key, ch->cooldown_s));
    hush_launch_index_key(key, sizeof(key), "channel_robot_hops", idx);
    HUSH_TRY(hush_launch_put_int_field(out, outsz, off, key, ch->robot_hops));
    hush_launch_index_key(key, sizeof(key), "channel_max_robot_turns", idx);
    HUSH_TRY(hush_launch_put_int_field(out, outsz, off, key,
                                       ch->max_robot_turns));
    hush_launch_index_key(key, sizeof(key), "channel_chaperon", idx);
    return hush_launch_put_field(out, outsz, off, key, ch->chaperon);
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
        HUSH_TRY(hush_launch_put_agent_extras(&launch->roster.agents[i], i,
                                              out, outsz, off));
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
    launch->dev_log_enabled = 0;
    if (hush_launch_json_string(json, "dev_log_enabled", flag, sizeof(flag))
        && flag[0] == '1')
        launch->dev_log_enabled = 1;
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
        /* Restore channel about/topic for prompt injection (pills/topics). */
        hush_launch_index_key(key, sizeof(key), "channel_about", i);
        (void)hush_launch_json_string(json, key, ch->about, sizeof(ch->about));
        hush_launch_take_channel_lists(ch, json, i);
        hush_launch_take_channel_policy(ch, json, i);
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

static int hush_launch_take_int_field(const char *json, const char *key,
                                      int fallback)
{
    char text[HUSH_LAUNCH_COUNT_MAX];

    assert(json != NULL);
    assert(key != NULL);
    if (!hush_launch_json_string(json, key, text, sizeof(text)))
        return fallback;
    return atoi(text);
}

static void hush_launch_take_policy_text(hush_launch_channel_t *ch,
                                         const char *json, size_t idx)
{
    char key[HUSH_LAUNCH_KEY_MAX];

    assert(ch != NULL);
    hush_launch_index_key(key, sizeof(key), "channel_kind", idx);
    (void)hush_launch_json_string(json, key, ch->kind, sizeof(ch->kind));
    if (!hush_launch_kind_ok(ch->kind))
        memcpy(ch->kind, HUSH_LAUNCH_KIND_OPEN, sizeof(HUSH_LAUNCH_KIND_OPEN));
    hush_launch_index_key(key, sizeof(key), "channel_robot_reply", idx);
    (void)hush_launch_json_string(json, key, ch->robot_reply,
                                  sizeof(ch->robot_reply));
    if (!hush_launch_reply_ok(ch->robot_reply))
        memcpy(ch->robot_reply, HUSH_LAUNCH_REPLY_MENTION,
               sizeof(HUSH_LAUNCH_REPLY_MENTION));
    hush_launch_index_key(key, sizeof(key), "channel_chaperon", idx);
    (void)hush_launch_json_string(json, key, ch->chaperon, sizeof(ch->chaperon));
}

static int hush_launch_take_named_int(const char *json, size_t idx,
                                      const char *stem, int fallback)
{
    char key[HUSH_LAUNCH_KEY_MAX];

    hush_launch_index_key(key, sizeof(key), stem, idx);
    return hush_launch_take_int_field(json, key, fallback);
}

static void hush_launch_take_policy_nums(hush_launch_channel_t *ch,
                                         const char *json, size_t idx)
{
    assert(ch != NULL);
    ch->robot_talk = hush_launch_take_named_int(json, idx,
                                                "channel_robot_talk", 0) != 0;
    ch->burst_ms = hush_launch_take_named_int(json, idx, "channel_burst_ms",
                                              HUSH_LAUNCH_BURST_MS_DEFAULT);
    if (!hush_launch_burst_ok(ch->burst_ms))
        ch->burst_ms = HUSH_LAUNCH_BURST_MS_DEFAULT;
    ch->max_jobs = hush_launch_take_named_int(json, idx, "channel_max_jobs",
                                              HUSH_LAUNCH_MAX_JOBS_DEFAULT);
    if (!hush_launch_jobs_ok(ch->max_jobs))
        ch->max_jobs = HUSH_LAUNCH_MAX_JOBS_DEFAULT;
    ch->cooldown_s = hush_launch_take_named_int(json, idx,
                                                "channel_cooldown_s",
                                                HUSH_LAUNCH_COOLDOWN_S_DEFAULT);
    if (!hush_launch_cooldown_ok(ch->cooldown_s))
        ch->cooldown_s = HUSH_LAUNCH_COOLDOWN_S_DEFAULT;
    ch->robot_hops = hush_launch_take_named_int(json, idx,
                                                "channel_robot_hops", 0) != 0;
    ch->max_robot_turns = hush_launch_take_named_int(json, idx,
                                                     "channel_max_robot_turns",
                                                     HUSH_LAUNCH_TURNS_DEFAULT);
    if (!hush_launch_turns_ok(ch->max_robot_turns))
        ch->max_robot_turns = HUSH_LAUNCH_TURNS_DEFAULT;
}

static void hush_launch_take_channel_policy(hush_launch_channel_t *ch,
                                            const char *json, size_t idx)
{
    assert(ch != NULL);
    hush_launch_policy_default(ch);
    hush_launch_take_policy_text(ch, json, idx);
    hush_launch_take_policy_nums(ch, json, idx);
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
    hush_launch_take_agent_extras(agent, json, idx);
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

static int hush_launch_kind_ok(const char *kind)
{
    if (kind == NULL)
        return 0;
    if (strcmp(kind, HUSH_LAUNCH_KIND_OPEN) == 0)
        return 1;
    if (strcmp(kind, HUSH_LAUNCH_KIND_HUMANS) == 0)
        return 1;
    if (strcmp(kind, HUSH_LAUNCH_KIND_ROBOTS) == 0)
        return 1;
    if (strcmp(kind, HUSH_LAUNCH_KIND_MIXED) == 0)
        return 1;
    return 0;
}

static int hush_launch_reply_ok(const char *reply)
{
    if (reply == NULL)
        return 0;
    if (strcmp(reply, HUSH_LAUNCH_REPLY_OFF) == 0)
        return 1;
    if (strcmp(reply, HUSH_LAUNCH_REPLY_MENTION) == 0)
        return 1;
    if (strcmp(reply, HUSH_LAUNCH_REPLY_CONFIRM) == 0)
        return 1;
    return 0;
}

static int hush_launch_burst_ok(int burst_ms)
{
    if (burst_ms == HUSH_LAUNCH_BURST_MS_FAST)
        return 1;
    if (burst_ms == HUSH_LAUNCH_BURST_MS_DEFAULT)
        return 1;
    if (burst_ms == HUSH_LAUNCH_BURST_MS_SLOW)
        return 1;
    return 0;
}

static int hush_launch_jobs_ok(int max_jobs)
{
    if (max_jobs == HUSH_LAUNCH_MAX_JOBS_MIN)
        return 1;
    if (max_jobs == HUSH_LAUNCH_MAX_JOBS_DEFAULT)
        return 1;
    if (max_jobs == HUSH_LAUNCH_MAX_JOBS_MAX)
        return 1;
    return 0;
}

static int hush_launch_cooldown_ok(int cooldown_s)
{
    if (cooldown_s == HUSH_LAUNCH_COOLDOWN_S_OFF)
        return 1;
    if (cooldown_s == HUSH_LAUNCH_COOLDOWN_S_DEFAULT)
        return 1;
    if (cooldown_s == HUSH_LAUNCH_COOLDOWN_S_LONG)
        return 1;
    return 0;
}

static int hush_launch_turns_ok(int max_robot_turns)
{
    if (max_robot_turns == 0)
        return 1;
    if (max_robot_turns == HUSH_LAUNCH_TURNS_MIN)
        return 1;
    if (max_robot_turns == HUSH_LAUNCH_TURNS_PAIR)
        return 1;
    if (max_robot_turns == HUSH_LAUNCH_TURNS_DEFAULT)
        return 1;
    if (max_robot_turns == HUSH_LAUNCH_TURNS_MAX)
        return 1;
    return 0;
}

static hush_status_t hush_launch_policy_check(const hush_launch_policy_t *in)
{
    assert(in != NULL);
    if (!hush_launch_kind_ok(in->kind))
        return HUSH_ERR_PARSE;
    if (!hush_launch_reply_ok(in->robot_reply))
        return HUSH_ERR_PARSE;
    if (!hush_launch_burst_ok(in->burst_ms))
        return HUSH_ERR_PARSE;
    if (!hush_launch_jobs_ok(in->max_jobs))
        return HUSH_ERR_PARSE;
    if (!hush_launch_cooldown_ok(in->cooldown_s))
        return HUSH_ERR_PARSE;
    if (in->robot_talk != 0 && in->robot_talk != 1)
        return HUSH_ERR_PARSE;
    if (in->robot_hops != 0 && in->robot_hops != 1)
        return HUSH_ERR_PARSE;
    if (!hush_launch_turns_ok(in->max_robot_turns))
        return HUSH_ERR_PARSE;
    return HUSH_OK;
}

static void hush_launch_fill_payne_defaults(hush_launch_t *launch)
{
    assert(launch != NULL);
    if (launch->payne_name[0] == '\0')
        hush_launch_copy_name(launch->payne_name, sizeof(launch->payne_name),
                              HUSH_LAUNCH_PAYNE_NAME, "");
    if (launch->payne_prompt[0] == '\0')
        hush_launch_copy_name(launch->payne_prompt, sizeof(launch->payne_prompt),
                              HUSH_LAUNCH_PAYNE_ABOUT, "");
    if (launch->payne_picture[0] == '\0')
        hush_launch_copy_name(launch->payne_picture,
                              sizeof(launch->payne_picture),
                              "panel:robots:1", "");
}

static void hush_launch_push_template_skill(hush_roster_agent_in_t *in,
                                            const char *id)
{
    size_t n;

    assert(in != NULL);
    assert(id != NULL);
    if (in->nskills >= (size_t)HUSH_SKILL_EQUIP_MAX)
        return;
    n = strlen(id);
    if (n + 1 > sizeof(in->skills[0]))
        return;
    memcpy(in->skills[in->nskills], id, n + 1);
    in->nskills++;
}

static hush_status_t hush_launch_copy_payne_skills(hush_launch_t *launch,
                                                   const hush_roster_agent_in_t *in)
{
    size_t i;

    assert(launch != NULL);
    assert(in != NULL);
    if (in->nskills > (size_t)HUSH_SKILL_EQUIP_MAX)
        return HUSH_ERR_FULL;
    memset(launch->payne_skills, 0, sizeof(launch->payne_skills));
    launch->npayne_skills = 0;
    for (i = 0; i < in->nskills; ++i) {
        if (in->skills[i][0] == '\0')
            continue;
        hush_launch_copy_name(launch->payne_skills[launch->npayne_skills],
                              sizeof(launch->payne_skills[0]),
                              in->skills[i], "");
        launch->npayne_skills++;
    }
    return HUSH_OK;
}

static hush_status_t hush_launch_put_payne_profile(const hush_launch_t *launch,
                                                   char *out, size_t outsz,
                                                   size_t *off)
{
    char key[HUSH_LAUNCH_KEY_MAX];
    char count[HUSH_LAUNCH_COUNT_MAX];
    size_t i;

    assert(launch != NULL);
    HUSH_TRY(hush_launch_put_field(out, outsz, off, "payne_name",
                                   launch->payne_name));
    HUSH_TRY(hush_launch_put_field(out, outsz, off, "payne_prompt",
                                   launch->payne_prompt));
    HUSH_TRY(hush_launch_put_field(out, outsz, off, "payne_picture",
                                   launch->payne_picture));
    HUSH_TRY(hush_launch_put_field(out, outsz, off, "payne_voice",
                                   launch->payne_voice));
    HUSH_TRY(hush_launch_put_field(out, outsz, off, "payne_enabled",
                                   launch->payne_enabled ? "1" : "0"));
    if (snprintf(count, sizeof(count), "%zu", launch->npayne_skills)
        >= (int)sizeof(count))
        return HUSH_ERR_FULL;
    HUSH_TRY(hush_launch_put_field(out, outsz, off, "npayne_skills", count));
    for (i = 0; i < launch->npayne_skills; ++i) {
        hush_launch_index_key(key, sizeof(key), "payne_skill", i);
        HUSH_TRY(hush_launch_put_field(out, outsz, off, key,
                                       launch->payne_skills[i]));
    }
    return HUSH_OK;
}

static void hush_launch_take_payne_profile(hush_launch_t *launch,
                                           const char *json)
{
    char key[HUSH_LAUNCH_KEY_MAX];
    size_t i;
    size_t n;

    assert(launch != NULL);
    assert(json != NULL);
    (void)hush_launch_json_string(json, "payne_name", launch->payne_name,
                                  sizeof(launch->payne_name));
    (void)hush_launch_json_string(json, "payne_prompt", launch->payne_prompt,
                                  sizeof(launch->payne_prompt));
    (void)hush_launch_json_string(json, "payne_picture", launch->payne_picture,
                                  sizeof(launch->payne_picture));
    (void)hush_launch_json_string(json, "payne_voice", launch->payne_voice,
                                  sizeof(launch->payne_voice));
    launch->payne_enabled = 1;
    {
        char flag[2];

        if (hush_launch_json_string(json, "payne_enabled", flag, sizeof(flag))
            && flag[0] == '0')
            launch->payne_enabled = 0;
    }
    launch->npayne_skills = 0;
    n = hush_launch_json_count(json, "npayne_skills",
                               (size_t)HUSH_SKILL_EQUIP_MAX);
    for (i = 0; i < n; ++i) {
        hush_launch_index_key(key, sizeof(key), "payne_skill", i);
        if (!hush_launch_json_string(json, key,
                                     launch->payne_skills[launch->npayne_skills],
                                     sizeof(launch->payne_skills[0])))
            continue;
        launch->npayne_skills++;
    }
}

static hush_status_t hush_launch_put_agent_extras(const hush_roster_agent_t *agent,
                                                  size_t idx,
                                                  char *out, size_t outsz,
                                                  size_t *off)
{
    char key[HUSH_LAUNCH_KEY_MAX];
    char stem[HUSH_LAUNCH_KEY_MAX];
    char count[HUSH_LAUNCH_COUNT_MAX];
    size_t i;

    assert(agent != NULL);
    hush_launch_index_key(key, sizeof(key), "agent_picture", idx);
    HUSH_TRY(hush_launch_put_field(out, outsz, off, key, agent->picture));
    hush_launch_index_key(key, sizeof(key), "agent_voice", idx);
    HUSH_TRY(hush_launch_put_field(out, outsz, off, key, agent->voice));
    hush_launch_index_key(key, sizeof(key), "agent_enabled", idx);
    HUSH_TRY(hush_launch_put_field(out, outsz, off, key,
                                   agent->enabled ? "1" : "0"));
    hush_launch_index_key(key, sizeof(key), "agent_role", idx);
    HUSH_TRY(hush_launch_put_field(out, outsz, off, key,
                                   agent->role[0] ? agent->role
                                                  : HUSH_ROSTER_ROLE_WORKER));
    hush_launch_index_key(key, sizeof(key), "agent_locked", idx);
    HUSH_TRY(hush_launch_put_field(out, outsz, off, key,
                                   agent->locked ? "1" : "0"));
    if (snprintf(count, sizeof(count), "%zu", agent->nskills)
        >= (int)sizeof(count))
        return HUSH_ERR_FULL;
    hush_launch_index_key(key, sizeof(key), "agent_nskills", idx);
    HUSH_TRY(hush_launch_put_field(out, outsz, off, key, count));
    for (i = 0; i < agent->nskills; ++i) {
        if (snprintf(stem, sizeof(stem), "agent_%zu_skill", idx)
            >= (int)sizeof(stem))
            return HUSH_ERR_FULL;
        hush_launch_index_key(key, sizeof(key), stem, i);
        HUSH_TRY(hush_launch_put_field(out, outsz, off, key, agent->skills[i]));
    }
    return HUSH_OK;
}

static void hush_launch_take_agent_extras(hush_roster_agent_t *agent,
                                          const char *json, size_t idx)
{
    char key[HUSH_LAUNCH_KEY_MAX];
    char stem[HUSH_LAUNCH_KEY_MAX];
    size_t i;
    size_t n;

    assert(agent != NULL);
    assert(json != NULL);
    hush_launch_index_key(key, sizeof(key), "agent_picture", idx);
    (void)hush_launch_json_string(json, key, agent->picture,
                                  sizeof(agent->picture));
    hush_launch_index_key(key, sizeof(key), "agent_voice", idx);
    (void)hush_launch_json_string(json, key, agent->voice, sizeof(agent->voice));
    hush_launch_index_key(key, sizeof(key), "agent_enabled", idx);
    agent->enabled = 1;
    {
        char flag[2];

        if (hush_launch_json_string(json, key, flag, sizeof(flag))
            && flag[0] == '0')
            agent->enabled = 0;
    }
    hush_launch_index_key(key, sizeof(key), "agent_role", idx);
    (void)hush_launch_json_string(json, key, agent->role, sizeof(agent->role));
    if (!hush_roster_is_role(agent->role))
        hush_launch_copy_name(agent->role, sizeof(agent->role),
                              HUSH_ROSTER_ROLE_WORKER, "");
    hush_launch_index_key(key, sizeof(key), "agent_locked", idx);
    agent->locked = 0;
    {
        char flag[2];

        if (hush_launch_json_string(json, key, flag, sizeof(flag))
            && flag[0] == '1')
            agent->locked = 1;
    }
    hush_launch_index_key(key, sizeof(key), "agent_nskills", idx);
    n = hush_launch_json_count(json, key, (size_t)HUSH_SKILL_EQUIP_MAX);
    agent->nskills = 0;
    for (i = 0; i < n && agent->nskills < (size_t)HUSH_SKILL_EQUIP_MAX; ++i) {
        if (snprintf(stem, sizeof(stem), "agent_%zu_skill", idx)
            >= (int)sizeof(stem))
            continue;
        hush_launch_index_key(key, sizeof(key), stem, i);
        if (!hush_launch_json_string(json, key,
                                     agent->skills[agent->nskills],
                                     sizeof(agent->skills[0])))
            continue;
        agent->nskills++;
    }
}

static hush_status_t hush_launch_format_payne_tail(const hush_launch_t *launch,
                                                   char *out, size_t outsz,
                                                   size_t *off)
{
    size_t i;
    int n;

    assert(launch != NULL);
    assert(out != NULL);
    assert(off != NULL);
    n = snprintf(out + *off, outsz - *off, "[");
    if (n < 0 || *off + (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    *off += (size_t)n;
    for (i = 0; i < launch->npayne_skills; ++i) {
        n = snprintf(out + *off, outsz - *off, "%s\"%s\"",
                     (i == 0) ? "" : ",", launch->payne_skills[i]);
        if (n < 0 || *off + (size_t)n >= outsz)
            return HUSH_ERR_FULL;
        *off += (size_t)n;
    }
    n = snprintf(out + *off, outsz - *off, "]},\"channels\":[");
    if (n < 0 || *off + (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    *off += (size_t)n;
    return HUSH_OK;
}
