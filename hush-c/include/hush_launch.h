/* hush_launch.h: first-launch session, vibe, channels, projects, Payne. */

#ifndef HUSH_LAUNCH_H
#define HUSH_LAUNCH_H

#include <stddef.h>
#include <stdint.h>
#include "hush_identity.h"
#include "hush_pass.h"
#include "hush_roster.h"
#include "hush_status.h"
#include "hush_store.h"

enum {
    HUSH_LAUNCH_NAME_MAX = 64,
    HUSH_LAUNCH_ABOUT_MAX = 256,
    HUSH_LAUNCH_PATH_MAX = 256,
    HUSH_LAUNCH_CHANNELS_MAX = 16,
    HUSH_LAUNCH_PROJECTS_MAX = 16,
    HUSH_LAUNCH_GROUPS_MAX = 8,
    HUSH_LAUNCH_CHAN_HUMANS_MAX = 8,
    HUSH_LAUNCH_CHAN_ROBOTS_MAX = 8,
    HUSH_LAUNCH_ID_HEX = 32,
    HUSH_LAUNCH_JSON_MAX = 32768,
    HUSH_LAUNCH_POLICY_MAX = 16,
    HUSH_LAUNCH_BURST_MS_FAST = 500,
    HUSH_LAUNCH_BURST_MS_DEFAULT = 2000,
    HUSH_LAUNCH_BURST_MS_SLOW = 5000,
    HUSH_LAUNCH_MAX_JOBS_MIN = 1,
    HUSH_LAUNCH_MAX_JOBS_DEFAULT = 2,
    HUSH_LAUNCH_MAX_JOBS_MAX = 4,
    HUSH_LAUNCH_COOLDOWN_S_OFF = 0,
    HUSH_LAUNCH_COOLDOWN_S_DEFAULT = 10,
    HUSH_LAUNCH_COOLDOWN_S_LONG = 30,
    HUSH_LAUNCH_PAYNE_PROVIDERS_MAX = 4
};

#define HUSH_LAUNCH_KIND_OPEN "open"
#define HUSH_LAUNCH_KIND_HUMANS "humans"
#define HUSH_LAUNCH_KIND_ROBOTS "robots"
#define HUSH_LAUNCH_KIND_MIXED "mixed"
#define HUSH_LAUNCH_REPLY_OFF "off"
#define HUSH_LAUNCH_REPLY_MENTION "mention"
#define HUSH_LAUNCH_REPLY_CONFIRM "confirm"

#define HUSH_LAUNCH_PASS_FAIL "pass helper failed"

#define HUSH_LAUNCH_PAYNE_NAME "Major"
#define HUSH_LAUNCH_PAYNE_SLUG "sgt-major-payne"
#define HUSH_LAUNCH_PAYNE_ABOUT \
    "Organizes single agents, squads, teams, and swarms. Finds the right robot, or builds one."

typedef struct {
    char name[HUSH_LAUNCH_NAME_MAX];
    char slug[HUSH_LAUNCH_NAME_MAX];
    char id[HUSH_LAUNCH_ID_HEX + 1];
    char group_id[HUSH_LAUNCH_ID_HEX + 1];
    char humans[HUSH_LAUNCH_CHAN_HUMANS_MAX][HUSH_IDENTITY_NPUB_MAX];
    size_t nhumans;
    char robots[HUSH_LAUNCH_CHAN_ROBOTS_MAX][HUSH_LAUNCH_NAME_MAX];
    size_t nrobots;
    char kind[HUSH_LAUNCH_POLICY_MAX];
    char robot_reply[HUSH_LAUNCH_POLICY_MAX];
    int robot_talk;
    int burst_ms;
    int max_jobs;
    int cooldown_s;
    int robot_hops;
    /* Optional topic/about for this channel. When set, injected into robot
     * system prompts for jobs on this channel (quick LLM context pointer). */
    char about[HUSH_LAUNCH_ABOUT_MAX];
} hush_launch_channel_t;

typedef struct {
    char kind[HUSH_LAUNCH_POLICY_MAX];
    char robot_reply[HUSH_LAUNCH_POLICY_MAX];
    int robot_talk;
    int burst_ms;
    int max_jobs;
    int cooldown_s;
    int robot_hops;
} hush_launch_policy_t;

typedef struct {
    char name[HUSH_LAUNCH_NAME_MAX];
    char id[HUSH_LAUNCH_ID_HEX + 1];
} hush_launch_group_t;

