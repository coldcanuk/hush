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
    HUSH_LAUNCH_JSON_MAX = 16384
};

#define HUSH_LAUNCH_PASS_FAIL "pass helper failed"

#define HUSH_LAUNCH_PAYNE_NAME "Sgt Major Payne"
#define HUSH_LAUNCH_PAYNE_SLUG "sgt-major-payne"
#define HUSH_LAUNCH_PAYNE_ABOUT \
    "Organizes single agents, squads, teams, and swarms. Finds the right robot, or builds one."

typedef struct {
    char name[HUSH_LAUNCH_NAME_MAX];
    char slug[HUSH_LAUNCH_NAME_MAX];
} hush_launch_channel_t;

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
    char vibe_name[HUSH_LAUNCH_NAME_MAX];
    char vibe_about[HUSH_LAUNCH_ABOUT_MAX];
    int vibe_public;
    char vibe_token[HUSH_LAUNCH_NAME_MAX];
    hush_launch_channel_t channels[HUSH_LAUNCH_CHANNELS_MAX];
    size_t nchannels;
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

#endif /* HUSH_LAUNCH_H */
