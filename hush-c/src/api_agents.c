/* api_agents.c: owns agent, payne, skill, and context routes. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hush_home.h"
#include "hush_http_internal.h"
#include "hush_provider.h"
#include "hush_roster.h"
#include "hush_skill.h"
#include "hush_skillui.h"

/* Robot refusal reasons: each is the whole 400 body (plus a newline).
 * tests/check_reasons.py pins every string; reword both together. */
#define HUSH_AGENT_WHY_GATE "Log in and set up your vibe before changing robots."
#define HUSH_AGENT_WHY_NAME "Robot name is required."
#define HUSH_AGENT_WHY_TAKEN "Robot name taken: slug %s is already in use."
#define HUSH_AGENT_WHY_PROMPT "System prompt is required."
#define HUSH_AGENT_WHY_NO_PROVIDER "Provider is required."
#define HUSH_AGENT_WHY_PROVIDER "Unknown provider: %s."
#define HUSH_AGENT_WHY_VOICE "Unknown voice: %s."
#define HUSH_AGENT_WHY_MIN1 "Keep at least one skill equipped."
#define HUSH_AGENT_WHY_ROLE "Skill %s is for %s robots only."
#define HUSH_AGENT_WHY_BUDGET \
    "Loadout over budget: at most %d skills, %d characters, complexity %d."
#define HUSH_AGENT_WHY_NO_FILES "Provider %s cannot read context files."
#define HUSH_AGENT_WHY_FILES \
    "Context files must be plain text or Markdown, at most %d bytes each."
#define HUSH_AGENT_WHY_FULL "Robot roster is full (%d robots)."
#define HUSH_AGENT_WHY_SLUG "Robot slug is required."
#define HUSH_AGENT_WHY_MISSING "No robot with slug %s."
#define HUSH_AGENT_WHY_MAJOR "Major cannot be deleted or cloned."

static char g_context_text[HUSH_ROSTER_CONTEXT_MAX][HUSH_ROSTER_CONTEXT_BYTES];

static hush_status_t hush_http_create_agent(int fd, const char *body,
                                            hush_store_t *store);
static hush_status_t hush_http_delete_agent(int fd, const char *body);
static hush_status_t hush_http_clone_agent(int fd, const char *body,
                                           hush_store_t *store);
static hush_status_t hush_http_update_payne(int fd, const char *body);
static hush_status_t hush_http_update_agent(int fd, const char *body);
static int hush_http_is_payne_slug(const char *body);
static int hush_http_is_action(const char *body, const char *want);
static void hush_http_fill_agent_extras(hush_roster_agent_in_t *in,
                                        const char *body);
static void hush_http_take_providers(hush_roster_agent_in_t *in,
                                     const char *body);
static void hush_http_fill_agent_skills(hush_roster_agent_in_t *in,
                                        const char *body);
/* Refuses any loadout write that leaves zero skills (PE-3 min-1 law).
 * in and robot_role are borrowed; slug may be "". Fails HUSH_ERR_DENIED
 * on an empty write, a role wall, or a cross-slug equip; why names it. */
static hush_status_t hush_http_check_loadout(char *why, size_t whysz,
                                             const hush_roster_agent_in_t *in,
                                             const char *robot_role,
                                             const char *slug);
/* Writes the reason hush_skill_try_equip refused skill_id with st. */
static void hush_http_equip_why(char *why, size_t whysz,
                                const hush_skill_catalog_t *cat,
                                const char *skill_id, hush_status_t st);
static const char *hush_http_agent_role(const char *slug,
                                        const hush_roster_agent_in_t *in);
static hush_status_t hush_http_fill_agent_context(hush_roster_agent_in_t *in,
                                                  const char *body);
static hush_status_t hush_http_read_context_slot(hush_roster_context_in_t *slot,
                                                 const char *body, size_t idx);
static hush_status_t hush_http_read_legacy_context(hush_roster_context_in_t *slot,
                                                   const char *body);
/* True when the relay is logged in with a vibe: every robot write needs it. */
static int hush_http_agent_gate_open(void);
/* Writes why for a refused create, checking in the roster's own order. */
static void hush_http_create_why(char *why, size_t whysz,
                                 const hush_roster_agent_in_t *in);