typedef struct {
    char name[HUSH_LAUNCH_NAME_MAX];
    char slug[HUSH_LAUNCH_NAME_MAX];
    char path[HUSH_LAUNCH_PATH_MAX];
} hush_launch_project_t;

typedef struct {
    int logged_in;
    int backup_acked;
    int has_vibe;
    int save_pass;
    int pass_saved;
    char pass_error[HUSH_PASS_ERR_MAX];
    hush_identity_t human;
    hush_identity_t payne;
    char payne_providers[HUSH_LAUNCH_PAYNE_PROVIDERS_MAX][HUSH_ROSTER_PROVIDER_MAX];
    size_t npayne_providers;
    char payne_name[HUSH_LAUNCH_NAME_MAX];
    char payne_prompt[HUSH_ROSTER_PROMPT_MAX];
    char payne_picture[HUSH_ROSTER_PATH_MAX];
    char payne_voice[HUSH_SKILL_VOICE_MAX];
    char payne_skills[HUSH_SKILL_EQUIP_MAX][HUSH_SKILL_ID_MAX];
    size_t npayne_skills;
    int payne_enabled;
    char vibe_name[HUSH_LAUNCH_NAME_MAX];
    char vibe_about[HUSH_LAUNCH_ABOUT_MAX];
    int vibe_public;
    char vibe_token[HUSH_LAUNCH_NAME_MAX];
    /* Developer Logging (default 0 = off/disabled).
     * When 1: "Mention received.", on-deck intros, internal debug route to
     * a separate panel (syslog format). Suppressed from main chat stream.
     * See M3.1, UI_SPEC §6, and PLAN_CHAT_ROBOTS_INVENTORY.md. */
    int dev_log_enabled;
    hush_launch_channel_t channels[HUSH_LAUNCH_CHANNELS_MAX];
    size_t nchannels;
    hush_launch_group_t groups[HUSH_LAUNCH_GROUPS_MAX];
    size_t ngroups;
    hush_launch_project_t projects[HUSH_LAUNCH_PROJECTS_MAX];
    size_t nprojects;
    hush_roster_t roster;
} hush_launch_t;

/* Zeros session. Safe on NULL. */
void hush_launch_init(hush_launch_t *launch);

/* Creates a human identity. Overwrites a previous login. */
hush_status_t hush_launch_create_identity(hush_launch_t *launch);

/* Imports nsec1… or 64-char hex. */
hush_status_t hush_launch_import_identity(hush_launch_t *launch,
                                          const char *secret);

/* Marks the backup step done. save_pass 1 (default) writes the nsec
 * to pass. Fails if not logged in. */
hush_status_t hush_launch_ack_backup(hush_launch_t *launch, int save_pass);

/* Loads hush/identity/nsec when present. Soft-fails if pass is absent. */
hush_status_t hush_launch_restore_identity(hush_launch_t *launch);

/* Writes non-secret hive metadata to hush/vibe.json. No-op without a vibe. */
hush_status_t hush_launch_save_vibe(const hush_launch_t *launch);

/* Loads hush/vibe.json when present. Soft-fails if the file is absent. */
hush_status_t hush_launch_restore_vibe(hush_launch_t *launch);

/* Clears the human login. Vibe and roster stay. */
hush_status_t hush_launch_logout(hush_launch_t *launch);

/* Copies profile fields onto the roster. Rejects a bad theme. */
hush_status_t hush_launch_set_profile(hush_launch_t *launch,
                                      const hush_roster_profile_t *in);

/* Adds a human member by npub. Requires a vibe. */
hush_status_t hush_launch_add_member(hush_launch_t *launch,
                                     const char *key,
                                     const char *name);

/* Raises an agent on the roster. Requires a vibe. */
hush_status_t hush_launch_add_agent(hush_launch_t *launch,
                                    hush_store_t *store,
                                    const hush_roster_agent_in_t *in,
                                    int save_pass);

/* Drops a raised agent by slug. Payne is refused. */
hush_status_t hush_launch_remove_agent(hush_launch_t *launch, const char *slug);

/* Updates a raised agent's name, prompt, picture, voice, and skills. */
hush_status_t hush_launch_update_agent(hush_launch_t *launch, const char *slug,
                                       const hush_roster_agent_in_t *in);

/* Updates Payne picture, voice, skills, and enabled. Name and prompt stay
 * locked to the platform identity. */
hush_status_t hush_launch_update_payne_profile(hush_launch_t *launch,
                                               const hush_roster_agent_in_t *in);

