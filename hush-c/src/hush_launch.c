/* hush_launch.c: owns first-launch session, vibe, channels, projects, Payne. */

#include <assert.h>
#include <ctype.h>
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
    HUSH_LAUNCH_CMD_MAX = 768
};

#define HUSH_LAUNCH_DEFAULT_VIBE "local hive"
#define HUSH_LAUNCH_CHAN_GENERAL "general"
#define HUSH_LAUNCH_CHAN_WELCOME "welcome"
#define HUSH_LAUNCH_CHAN_AGENTS "agents"

/* Copies text, trimmed, into dst. Empty becomes fallback. */
static void hush_launch_copy_name(char *dst, size_t dstsz,
                                  const char *text, const char *fallback);

/* Writes a lowercase slug of name into dst. */
static void hush_launch_slugify(char *dst, size_t dstsz, const char *name);

/* True when slug already exists in the channel table. */
static int hush_launch_has_channel(const hush_launch_t *launch, const char *slug);

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

/* Appends the projects array body. */
static hush_status_t hush_launch_format_projects(const hush_launch_t *launch,
                                                 char *out, size_t outsz,
                                                 size_t *off);

/* Appends roster JSON and closes the session object. */
static hush_status_t hush_launch_format_roster(const hush_launch_t *launch,
                                               char *out, size_t outsz,
                                               size_t *off);

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
    return hush_roster_set_profile(&launch->roster, in);
}

hush_status_t hush_launch_add_member(hush_launch_t *launch,
                                     const char *key,
                                     const char *name)
{
    if (launch == NULL || key == NULL)
        return HUSH_ERR_ARG;
    if (!launch->has_vibe)
        return HUSH_ERR_ARG;
    return hush_roster_add_member(&launch->roster, key, name);
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
    return hush_roster_add_agent(&launch->roster, store, in, save_pass);
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
    return HUSH_OK;
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
    return HUSH_OK;
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
    return hush_launch_push_channel(launch, name);
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
    return HUSH_OK;
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
        if (*off + 64 >= outsz)
            return HUSH_ERR_FULL;
        hush_launch_json_escape(launch->channels[i].name, esc_name,
                                sizeof(esc_name));
        n = snprintf(out + *off, outsz - *off,
                     "%s{\"name\":\"%s\",\"slug\":\"%s\"}",
                     (i == 0) ? "" : ",",
                     esc_name, launch->channels[i].slug);
        if (n < 0)
            return HUSH_ERR_FULL;
        *off += (size_t)n;
    }
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
