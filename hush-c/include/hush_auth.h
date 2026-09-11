/* hush_auth.h: session token minting and constant-time credential checks. */

#ifndef HUSH_AUTH_H
#define HUSH_AUTH_H

#include <stddef.h>
#include "hush_status.h"

enum {
    HUSH_AUTH_TOKEN_HEX = 32,
    HUSH_AUTH_TOKEN_BUF = HUSH_AUTH_TOKEN_HEX + 1
};

#define HUSH_AUTH_FILE "session.token"
#define HUSH_AUTH_COOKIE "hush_session"
#define HUSH_AUTH_HEADER "X-Hush-Token"

/* Loads the session token from $HUSH_HOME/session.token, minting a 0600 file
 * when absent or malformed. Idempotent. Fails HUSH_ERR_IO when the home or
 * token file cannot be secured. */
hush_status_t hush_auth_init(void);

/* Copies the live token into out. Fails HUSH_ERR_ARG or HUSH_ERR_IO. */
hush_status_t hush_auth_token_copy(char *out, size_t outsz);

/* True when presented equals the live token. Constant time on equal length. */
int hush_auth_token_matches(const char *presented);

/* Constant-time equality for two NUL-terminated strings. */
int hush_auth_tokens_equal(const char *a, const char *b);

#endif /* HUSH_AUTH_H */
