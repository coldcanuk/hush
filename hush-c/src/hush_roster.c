/* hush_roster.c: owns vibe members, agents, themes, and context MIME checks. */

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "hush_event.h"
#include "hush_home.h"
#include "hush_pass.h"
#include "hush_provider.h"
#include "hush_roster.h"
#include "hush_skill.h"

enum {
    HUSH_ROSTER_KIND_META = 0,
    HUSH_ROSTER_KIND_NOTE = 1,
    HUSH_ROSTER_THEME_COUNT = 7,
    HUSH_ROSTER_ROLE_COUNT = 2,
    /* A new robot whose id is held gets "-2", then "-3", and so on. */
    HUSH_ROSTER_SUFFIX_FIRST = 2,
    /* Room for "-" plus the digits of the last suffix tried, and the NUL. */
    HUSH_ROSTER_SUFFIX_MAX = 8
};

#define HUSH_ROSTER_CHAN_AGENTS "agents"

static const char *const hush_roster_themes[HUSH_ROSTER_THEME_COUNT] = {
    "dark",
    "light",
    "color-blind",
    "dracula",
    "desert",
    "monochrome",
    "christmas"
};

static const char *const hush_roster_roles[HUSH_ROSTER_ROLE_COUNT] = {
    HUSH_ROSTER_ROLE_WORKER,
    HUSH_ROSTER_ROLE_CHAPERON
};

/* Copies trimmed text into dst. Empty becomes fallback (may be ""). */
static void hush_roster_copy_text(char *dst, size_t dstsz,
                                  const char *text, const char *fallback);

/* Writes a lowercase slug of name into dst. */
static void hush_roster_slugify(char *dst, size_t dstsz, const char *name);

/* True when a robot's id is slug, or slug is Payne's reserved id. */
static int hush_roster_is_id_taken(const hush_roster_t *roster,
                                   const char *slug);

/* Makes slug an id hush_roster_is_id_taken refuses no longer: a held id
 * becomes the first free "<slug>-2", "<slug>-3", ... */
static void hush_roster_free_slug(const hush_roster_t *roster,
                                  char *slug, size_t slugsz);

/* Writes base plus "-n" into slug, cutting base (and a dash it would end
 * on) so the whole id fits slugsz. */
static void hush_roster_suffix_slug(char *slug, size_t slugsz,
                                    const char *base, size_t n);

/* True when pubkey already listed. */
static int hush_roster_has_member(const hush_roster_t *roster,
                                  const char *pubkey_hex);

/* Copies one context slot after a MIME check. */
static hush_status_t hush_roster_copy_context(hush_roster_context_t *dst,
                                              const hush_roster_context_in_t *src);

/* Inserts a kind 0 profile for the agent. */
static hush_status_t hush_roster_store_agent_profile(hush_store_t *store,
                                                     const hush_roster_agent_t *agent);

/* Inserts a kind 1 note in #agents announcing the robot. */
static hush_status_t hush_roster_store_agent_note(hush_store_t *store,
                                                  const hush_roster_agent_t *agent);

/* Fills a stored event skeleton. */
static void hush_roster_fill_event(hush_event_t *ev, const char *pubkey_hex,
                                   uint32_t kind, const char *content,
                                   const char *channel);

/* JSON-escapes src into dst. */
static size_t hush_roster_json_escape(const char *src, char *dst, size_t dstsz);

/* Best-effort pass insert. Never fails the caller. */
static void hush_roster_try_save_agent(const char *slug, const char *secret);

/* Decodes npub1… or 64-hex into pubkey hex + npub. */
static hush_status_t hush_roster_parse_pubkey(char *out_hex, char *out_npub,
                                              const char *key);

/* Writes hex digits of 32 raw bytes. */
static void hush_roster_hex_encode(char *out65, const unsigned char *raw);

/* Appends the profile object. */
static hush_status_t hush_roster_format_profile(const hush_roster_t *roster,
                                                char *out, size_t outsz,
                                                size_t *off);

/* Appends the agents array body. */
static hush_status_t hush_roster_format_agents(const hush_roster_t *roster,
                                               char *out, size_t outsz,
                                               size_t *off);

/* Appends the members array and closing. */
static hush_status_t hush_roster_format_members(const hush_roster_t *roster,
                                                char *out, size_t outsz,
                                                size_t *off);