/* Stored Payne display name, or HUSH_LAUNCH_PAYNE_NAME. */
const char *hush_launch_payne_name(const hush_launch_t *launch);

/* Stored Payne system prompt, or HUSH_LAUNCH_PAYNE_ABOUT. */
const char *hush_launch_payne_prompt(const hush_launch_t *launch);

/* Replaces Payne's ranked provider list. Name and about stay locked.
 * Empty or all-invalid ids restore to goose. Requires a vibe. */
hush_status_t hush_launch_set_payne_providers(hush_launch_t *launch,
                                              const char *const *ids,
                                              size_t nids);

/* Names this relay and seeds starter channels + Payne. */
hush_status_t hush_launch_create_vibe(hush_launch_t *launch,
                                      hush_store_t *store,
                                      const char *name,
                                      const char *about);

/* public=1 discoverable; public=0 requires join token. */
hush_status_t hush_launch_set_vibe_visibility(hush_launch_t *launch,
                                              int is_public);

/* Adds an open channel. Fails HUSH_ERR_FULL at cap. */
hush_status_t hush_launch_add_channel(hush_launch_t *launch,
                                      const char *name);

/* Drops a channel by slug. Refuses the last remaining channel. */
hush_status_t hush_launch_remove_channel(hush_launch_t *launch,
                                         const char *slug);

/* Creates a parent group. Fails HUSH_ERR_FULL at cap. */
hush_status_t hush_launch_add_group(hush_launch_t *launch, const char *name);

/* Sets or clears channel.group_id. Empty group_id ungroups. */
hush_status_t hush_launch_set_channel_group(hush_launch_t *launch,
                                            const char *slug,
                                            const char *group_id);

/* Replaces the channel human/robot lists. Empty counts mean whole hive. */
hush_status_t hush_launch_set_channel_roster(hush_launch_t *launch,
                                             const char *slug,
                                             const char *const *humans,
                                             size_t nhumans,
                                             const char *const *robots,
                                             size_t nrobots);

/* Writes default open/mention policy onto ch. Ignores NULL. */
void hush_launch_policy_default(hush_launch_channel_t *ch);

/* Copies ch policy into out. Fails HUSH_ERR_ARG on NULL. */
hush_status_t hush_launch_policy_copy(hush_launch_policy_t *out,
                                      const hush_launch_channel_t *ch);

/* Replaces channel policy. Unknown kind/reply fail HUSH_ERR_PARSE. */
hush_status_t hush_launch_set_channel_policy(hush_launch_t *launch,
                                             const char *slug,
                                             const hush_launch_policy_t *in);

/* Records a project and optionally runs git init at path. */
hush_status_t hush_launch_add_project(hush_launch_t *launch,
                                      hush_store_t *store,
                                      const char *name,
                                      const char *path,
                                      int init_git);

/* Writes session JSON. Truncates if outsz is too small. */
hush_status_t hush_launch_format_session(const hush_launch_t *launch,
                                         uint16_t port,
                                         char *out, size_t outsz,
                                         size_t *out_len);

/* True when the hive UI may render (logged in + vibe). */
int hush_launch_is_ready(const hush_launch_t *launch);

/* Returns the about/topic string for a channel slug, or empty string if none.
 * Safe on NULL launch or unknown slug. */
const char *hush_launch_channel_about(const hush_launch_t *launch, const char *slug);

/* Replaces channel about/topic text. Empty about clears. Requires a vibe. */
hush_status_t hush_launch_set_channel_about(hush_launch_t *launch,
                                            const char *slug,
                                            const char *about);

#endif /* HUSH_LAUNCH_H */
/* M2 Architecture lock (chat-robots-inventory-ui):
 * - Developer Logging: new toggle (default 0/off). When on, "Mention received.",
 *   on-deck intros, and debug lines go to a separate panel (syslog fmt).
 *   Suppressed from main chat stream when disabled.
 * - Robots inventory: web grid primary (CSS/JS 2D spatial in embed).
 *   Raylib reference only in examples/inventory-raylib (optional, no dep).
 * - Mention fidelity: original @ positions preserved (non-destructive render).
 * - Progressive acks: thinking/reacting states → emoji-only final ack.
 * - Single robot intro guard + multi-robot deliberation via co_npubs + prompt.
 * See PLAN_CHAT_ROBOTS_INVENTORY.md and UI_SPEC.md §M2.
 */
