/* hush_roster.h: vibe members, agents, avatars, MIME-checked context. */

#ifndef HUSH_ROSTER_H
#define HUSH_ROSTER_H

#include <stddef.h>
#include <stdint.h>
#include "hush_identity.h"
#include "hush_skill.h"
#include "hush_status.h"
#include "hush_store.h"

enum {
    HUSH_ROSTER_NAME_MAX = 64,
    HUSH_ROSTER_PROMPT_MAX = 1024,
    HUSH_ROSTER_EMAIL_MAX = 128,
    HUSH_ROSTER_AGENTS_MAX = 16,
    HUSH_ROSTER_MEMBERS_MAX = 32,
    HUSH_ROSTER_CONTEXT_MAX = 3,
    HUSH_ROSTER_CONTEXT_BYTES = 4096,
    HUSH_ROSTER_PATH_MAX = 256,
    HUSH_ROSTER_JSON_MAX = 16384,
    HUSH_ROSTER_PROVIDER_MAX = 32,
    HUSH_ROSTER_PROVIDERS_MAX = 4,
    HUSH_ROSTER_PROMPT_PREVIEW = 160,
    HUSH_ROSTER_INTRO_MAX = 240
};

#define HUSH_ROSTER_INTRO_DEFAULT "I am on deck. Standing orders are noted."

#define HUSH_ROSTER_MIME_PLAIN "text/plain"
#define HUSH_ROSTER_MIME_MARKDOWN "text/markdown"
#define HUSH_ROSTER_MIME_XMARKDOWN "text/x-markdown"
#define HUSH_ROSTER_THEME_DEFAULT "field-office"
/* #279 approval setting ids, as stored in vibe.json and sent on /api/profile. */
#define HUSH_ROSTER_APPROVAL_AUTO_ID "auto_approve"
#define HUSH_ROSTER_APPROVAL_EVERY_ID "approve_every_action"
#define HUSH_ROSTER_PAYNE_SLUG "sgt-major-payne"
#define HUSH_ROSTER_ROLE_WORKER "worker"
#define HUSH_ROSTER_ROLE_CHAPERON "chaperon"

#define HUSH_ROSTER_PROVIDER_GOOSE "goose"
#define HUSH_ROSTER_PROVIDER_GROK_BUILD "grok-build"
#define HUSH_ROSTER_PROVIDER_CODEX "codex"
#define HUSH_ROSTER_PROVIDER_CLINE "cline"
#define HUSH_ROSTER_PROVIDER_GEMINI "gemini-api"
#define HUSH_ROSTER_PROVIDER_XAI "xai-api"
#define HUSH_ROSTER_PROVIDER_OPENAI "openai-api"
#define HUSH_ROSTER_PROVIDER_ANTHROPIC "anthropic-api"
#define HUSH_ROSTER_PROVIDER_DEEPSEEK "deepseek-api"
#define HUSH_ROSTER_PROVIDER_COPILOT "copilot"
#define HUSH_ROSTER_PROVIDER_OLLAMA "ollama"
#define HUSH_ROSTER_PROVIDER_CUSTOM "custom"

typedef struct {
    char name[HUSH_ROSTER_NAME_MAX];
    char mime[HUSH_ROSTER_NAME_MAX];
    size_t bytes;
    /* Plaintext/Markdown body, kept in memory for turn injection. Not
     * serialized to JSON (session payload stays lean); lost on restart. */
    char text[HUSH_ROSTER_CONTEXT_BYTES + 1];
} hush_roster_context_t;

typedef struct {
    hush_identity_t id;
    char name[HUSH_ROSTER_NAME_MAX];
    char slug[HUSH_ROSTER_NAME_MAX];
    char prompt[HUSH_ROSTER_PROMPT_MAX];
    char provider[HUSH_ROSTER_PROVIDER_MAX];
    /* Ranked provider list (index 0 = primary). provider[] mirrors index 0 for
     * backward compatibility. Empty entries are skipped. */
    char providers[HUSH_ROSTER_PROVIDERS_MAX][HUSH_ROSTER_PROVIDER_MAX];
    size_t nproviders;
    char picture[HUSH_ROSTER_PATH_MAX];
    char voice[HUSH_SKILL_VOICE_MAX];
    char skills[HUSH_SKILL_EQUIP_MAX][HUSH_SKILL_ID_MAX];
    size_t nskills;
    int enabled;
    int locked;
    char role[HUSH_ROSTER_NAME_MAX];
    int intro_enabled;
    char intro[HUSH_ROSTER_INTRO_MAX];
    hush_roster_context_t context[HUSH_ROSTER_CONTEXT_MAX];
    size_t ncontext;
} hush_roster_agent_t;