/* Copies name, slug, prompt, provider, picture, voice, skills from in. */
static hush_status_t hush_roster_fill_agent(hush_roster_t *roster,
                                            hush_roster_agent_t *agent,
                                            const hush_roster_agent_in_t *in);

/* Copies equipped skill ids. Caps at HUSH_SKILL_EQUIP_MAX. */
static hush_status_t hush_roster_copy_skills(hush_roster_agent_t *agent,
                                             const hush_roster_agent_in_t *in);

/* Applies intro_enabled and intro from in. Defaults stay when flags are off. */
static void hush_roster_apply_intro(hush_roster_agent_t *agent,
                                    const hush_roster_agent_in_t *in);

/* Appends intro_enabled and intro, then closes the agent object. */
static hush_status_t hush_roster_format_intro(const hush_roster_agent_t *agent,
                                              char *out, size_t outsz,
                                              size_t *off);

/* Index of the agent whose id is slug; nagents when none is. */
static size_t hush_roster_agent_index(const hush_roster_t *roster,
                                      const char *slug);

/* Finds an agent by slug. NULL when missing. */
static hush_roster_agent_t *hush_roster_find_agent(hush_roster_t *roster,
                                                   const char *slug);

/* Applies update fields onto an existing agent once
 * hush_roster_check_update passes; a refused update writes nothing. */
static hush_status_t hush_roster_apply_update(const hush_roster_t *roster,
                                              hush_roster_agent_t *agent,
                                              const hush_roster_agent_in_t *in);

/* Checks every field of an update before any is written: a new name must
 * be non-blank and must not clash with another robot's name; providers, voice,
 * role and skill count must be valid. HUSH_OK when apply_update may write. */
static hush_status_t hush_roster_check_update(const hush_roster_t *roster,
                                              const hush_roster_agent_t *agent,
                                              const hush_roster_agent_in_t *in);

/* True when a rename to name keeps the rules: the current name unchanged
 * (even an older one that breaks them), or non-blank after trimming, at
 * least one letter A-Z or digit, and no clash with another robot's current
 * name. The robot's id never moves. */
static int hush_roster_rename_ok(const hush_roster_t *roster,
                                 const hush_roster_agent_t *agent,
                                 const char *name);

/* True when hush_roster_copy_providers would accept in's provider list. */
static int hush_roster_providers_ok(const hush_roster_agent_in_t *in);

/* Appends one agent object. */
static hush_status_t hush_roster_format_one_agent(const hush_roster_agent_t *agent,
                                                  char *out, size_t outsz,
                                                  size_t *off, int first);

/* Appends the equipped skills array. */
static hush_status_t hush_roster_format_skills(const hush_roster_agent_t *agent,
                                               char *out, size_t outsz,
                                               size_t *off);

/* Writes a session-safe prompt preview into dst. */
static void hush_roster_preview_prompt(char *dst, size_t dstsz,
                                       const char *prompt);

/* True when slug is Payne's reserved organizer slug. */
static int hush_roster_is_payne_slug(const char *slug);

/* Compacts the agent table after removing index. */
static void hush_roster_compact_agents(hush_roster_t *roster, size_t idx);

/* Copies context slots after MIME checks. */
static hush_status_t hush_roster_fill_context(hush_roster_agent_t *agent,
                                              const hush_roster_agent_in_t *in);

void hush_roster_slug_of(char *out, size_t outsz, const char *name)
{
    char trimmed[HUSH_ROSTER_NAME_MAX];

    if (out == NULL || outsz == 0)
        return;
    /* Same two steps as hush_roster_fill_agent: trim, then slugify. */
    hush_roster_copy_text(trimmed, sizeof(trimmed), name, "");
    hush_roster_slugify(out, outsz, trimmed);
}

void hush_roster_name_key(char *out, size_t outsz, const char *name)
{
    size_t i = 0;
    size_t o = 0;

    if (out == NULL || outsz == 0)
        return;
    if (name == NULL)
        name = "";
    /* The slug's letters and digits (C locale), without its '-' breaks. */
    while (name[i] != '\0' && o + 1 < outsz) {
        unsigned char c = (unsigned char)name[i++];

        if (isalnum(c))
            out[o++] = (char)tolower(c);
    }
    out[o] = '\0';
}

