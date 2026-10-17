/* hush_auth.h: session token minting and constant-time credential checks. */

#ifndef HUSH_AUTH_H
#define HUSH_AUTH_H

#include <stddef.h>
#include "hush_status.h"

enum {
    HUSH_AUTH_TOKEN_HEX = 32,
    HUSH_AUTH_TOKEN_BUF = HUSH_AUTH_TOKEN_HEX + 1,
    HUSH_AUTH_CHALLENGE_HEX = 64,
    HUSH_AUTH_CHALLENGE_BUF = HUSH_AUTH_CHALLENGE_HEX + 1,
    HUSH_AUTH_SHA256_HEX = 64,
    HUSH_AUTH_SHA256_BUF = HUSH_AUTH_SHA256_HEX + 1,
    /* One-time browser pairing code (#336). */
    HUSH_AUTH_PAIR_HEX = 32,
    HUSH_AUTH_PAIR_BUF = HUSH_AUTH_PAIR_HEX + 1,
    /* Browser-session secret behind approvals (#336). */
    HUSH_AUTH_UI_HEX = 32,
    HUSH_AUTH_UI_BUF = HUSH_AUTH_UI_HEX + 1
};

#define HUSH_AUTH_FILE "session.token"
#define HUSH_AUTH_COOKIE "hush_session"
#define HUSH_AUTH_HEADER "X-Hush-Token"
#define HUSH_AUTH_PAIR_PARAM "pair"
#define HUSH_AUTH_UI_HEADER "X-Hush-Nonce"

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

/* Mints the one-time browser pairing code. It lives in memory only: never
 * written to disk, never derived from session.token, new on every relay
 * start. Idempotent. Fails HUSH_ERR_IO when entropy is unavailable. */
hush_status_t hush_auth_pair_init(void);

/* Copies the live pairing code into out. Fails HUSH_ERR_ARG on a short
 * buffer, HUSH_ERR_IO before hush_auth_pair_init or after a failed re-mint. */
hush_status_t hush_auth_pair_copy(char *out, size_t outsz);

/* Redeems a pairing code: true when presented equals the live code
 * (constant time). A redeemed code is re-minted at once, so it works once. */
int hush_auth_pair_take(const char *presented);

/* Mints a new browser-session secret, ending the previous one. Called when
 * a pairing code is redeemed, so the last paired window holds the only
 * live secret. Memory only; not derived from session.token. Fails
 * HUSH_ERR_IO when entropy is unavailable (then nothing matches). */
hush_status_t hush_auth_ui_rotate(void);

/* Copies the live browser-session secret. HUSH_ERR_ARG on a short buffer,
 * HUSH_ERR_IO when none is live. */
hush_status_t hush_auth_ui_copy(char *out, size_t outsz);

/* True when presented equals the live browser-session secret (constant
 * time). False when none is live. */
int hush_auth_ui_matches(const char *presented);

/* Mints a fresh NIP-42 challenge: 32 random bytes hex-encoded (64 chars).
 * Fails HUSH_ERR_ARG on a short buffer, HUSH_ERR_IO when entropy is
 * unavailable. */
hush_status_t hush_auth_challenge_mint(char *out, size_t outsz);

/* Writes hex(SHA-256(text)) into out. Fails HUSH_ERR_ARG on NULL or a
 * short buffer, HUSH_ERR_CRYPTO on a digest failure. */
hush_status_t hush_auth_sha256_hex(const char *text, char *out, size_t outsz);

/* True when hex(SHA-256(presented)) equals the stored join-token hash.
 * Constant time on the digest comparison. */
int hush_auth_join_matches(const char *presented, const char *stored_hash);

#endif /* HUSH_AUTH_H */