/* Writes why for a refused update of slug; empty when no rule matches. */
static void hush_http_update_why(char *why, size_t whysz, const char *slug,
                                 const hush_roster_agent_in_t *in);
/* Writes why for a refused delete or clone of slug by status. */
static void hush_http_slug_why(char *why, size_t whysz, hush_status_t st,
                               const char *slug);
/* Writes why for a bad provider list or voice. 1 when a rule matched. */
static int hush_http_setup_why(char *why, size_t whysz,
                               const hush_roster_agent_in_t *in);
/* Writes why for context files the primary provider refuses. 1 on match. */
static int hush_http_context_why(char *why, size_t whysz,
                                 const hush_roster_agent_in_t *in);
/* Returns the provider id the roster would rank first for in. */
static const char *hush_http_primary_provider(const hush_roster_agent_in_t *in);
/* Writes fmt into why with id echoed through hush_http_safe_id. */
static void hush_http_why_id(char *why, size_t whysz, const char *fmt,
                             const char *id);
/* True when text holds no byte but whitespace. */
static int hush_http_is_blank(const char *text);
/* True when a roster robot already owns slug. */
static int hush_http_slug_taken(const char *slug);

hush_status_t hush_http_serve_agent(int fd, const char *body,
                                    hush_store_t *store)
{
    if (hush_http_launch() == NULL || body == NULL || store == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_ARG);
    if (!hush_http_agent_gate_open())
        return hush_http_reply_refused(fd, HUSH_ERR_ARG, HUSH_AGENT_WHY_GATE);
    if (hush_http_is_action(body, "delete"))
        return hush_http_delete_agent(fd, body);
    if (hush_http_is_action(body, "clone"))
        return hush_http_clone_agent(fd, body, store);
    if (hush_http_is_payne_slug(body))
        return hush_http_update_payne(fd, body);
    if (hush_http_is_action(body, "update"))
        return hush_http_update_agent(fd, body);
    return hush_http_create_agent(fd, body, store);
}

static hush_status_t hush_http_create_agent(int fd, const char *body,
                                            hush_store_t *store)
{
    hush_roster_agent_in_t in;
    char why[HUSH_HTTP_WHY_MAX] = {0};
    hush_status_t st;

    assert(body != NULL && store != NULL);
    memset(&in, 0, sizeof(in));
    if (!hush_http_json_field(body, "name", in.name, sizeof(in.name)))
        return hush_http_reply_refused(fd, HUSH_ERR_PARSE, HUSH_AGENT_WHY_NAME);
    if (!hush_http_json_field(body, "system_prompt", in.prompt, sizeof(in.prompt)))
        return hush_http_reply_refused(fd, HUSH_ERR_PARSE, HUSH_AGENT_WHY_PROMPT);
    if (!hush_http_json_field(body, "provider", in.provider, sizeof(in.provider)))
        return hush_http_reply_refused(fd, HUSH_ERR_PARSE,
                                       HUSH_AGENT_WHY_NO_PROVIDER);
    hush_http_take_providers(&in, body);
    hush_http_fill_agent_extras(&in, body);
    st = hush_http_check_loadout(why, sizeof(why), &in,
                                 hush_http_agent_role(NULL, &in), "");
    if (st != HUSH_OK)
        return hush_http_reply_refused(fd, st, why);
    st = hush_http_fill_agent_context(&in, body);
    if (st != HUSH_OK)
        return hush_http_reply_session(fd, st);
    st = hush_launch_add_agent(hush_http_launch(), store, &in,
                               hush_http_want_save_pass(body));
    if (st != HUSH_OK)
        hush_http_create_why(why, sizeof(why), &in);
    return hush_http_reply_refused(fd, st, why);
}

static hush_status_t hush_http_delete_agent(int fd, const char *body)
{
    char slug[HUSH_ROSTER_NAME_MAX];
    char why[HUSH_HTTP_WHY_MAX] = {0};
    hush_status_t st;

    if (!hush_http_json_field(body, "slug", slug, sizeof(slug)))
        return hush_http_reply_refused(fd, HUSH_ERR_PARSE, HUSH_AGENT_WHY_SLUG);
    st = hush_launch_remove_agent(hush_http_launch(), slug);
    hush_http_slug_why(why, sizeof(why), st, slug);
    return hush_http_reply_refused(fd, st, why);
}

