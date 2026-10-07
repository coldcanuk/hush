/* api_agents.c: owns agent, payne, skill, and context routes. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hush_home.h"
#include "hush_pass.h"
#include "hush_http_internal.h"
#include "hush_provider.h"
#include "hush_roster.h"
#include "hush_skill.h"
#include "hush_skillui.h"

/* Robot refusal reasons: each is the whole 400 body (plus a newline).
 * tests/check_reasons.py pins every string; reword both together. */
#define HUSH_AGENT_WHY_GATE "Log in and set up your vibe before changing robots."
#define HUSH_AGENT_WHY_NAME "Robot name is required."
#define HUSH_AGENT_WHY_NAME_CHARS "Robot names need a letter (A-Z) or digit."
#define HUSH_AGENT_WHY_NAME_PRINT \
    "Robot names cannot include line breaks or other characters that do not print."
#define HUSH_AGENT_WHY_TAKEN "A robot named %s already exists."
#define HUSH_AGENT_WHY_LIKE \
    "Too close to %s: names must differ in letters (A-Z) or 0-9."
#define HUSH_AGENT_WHY_PROMPT "System prompt is required."
#define HUSH_AGENT_WHY_NO_PROVIDER "Provider is required."
#define HUSH_AGENT_WHY_PROVIDER "Unknown AI provider. Choose one from the list."
#define HUSH_AGENT_WHY_VOICE "Unknown voice: %s."
#define HUSH_AGENT_WHY_MIN1 "Keep at least one skill equipped."
#define HUSH_AGENT_WHY_ROLE "Skill %s is for %s robots only."
#define HUSH_AGENT_WHY_BAD_ROLE "Role must be worker or chaperon."
#define HUSH_AGENT_WHY_BUDGET \
    "Loadout over budget: at most %d skills, %d characters, complexity %d."
#define HUSH_AGENT_WHY_NO_FILES "%s cannot read context files."
#define HUSH_AGENT_WHY_FILES \
    "Context files must be plain text or Markdown, at most %d bytes each."
#define HUSH_AGENT_WHY_FULL "Robot roster is full (%d robots)."
#define HUSH_AGENT_WHY_SLUG "Robot id is required."
#define HUSH_AGENT_WHY_MISSING "That robot is already gone."
#define HUSH_AGENT_WHY_MAJOR "Major cannot be deleted or cloned."
#define HUSH_AGENT_WHY_CLONE_LONG \
    "That name would be too long after adding \" copy\". Shorten the name first."
#define HUSH_AGENT_WHY_PASS_MISSING \
    "Could not save the robot's key to pass: pass is not available."
#define HUSH_AGENT_WHY_PASS_PATH \
    "Could not save the robot's key to pass: path is too long."
#define HUSH_AGENT_WHY_PASS_FAIL_FMT \
    "Could not save the robot's key to pass: %s."

enum {
    HUSH_AGENT_ACTION_MAX = 16,
    HUSH_AGENT_FIELD_KEY_MAX = 24
};

/* Room for one byte past the limit: a context text longer than
 * HUSH_ROSTER_CONTEXT_BYTES reads as CONTEXT_BYTES + 1 bytes, so the
 * roster's size check refuses it instead of the reader cutting it short. */
static char g_context_text[HUSH_ROSTER_CONTEXT_MAX][HUSH_ROSTER_CONTEXT_BYTES + 2];

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
/* True when the body names skill_8 or later, or nskills above 8.
 * The roster stores at most eight, so those used to be dropped with 200. */
static int hush_http_skills_over_cap(const char *body);
/* Writes the existing loadout budget reason. No new wording. */
static void hush_http_budget_why(char *why, size_t whysz);
/* Refuses any loadout write that leaves zero skills (PE-3 min-1 law).
 * in and robot_role are borrowed; slug may be "". Fails HUSH_ERR_DENIED
 * on an empty write, a role wall, or a cross-slug equip; why names it. */
/* why is HUSH_HTTP_WHY_MAX bytes. in and robot_role are borrowed. */
static hush_status_t hush_http_check_loadout(char *why,
                                             const hush_roster_agent_in_t *in,
                                             const char *robot_role,
                                             const char *slug);
