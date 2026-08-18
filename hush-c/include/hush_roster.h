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
    HUSH_ROSTER_CONTEXT_MAX = 4,
    HUSH_ROSTER_CONTEXT_BYTES = 4096,
    HUSH_ROSTER_PATH_MAX = 256,
    HUSH_ROSTER_JSON_MAX = 8192
};

#define HUSH_ROSTER_MIME_PLAIN "text/plain"
#define HUSH_ROSTER_MIME_MARKDOWN "text/markdown"
#define HUSH_ROSTER_MIME_XMARKDOWN "text/x-markdown"
#define HUSH_ROSTER_THEME_DEFAULT "dark"

typedef struct {
    char name[HUSH_ROSTER_NAME_MAX];
    char mime[HUSH_ROSTER_NAME_MAX];
    char text[HUSH_ROSTER_CONTEXT_BYTES];
    size_t bytes;
} hush_roster_context_t;

typedef struct {
    hush_identity_t id;
    char name[HUSH_ROSTER_NAME_MAX];
    char slug[HUSH_ROSTER_NAME_MAX];
    char prompt[HUSH_ROSTER_PROMPT_MAX];
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

/* Copies profile fields. Rejects a bad theme. */
hush_status_t hush_roster_set_profile(hush_roster_t *roster,
                                      const hush_roster_profile_t *in);

/* Adds a human member by npub or 64-char hex. */
hush_status_t hush_roster_add_member(hush_roster_t *roster,
                                     const char *key,
                                     const char *name);

/* Creates an agent identity, stores optional context, optional pass save. */
hush_status_t hush_roster_add_agent(hush_roster_t *roster,
                                    hush_store_t *store,
                                    const hush_roster_agent_t *in,
                                    int save_pass);

/* Appends agents and members JSON after a channels/projects-style cursor.
 * Writes a comma-prefixed fragment: ,"agents":[...],"members":[...]
 * Caller owns the surrounding object. */
hush_status_t hush_roster_format_json(const hush_roster_t *roster,
                                      char *out, size_t outsz,
                                      size_t *out_len);

#endif /* HUSH_ROSTER_H */