static hush_status_t hush_http_clone_agent(int fd, const char *body,
                                           hush_store_t *store)
{
    char slug[HUSH_ROSTER_NAME_MAX];
    char why[HUSH_HTTP_WHY_MAX] = {0};
    hush_status_t st;

    if (!hush_http_json_field(body, "slug", slug, sizeof(slug)))
        return hush_http_reply_refused(fd, HUSH_ERR_PARSE, HUSH_AGENT_WHY_SLUG);
    st = hush_launch_clone_agent(hush_http_launch(), store, slug);
    hush_http_slug_why(why, sizeof(why), st, slug);
    return hush_http_reply_refused(fd, st, why);
}

static int hush_http_is_action(const char *body, const char *want)
{
    char action[16];

    assert(body != NULL && want != NULL);
    if (!hush_http_json_field(body, "action", action, sizeof(action)))
        return 0;
    return strcmp(action, want) == 0;
}

static int hush_http_is_payne_slug(const char *body)
{
    char slug[HUSH_ROSTER_NAME_MAX];

    if (body == NULL)
        return 0;
    if (!hush_http_json_field(body, "slug", slug, sizeof(slug)))
        return 0;
    return strcmp(slug, HUSH_LAUNCH_PAYNE_SLUG) == 0;
}

static hush_status_t hush_http_update_payne(int fd, const char *body)
{
    char ids[HUSH_LAUNCH_PAYNE_PROVIDERS_MAX][HUSH_ROSTER_PROVIDER_MAX];
    const char *ptrs[HUSH_LAUNCH_PAYNE_PROVIDERS_MAX];
    hush_roster_agent_in_t in;
    char key[24];
    char why[HUSH_HTTP_WHY_MAX] = {0};
    size_t n = 0;
    size_t i;
    hush_status_t st;

    if (hush_http_launch() == NULL || body == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_ARG);
    memset(ids, 0, sizeof(ids));
    for (i = 0; i < (size_t)HUSH_LAUNCH_PAYNE_PROVIDERS_MAX; ++i) {
        if (snprintf(key, sizeof(key), "provider_%zu", i) >= (int)sizeof(key))
            return hush_http_reply_session(fd, HUSH_ERR_FULL);
        if (!hush_http_json_field(body, key, ids[n], sizeof(ids[n])))
            continue;
        if (ids[n][0] == '\0')
            continue;
        ptrs[n] = ids[n];
        n++;
    }
    st = hush_launch_set_payne_providers(hush_http_launch(), ptrs, n);
    if (st != HUSH_OK)
        return hush_http_reply_session(fd, st);
    memset(&in, 0, sizeof(in));
    hush_http_fill_agent_extras(&in, body);
    st = hush_http_check_loadout(why, sizeof(why), &in,
                                 HUSH_ROSTER_ROLE_WORKER,
                                 HUSH_LAUNCH_PAYNE_SLUG);
    if (st != HUSH_OK)
        return hush_http_reply_refused(fd, st, why);
    if (in.has_picture || in.has_voice || in.has_skills || in.has_enabled)
        st = hush_launch_update_payne_profile(hush_http_launch(), &in);
    return hush_http_reply_session(fd, st);
}

static hush_status_t hush_http_update_agent(int fd, const char *body)
{
    hush_roster_agent_in_t in;
    char slug[HUSH_ROSTER_NAME_MAX];
    char why[HUSH_HTTP_WHY_MAX] = {0};
    hush_status_t st;

    if (hush_http_launch() == NULL || body == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_ARG);
    if (!hush_http_json_field(body, "slug", slug, sizeof(slug)))
        return hush_http_reply_refused(fd, HUSH_ERR_PARSE, HUSH_AGENT_WHY_SLUG);
    memset(&in, 0, sizeof(in));
    (void)hush_http_json_field(body, "name", in.name, sizeof(in.name));
    (void)hush_http_json_field(body, "system_prompt", in.prompt, sizeof(in.prompt));
    (void)hush_http_json_field(body, "provider", in.provider, sizeof(in.provider));
    hush_http_take_providers(&in, body);
    hush_http_fill_agent_extras(&in, body);
    st = hush_http_check_loadout(why, sizeof(why), &in,
                                 hush_http_agent_role(slug, &in), slug);
    if (st != HUSH_OK)
        return hush_http_reply_refused(fd, st, why);
    st = hush_launch_update_agent(hush_http_launch(), slug, &in);
    if (st != HUSH_OK)
        hush_http_update_why(why, sizeof(why), slug, &in);
    return hush_http_reply_refused(fd, st, why);
}