/* Writes the reason hush_skill_try_equip refused skill_id with st.
 * why is HUSH_HTTP_WHY_MAX bytes. */
static void hush_http_equip_why(char *why, const hush_skill_catalog_t *cat,
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
/* Writes why for a refused pass save (missing|fail|path). Only when THIS
 * request asked save_pass:true and launch named robot_pass_error (B1/B2). */
static int hush_http_pass_why(char *why, size_t whysz, hush_status_t st,
                              int want_save_pass);
static int hush_http_pass_why(char *why, size_t whysz, hush_status_t st,
                              int want_save_pass)
{
    hush_launch_t *launch;
    char detail[96];
    size_t n;
    int wrote;

    if (why == NULL || whysz == 0 || !want_save_pass)
        return 0;
    launch = hush_http_launch();
    if (launch == NULL || launch->robot_pass_error[0] == '\0')
        return 0;
    if (st == HUSH_ERR_DENIED) {
        snprintf(why, whysz, "%s", HUSH_AGENT_WHY_PASS_MISSING);
        return 1;
    }
    if (st == HUSH_ERR_ARG) {
        snprintf(why, whysz, "%s", HUSH_AGENT_WHY_PASS_PATH);
        return 1;
    }
    if (st != HUSH_ERR_IO)
        return 0;
    /* Cap the helper text so the 400 line fits HUSH_HTTP_WHY_MAX. */
    n = strlen(launch->robot_pass_error);
    if (n >= sizeof(detail))
        n = sizeof(detail) - 1;
    memcpy(detail, launch->robot_pass_error, n);
    detail[n] = '\0';
    wrote = snprintf(why, whysz, HUSH_AGENT_WHY_PASS_FAIL_FMT, detail);
    if (wrote < 0 || (size_t)wrote >= whysz)
        snprintf(why, whysz, "%s", "Could not save the robot's key to pass: save failed.");
    return 1;
}

static void hush_http_slug_why(char *why, size_t whysz, hush_status_t st,
                               const char *slug);
/* Writes why for a refused clone of slug, in hush_roster_clone_agent's
 * order: "<name> copy" too long, then roster full, then slug taken. */
static void hush_http_clone_why(char *why, size_t whysz, hush_status_t st,
                                const char *slug);
/* Writes why for a rename of slug to name that the roster refuses: blank
 * after trimming, a character that does not print, no letter A-Z or digit,
 * or a name that clashes with another robot's current name. 1 when a rule
 * matched. */
static int hush_http_rename_why(char *why, size_t whysz, const char *slug,
                                const char *name);
/* Writes why for a bad provider list or voice. 1 when a rule matched. */
static int hush_http_setup_why(char *why, size_t whysz,
                               const hush_roster_agent_in_t *in);
/* Writes why for context files the primary provider refuses. 1 on match. */
static int hush_http_context_why(char *why, size_t whysz,
                                 const hush_roster_agent_in_t *in);
/* Returns the provider id the roster would rank first for in. */
static const char *hush_http_primary_provider(const hush_roster_agent_in_t *in);
/* Writes fmt into why with id echoed through hush_http_safe_id. */

/* True when text holds no byte but whitespace. */
static int hush_http_is_blank(const char *text);
/* The roster robot whose id is slug; NULL when none is. */
static const hush_roster_agent_t *hush_http_find_robot(const char *slug);
/* Writes why when another robot's current name, or Major's, clashes with
 * name: the same name, or one too close to it. except (may be NULL) is the
 * robot being renamed. 1 when a name clashes. */
static int hush_http_name_why(char *why, size_t whysz, const char *name,
                              const hush_roster_agent_t *except);
/* Writes fmt into why with a robot's display name echoed through
 * hush_http_safe_name. */
static void hush_http_why_name(char *why, size_t whysz, const char *fmt,
                               const char *name);
/* Copies name for echoing in a refusal: control bytes show as '?'. */
static void hush_http_safe_name(char *out, size_t outsz, const char *name);

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
    hush_roster_agent_in_t in = {0};
    char why[HUSH_HTTP_WHY_MAX] = {0};
    hush_status_t st = HUSH_OK;

    assert(body != NULL && store != NULL);
    if (!hush_http_json_field(body, "name", in.name, sizeof(in.name)))
        return hush_http_reply_refused(fd, HUSH_ERR_PARSE, HUSH_AGENT_WHY_NAME);
    if (!hush_http_json_field(body, "system_prompt", in.prompt, sizeof(in.prompt)))
        return hush_http_reply_refused(fd, HUSH_ERR_PARSE, HUSH_AGENT_WHY_PROMPT);
    if (!hush_http_json_field(body, "provider", in.provider, sizeof(in.provider)))
        return hush_http_reply_refused(fd, HUSH_ERR_PARSE,
                                       HUSH_AGENT_WHY_NO_PROVIDER);
    hush_http_take_providers(&in, body);
    hush_http_fill_agent_extras(&in, body);
    if (hush_http_skills_over_cap(body)) {
        hush_http_budget_why(why, sizeof(why));
        return hush_http_reply_refused(fd, HUSH_ERR_FULL, why);
    }
    st = hush_http_check_loadout(why, &in,
                                 hush_http_agent_role(NULL, &in), "");
    if (st != HUSH_OK)
        return hush_http_reply_refused(fd, st, why);
    st = hush_http_fill_agent_context(&in, body);
    if (st != HUSH_OK)
        return hush_http_reply_session(fd, st);
    {
        int want_save = hush_http_want_save_pass(body);

        st = hush_launch_add_agent(hush_http_launch(), store, &in, want_save);
        if (st != HUSH_OK) {
            if (!hush_http_pass_why(why, sizeof(why), st, want_save))
                hush_http_create_why(why, sizeof(why), &in);
        }
        return hush_http_reply_refused(fd, st, why);
    }
}