typedef struct {
    char npub[HUSH_IDENTITY_NPUB_MAX];
    char pubkey_hex[HUSH_IDENTITY_HEX_LEN + 1];
    char name[HUSH_ROSTER_NAME_MAX];
} hush_roster_member_t;

/* #279: whether the owner approves each robot turn before Hush starts the
 * robot's runtime. Zero is the default, so a fresh or old vibe auto-approves. */
typedef enum {
    HUSH_ROSTER_APPROVAL_AUTO = 0,
    HUSH_ROSTER_APPROVAL_EVERY = 1
} hush_roster_approval_t;

typedef struct {
    char first_name[HUSH_ROSTER_NAME_MAX];
    char last_name[HUSH_ROSTER_NAME_MAX];
    char email[HUSH_ROSTER_EMAIL_MAX];
    char organization[HUSH_ROSTER_NAME_MAX];
    char theme[HUSH_ROSTER_NAME_MAX];
    char picture[HUSH_ROSTER_PATH_MAX];
    /* Set only by hush_launch_set_approval; hush_roster_set_profile keeps it. */
    hush_roster_approval_t approval;
} hush_roster_profile_t;

typedef struct {
    hush_roster_profile_t profile;
    hush_roster_agent_t agents[HUSH_ROSTER_AGENTS_MAX];
    size_t nagents;
    hush_roster_member_t members[HUSH_ROSTER_MEMBERS_MAX];
    size_t nmembers;
} hush_roster_t;

/* Zeros roster and sets theme to dark. Safe on NULL. */
void hush_roster_init(hush_roster_t *roster);

/* True when mime or filename is plaintext or Markdown. */
int hush_roster_is_context_mime(const char *mime, const char *filename);

/* True when theme is one of the eight named palettes. */
int hush_roster_is_theme(const char *theme);
/* Reads an approval id. Returns 1 and sets *out for a known id; returns 0
 * and sets *out to HUSH_ROSTER_APPROVAL_AUTO for anything else. */
int hush_roster_approval_parse(const char *id, hush_roster_approval_t *out);
/* The stored id for mode; an unknown value reads as auto-approve. */
const char *hush_roster_approval_id(hush_roster_approval_t mode);

/* True when provider is one of the known named runtimes. */
int hush_roster_is_provider(const char *provider);

/* True when role is worker or chaperon. */
int hush_roster_is_role(const char *role);

/* Writes into out the slug of name: trimmed, lowercase ASCII letters and
 * digits kept, every other run folded to one '-'. Empty when name has no
 * ASCII letter or digit, which add and rename refuse. hush_roster_add_agent
 * starts a new robot's id from it and adds "-2", "-3", ... when that id is
 * already held. Names clash on hush_roster_name_key, not on the slug. */
void hush_roster_slug_of(char *out, size_t outsz, const char *name);

/* Writes into out the clash key of name: its ASCII letters and digits,
 * lowercased, with every other byte dropped (spaces and punctuation leave
 * no word break). Empty exactly when the slug is empty. */
void hush_roster_name_key(char *out, size_t outsz, const char *name);

/* True when name and other have the same non-empty key
 * (hush_roster_name_key): the same ASCII letters and digits in the same
 * order, ignoring case and every other byte. */
int hush_roster_is_name_clash(const char *name, const char *other);

/* True when name, trimmed the way add and update trim it, is current byte
 * for byte. Saving a robot's current name unchanged is never refused. */
int hush_roster_is_same_name(const char *name, const char *current);

/* True when name, trimmed the way add and update trim it, has no byte
 * below ASCII space and no DEL. Empty and NULL are true: a blank name
 * is a different refusal. */
int hush_roster_name_prints(const char *name);

/* Returns the robot other than except (may be NULL) whose current name
 * clashes with name (hush_roster_is_name_clash), or NULL when none does. */
const hush_roster_agent_t *
hush_roster_name_holder(const hush_roster_t *roster, const char *name,
                        const hush_roster_agent_t *except);