/* Parses "providers":"a,b,c" into in->providers (ranked, index 0 = primary). */
static void hush_http_take_providers(hush_roster_agent_in_t *in,
                                     const char *body)
{
    char plist[HUSH_ROSTER_PROVIDERS_MAX * (HUSH_ROSTER_PROVIDER_MAX + 1)];
    char *tok;
    char *save;
    size_t n = 0;

    assert(in != NULL);
    assert(body != NULL);
    if (!hush_http_json_field(body, "providers", plist, sizeof(plist)))
        return;
    for (tok = strtok_r(plist, ",", &save);
         tok != NULL && n < (size_t)HUSH_ROSTER_PROVIDERS_MAX;
         tok = strtok_r(NULL, ",", &save)) {
        size_t len;

        if (tok[0] == '\0')
            continue;
        len = strlen(tok);
        if (len >= sizeof(in->providers[n]))
            len = sizeof(in->providers[n]) - 1;
        memcpy(in->providers[n], tok, len);
        in->providers[n][len] = '\0';
        n++;
    }
    in->nproviders = n;
    in->has_providers = 1;
}

static void hush_http_fill_agent_extras(hush_roster_agent_in_t *in,
                                        const char *body)
{
    char raw[8];

    assert(in != NULL);
    assert(body != NULL);
    if (hush_http_json_field(body, "picture", in->picture, sizeof(in->picture)))
        in->has_picture = 1;
    if (hush_http_json_field(body, "voice", in->voice, sizeof(in->voice)))
        in->has_voice = 1;
    if (hush_http_json_bare_field(body, "enabled", raw, sizeof(raw))) {
        in->has_enabled = 1;
        in->enabled = (strcmp(raw, "false") == 0 || strcmp(raw, "0") == 0)
            ? 0 : 1;
    }
    if (hush_http_json_field(body, "role", in->role, sizeof(in->role)))
        in->has_role = 1;
    if (hush_http_json_bare_field(body, "intro_enabled", raw, sizeof(raw))) {
        in->has_intro_enabled = 1;
        in->intro_enabled = (strcmp(raw, "false") == 0 || strcmp(raw, "0") == 0)
            ? 0 : 1;
    }
    if (hush_http_json_field(body, "intro", in->intro, sizeof(in->intro)))
        in->has_intro = 1;
    hush_http_fill_agent_skills(in, body);
}

static void hush_http_fill_agent_skills(hush_roster_agent_in_t *in,
                                        const char *body)
{
    char key[24];
    size_t i;

    assert(in != NULL);
    assert(body != NULL);
    in->nskills = 0;
    if (hush_http_json_has_key(body, "skill_0") ||
        hush_http_json_has_key(body, "nskills"))
        in->has_skills = 1;
    for (i = 0; i < (size_t)HUSH_SKILL_EQUIP_MAX; ++i) {
        if (snprintf(key, sizeof(key), "skill_%zu", i) >= (int)sizeof(key))
            return;
        if (!hush_http_json_field(body, key, in->skills[in->nskills],
                             sizeof(in->skills[0])))
            continue;
        if (in->skills[in->nskills][0] == '\0')
            continue;
        in->nskills++;
    }
}