static hush_status_t hush_http_delete_agent(int fd, const char *body)
{
    char slug[HUSH_ROSTER_NAME_MAX] = {0};
    char why[HUSH_HTTP_WHY_MAX] = {0};
    hush_status_t st = HUSH_OK;

    if (!hush_http_json_field(body, "slug", slug, sizeof(slug)))
        return hush_http_reply_refused(fd, HUSH_ERR_PARSE, HUSH_AGENT_WHY_SLUG);
    st = hush_launch_remove_agent(hush_http_launch(), slug);
    hush_http_slug_why(why, sizeof(why), st, slug);
    return hush_http_reply_refused(fd, st, why);
}

static hush_status_t hush_http_clone_agent(int fd, const char *body,
                                           hush_store_t *store)
{
    char slug[HUSH_ROSTER_NAME_MAX] = {0};
    char why[HUSH_HTTP_WHY_MAX] = {0};
    hush_status_t st = HUSH_OK;

    if (!hush_http_json_field(body, "slug", slug, sizeof(slug)))
        return hush_http_reply_refused(fd, HUSH_ERR_PARSE, HUSH_AGENT_WHY_SLUG);
    st = hush_launch_clone_agent(hush_http_launch(), store, slug);
    hush_http_clone_why(why, sizeof(why), st, slug);
    return hush_http_reply_refused(fd, st, why);
}

static int hush_http_is_action(const char *body, const char *want)
{
    char action[HUSH_AGENT_ACTION_MAX] = {0};

    assert(body != NULL && want != NULL);
    if (!hush_http_json_field(body, "action", action, sizeof(action)))
        return 0;
    return strcmp(action, want) == 0;
}

static int hush_http_is_payne_slug(const char *body)
{
    char slug[HUSH_ROSTER_NAME_MAX] = {0};

    if (body == NULL)
        return 0;
    if (!hush_http_json_field(body, "slug", slug, sizeof(slug)))
        return 0;
    return strcmp(slug, HUSH_LAUNCH_PAYNE_SLUG) == 0;
}