/* Returns the robot whose id is slug, or NULL when none is. */
const hush_roster_agent_t *hush_roster_agent_by_slug(const hush_roster_t *roster,
                                                     const char *slug);

/* Copies profile fields. Rejects a bad theme. */
hush_status_t hush_roster_set_profile(hush_roster_t *roster,
                                      const hush_roster_profile_t *in);

/* Adds a human member by npub or 64-char hex. */
hush_status_t hush_roster_add_member(hush_roster_t *roster,
                                     const char *key,
                                     const char *name);

/* One inbound context file (validated, not stored on the live roster). */
typedef struct {
    char name[HUSH_ROSTER_NAME_MAX];
    char mime[HUSH_ROSTER_NAME_MAX];
    const char *text;
    size_t bytes;
} hush_roster_context_in_t;

typedef struct {
    char name[HUSH_ROSTER_NAME_MAX];
    char prompt[HUSH_ROSTER_PROMPT_MAX];
    char provider[HUSH_ROSTER_PROVIDER_MAX];
    char providers[HUSH_ROSTER_PROVIDERS_MAX][HUSH_ROSTER_PROVIDER_MAX];
    size_t nproviders;
    int has_providers;
    char picture[HUSH_ROSTER_PATH_MAX];
    char voice[HUSH_SKILL_VOICE_MAX];
    char skills[HUSH_SKILL_EQUIP_MAX][HUSH_SKILL_ID_MAX];
    size_t nskills;
    int enabled;
    int has_enabled;
    int locked;
    int has_locked;
    char role[HUSH_ROSTER_NAME_MAX];
    int has_role;
    int has_picture;
    int has_voice;
    int has_skills;
    int intro_enabled;
    int has_intro_enabled;
    char intro[HUSH_ROSTER_INTRO_MAX];
    int has_intro;
    hush_roster_context_in_t context[HUSH_ROSTER_CONTEXT_MAX];
    size_t ncontext;
} hush_roster_agent_in_t;

/* Key write for a new agent: none, soft op/secret offer, or pass (hard). */
enum {
    HUSH_ROSTER_KEY_NONE = 0,
    HUSH_ROSTER_KEY_OFFER = 1,
    HUSH_ROSTER_KEY_PASS = 2
};

/* Creates an agent identity. Requires name, prompt, and provider.
 * key_mode selects keep behaviour. When key_mode is PASS and keep fails,
 * *pass_st (if non-NULL) receives that keep status and the agent is not
 * added; fill/context refusals leave *pass_st as HUSH_OK. */
hush_status_t hush_roster_add_agent(hush_roster_t *roster,
                                    hush_store_t *store,
                                    const hush_roster_agent_in_t *in,
                                    int key_mode,
                                    hush_status_t *pass_st);

/* Drops an agent by slug. Payne's slug is refused. */
hush_status_t hush_roster_remove_agent(hush_roster_t *roster, const char *slug);

/* Updates name, prompt, provider, picture, voice, intro, and equipped skills.
 * Slug and identity stay. Payne's slug is refused. Locked templates
 * accept enable and intro changes. */
hush_status_t hush_roster_update_agent(hush_roster_t *roster, const char *slug,
                                       const hush_roster_agent_in_t *in);

/* Writes agents/<slug>/nsec to pass for an existing roster robot.
 * Missing pass → DENIED; helper fail → IO; overlong path → ARG. */
hush_status_t hush_roster_save_agent_pass(const hush_roster_t *roster,
                                          const char *slug);

/* Same pass write as create/update, by slug + secret (path pin / tests). */
hush_status_t hush_roster_write_agent_pass(const char *slug,
                                           const char *secret);

/* Clones an agent to "<name> copy" unlocked. Payne is refused. */
hush_status_t hush_roster_clone_agent(hush_roster_t *roster,
                                      hush_store_t *store,
                                      const char *slug);

/* Appends agents and members JSON after a channels/projects-style cursor.
 * Writes a comma-prefixed fragment: ,"agents":[...],"members":[...]
 * Caller owns the surrounding object. */
hush_status_t hush_roster_format_json(const hush_roster_t *roster,
                                      char *out, size_t outsz,
                                      size_t *out_len);

#endif /* HUSH_ROSTER_H */
