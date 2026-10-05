/* hush_vault.h: client for the public vault.onerelay.app edge. */

#ifndef HUSH_VAULT_H
#define HUSH_VAULT_H

#include <stddef.h>
#include <stdint.h>

#include "hush_status.h"

/* Contract copied from host/vault/cloud/ntfy_client.py and
 * vault-cloud.server.conf. No other host, path, or field is sent. */
#define HUSH_VAULT_ORIGIN "https://vault.onerelay.app"
#define HUSH_VAULT_HEALTH_PATH "/v1/sys/health"
#define HUSH_VAULT_LOGIN_PATH "/v1/auth/cursor-ntfy/login"
#define HUSH_VAULT_SECRET_PATH "/v1/pass/data/ae/eggdrop/ntfy"
#define HUSH_VAULT_RENEW_PATH "/v1/auth/token/renew-self"
#define HUSH_VAULT_REVOKE_PATH "/v1/auth/token/revoke-self"
#define HUSH_VAULT_LOOKUP_PATH "/v1/auth/token/lookup-self"
#define HUSH_VAULT_USER_AGENT "thePlatform-ntfy-client/1.0"
#define HUSH_VAULT_ROLE "eggdrop"
#define HUSH_VAULT_POLICY "eggdrop-ntfy"
#define HUSH_VAULT_RENEW_BODY "{\"increment\":\"5m\"}"
#define HUSH_VAULT_REVOKE_BODY "{}"
#define HUSH_VAULT_NTFY_ORIGIN "https://ntfy.onerelay.app"
#define HUSH_VAULT_NTFY_USER "eggdrop"
#define HUSH_VAULT_PASS_OPENER "vault/onerelay/opener"

enum {
    HUSH_VAULT_TTL_MAX = 600,
    HUSH_VAULT_TOKEN_MAX = 512,
    HUSH_VAULT_JWT_MAX = 1024,
    HUSH_VAULT_SECRET_MAX = 512,
    HUSH_VAULT_URL_MAX = 160,
    HUSH_VAULT_BODY_MAX = 8192,
    HUSH_VAULT_CALL_MAX = 8,
    HUSH_VAULT_NAME_MAX = 64
};

/* One request the client built from the contract. Body is empty when the
 * call has no JSON body. Token is empty unless X-Vault-Token is sent. */
typedef struct {
    char method[8];
    char url[HUSH_VAULT_URL_MAX];
    char body[HUSH_VAULT_BODY_MAX];
    char token[HUSH_VAULT_TOKEN_MAX];
    int32_t has_body;
    int32_t has_token;
} hush_vault_call_t;

/* Scripted HTTP reply. Used when replies is set; otherwise libcurl runs. */
typedef struct {
    int32_t http_status;
    char body[HUSH_VAULT_BODY_MAX];
} hush_vault_reply_t;

/* Credential object at data.data from GET /v1/pass/data/ae/eggdrop/ntfy.
 * password is vault's value. This client never writes it into pass. */
typedef struct {
    char server[HUSH_VAULT_URL_MAX];
    char username[HUSH_VAULT_NAME_MAX];
    char password[HUSH_VAULT_SECRET_MAX];
} hush_vault_ntfy_t;

/* minted is 1 only after cursor-ntfy login. A token loaded from pass stays
 * minted 0 so close does not revoke the pass copy. ttl_seconds is 0 when
 * the token was adopted rather than minted. replies is borrowed. */
typedef struct {
    char token[HUSH_VAULT_TOKEN_MAX];
    char jwt[HUSH_VAULT_JWT_MAX];
    int32_t ttl_seconds;
    int32_t minted;
    const hush_vault_reply_t *replies;
    size_t reply_count;
    size_t reply_index;
    hush_vault_call_t calls[HUSH_VAULT_CALL_MAX];
    size_t call_count;
} hush_vault_session_t;


/* Zeros session. replies stays unset, so later calls use libcurl. */
void hush_vault_session_init(hush_vault_session_t *session);

/* Borrows replies for later calls. NULL restores libcurl. */
void hush_vault_session_use_fixture(hush_vault_session_t *session,
                                    const hush_vault_reply_t *replies,
                                    size_t reply_count);

/* Copies call idx. Fails HUSH_ERR_ARG or HUSH_ERR_NOT_FOUND. */
hush_status_t hush_vault_session_call(hush_vault_call_t *out,
                                      const hush_vault_session_t *session,
                                      size_t idx);

/* GET /v1/sys/health with no token. out_status receives the HTTP code. */
hush_status_t hush_vault_health(hush_vault_session_t *session,
                                int32_t *out_status);

/* POST /v1/auth/cursor-ntfy/login. jwt is a compact JWT (two dots,
 * base64url). Stores the client token only when policy is exactly
 * eggdrop-ntfy and 0 < ttl <= 600. */
hush_status_t hush_vault_login(hush_vault_session_t *session, const char *jwt);

/* POST /v1/auth/token/renew-self with {"increment":"5m"}. On HTTP 400
 * or 403, logs in again when session still holds a jwt. */
hush_status_t hush_vault_renew(hush_vault_session_t *session);

/* POST /v1/auth/token/revoke-self with {}. Clears the local token. */
hush_status_t hush_vault_revoke(hush_vault_session_t *session);

/* GET /v1/auth/token/lookup-self. Does not parse the body. */
hush_status_t hush_vault_lookup_self(hush_vault_session_t *session,
                                     int32_t *out_status);

/* GET /v1/pass/data/ae/eggdrop/ntfy. One fresh login retry after HTTP 403
 * when a jwt is stored. Refuses a secret whose server or username disagrees
 * with the ntfy contract. */
hush_status_t hush_vault_read_ntfy(hush_vault_session_t *session,
                                   hush_vault_ntfy_t *out);

/* Reads pass vault/onerelay/opener. A compact JWT logs in. Any other
 * header-safe value is sent as X-Vault-Token. Does not write pass. */
hush_status_t hush_vault_open(hush_vault_session_t *session,
                              hush_vault_ntfy_t *out);

/* Revokes only a token this session minted. Adopted pass tokens stay. */
hush_status_t hush_vault_close(hush_vault_session_t *session);

#endif /* HUSH_VAULT_H */