static hush_status_t hush_http_update_payne(int fd, const char *body)
{
    char ids[HUSH_LAUNCH_PAYNE_PROVIDERS_MAX][HUSH_ROSTER_PROVIDER_MAX] = {0};
    const char *ptrs[HUSH_LAUNCH_PAYNE_PROVIDERS_MAX] = {0};
    hush_roster_agent_in_t in = {0};
    char key[HUSH_AGENT_FIELD_KEY_MAX] = {0};
    char why[HUSH_HTTP_WHY_MAX] = {0};
    size_t n = 0;
    size_t i = 0;
    hush_status_t st = HUSH_OK;

    if (hush_http_launch() == NULL || body == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_ARG);
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
        return hush_http_reply_refused(fd, st, HUSH_AGENT_WHY_PROVIDER);
    hush_http_fill_agent_extras(&in, body);
    if (hush_http_skills_over_cap(body)) {
        hush_http_budget_why(why, sizeof(why));
        return hush_http_reply_refused(fd, HUSH_ERR_FULL, why);
    }
    st = hush_http_check_loadout(why, &in, HUSH_ROSTER_ROLE_WORKER,
                                 HUSH_LAUNCH_PAYNE_SLUG);
    if (st != HUSH_OK)
        return hush_http_reply_refused(fd, st, why);
    if (in.has_picture || in.has_voice || in.has_skills || in.has_enabled)
        st = hush_launch_update_payne_profile(hush_http_launch(), &in);
    if (st != HUSH_OK && !hush_http_setup_why(why, sizeof why, &in))
        (void)snprintf(why, sizeof why, "%s", "Could not update Major.");
    if (st != HUSH_OK)
        return hush_http_reply_refused(fd, st, why);
    return hush_http_reply_session(fd, st);
}