int hush_roster_is_name_clash(const char *name, const char *other)
{
    char want[HUSH_ROSTER_NAME_MAX] = {0};
    char have[HUSH_ROSTER_NAME_MAX] = {0};

    if (name == NULL || other == NULL)
        return 0;
    hush_roster_name_key(want, sizeof(want), name);
    hush_roster_name_key(have, sizeof(have), other);
    return want[0] != '\0' && strcmp(have, want) == 0;
}

int hush_roster_is_same_name(const char *name, const char *current)
{
    char trimmed[HUSH_ROSTER_NAME_MAX] = {0};

    if (name == NULL || current == NULL)
        return 0;
    hush_roster_copy_text(trimmed, sizeof(trimmed), name, "");
    return strcmp(trimmed, current) == 0;
}

const hush_roster_agent_t *
hush_roster_name_holder(const hush_roster_t *roster, const char *name,
                        const hush_roster_agent_t *except)
{
    size_t i = 0;

    if (roster == NULL || name == NULL)
        return NULL;
    for (i = 0; i < roster->nagents; i++) {
        if (&roster->agents[i] != except &&
            hush_roster_is_name_clash(name, roster->agents[i].name))
            return &roster->agents[i];
    }
    return NULL;
}

const hush_roster_agent_t *hush_roster_agent_by_slug(const hush_roster_t *roster,
                                                     const char *slug)
{
    size_t i = 0;

    if (roster == NULL || slug == NULL)
        return NULL;
    i = hush_roster_agent_index(roster, slug);
    return i < roster->nagents ? &roster->agents[i] : NULL;
}

void hush_roster_init(hush_roster_t *roster)
{
    if (roster == NULL)
        return;
    memset(roster, 0, sizeof(*roster));
    memcpy(roster->profile.theme, HUSH_ROSTER_THEME_DEFAULT,
           sizeof(HUSH_ROSTER_THEME_DEFAULT));
}

int hush_roster_is_context_mime(const char *mime, const char *filename)
{
    const char *dot;

    if (mime != NULL) {
        if (strcmp(mime, HUSH_ROSTER_MIME_PLAIN) == 0)
            return 1;
        if (strcmp(mime, HUSH_ROSTER_MIME_MARKDOWN) == 0)
            return 1;
        if (strcmp(mime, HUSH_ROSTER_MIME_XMARKDOWN) == 0)
            return 1;
    }
    if (filename == NULL)
        return 0;
    dot = strrchr(filename, '.');
    if (dot == NULL)
        return 0;
    if (strcmp(dot, ".txt") == 0)
        return 1;
    if (strcmp(dot, ".md") == 0)
        return 1;
    if (strcmp(dot, ".markdown") == 0)
        return 1;
    return 0;
}

int hush_roster_is_theme(const char *theme)
{
    size_t i;

    if (theme == NULL || theme[0] == '\0')
        return 0;
    for (i = 0; i < (size_t)HUSH_ROSTER_THEME_COUNT; ++i) {
        if (strcmp(theme, hush_roster_themes[i]) == 0)
            return 1;
    }
    return 0;
}

int hush_roster_is_provider(const char *provider)
{
    /* Single source of truth: the provider meta table in hush_provider.c. */
    return hush_provider_is_id(provider);
}

int hush_roster_is_role(const char *role)
{
    size_t i;

    if (role == NULL || role[0] == '\0')
        return 0;
    for (i = 0; i < (size_t)HUSH_ROSTER_ROLE_COUNT; ++i) {
        if (strcmp(role, hush_roster_roles[i]) == 0)
            return 1;
    }
    return 0;
}

hush_status_t hush_roster_set_profile(hush_roster_t *roster,
                                      const hush_roster_profile_t *in)
{
    if (roster == NULL || in == NULL)
        return HUSH_ERR_ARG;
    if (in->theme[0] != '\0' && !hush_roster_is_theme(in->theme))
        return HUSH_ERR_PARSE;
    hush_roster_copy_text(roster->profile.first_name,
                          sizeof(roster->profile.first_name),
                          in->first_name, "");
    hush_roster_copy_text(roster->profile.last_name,
                          sizeof(roster->profile.last_name),
                          in->last_name, "");
    hush_roster_copy_text(roster->profile.email,
                          sizeof(roster->profile.email),
                          in->email, "");
    hush_roster_copy_text(roster->profile.organization,
                          sizeof(roster->profile.organization),
                          in->organization, "");
    if (in->theme[0] != '\0')
        hush_roster_copy_text(roster->profile.theme,
                              sizeof(roster->profile.theme),
                              in->theme, HUSH_ROSTER_THEME_DEFAULT);
    if (in->picture[0] != '\0')
        hush_roster_copy_text(roster->profile.picture,
                              sizeof(roster->profile.picture),
                              in->picture, "");
    return HUSH_OK;
}