static hush_status_t hush_http_check_loadout(char *why, size_t whysz,
                                             const hush_roster_agent_in_t *in,
                                             const char *robot_role,
                                             const char *slug)
{
    hush_skill_catalog_t cat;
    const hush_skill_t *skill;
    char ids[HUSH_SKILL_EQUIP_MAX][HUSH_SKILL_ID_MAX];
    size_t n = 0;
    size_t i;
    hush_status_t st;

    assert(why != NULL && whysz > 0);
    if (in == NULL)
        return HUSH_ERR_ARG;
    if (!in->has_skills)
        return HUSH_OK;
    /* PE-3 min-1 law: every loadout write keeps at least one skill. */
    if (in->nskills < (size_t)HUSH_SKILL_EQUIP_LOW) {
        (void)snprintf(why, whysz, "%s", HUSH_AGENT_WHY_MIN1);
        return HUSH_ERR_DENIED;
    }
    hush_skill_init_catalog(&cat);
    if (hush_skill_load_catalog(&cat) != HUSH_OK)
        return HUSH_OK;
    memset(ids, 0, sizeof(ids));
    for (i = 0; i < in->nskills; i++) {
        st = hush_skill_try_equip(&cat, ids, &n, in->skills[i], robot_role);
        if (st != HUSH_OK) {
            hush_http_equip_why(why, whysz, &cat, in->skills[i], st);
            return st;
        }
        skill = hush_skill_find(&cat, in->skills[i]);
        if (skill == NULL || !hush_skill_robot_ok(skill, slug)) {
            hush_http_why_id(why, whysz, HUSH_HTTP_WHY_OWNED, in->skills[i]);
            return HUSH_ERR_DENIED;
        }
    }
    return HUSH_OK;
}

static void hush_http_equip_why(char *why, size_t whysz,
                                const hush_skill_catalog_t *cat,
                                const char *skill_id, hush_status_t st)
{
    char id[HUSH_HTTP_WHY_ID_MAX];
    const hush_skill_t *skill;

    assert(why != NULL && whysz > 0);
    assert(cat != NULL && skill_id != NULL);
    why[0] = '\0';
    if (st == HUSH_ERR_NOT_FOUND) {
        hush_http_why_id(why, whysz, HUSH_HTTP_WHY_SKILL, skill_id);
        return;
    }
    if (st == HUSH_ERR_FULL) {
        (void)snprintf(why, whysz, HUSH_AGENT_WHY_BUDGET,
                       (int)HUSH_SKILL_EQUIP_MAX, (int)HUSH_SKILL_CHAR_HIGH,
                       (int)HUSH_SKILL_COMPLEX_HIGH);
        return;
    }
    skill = hush_skill_find(cat, skill_id);
    if (st != HUSH_ERR_DENIED || skill == NULL)
        return;
    /* Role wall: try_equip denies only when the skill's role differs. */
    hush_http_safe_id(id, sizeof(id), skill_id);
    (void)snprintf(why, whysz, HUSH_AGENT_WHY_ROLE, id,
                   skill->role[0] != '\0' ? skill->role : HUSH_SKILL_ROLE_ANY);
}

static const char *hush_http_agent_role(const char *slug,
                                        const hush_roster_agent_in_t *in)
{
    size_t i;

    if (in != NULL && in->has_role && in->role[0] != '\0')
        return in->role;
    if (hush_http_launch() != NULL && slug != NULL) {
        for (i = 0; i < hush_http_launch()->roster.nagents; i++) {
            if (strcmp(hush_http_launch()->roster.agents[i].slug, slug) == 0 &&
                hush_http_launch()->roster.agents[i].role[0] != '\0')
                return hush_http_launch()->roster.agents[i].role;
        }
    }
    return HUSH_ROSTER_ROLE_WORKER;
}

void hush_http_serve_skills_get(int fd)
{
    hush_skill_catalog_t cat;
    char body[HUSH_SKILL_JSON_MAX];
    size_t n = 0;

    hush_skill_init_catalog(&cat);
    if (hush_skill_load_catalog(&cat) != HUSH_OK) {
        hush_http_reply(fd, "500 Internal Server Error", "application/json",
                        "{\"ok\":false}\n", 14);
        return;
    }
    if (hush_skill_format_json(&cat, 1, body, sizeof(body), &n) != HUSH_OK) {
        hush_http_reply(fd, "500 Internal Server Error", "application/json",
                        "{\"ok\":false}\n", 14);
        return;
    }
    hush_http_reply(fd, "200 OK", "application/json", body, n);
}