static hush_status_t hush_http_update_agent(int fd, const char *body)
{
    hush_roster_agent_in_t in = {0};
    char slug[HUSH_ROSTER_NAME_MAX] = {0};
    char why[HUSH_HTTP_WHY_MAX] = {0};
    hush_status_t st = HUSH_OK;

    if (hush_http_launch() == NULL || body == NULL)
        return hush_http_reply_session(fd, HUSH_ERR_ARG);
    if (!hush_http_json_field(body, "slug", slug, sizeof(slug)))
        return hush_http_reply_refused(fd, HUSH_ERR_PARSE, HUSH_AGENT_WHY_SLUG);
    (void)hush_http_json_field(body, "name", in.name, sizeof(in.name));
    (void)hush_http_json_field(body, "system_prompt", in.prompt, sizeof(in.prompt));
    (void)hush_http_json_field(body, "provider", in.provider, sizeof(in.provider));
    hush_http_take_providers(&in, body);
    hush_http_fill_agent_extras(&in, body);
    if (hush_http_skills_over_cap(body)) {
        hush_http_budget_why(why, sizeof(why));
        return hush_http_reply_refused(fd, HUSH_ERR_FULL, why);
    }
    st = hush_http_check_loadout(why, &in,
                                 hush_http_agent_role(slug, &in), slug);
    if (st != HUSH_OK)
        return hush_http_reply_refused(fd, st, why);
    {
        int want_save = hush_http_want_save_pass(body);

        st = hush_launch_update_agent(hush_http_launch(), slug, &in, want_save);
        if (st != HUSH_OK) {
            if (!hush_http_pass_why(why, sizeof(why), st, want_save))
                hush_http_update_why(why, sizeof(why), slug, &in);
        }
        return hush_http_reply_refused(fd, st, why);
    }
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

static void hush_http_budget_why(char *why, size_t whysz)
{
    assert(why != NULL && whysz > 0);
    (void)snprintf(why, whysz, HUSH_AGENT_WHY_BUDGET,
                   (int)HUSH_SKILL_EQUIP_MAX, (int)HUSH_SKILL_CHAR_HIGH,
                   (int)HUSH_SKILL_COMPLEX_HIGH);
}

/* A JSON number, or a quoted number, above HUSH_SKILL_EQUIP_MAX.
 * Missing and non-numeric values are not over the cap. */
static int hush_http_count_over_cap(const char *raw)
{
    const char *p;
    char *end;
    unsigned long n;

    if (raw == NULL || raw[0] == '\0')
        return 0;
    p = raw;
    if (*p == '"')
        p++;
    n = strtoul(p, &end, 10);
    if (end == p)
        return 0;
    if (*end == '"')
        end++;
    if (*end != '\0')
        return 0;
    return n > (unsigned long)HUSH_SKILL_EQUIP_MAX;
}

/* Non-empty skill_N for N >= 8. The id is not copied into the eight slots. */
static int hush_http_skill_index_over_cap(const char *body)
{
    const char *p = body;

    if (body == NULL)
        return 0;
    while ((p = strstr(p, "\"skill_")) != NULL) {
        const char *digits;
        const char *end;
        char *conv;
        char key[60];
        char id[HUSH_SKILL_ID_MAX];
        unsigned long idx;
        size_t klen;

        if (p != body && p[-1] != '{' && p[-1] != ',' && p[-1] != ' ') {
            p++;
            continue;
        }
        digits = p + 7;
        end = digits;
        while (*end >= '0' && *end <= '9')
            end++;
        if (end == digits || *end != '"') {
            p++;
            continue;
        }
        idx = strtoul(digits, &conv, 10);
        if (conv != end || idx < (unsigned long)HUSH_SKILL_EQUIP_MAX) {
            p = end + 1;
            continue;
        }
        /* Longer than the field reader can name: still past the cap. */
        klen = (size_t)(end - (p + 1));
        if (klen + 1 > sizeof(key))
            return 1;
        memcpy(key, p + 1, klen);
        key[klen] = '\0';
        if (hush_http_json_field(body, key, id, sizeof(id)) && id[0] != '\0')
            return 1;
        p = end + 1;
    }
    return 0;
}

static int hush_http_skills_over_cap(const char *body)
{
    char raw[64];

    if (body == NULL)
        return 0;
    if (hush_http_skill_index_over_cap(body))
        return 1;
    if (!hush_http_json_bare_field(body, "nskills", raw, sizeof(raw)))
        return 0;
    return hush_http_count_over_cap(raw);
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

static hush_status_t hush_http_check_loadout(char *why,
                                             const hush_roster_agent_in_t *in,
                                             const char *robot_role,
                                             const char *slug)
{
    hush_skill_catalog_t cat = {0};
    const hush_skill_t *skill = NULL;
    char ids[HUSH_SKILL_EQUIP_MAX][HUSH_SKILL_ID_MAX] = {0};
    size_t n = 0;
    hush_status_t st = HUSH_OK;

    assert(why != NULL);
    if (in == NULL)
        return HUSH_ERR_ARG;
    if (!in->has_skills)
        return HUSH_OK;
    /* PE-3 min-1 law: every loadout write keeps at least one skill. */
    if (in->nskills < (size_t)HUSH_SKILL_EQUIP_LOW) {
        (void)snprintf(why, HUSH_HTTP_WHY_MAX, "%s", HUSH_AGENT_WHY_MIN1);
        return HUSH_ERR_DENIED;
    }
    hush_skill_init_catalog(&cat);
    if (hush_skill_load_catalog(&cat) != HUSH_OK)
        return HUSH_OK;
    for (size_t i = 0; i < in->nskills; i++) {
        st = hush_skill_try_equip(&cat, ids, &n, in->skills[i], robot_role);
        if (st != HUSH_OK) {
            hush_http_equip_why(why, &cat, in->skills[i], st);
            return st;
        }
        skill = hush_skill_find(&cat, in->skills[i]);
        if (skill == NULL || !hush_skill_robot_ok(skill, slug)) {
            hush_http_why_id(why, HUSH_HTTP_WHY_MAX, HUSH_HTTP_WHY_OWNED,
                             in->skills[i]);
            return HUSH_ERR_DENIED;
        }
    }
    return HUSH_OK;
}

static void hush_http_equip_why(char *why, const hush_skill_catalog_t *cat,
                                const char *skill_id, hush_status_t st)
{
    char id[HUSH_HTTP_WHY_ID_MAX] = {0};
    const hush_skill_t *skill = NULL;

    assert(why != NULL);
    assert(cat != NULL && skill_id != NULL);
    why[0] = '\0';
    if (st == HUSH_ERR_NOT_FOUND) {
        hush_http_why_id(why, HUSH_HTTP_WHY_MAX, HUSH_HTTP_WHY_SKILL, skill_id);
        return;
    }
    if (st == HUSH_ERR_FULL) {
        hush_http_budget_why(why, HUSH_HTTP_WHY_MAX);
        return;
    }
    skill = hush_skill_find(cat, skill_id);
    if (st != HUSH_ERR_DENIED || skill == NULL)
        return;
    /* Role wall: try_equip denies only when the skill's role differs. */
    if (!hush_http_safe_id(id, sizeof(id), skill_id)) {
        (void)snprintf(why, HUSH_HTTP_WHY_MAX, "%s", HUSH_HTTP_WHY_ID_LONG);
        return;
    }
    (void)snprintf(why, HUSH_HTTP_WHY_MAX, HUSH_AGENT_WHY_ROLE, id,
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
    char slug[HUSH_ROSTER_NAME_MAX] = {0};

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
    if (!hush_roster_name_prints(in->name)) {
        (void)snprintf(why, whysz, "%s", HUSH_AGENT_WHY_NAME_PRINT);
        return;
    }
    hush_roster_slug_of(slug, sizeof(slug), in->name);
    if (slug[0] == '\0') {
        (void)snprintf(why, whysz, "%s", HUSH_AGENT_WHY_NAME_CHARS);
        return;
    }
    if (hush_http_name_why(why, whysz, in->name, NULL))
        return;
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
    if (in->has_role && in->role[0] != '\0' && !hush_roster_is_role(in->role)) {
        (void)snprintf(why, whysz, "%s", HUSH_AGENT_WHY_BAD_ROLE);
        return;
    }
    if (hush_http_find_robot(slug) == NULL) {
        hush_http_slug_why(why, whysz, HUSH_ERR_NOT_FOUND, slug);
        return;
    }
    if (hush_http_rename_why(why, whysz, slug, in->name))
        return;
    /* apply_update checks providers only when the body names one. */
    if (!in->has_providers && in->provider[0] == '\0' && in->voice[0] == '\0')
        return;
    (void)hush_http_setup_why(why, whysz, in);
}

static int hush_http_rename_why(char *why, size_t whysz, const char *slug,
                                const char *name)
{
    const hush_roster_agent_t *robot = hush_http_find_robot(slug);
    char next[HUSH_ROSTER_NAME_MAX] = {0};

    assert(why != NULL && whysz > 0 && slug != NULL && name != NULL);
    if (name[0] == '\0')
        return 0;
    if (hush_http_is_blank(name)) {
        (void)snprintf(why, whysz, "%s", HUSH_AGENT_WHY_NAME);
        return 1;
    }
    if (!hush_roster_name_prints(name)) {
        (void)snprintf(why, whysz, "%s", HUSH_AGENT_WHY_NAME_PRINT);
        return 1;
    }
    /* An unchanged name that still prints is never refused, even an older
     * one that breaks the rules below (symbol-only, or shared before names
     * had to differ). */
    if (robot != NULL && hush_roster_is_same_name(name, robot->name))
        return 0;
    hush_roster_slug_of(next, sizeof(next), name);
    if (next[0] == '\0') {
        (void)snprintf(why, whysz, "%s", HUSH_AGENT_WHY_NAME_CHARS);
        return 1;
    }
    return hush_http_name_why(why, whysz, name, robot);
}

static void hush_http_slug_why(char *why, size_t whysz, hush_status_t st,
                               const char *slug)
{
    assert(why != NULL && whysz > 0 && slug != NULL);
    why[0] = '\0';
    if (st == HUSH_ERR_DENIED)
        (void)snprintf(why, whysz, "%s", HUSH_AGENT_WHY_MAJOR);
    else if (st == HUSH_ERR_NOT_FOUND)
        (void)snprintf(why, whysz, "%s", HUSH_AGENT_WHY_MISSING);
    else if (st == HUSH_ERR_ARG && slug[0] == '\0')
        (void)snprintf(why, whysz, "%s", HUSH_AGENT_WHY_SLUG);
    else if (st == HUSH_ERR_FULL &&
             hush_http_launch()->roster.nagents >= (size_t)HUSH_ROSTER_AGENTS_MAX)
        (void)snprintf(why, whysz, HUSH_AGENT_WHY_FULL,
                       (int)HUSH_ROSTER_AGENTS_MAX);
}

static void hush_http_clone_why(char *why, size_t whysz, hush_status_t st,
                                const char *slug)
{
    const hush_roster_agent_t *src = hush_http_find_robot(slug);
    char copy[HUSH_ROSTER_NAME_MAX * 2] = {0};
    int n = 0;

    assert(why != NULL && whysz > 0 && slug != NULL);
    if (st != HUSH_ERR_FULL && st != HUSH_ERR_PARSE) {
        hush_http_slug_why(why, whysz, st, slug);
        return;
    }
    why[0] = '\0';
    if (src == NULL)
        return;
    n = snprintf(copy, sizeof(copy), "%s copy", src->name);
    if (n < 0)
        return;
    if ((size_t)n >= (size_t)HUSH_ROSTER_NAME_MAX) {
        (void)snprintf(why, whysz, "%s", HUSH_AGENT_WHY_CLONE_LONG);
        return;
    }
    if (hush_http_launch()->roster.nagents >= (size_t)HUSH_ROSTER_AGENTS_MAX) {
        (void)snprintf(why, whysz, HUSH_AGENT_WHY_FULL,
                       (int)HUSH_ROSTER_AGENTS_MAX);
        return;
    }
    (void)hush_http_name_why(why, whysz, copy, NULL);
}

static int hush_http_setup_why(char *why, size_t whysz,
                               const hush_roster_agent_in_t *in)
{
    const char *primary = NULL;
    size_t i = 0;

    assert(why != NULL && whysz > 0 && in != NULL);
    for (i = 0; in->has_providers && i < in->nproviders; i++) {
        if (in->providers[i][0] != '\0' &&
            !hush_roster_is_provider(in->providers[i])) {
            (void)snprintf(why, whysz, "%s", HUSH_AGENT_WHY_PROVIDER);
            return 1;
        }
    }
    primary = hush_http_primary_provider(in);
    if (primary[0] == '\0') {
        (void)snprintf(why, whysz, "%s", HUSH_AGENT_WHY_NO_PROVIDER);
        return 1;
    }
    if (!hush_roster_is_provider(primary)) {
        (void)snprintf(why, whysz, "%s", HUSH_AGENT_WHY_PROVIDER);
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
    char label[HUSH_PROVIDER_LABEL_MAX] = {0};
    const char *primary = NULL;
    size_t i = 0;

    assert(why != NULL && whysz > 0 && in != NULL);
    if (in->ncontext == 0)
        return 0;
    primary = hush_http_primary_provider(in);
    if (!hush_provider_can(primary, HUSH_PROVIDER_CAP_FILE_ATTACH)) {
        /* The display label ("Gemini API"), never the raw provider id. */
        hush_provider_label(label, sizeof(label), primary);
        (void)snprintf(why, whysz, HUSH_AGENT_WHY_NO_FILES,
                       label[0] != '\0' ? label : "This provider");
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

static const hush_roster_agent_t *hush_http_find_robot(const char *slug)
{
    assert(slug != NULL);
    return hush_roster_agent_by_slug(&hush_http_launch()->roster, slug);
}

static int hush_http_name_why(char *why, size_t whysz, const char *name,
                              const hush_roster_agent_t *except)
{
    const hush_launch_t *launch = hush_http_launch();
    const hush_roster_agent_t *holder =
        hush_roster_name_holder(&launch->roster, name, except);
    const char *have =
        holder != NULL ? holder->name : hush_launch_payne_name(launch);

    assert(why != NULL && whysz > 0 && name != NULL);
    if (holder == NULL && !hush_roster_is_name_clash(name, have))
        return 0;
    /* The same name keeps the plain reason; a near one says what clashed. */
    if (hush_roster_is_same_name(name, have))
        hush_http_why_name(why, whysz, HUSH_AGENT_WHY_TAKEN, have);
    else
        hush_http_why_name(why, whysz, HUSH_AGENT_WHY_LIKE, have);
    return 1;
}

static void hush_http_why_name(char *why, size_t whysz, const char *fmt,
                               const char *name)
{
    char safe[HUSH_ROSTER_NAME_MAX] = {0};

    assert(why != NULL && whysz > 0 && fmt != NULL);
    hush_http_safe_name(safe, sizeof(safe), name);
    (void)snprintf(why, whysz, fmt, safe);
}

static void hush_http_safe_name(char *out, size_t outsz, const char *name)
{
    size_t i = 0;

    assert(out != NULL && outsz > 0);
    if (name == NULL)
        name = "";
    while (name[i] != '\0' && i + 1 < outsz) {
        unsigned char c = (unsigned char)name[i];

        out[i] = (c < 0x20 || c == 0x7f) ? '?' : (char)c;
        i++;
    }
    out[i] = '\0';
}