hush_status_t hush_roster_add_member(hush_roster_t *roster,
                                     const char *key,
                                     const char *name)
{
    hush_roster_member_t *mem;
    hush_status_t st;

    if (roster == NULL || key == NULL)
        return HUSH_ERR_ARG;
    if (roster->nmembers >= (size_t)HUSH_ROSTER_MEMBERS_MAX)
        return HUSH_ERR_FULL;
    mem = &roster->members[roster->nmembers];
    memset(mem, 0, sizeof(*mem));
    st = hush_roster_parse_pubkey(mem->pubkey_hex, mem->npub, key);
    if (st != HUSH_OK)
        return st;
    if (hush_roster_has_member(roster, mem->pubkey_hex))
        return HUSH_OK;
    hush_roster_copy_text(mem->name, sizeof(mem->name), name, "human");
    roster->nmembers++;
    return HUSH_OK;
}

/* Copies in->providers (or the single in->provider fallback) into agent as a
 * validated ranked list, and mirrors index 0 into agent->provider. */
static hush_status_t hush_roster_copy_providers(hush_roster_agent_t *agent,
                                                const hush_roster_agent_in_t *in)
{
    size_t i;
    size_t n = 0;

    assert(agent != NULL);
    assert(in != NULL);
    if (in->has_providers && in->nproviders > 0) {
        for (i = 0; i < in->nproviders &&
                    n < (size_t)HUSH_ROSTER_PROVIDERS_MAX; i++) {
            if (in->providers[i][0] == '\0')
                continue;
            if (!hush_roster_is_provider(in->providers[i]))
                return HUSH_ERR_PARSE;
            hush_roster_copy_text(agent->providers[n],
                                  sizeof(agent->providers[n]),
                                  in->providers[i], "");
            n++;
        }
    }
    if (n == 0) {
        hush_roster_copy_text(agent->providers[0],
                              sizeof(agent->providers[0]), in->provider, "");
        n = 1;
    }
    agent->nproviders = n;
    hush_roster_copy_text(agent->provider, sizeof(agent->provider),
                          agent->providers[0], "");
    if (!hush_roster_is_provider(agent->provider))
        return HUSH_ERR_PARSE;
    return HUSH_OK;
}

hush_status_t hush_roster_add_agent(hush_roster_t *roster,
                                    hush_store_t *store,
                                    const hush_roster_agent_in_t *in,
                                    int save_pass)
{
    hush_roster_agent_t *agent;
    hush_status_t st;

    if (roster == NULL || store == NULL || in == NULL)
        return HUSH_ERR_ARG;
    if (roster->nagents >= (size_t)HUSH_ROSTER_AGENTS_MAX)
        return HUSH_ERR_FULL;
    agent = &roster->agents[roster->nagents];
    memset(agent, 0, sizeof(*agent));
    st = hush_roster_fill_agent(roster, agent, in);
    if (st != HUSH_OK)
        return st;
    st = hush_roster_fill_context(agent, in);
    if (st != HUSH_OK)
        return st;
    if (hush_identity_generate(&agent->id) != HUSH_OK)
        return HUSH_ERR_CRYPTO;
    if (save_pass)
        hush_roster_try_save_agent(agent->slug, agent->id.nsec);
    /* Disk copy survives a restart when pass is missing. Templates pass
     * save_pass 0, so this is their only copy. */
    if (hush_home_store_agent_nsec(agent->slug, agent->id.nsec) != HUSH_OK)
        return HUSH_ERR_IO;
    if (hush_roster_store_agent_profile(store, agent) != HUSH_OK)
        return HUSH_ERR_FULL;
    if (hush_roster_store_agent_note(store, agent) != HUSH_OK)
        return HUSH_ERR_FULL;
    roster->nagents++;
    return HUSH_OK;
}
