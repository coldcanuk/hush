/* hush_roster.h: vibe members, agents, avatars, MIME-checked context. */

#ifndef HUSH_ROSTER_H
#define HUSH_ROSTER_H

#include <stddef.h>
#include <stdint.h>
#include "hush_identity.h"
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
    HUSH_ROSTER_JSON_MAX = 8192,
    HUSH_ROSTER_PROVIDER_MAX = 32,
    HUSH_ROSTER_PROMPT_PREVIEW = 160
};

#define HUSH_ROSTER_MIME_PLAIN "text/plain"
#define HUSH_ROSTER_MIME_MARKDOWN "text/markdown"
#define HUSH_ROSTER_MIME_XMARKDOWN "text/x-markdown"
#define HUSH_ROSTER_THEME_DEFAULT "dark"
#define HUSH_ROSTER_PAYNE_SLUG "sgt-major-payne"

#define HUSH_ROSTER_PROVIDER_GOOSE "goose"
#define HUSH_ROSTER_PROVIDER_GROK_BUILD "grok-build"
#define HUSH_ROSTER_PROVIDER_CODEX "codex"
#define HUSH_ROSTER_PROVIDER_CLINE "cline"
#define HUSH_ROSTER_PROVIDER_GEMINI "gemini-api"
#define HUSH_ROSTER_PROVIDER_XAI "xai-api"
#define HUSH_ROSTER_PROVIDER_OPENAI "openai-api"
#define HUSH_ROSTER_PROVIDER_ANTHROPIC "anthropic-api"
#define HUSH_ROSTER_PROVIDER_DEEPSEEK "deepseek-api"

typedef struct {
    char name[HUSH_ROSTER_NAME_MAX];
    char mime[HUSH_ROSTER_NAME_MAX];
    size_t bytes;
} hush_roster_context_t;

typedef struct {
    hush_identity_t id;
    char name[HUSH_ROSTER_NAME_MAX];
    char slug[HUSH_ROSTER_NAME_MAX];
    char prompt[HUSH_ROSTER_PROMPT_MAX];
    char provider[HUSH_ROSTER_PROVIDER_MAX];
    char picture[HUSH_ROSTER_PATH_MAX];
    hush_roster_context_t context[HUSH_ROSTER_CONTEXT_MAX];
    size_t ncontext;
} hush_roster_agent_t;

typedef struct {
    char npub[HUSH_IDENTITY_NPUB_MAX];
    char pubkey_hex[HUSH_IDENTITY_HEX_LEN + 1];
    char name[HUSH_ROSTER_NAME_MAX];
} hush_roster_member_t;

typedef struct {
    char first_name[HUSH_ROSTER_NAME_MAX];
    char last_name[HUSH_ROSTER_NAME_MAX];
    char email[HUSH_ROSTER_EMAIL_MAX];
    char organization[HUSH_ROSTER_NAME_MAX];
    char theme[HUSH_ROSTER_NAME_MAX];
    char picture[HUSH_ROSTER_PATH_MAX];
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

/* True when theme is one of the seven named palettes. */
int hush_roster_is_theme(const char *theme);

/* True when provider is one of the nine named runtimes. */
int hush_roster_is_provider(const char *provider);

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
    char picture[HUSH_ROSTER_PATH_MAX];
    hush_roster_context_in_t context[HUSH_ROSTER_CONTEXT_MAX];
    size_t ncontext;
} hush_roster_agent_in_t;

/* Creates an agent identity. Requires name, prompt, and provider. */
hush_status_t hush_roster_add_agent(hush_roster_t *roster,
                                    hush_store_t *store,
                                    const hush_roster_agent_in_t *in,
                                    int save_pass);

/* Drops an agent by slug. Payne's slug is refused. */
hush_status_t hush_roster_remove_agent(hush_roster_t *roster, const char *slug);

/* Appends agents and members JSON after a channels/projects-style cursor.
 * Writes a comma-prefixed fragment: ,"agents":[...],"members":[...]
 * Caller owns the surrounding object. */
hush_status_t hush_roster_format_json(const hush_roster_t *roster,
                                      char *out, size_t outsz,
                                      size_t *out_len);

#endif /* HUSH_ROSTER_H */