hush_status_t hush_http_serve_skill_post(int fd, const char *body)
{
    hush_skill_forge_in_t in;
    char id[HUSH_SKILL_ID_MAX];
    char reply[HUSH_SKILL_ID_MAX + 64];
    int n;

    if (body == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_ARG);
    memset(&in, 0, sizeof(in));
    if (!hush_http_json_field(body, "name", in.name, sizeof(in.name)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    (void)hush_http_json_field(body, "summary", in.summary, sizeof(in.summary));
    (void)hush_http_json_field(body, "body", in.body, sizeof(in.body));
    if (!hush_http_json_field(body, "scope", in.scope, sizeof(in.scope)))
        memcpy(in.scope, HUSH_SKILL_SCOPE_USER, sizeof(HUSH_SKILL_SCOPE_USER));
    (void)hush_http_json_field(body, "robot", in.robot, sizeof(in.robot));
    if (hush_skill_forge(&in, id, sizeof(id)) != HUSH_OK)
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    n = snprintf(reply, sizeof(reply), "{\"ok\":true,\"id\":\"%s\"}\n", id);
    if (n < 0 || (size_t)n >= sizeof(reply))
        return hush_http_reply_session(fd, HUSH_ERR_FULL);
    hush_http_reply(fd, "200 OK", "application/json", reply, (size_t)n);
    return HUSH_OK;
}

hush_status_t hush_http_serve_skillui(int fd, const char *body)
{
    hush_skillui_t tok;
    char html[HUSH_SKILLUI_JSON_MAX];
    char name[HUSH_SKILLUI_NAME_MAX];
    char json[HUSH_SKILLUI_JSON_MAX];
    char dir[HUSH_HOME_PATH_MAX];
    size_t n = 0;

    if (body == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_ARG);
    if (!hush_http_json_field(body, "html", html, sizeof(html)))
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    if (!hush_http_json_field(body, "name", name, sizeof(name)))
        memcpy(name, "extract", 8);
    if (hush_skillui_extract(&tok, html, strlen(html)) != HUSH_OK)
        return hush_http_reply_session(fd, HUSH_ERR_PARSE);
    if (hush_home_skills_dir(dir, sizeof(dir), HUSH_SKILL_SCOPE_USER, NULL)
        != HUSH_OK)
        return hush_http_reply_session(fd, HUSH_ERR_IO);
    (void)hush_skillui_write_skill(dir, name, &tok);
    if (hush_skillui_format_json(&tok, json, sizeof(json), &n) != HUSH_OK)
        return hush_http_reply_session(fd, HUSH_ERR_FULL);
    hush_http_reply(fd, "200 OK", "application/json", json, n);
    return HUSH_OK;
}

static hush_status_t hush_http_fill_agent_context(hush_roster_agent_in_t *in,
                                                  const char *body)
{
    size_t i;
    hush_status_t st;

    assert(in != NULL);
    assert(body != NULL);
    for (i = 0; i < (size_t)HUSH_ROSTER_CONTEXT_MAX; ++i) {
        st = hush_http_read_context_slot(&in->context[in->ncontext], body, i);
        if (st == HUSH_ERR_NOT_FOUND)
            continue;
        if (st != HUSH_OK)
            return st;
        in->ncontext++;
    }
    return HUSH_OK;
}

static hush_status_t hush_http_read_context_slot(hush_roster_context_in_t *slot,
                                                 const char *body, size_t idx)
{
    char key[24];

    assert(slot != NULL);
    assert(body != NULL);
    assert(idx < (size_t)HUSH_ROSTER_CONTEXT_MAX);
    if (snprintf(key, sizeof(key), "context_name_%zu", idx) >= (int)sizeof(key))
        return HUSH_ERR_FULL;
    if (!hush_http_json_field(body, key, slot->name, sizeof(slot->name))) {
        if (idx == 0)
            return hush_http_read_legacy_context(slot, body);
        return HUSH_ERR_NOT_FOUND;
    }
    if (snprintf(key, sizeof(key), "context_mime_%zu", idx) >= (int)sizeof(key))
        return HUSH_ERR_FULL;
    if (!hush_http_json_field(body, key, slot->mime, sizeof(slot->mime)))
        memcpy(slot->mime, HUSH_ROSTER_MIME_PLAIN,
               sizeof(HUSH_ROSTER_MIME_PLAIN));
    if (snprintf(key, sizeof(key), "context_text_%zu", idx) >= (int)sizeof(key))
        return HUSH_ERR_FULL;
    if (!hush_http_json_field(body, key, g_context_text[idx],
                         sizeof(g_context_text[idx])))
        g_context_text[idx][0] = '\0';
    slot->text = g_context_text[idx];
    slot->bytes = strlen(g_context_text[idx]);
    return HUSH_OK;
}

static hush_status_t hush_http_read_legacy_context(hush_roster_context_in_t *slot,
                                                   const char *body)
{
    assert(slot != NULL);
    assert(body != NULL);
    if (!hush_http_json_field(body, "context_name", slot->name, sizeof(slot->name)))
        return HUSH_ERR_NOT_FOUND;
    if (!hush_http_json_field(body, "context_mime", slot->mime, sizeof(slot->mime)))
        memcpy(slot->mime, HUSH_ROSTER_MIME_PLAIN,
               sizeof(HUSH_ROSTER_MIME_PLAIN));
    if (!hush_http_json_field(body, "context_text", g_context_text[0],
                         sizeof(g_context_text[0])))
        g_context_text[0][0] = '\0';
    slot->text = g_context_text[0];
    slot->bytes = strlen(g_context_text[0]);
    return HUSH_OK;
}

static int hush_http_agent_gate_open(void)
{
    const hush_launch_t *launch = hush_http_launch();

    return launch != NULL && launch->logged_in && launch->has_vibe;
}

static void hush_http_create_why(char *why, size_t whysz,
                                 const hush_roster_agent_in_t *in)
{
    char slug[HUSH_ROSTER_NAME_MAX];

    assert(why != NULL && whysz > 0 && in != NULL);
    why[0] = '\0';
    if (hush_http_launch()->roster.nagents >= (size_t)HUSH_ROSTER_AGENTS_MAX) {
        (void)snprintf(why, whysz, HUSH_AGENT_WHY_FULL,
                       (int)HUSH_ROSTER_AGENTS_MAX);
        return;
    }
    if (hush_http_is_blank(in->name)) {
        (void)snprintf(why, whysz, "%s", HUSH_AGENT_WHY_NAME);
        return;
    }
    hush_roster_slug_of(slug, sizeof(slug), in->name);
    if (hush_http_slug_taken(slug)) {
        hush_http_why_id(why, whysz, HUSH_AGENT_WHY_TAKEN, slug);
        return;
    }
    if (hush_http_is_blank(in->prompt)) {
        (void)snprintf(why, whysz, "%s", HUSH_AGENT_WHY_PROMPT);
        return;
    }
    if (hush_http_setup_why(why, whysz, in))
        return;
    (void)hush_http_context_why(why, whysz, in);
}

static void hush_http_update_why(char *why, size_t whysz, const char *slug,
                                 const hush_roster_agent_in_t *in)
{
    assert(why != NULL && whysz > 0);
    assert(slug != NULL && in != NULL);
    why[0] = '\0';
    if (!hush_http_slug_taken(slug)) {
        hush_http_slug_why(why, whysz, HUSH_ERR_NOT_FOUND, slug);
        return;
    }
    /* apply_update checks providers only when the body names one. */
    if (!in->has_providers && in->provider[0] == '\0' && in->voice[0] == '\0')
        return;
    (void)hush_http_setup_why(why, whysz, in);
}

static void hush_http_slug_why(char *why, size_t whysz, hush_status_t st,
                               const char *slug)
{
    assert(why != NULL && whysz > 0 && slug != NULL);
    why[0] = '\0';
    if (st == HUSH_ERR_DENIED)
        (void)snprintf(why, whysz, "%s", HUSH_AGENT_WHY_MAJOR);
    else if (st == HUSH_ERR_NOT_FOUND)
        hush_http_why_id(why, whysz, HUSH_AGENT_WHY_MISSING, slug);
    else if (st == HUSH_ERR_ARG && slug[0] == '\0')
        (void)snprintf(why, whysz, "%s", HUSH_AGENT_WHY_SLUG);
    else if (st == HUSH_ERR_FULL &&
             hush_http_launch()->roster.nagents >= (size_t)HUSH_ROSTER_AGENTS_MAX)
        (void)snprintf(why, whysz, HUSH_AGENT_WHY_FULL,
                       (int)HUSH_ROSTER_AGENTS_MAX);
}

static int hush_http_setup_why(char *why, size_t whysz,
                               const hush_roster_agent_in_t *in)
{
    const char *primary;
    size_t i;

    assert(why != NULL && whysz > 0 && in != NULL);
    for (i = 0; in->has_providers && i < in->nproviders; i++) {
        if (in->providers[i][0] != '\0' &&
            !hush_roster_is_provider(in->providers[i])) {
            hush_http_why_id(why, whysz, HUSH_AGENT_WHY_PROVIDER,
                             in->providers[i]);
            return 1;
        }
    }
    primary = hush_http_primary_provider(in);
    if (primary[0] == '\0') {
        (void)snprintf(why, whysz, "%s", HUSH_AGENT_WHY_NO_PROVIDER);
        return 1;
    }
    if (!hush_roster_is_provider(primary)) {
        hush_http_why_id(why, whysz, HUSH_AGENT_WHY_PROVIDER, primary);
        return 1;
    }
    if (in->voice[0] != '\0' && !hush_skill_is_voice(in->voice)) {
        hush_http_why_id(why, whysz, HUSH_AGENT_WHY_VOICE, in->voice);
        return 1;
    }
    return 0;
}

static int hush_http_context_why(char *why, size_t whysz,
                                 const hush_roster_agent_in_t *in)
{
    const char *primary;
    size_t i;

    assert(why != NULL && whysz > 0 && in != NULL);
    if (in->ncontext == 0)
        return 0;
    primary = hush_http_primary_provider(in);
    if (!hush_provider_can(primary, HUSH_PROVIDER_CAP_FILE_ATTACH)) {
        hush_http_why_id(why, whysz, HUSH_AGENT_WHY_NO_FILES, primary);
        return 1;
    }
    for (i = 0; i < in->ncontext && i < (size_t)HUSH_ROSTER_CONTEXT_MAX; i++) {
        if (!hush_roster_is_context_mime(in->context[i].mime,
                                         in->context[i].name) ||
            in->context[i].bytes > (size_t)HUSH_ROSTER_CONTEXT_BYTES) {
            (void)snprintf(why, whysz, HUSH_AGENT_WHY_FILES,
                           (int)HUSH_ROSTER_CONTEXT_BYTES);
            return 1;
        }
    }
    return 0;
}

static const char *hush_http_primary_provider(const hush_roster_agent_in_t *in)
{
    size_t i;

    assert(in != NULL);
    /* Mirrors hush_roster_copy_providers: first non-empty ranked id wins. */
    for (i = 0; in->has_providers && i < in->nproviders; i++) {
        if (in->providers[i][0] != '\0')
            return in->providers[i];
    }
    return in->provider;
}

static void hush_http_why_id(char *why, size_t whysz, const char *fmt,
                             const char *id)
{
    char safe[HUSH_HTTP_WHY_ID_MAX];

    assert(why != NULL && whysz > 0 && fmt != NULL);
    hush_http_safe_id(safe, sizeof(safe), id);
    (void)snprintf(why, whysz, fmt, safe);
}

static int hush_http_is_blank(const char *text)
{
    size_t i;

    assert(text != NULL);
    for (i = 0; text[i] != '\0'; i++) {
        if (!isspace((unsigned char)text[i]))
            return 0;
    }
    return 1;
}

static int hush_http_slug_taken(const char *slug)
{
    const hush_roster_t *roster = &hush_http_launch()->roster;
    size_t i;

    assert(slug != NULL);
    for (i = 0; i < roster->nagents; i++) {
        if (strcmp(roster->agents[i].slug, slug) == 0)
            return 1;
    }
    return 0;
}
