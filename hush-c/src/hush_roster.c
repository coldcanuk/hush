/* hush_roster.c: owns vibe members, agents, themes, and context MIME checks. */

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "hush_event.h"
#include "hush_pass.h"
#include "hush_roster.h"
#include "hush_skill.h"

enum {
    HUSH_ROSTER_KIND_META = 0,
    HUSH_ROSTER_KIND_NOTE = 1,
    HUSH_ROSTER_ID_WIDTH = 16,
    HUSH_ROSTER_SLUG_FALLBACK = 'a',
    HUSH_ROSTER_THEME_COUNT = 7,
    HUSH_ROSTER_PROVIDER_COUNT = 9,
    HUSH_ROSTER_ROLE_COUNT = 2
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

static const char *const hush_roster_providers[HUSH_ROSTER_PROVIDER_COUNT] = {
    HUSH_ROSTER_PROVIDER_GOOSE,
    HUSH_ROSTER_PROVIDER_GROK_BUILD,
    HUSH_ROSTER_PROVIDER_CODEX,
    HUSH_ROSTER_PROVIDER_CLINE,
    HUSH_ROSTER_PROVIDER_GEMINI,
    HUSH_ROSTER_PROVIDER_XAI,
    HUSH_ROSTER_PROVIDER_OPENAI,
    HUSH_ROSTER_PROVIDER_ANTHROPIC,
    HUSH_ROSTER_PROVIDER_DEEPSEEK
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

/* True when slug already exists. */
static int hush_roster_has_agent_slug(const hush_roster_t *roster,
                                      const char *slug);

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

/* Writes a deterministic hex id from time + seq. */
static void hush_roster_make_id(char *out65);

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

/* Finds an agent by slug. NULL when missing. */
static hush_roster_agent_t *hush_roster_find_agent(hush_roster_t *roster,
                                                   const char *slug);

/* Applies update fields onto an existing agent. */
static hush_status_t hush_roster_apply_update(hush_roster_agent_t *agent,
                                              const hush_roster_agent_in_t *in);

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
    size_t i;

    if (provider == NULL || provider[0] == '\0')
        return 0;
    for (i = 0; i < (size_t)HUSH_ROSTER_PROVIDER_COUNT; ++i) {
        if (strcmp(provider, hush_roster_providers[i]) == 0)
            return 1;
    }
    return 0;
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
    if (hush_roster_store_agent_profile(store, agent) != HUSH_OK)
        return HUSH_ERR_FULL;
    if (hush_roster_store_agent_note(store, agent) != HUSH_OK)
        return HUSH_ERR_FULL;
    roster->nagents++;
    return HUSH_OK;
}

hush_status_t hush_roster_remove_agent(hush_roster_t *roster, const char *slug)
{
    size_t i;

    if (roster == NULL || slug == NULL || slug[0] == '\0')
        return HUSH_ERR_ARG;
    if (hush_roster_is_payne_slug(slug))
        return HUSH_ERR_DENIED;
    for (i = 0; i < roster->nagents; ++i) {
        if (strcmp(roster->agents[i].slug, slug) != 0)
            continue;
        hush_roster_compact_agents(roster, i);
        return HUSH_OK;
    }
    return HUSH_ERR_NOT_FOUND;
}

hush_status_t hush_roster_update_agent(hush_roster_t *roster, const char *slug,
                                       const hush_roster_agent_in_t *in)
{
    hush_roster_agent_t *agent;

    if (roster == NULL || slug == NULL || slug[0] == '\0' || in == NULL)
        return HUSH_ERR_ARG;
    if (hush_roster_is_payne_slug(slug))
        return HUSH_ERR_DENIED;
    agent = hush_roster_find_agent(roster, slug);
    if (agent == NULL)
        return HUSH_ERR_NOT_FOUND;
    return hush_roster_apply_update(agent, in);
}

hush_status_t hush_roster_format_json(const hush_roster_t *roster,
                                      char *out, size_t outsz,
                                      size_t *out_len)
{
    size_t off = 0;

    if (roster == NULL || out == NULL || outsz == 0)
        return HUSH_ERR_ARG;
    HUSH_TRY(hush_roster_format_profile(roster, out, outsz, &off));
    HUSH_TRY(hush_roster_format_agents(roster, out, outsz, &off));
    HUSH_TRY(hush_roster_format_members(roster, out, outsz, &off));
    if (out_len != NULL)
        *out_len = off;
    return HUSH_OK;
}

static hush_status_t hush_roster_fill_agent(hush_roster_t *roster,
                                            hush_roster_agent_t *agent,
                                            const hush_roster_agent_in_t *in)
{
    assert(roster != NULL);
    assert(agent != NULL);
    assert(in != NULL);
    hush_roster_copy_text(agent->name, sizeof(agent->name), in->name, "");
    if (agent->name[0] == '\0')
        return HUSH_ERR_PARSE;
    hush_roster_slugify(agent->slug, sizeof(agent->slug), agent->name);
    if (hush_roster_has_agent_slug(roster, agent->slug))
        return HUSH_ERR_PARSE;
    hush_roster_copy_text(agent->prompt, sizeof(agent->prompt), in->prompt, "");
    if (agent->prompt[0] == '\0')
        return HUSH_ERR_PARSE;
    hush_roster_copy_text(agent->provider, sizeof(agent->provider),
                          in->provider, "");
    if (!hush_roster_is_provider(agent->provider))
        return HUSH_ERR_PARSE;
    hush_roster_copy_text(agent->picture, sizeof(agent->picture),
                          in->picture, "");
    if (in->voice[0] != '\0' && !hush_skill_is_voice(in->voice))
        return HUSH_ERR_PARSE;
    hush_roster_copy_text(agent->voice, sizeof(agent->voice), in->voice, "");
    agent->enabled = 1;
    if (in->has_enabled)
        agent->enabled = in->enabled ? 1 : 0;
    hush_roster_copy_text(agent->role, sizeof(agent->role),
                          hush_roster_is_role(in->role)
                              ? in->role : HUSH_ROSTER_ROLE_WORKER,
                          HUSH_ROSTER_ROLE_WORKER);
    return hush_roster_copy_skills(agent, in);
}

static hush_status_t hush_roster_fill_context(hush_roster_agent_t *agent,
                                              const hush_roster_agent_in_t *in)
{
    size_t i;

    assert(agent != NULL);
    assert(in != NULL);
    if (in->ncontext > (size_t)HUSH_ROSTER_CONTEXT_MAX)
        return HUSH_ERR_FULL;
    for (i = 0; i < in->ncontext; ++i) {
        if (hush_roster_copy_context(&agent->context[i],
                                     &in->context[i]) != HUSH_OK)
            return HUSH_ERR_DENIED;
        agent->ncontext++;
    }
    return HUSH_OK;
}

static hush_status_t hush_roster_format_profile(const hush_roster_t *roster,
                                                char *out, size_t outsz,
                                                size_t *off)
{
    char esc[HUSH_ROSTER_EMAIL_MAX * 2];
    int n;

    assert(roster != NULL);
    assert(out != NULL);
    assert(off != NULL);
    hush_roster_json_escape(roster->profile.first_name, esc, sizeof(esc));
    n = snprintf(out, outsz,
                 ",\"theme\":\"%s\",\"profile\":{\"first_name\":\"%s\"",
                 roster->profile.theme, esc);
    if (n < 0 || (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    *off = (size_t)n;
    hush_roster_json_escape(roster->profile.last_name, esc, sizeof(esc));
    n = snprintf(out + *off, outsz - *off, ",\"last_name\":\"%s\"", esc);
    if (n < 0)
        return HUSH_ERR_FULL;
    *off += (size_t)n;
    hush_roster_json_escape(roster->profile.email, esc, sizeof(esc));
    n = snprintf(out + *off, outsz - *off, ",\"email\":\"%s\"", esc);
    if (n < 0)
        return HUSH_ERR_FULL;
    *off += (size_t)n;
    hush_roster_json_escape(roster->profile.organization, esc, sizeof(esc));
    n = snprintf(out + *off, outsz - *off,
                 ",\"organization\":\"%s\",\"picture\":\"%s\"},\"agents\":[",
                 esc, roster->profile.picture);
    if (n < 0 || *off + (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    *off += (size_t)n;
    return HUSH_OK;
}

static hush_status_t hush_roster_format_agents(const hush_roster_t *roster,
                                               char *out, size_t outsz,
                                               size_t *off)
{
    size_t i;

    assert(roster != NULL);
    assert(out != NULL);
    assert(off != NULL);
    for (i = 0; i < roster->nagents; ++i) {
        if (hush_roster_format_one_agent(&roster->agents[i], out, outsz, off,
                                         i == 0) != HUSH_OK)
            return HUSH_ERR_FULL;
    }
    return HUSH_OK;
}

static hush_status_t hush_roster_format_members(const hush_roster_t *roster,
                                                char *out, size_t outsz,
                                                size_t *off)
{
    char esc[HUSH_ROSTER_NAME_MAX * 2];
    size_t i;
    int n;

    assert(roster != NULL);
    assert(out != NULL);
    assert(off != NULL);
    n = snprintf(out + *off, outsz - *off, "],\"members\":[");
    if (n < 0)
        return HUSH_ERR_FULL;
    *off += (size_t)n;
    for (i = 0; i < roster->nmembers; ++i) {
        hush_roster_json_escape(roster->members[i].name, esc, sizeof(esc));
        n = snprintf(out + *off, outsz - *off,
                     "%s{\"name\":\"%s\",\"npub\":\"%s\",\"pubkey\":\"%s\"}",
                     (i == 0) ? "" : ",",
                     esc, roster->members[i].npub,
                     roster->members[i].pubkey_hex);
        if (n < 0 || *off + (size_t)n >= outsz)
            return HUSH_ERR_FULL;
        *off += (size_t)n;
    }
    if (*off + 2 >= outsz)
        return HUSH_ERR_FULL;
    out[(*off)++] = ']';
    out[*off] = '\0';
    return HUSH_OK;
}

static void hush_roster_copy_text(char *dst, size_t dstsz,
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
    if (dst[0] == '\0' && fallback[0] != '\0') {
        strncpy(dst, fallback, dstsz - 1);
        dst[dstsz - 1] = '\0';
    }
}

static void hush_roster_slugify(char *dst, size_t dstsz, const char *name)
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
        dst[0] = (char)HUSH_ROSTER_SLUG_FALLBACK;
        dst[1] = '\0';
    }
}

static int hush_roster_has_agent_slug(const hush_roster_t *roster,
                                      const char *slug)
{
    size_t i;

    assert(roster != NULL);
    assert(slug != NULL);
    for (i = 0; i < roster->nagents; ++i) {
        if (strcmp(roster->agents[i].slug, slug) == 0)
            return 1;
    }
    return 0;
}

static int hush_roster_has_member(const hush_roster_t *roster,
                                  const char *pubkey_hex)
{
    size_t i;

    assert(roster != NULL);
    assert(pubkey_hex != NULL);
    for (i = 0; i < roster->nmembers; ++i) {
        if (strcmp(roster->members[i].pubkey_hex, pubkey_hex) == 0)
            return 1;
    }
    return 0;
}

static hush_status_t hush_roster_copy_context(hush_roster_context_t *dst,
                                              const hush_roster_context_in_t *src)
{
    size_t n;

    assert(dst != NULL);
    assert(src != NULL);
    if (!hush_roster_is_context_mime(src->mime, src->name))
        return HUSH_ERR_DENIED;
    n = src->bytes;
    if (src->text != NULL && n == 0)
        n = strlen(src->text);
    if (n > (size_t)HUSH_ROSTER_CONTEXT_BYTES)
        return HUSH_ERR_FULL;
    memset(dst, 0, sizeof(*dst));
    hush_roster_copy_text(dst->name, sizeof(dst->name), src->name, "notes.txt");
    hush_roster_copy_text(dst->mime, sizeof(dst->mime), src->mime,
                          HUSH_ROSTER_MIME_PLAIN);
    dst->bytes = n;
    return HUSH_OK;
}

static hush_status_t hush_roster_store_agent_profile(hush_store_t *store,
                                                     const hush_roster_agent_t *agent)
{
    hush_event_t ev;
    char content[HUSH_EVENT_MAX_CONTENT];
    char esc_name[HUSH_ROSTER_NAME_MAX * 2];
    char esc_about[HUSH_ROSTER_PROMPT_MAX * 2];

    assert(store != NULL);
    assert(agent != NULL);
    hush_roster_json_escape(agent->name, esc_name, sizeof(esc_name));
    hush_roster_json_escape(agent->prompt, esc_about, sizeof(esc_about));
    if (snprintf(content, sizeof(content),
                 "{\"name\":\"%s\",\"about\":\"%s\"}",
                 esc_name, esc_about) >= (int)sizeof(content))
        return HUSH_ERR_FULL;
    hush_roster_fill_event(&ev, agent->id.pubkey_hex, HUSH_ROSTER_KIND_META,
                           content, "");
    return hush_store_insert(store, &ev);
}

static hush_status_t hush_roster_store_agent_note(hush_store_t *store,
                                                  const hush_roster_agent_t *agent)
{
    hush_event_t ev;
    char content[HUSH_EVENT_MAX_CONTENT];
    char esc_name[HUSH_ROSTER_NAME_MAX * 2];

    assert(store != NULL);
    assert(agent != NULL);
    hush_roster_json_escape(agent->name, esc_name, sizeof(esc_name));
    if (snprintf(content, sizeof(content),
                 "At ease. Robot %s is on deck. — Major",
                 esc_name) >= (int)sizeof(content))
        return HUSH_ERR_FULL;
    hush_roster_fill_event(&ev, agent->id.pubkey_hex, HUSH_ROSTER_KIND_NOTE,
                           content, HUSH_ROSTER_CHAN_AGENTS);
    return hush_store_insert(store, &ev);
}

static void hush_roster_fill_event(hush_event_t *ev, const char *pubkey_hex,
                                   uint32_t kind, const char *content,
                                   const char *channel)
{
    assert(ev != NULL);
    assert(pubkey_hex != NULL);
    assert(content != NULL);
    memset(ev, 0, sizeof(*ev));
    hush_roster_make_id(ev->id);
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

static void hush_roster_make_id(char *out65)
{
    static unsigned seq;
    time_t now;

    assert(out65 != NULL);
    now = time(NULL);
    seq++;
    (void)snprintf(out65, HUSH_EVENT_ID_HEX_LEN + 1,
                   "%0*llx%0*x%0*x%0*x",
                   HUSH_ROSTER_ID_WIDTH, (unsigned long long)now,
                   HUSH_ROSTER_ID_WIDTH, seq,
                   HUSH_ROSTER_ID_WIDTH, seq ^ 0x51ed270bu,
                   HUSH_ROSTER_ID_WIDTH, seq * 5u);
}

static size_t hush_roster_json_escape(const char *src, char *dst, size_t dstsz)
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

static void hush_roster_try_save_agent(const char *slug, const char *secret)
{
    char path[HUSH_PASS_PATH_MAX];

    assert(slug != NULL);
    assert(secret != NULL);
    if (snprintf(path, sizeof(path), "agents/%s/nsec", slug) >= (int)sizeof(path))
        return;
    (void)hush_pass_save(path, secret);
}

static hush_status_t hush_roster_parse_pubkey(char *out_hex, char *out_npub,
                                              const char *key)
{
    unsigned char raw[HUSH_BECH32_DATA_LEN];
    char hrp[HUSH_BECH32_HRP_MAX + 1];
    hush_identity_t tmp;
    size_t i;

    assert(out_hex != NULL);
    assert(out_npub != NULL);
    assert(key != NULL);
    if (strncmp(key, "npub1", 5) == 0) {
        if (hush_bech32_decode(raw, hrp, sizeof(hrp), key) != HUSH_OK)
            return HUSH_ERR_PARSE;
        if (strcmp(hrp, HUSH_BECH32_HRP_NPUB) != 0)
            return HUSH_ERR_PARSE;
        if (hush_bech32_encode(out_npub, HUSH_IDENTITY_NPUB_MAX,
                               HUSH_BECH32_HRP_NPUB, raw) != HUSH_OK)
            return HUSH_ERR_PARSE;
        hush_roster_hex_encode(out_hex, raw);
        return HUSH_OK;
    }
    if (strlen(key) != (size_t)HUSH_IDENTITY_HEX_LEN)
        return HUSH_ERR_PARSE;
    memset(&tmp, 0, sizeof(tmp));
    for (i = 0; i < (size_t)HUSH_IDENTITY_HEX_LEN; ++i) {
        char c = key[i];

        if (!isxdigit((unsigned char)c))
            return HUSH_ERR_PARSE;
        out_hex[i] = (char)tolower((unsigned char)c);
    }
    out_hex[HUSH_IDENTITY_HEX_LEN] = '\0';
    /* Hex member keys are stored as-is; npub filled when they are npub. */
    memcpy(out_npub, out_hex, HUSH_IDENTITY_HEX_LEN + 1);
    return HUSH_OK;
}

static void hush_roster_hex_encode(char *out65, const unsigned char *raw)
{
    static const char hex[] = "0123456789abcdef";
    size_t i;

    assert(out65 != NULL);
    assert(raw != NULL);
    for (i = 0; i < (size_t)HUSH_BECH32_DATA_LEN; ++i) {
        out65[i * 2] = hex[(raw[i] >> 4) & 0x0f];
        out65[i * 2 + 1] = hex[raw[i] & 0x0f];
    }
    out65[HUSH_IDENTITY_HEX_LEN] = '\0';
}

static void hush_roster_preview_prompt(char *dst, size_t dstsz,
                                       const char *prompt)
{
    size_t i = 0;
    size_t cap;

    assert(dst != NULL);
    assert(dstsz > 0);
    if (prompt == NULL)
        prompt = "";
    cap = dstsz - 1;
    if (cap > (size_t)HUSH_ROSTER_PROMPT_PREVIEW)
        cap = (size_t)HUSH_ROSTER_PROMPT_PREVIEW;
    while (prompt[i] != '\0' && i < cap) {
        dst[i] = prompt[i];
        i++;
    }
    dst[i] = '\0';
}

static int hush_roster_is_payne_slug(const char *slug)
{
    assert(slug != NULL);
    return strcmp(slug, HUSH_ROSTER_PAYNE_SLUG) == 0;
}

static void hush_roster_compact_agents(hush_roster_t *roster, size_t idx)
{
    size_t i;

    assert(roster != NULL);
    assert(idx < roster->nagents);
    for (i = idx; i + 1 < roster->nagents; ++i)
        roster->agents[i] = roster->agents[i + 1];
    memset(&roster->agents[roster->nagents - 1], 0, sizeof(roster->agents[0]));
    roster->nagents--;
}

static hush_status_t hush_roster_copy_skills(hush_roster_agent_t *agent,
                                             const hush_roster_agent_in_t *in)
{
    size_t i;

    assert(agent != NULL);
    assert(in != NULL);
    if (in->nskills > (size_t)HUSH_SKILL_EQUIP_MAX)
        return HUSH_ERR_FULL;
    memset(agent->skills, 0, sizeof(agent->skills));
    agent->nskills = 0;
    for (i = 0; i < in->nskills; ++i) {
        if (in->skills[i][0] == '\0')
            continue;
        hush_roster_copy_text(agent->skills[agent->nskills],
                              sizeof(agent->skills[0]), in->skills[i], "");
        agent->nskills++;
    }
    return HUSH_OK;
}

static hush_roster_agent_t *hush_roster_find_agent(hush_roster_t *roster,
                                                   const char *slug)
{
    size_t i;

    assert(roster != NULL);
    assert(slug != NULL);
    for (i = 0; i < roster->nagents; ++i) {
        if (strcmp(roster->agents[i].slug, slug) == 0)
            return &roster->agents[i];
    }
    return NULL;
}

static hush_status_t hush_roster_apply_update(hush_roster_agent_t *agent,
                                              const hush_roster_agent_in_t *in)
{
    assert(agent != NULL);
    assert(in != NULL);
    if (in->name[0] != '\0')
        hush_roster_copy_text(agent->name, sizeof(agent->name), in->name, "");
    if (in->prompt[0] != '\0')
        hush_roster_copy_text(agent->prompt, sizeof(agent->prompt),
                              in->prompt, "");
    if (in->provider[0] != '\0') {
        if (!hush_roster_is_provider(in->provider))
            return HUSH_ERR_PARSE;
        hush_roster_copy_text(agent->provider, sizeof(agent->provider),
                              in->provider, "");
    }
    if (in->has_picture)
        hush_roster_copy_text(agent->picture, sizeof(agent->picture),
                              in->picture, "");
    if (in->has_voice) {
        if (in->voice[0] != '\0' && !hush_skill_is_voice(in->voice))
            return HUSH_ERR_PARSE;
        hush_roster_copy_text(agent->voice, sizeof(agent->voice), in->voice, "");
    }
    if (in->has_enabled)
        agent->enabled = in->enabled ? 1 : 0;
    if (in->has_role) {
        if (in->role[0] != '\0' && !hush_roster_is_role(in->role))
            return HUSH_ERR_PARSE;
        hush_roster_copy_text(agent->role, sizeof(agent->role),
                              hush_roster_is_role(in->role)
                                  ? in->role : HUSH_ROSTER_ROLE_WORKER,
                              HUSH_ROSTER_ROLE_WORKER);
    }
    if (!in->has_skills)
        return HUSH_OK;
    return hush_roster_copy_skills(agent, in);
}

static hush_status_t hush_roster_format_one_agent(const hush_roster_agent_t *agent,
                                                  char *out, size_t outsz,
                                                  size_t *off, int first)
{
    char esc[HUSH_ROSTER_NAME_MAX * 2];
    char preview[HUSH_ROSTER_PROMPT_PREVIEW + 1];
    char esc_prompt[HUSH_ROSTER_PROMPT_PREVIEW * 2];
    int n;

    assert(agent != NULL);
    assert(out != NULL);
    assert(off != NULL);
    hush_roster_json_escape(agent->name, esc, sizeof(esc));
    hush_roster_preview_prompt(preview, sizeof(preview), agent->prompt);
    hush_roster_json_escape(preview, esc_prompt, sizeof(esc_prompt));
    n = snprintf(out + *off, outsz - *off,
                 "%s{\"name\":\"%s\",\"slug\":\"%s\",\"npub\":\"%s\","
                 "\"pubkey\":\"%s\",\"provider\":\"%s\",\"prompt\":\"%s\","
                 "\"picture\":\"%s\",\"voice\":\"%s\",\"enabled\":%s,"
                 "\"role\":\"%s\",\"ncontext\":%zu,\"skills\":",
                 first ? "" : ",",
                 esc, agent->slug, agent->id.npub, agent->id.pubkey_hex,
                 agent->provider, esc_prompt, agent->picture, agent->voice,
                 agent->enabled ? "true" : "false",
                 agent->role[0] ? agent->role : HUSH_ROSTER_ROLE_WORKER,
                 agent->ncontext);
    if (n < 0 || *off + (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    *off += (size_t)n;
    return hush_roster_format_skills(agent, out, outsz, off);
}

static hush_status_t hush_roster_format_skills(const hush_roster_agent_t *agent,
                                               char *out, size_t outsz,
                                               size_t *off)
{
    size_t i;
    int n;

    assert(agent != NULL);
    assert(out != NULL);
    assert(off != NULL);
    n = snprintf(out + *off, outsz - *off, "[");
    if (n < 0 || *off + (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    *off += (size_t)n;
    for (i = 0; i < agent->nskills; ++i) {
        n = snprintf(out + *off, outsz - *off, "%s\"%s\"",
                     (i == 0) ? "" : ",", agent->skills[i]);
        if (n < 0 || *off + (size_t)n >= outsz)
            return HUSH_ERR_FULL;
        *off += (size_t)n;
    }
    n = snprintf(out + *off, outsz - *off, "]}");
    if (n < 0 || *off + (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    *off += (size_t)n;
    return HUSH_OK;
}
