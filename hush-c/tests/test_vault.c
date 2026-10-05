/* tests/test_vault.c: vault.onerelay.app contract against a scripted fixture. */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hush_pass.h"
#include "hush_vault.h"

#include "fake_curl_vault.h"

enum {
    TEST_DIR_MAX = 128
};

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

static void fill_auth(hush_vault_reply_t *reply, const char *token,
                      int ttl, const char *policies)
{
    reply->http_status = 200;
    snprintf(reply->body, sizeof(reply->body),
             "{\"auth\":{\"client_token\":\"%s\",\"lease_duration\":%d,"
             "\"policies\":%s}}",
             token, ttl, policies);
}

static void fill_secret(hush_vault_reply_t *reply, const char *server)
{
    reply->http_status = 200;
    snprintf(reply->body, sizeof(reply->body),
             "{\"data\":{\"data\":{\"server\":\"%s\","
             "\"username\":\"eggdrop\",\"password\":\"private-test-password\"}}}",
             server);
}

static void test_health_and_login_contract(void)
{
    hush_vault_session_t session;
    hush_vault_reply_t replies[2];
    hush_vault_call_t call;
    int32_t status = 0;

    memset(replies, 0, sizeof(replies));
    replies[0].http_status = 200;
    snprintf(replies[0].body, sizeof(replies[0].body), "{\"initialized\":true}");
    fill_auth(&replies[1], "short-lived-token", 300, "[\"eggdrop-ntfy\"]");
    hush_vault_session_init(&session);
    hush_vault_session_use_fixture(&session, replies, 2);
    expect(hush_vault_health(&session, &status) == HUSH_OK, "health ok");
    expect(status == 200, "health status");
    expect(hush_vault_session_call(&call, &session, 0) == HUSH_OK, "health call");
    expect(strcmp(call.method, "GET") == 0, "health method");
    expect(strcmp(call.url, "https://vault.onerelay.app/v1/sys/health") == 0,
           "health url");
    expect(!call.has_token && !call.has_body, "health has no token or body");
    expect(hush_vault_login(&session, "fresh.identity.0") == HUSH_OK, "login");
    expect(hush_vault_session_call(&call, &session, 1) == HUSH_OK, "login call");
    expect(strcmp(call.method, "POST") == 0, "login method");
    expect(strcmp(call.url, "https://vault.onerelay.app/v1/auth/cursor-ntfy/login") == 0,
           "login url");
    expect(!call.has_token, "login sends no vault token");
    expect(strcmp(call.body,
                  "{\"role\":\"eggdrop\",\"jwt\":\"fresh.identity.0\"}") == 0,
           "login body");
    expect(strcmp(session.token, "short-lived-token") == 0, "token stored");
    expect(session.ttl_seconds == 300, "ttl stored");
    expect(session.minted == 1, "login mints");
}

static void test_refuses_broad_or_long_token(void)
{
    hush_vault_session_t session;
    hush_vault_reply_t wide;
    hush_vault_reply_t long_ttl;

    memset(&wide, 0, sizeof(wide));
    memset(&long_ttl, 0, sizeof(long_ttl));
    fill_auth(&wide, "wide", 300, "[\"default\",\"eggdrop-ntfy\"]");
    fill_auth(&long_ttl, "long", 601, "[\"eggdrop-ntfy\"]");
    hush_vault_session_init(&session);
    hush_vault_session_use_fixture(&session, &wide, 1);
    expect(hush_vault_login(&session, "fresh.identity.0") == HUSH_ERR_DENIED,
           "wide policy denied");
    expect(session.token[0] == '\0', "wide token dropped");
    hush_vault_session_init(&session);
    hush_vault_session_use_fixture(&session, &long_ttl, 1);
    expect(hush_vault_login(&session, "fresh.identity.0") == HUSH_ERR_DENIED,
           "long ttl denied");
    expect(session.token[0] == '\0', "long token dropped");
}

static void test_renew_revoke_and_lookup(void)
{
    hush_vault_session_t session;
    hush_vault_reply_t replies[4];
    hush_vault_call_t call;
    int32_t status = 0;

    memset(replies, 0, sizeof(replies));
    fill_auth(&replies[0], "short-lived-token", 300, "[\"eggdrop-ntfy\"]");
    replies[1].http_status = 400;
    fill_auth(&replies[2], "renewed", 300, "[\"eggdrop-ntfy\"]");
    replies[3].http_status = 204;
    hush_vault_session_init(&session);
    hush_vault_session_use_fixture(&session, replies, 4);
    expect(hush_vault_login(&session, "fresh.identity.0") == HUSH_OK, "login");
    expect(hush_vault_renew(&session) == HUSH_OK, "renew falls back to login");
    expect(strcmp(session.token, "renewed") == 0, "renewed token");
    expect(hush_vault_session_call(&call, &session, 1) == HUSH_OK, "renew call");
    expect(strcmp(call.url, "https://vault.onerelay.app/v1/auth/token/renew-self") == 0,
           "renew url");
    expect(strcmp(call.body, HUSH_VAULT_RENEW_BODY) == 0, "renew body");
    expect(call.has_token, "renew sends token");
    expect(strcmp(session.token, "renewed") == 0, "token before lookup");
    /* lookup needs the minted token; login from renew already consumed slot 2.
     * Queue was 4 replies: login, renew 400, re-login, revoke. Lookup would
     * need another reply, so check revoke next. */
    expect(hush_vault_revoke(&session) == HUSH_OK, "revoke");
    expect(session.token[0] == '\0', "revoke clears token");
    expect(hush_vault_session_call(&call, &session, 3) == HUSH_OK, "revoke call");
    expect(strcmp(call.url, "https://vault.onerelay.app/v1/auth/token/revoke-self") == 0,
           "revoke url");
    expect(strcmp(call.body, "{}") == 0, "revoke body");
    expect(call.has_token, "revoke sent the token");
    (void)status;
}

static void test_lookup_self_contract(void)
{
    hush_vault_session_t session;
    hush_vault_reply_t replies[2];
    hush_vault_call_t call;
    int32_t status = 0;

    memset(replies, 0, sizeof(replies));
    fill_auth(&replies[0], "short-lived-token", 300, "[\"eggdrop-ntfy\"]");
    replies[1].http_status = 200;
    snprintf(replies[1].body, sizeof(replies[1].body), "{\"data\":{}}");
    hush_vault_session_init(&session);
    hush_vault_session_use_fixture(&session, replies, 2);
    expect(hush_vault_login(&session, "fresh.identity.0") == HUSH_OK, "login");
    expect(hush_vault_lookup_self(&session, &status) == HUSH_OK, "lookup");
    expect(status == 200, "lookup status");
    expect(hush_vault_session_call(&call, &session, 1) == HUSH_OK, "lookup call");
    expect(strcmp(call.method, "GET") == 0, "lookup method");
    expect(strcmp(call.url, "https://vault.onerelay.app/v1/auth/token/lookup-self") == 0,
           "lookup url");
    expect(call.has_token && !call.has_body, "lookup token and no body");
}

static void test_secret_shape_and_revoked_retry(void)
{
    hush_vault_session_t session;
    hush_vault_reply_t bad[2];
    hush_vault_reply_t again[4];
    hush_vault_ntfy_t cred;
    hush_vault_call_t call;

    memset(bad, 0, sizeof(bad));
    fill_auth(&bad[0], "short-lived-token", 300, "[\"eggdrop-ntfy\"]");
    fill_secret(&bad[1], "https://attacker.invalid");
    hush_vault_session_init(&session);
    hush_vault_session_use_fixture(&session, bad, 2);
    expect(hush_vault_login(&session, "fresh.identity.0") == HUSH_OK, "login");
    expect(hush_vault_read_ntfy(&session, &cred) == HUSH_ERR_DENIED,
           "wrong server denied");
    expect(session.call_count == 2, "shape denial does not re-login");
    memset(&cred, 0, sizeof(cred));
    memset(again, 0, sizeof(again));
    fill_auth(&again[0], "short-lived-token", 300, "[\"eggdrop-ntfy\"]");
    again[1].http_status = 403;
    fill_auth(&again[2], "new-token", 300, "[\"eggdrop-ntfy\"]");
    fill_secret(&again[3], "https://ntfy.onerelay.app/");
    hush_vault_session_init(&session);
    hush_vault_session_use_fixture(&session, again, 4);
    expect(hush_vault_login(&session, "fresh.identity.0") == HUSH_OK, "login 2");
    expect(hush_vault_read_ntfy(&session, &cred) == HUSH_OK, "retry read");
    expect(strcmp(cred.server, HUSH_VAULT_NTFY_ORIGIN) == 0, "slash stripped");
    expect(strcmp(cred.username, "eggdrop") == 0, "username");
    expect(strcmp(cred.password, "private-test-password") == 0, "password");
    expect(hush_vault_session_call(&call, &session, 2) == HUSH_OK, "relogin");
    expect(strcmp(call.url, "https://vault.onerelay.app/v1/auth/cursor-ntfy/login") == 0,
           "retry is login");
    expect(strcmp(session.token, "new-token") == 0, "replacement token");
}

static void use_fake_pass(const char *dir)
{
    expect(setenv("HUSH_FAKE_PASS_DIR", dir, 1) == 0, "pass dir");
    hush_pass_set_helper("tests/fake-pass.sh");
    if (access("tests/fake-pass.sh", X_OK) != 0)
        hush_pass_set_helper("./tests/fake-pass.sh");
}

static void test_open_from_pass_keeps_store(void)
{
    char dir[TEST_DIR_MAX];
    hush_vault_session_t session;
    hush_vault_reply_t jwt_replies[3];
    hush_vault_reply_t token_replies[1];
    hush_vault_ntfy_t cred;
    hush_vault_call_t call;
    char saved[HUSH_VAULT_JWT_MAX];

    snprintf(dir, sizeof(dir), "/tmp/hush-vault-pass-%ld", (long)getpid());
    use_fake_pass(dir);
    expect(hush_pass_save(HUSH_VAULT_PASS_OPENER, "fresh.identity.0") == HUSH_OK,
           "save jwt");
    memset(jwt_replies, 0, sizeof(jwt_replies));
    fill_auth(&jwt_replies[0], "short-lived-token", 300, "[\"eggdrop-ntfy\"]");
    fill_secret(&jwt_replies[1], "https://ntfy.onerelay.app");
    jwt_replies[2].http_status = 204;
    hush_vault_session_init(&session);
    hush_vault_session_use_fixture(&session, jwt_replies, 3);
    expect(hush_vault_open(&session, &cred) == HUSH_OK, "open jwt");
    expect(hush_pass_get(saved, sizeof(saved), HUSH_VAULT_PASS_OPENER) == HUSH_OK,
           "opener remains");
    expect(strcmp(saved, "fresh.identity.0") == 0, "opener not rewritten");
    expect(!hush_pass_has("vault/onerelay/ntfy"), "secret not copied to pass");
    expect(hush_vault_close(&session) == HUSH_OK, "close minted");
    expect(hush_vault_session_call(&call, &session, 2) == HUSH_OK, "close call");
    expect(strcmp(call.url, "https://vault.onerelay.app/v1/auth/token/revoke-self") == 0,
           "close revokes minted token");
    expect(session.token[0] == '\0', "closed token cleared");

    expect(hush_pass_save(HUSH_VAULT_PASS_OPENER, "hvs.adopted") == HUSH_OK,
           "save token");
    memset(token_replies, 0, sizeof(token_replies));
    fill_secret(&token_replies[0], "https://ntfy.onerelay.app");
    hush_vault_session_init(&session);
    hush_vault_session_use_fixture(&session, token_replies, 1);
    expect(hush_vault_open(&session, &cred) == HUSH_OK, "open token");
    expect(session.call_count == 1, "adopted token skips login");
    expect(hush_vault_session_call(&call, &session, 0) == HUSH_OK, "get call");
    expect(strcmp(call.url, "https://vault.onerelay.app/v1/pass/data/ae/eggdrop/ntfy") == 0,
           "secret url");
    expect(strcmp(call.token, "hvs.adopted") == 0, "pass token header");
    expect(session.minted == 0, "adopted is not minted");
    expect(hush_vault_close(&session) == HUSH_OK, "close adopted");
    expect(session.call_count == 1, "close does not revoke pass token");
    expect(hush_pass_get(saved, sizeof(saved), HUSH_VAULT_PASS_OPENER) == HUSH_OK,
           "token opener remains");
    expect(strcmp(saved, "hvs.adopted") == 0, "token opener unchanged");
    hush_pass_set_helper(NULL);
}

static void test_missing_opener(void)
{
    char dir[TEST_DIR_MAX];
    hush_vault_session_t session;
    hush_vault_ntfy_t cred;

    snprintf(dir, sizeof(dir), "/tmp/hush-vault-empty-%ld", (long)getpid());
    use_fake_pass(dir);
    hush_vault_session_init(&session);
    expect(hush_vault_open(&session, &cred) == HUSH_ERR_NOT_FOUND, "missing");
    expect(session.call_count == 0, "missing sends nothing");
    expect(hush_pass_save(HUSH_VAULT_PASS_OPENER, "not a token") == HUSH_OK,
           "save spaced");
    expect(hush_vault_open(&session, &cred) == HUSH_ERR_ARG, "spaced opener");
    expect(session.call_count == 0, "bad opener sends nothing");
    hush_pass_set_helper(NULL);
}


static int buffer_all_zero(const char *buf, size_t n)
{
    size_t idx = 0;

    for (idx = 0; idx < n; ++idx) {
        if (buf[idx] != '\0')
            return 0;
    }
    return 1;
}

/* Pins the body contract strings so a header-macro mutant fails (Gauge V1-V3). */
static void test_contract_literals(void)
{
    expect(strcmp(HUSH_VAULT_ORIGIN, "https://vault.onerelay.app") == 0,
           "origin literal");
    expect(strcmp(HUSH_VAULT_HEALTH_PATH, "/v1/sys/health") == 0, "health path");
    expect(strcmp(HUSH_VAULT_LOGIN_PATH, "/v1/auth/cursor-ntfy/login") == 0,
           "login path");
    expect(strcmp(HUSH_VAULT_SECRET_PATH, "/v1/pass/data/ae/eggdrop/ntfy") == 0,
           "secret path");
    expect(strcmp(HUSH_VAULT_RENEW_PATH, "/v1/auth/token/renew-self") == 0,
           "renew path");
    expect(strcmp(HUSH_VAULT_REVOKE_PATH, "/v1/auth/token/revoke-self") == 0,
           "revoke path");
    expect(strcmp(HUSH_VAULT_LOOKUP_PATH, "/v1/auth/token/lookup-self") == 0,
           "lookup path");
}

/* After close, token/JWT/call-log secret copies must be full-buffer zero. */
static void test_close_wipes_secrets(void)
{
    hush_vault_session_t session;
    hush_vault_reply_t replies[2];
    hush_vault_call_t call;
    size_t idx = 0;

    memset(replies, 0, sizeof(replies));
    fill_auth(&replies[0], "short-lived-token", 300, "[\"eggdrop-ntfy\"]");
    replies[1].http_status = 204;
    hush_vault_session_init(&session);
    hush_vault_session_use_fixture(&session, replies, 2);
    expect(hush_vault_login(&session, "fresh.identity.0") == HUSH_OK, "login");
    expect(session.token[0] != '\0', "token present before close");
    expect(session.jwt[0] != '\0', "jwt present before close");
    expect(hush_vault_session_call(&call, &session, 0) == HUSH_OK, "login call");
    expect(call.body[0] != '\0', "login body recorded");
    expect(hush_vault_close(&session) == HUSH_OK, "close");
    expect(buffer_all_zero(session.token, sizeof(session.token)),
           "token fully wiped");
    expect(buffer_all_zero(session.jwt, sizeof(session.jwt)), "jwt fully wiped");
    expect(session.ttl_seconds == 0, "ttl cleared");
    expect(session.minted == 0, "minted cleared");
    expect(session.call_count == 2, "login and revoke recorded");
    for (idx = 0; idx < session.call_count; ++idx) {
        expect(hush_vault_session_call(&call, &session, idx) == HUSH_OK,
               "call after wipe");
        expect(buffer_all_zero(call.body, sizeof(call.body)), "call body wiped");
        expect(buffer_all_zero(call.token, sizeof(call.token)), "call token wiped");
        expect(!call.has_body && !call.has_token, "call secret flags cleared");
    }
    expect(hush_vault_session_call(&call, &session, 1) == HUSH_OK, "revoke call");
    expect(strcmp(call.url,
                  "https://vault.onerelay.app/v1/auth/token/revoke-self") == 0,
           "revoke url kept");
}



/* --- wipe observer (N1): --wrap=hush_secure_zero records wipe calls --- */

enum {
    WIPE_LOG_MAX = 64,
    WIPE_SNIP_MAX = 512
};

typedef struct {
    size_t n;
    char snip[WIPE_SNIP_MAX];
} wipe_event_t;

static wipe_event_t g_wipes[WIPE_LOG_MAX];
static size_t g_wipe_n;

static void wipe_log_reset(void)
{
    memset(g_wipes, 0, sizeof(g_wipes));
    g_wipe_n = 0;
}

static void wipe_log_hook(const void *buf, size_t n)
{
    wipe_event_t *ev = NULL;
    size_t copy = 0;

    if (g_wipe_n >= (size_t)WIPE_LOG_MAX || buf == NULL)
        return;
    ev = &g_wipes[g_wipe_n];
    ev->n = n;
    copy = n;
    if (copy >= (size_t)WIPE_SNIP_MAX)
        copy = (size_t)WIPE_SNIP_MAX - 1;
    memcpy(ev->snip, buf, copy);
    ev->snip[copy] = '\0';
    g_wipe_n++;
}

static int wipe_log_saw(size_t n, const char *needle)
{
    size_t idx = 0;

    for (idx = 0; idx < g_wipe_n; ++idx) {
        if (g_wipes[idx].n != n)
            continue;
        if (needle == NULL || needle[0] == '\0')
            return 1;
        if (strstr(g_wipes[idx].snip, needle) != NULL)
            return 1;
    }
    return 0;
}

static int wipe_log_count(size_t n, const char *needle)
{
    size_t idx = 0;
    int count = 0;

    for (idx = 0; idx < g_wipe_n; ++idx) {
        if (g_wipes[idx].n != n)
            continue;
        if (needle == NULL || needle[0] == '\0'
            || strstr(g_wipes[idx].snip, needle) != NULL)
            count++;
    }
    return count;
}

/* Linked only into test_vault via -Wl,--wrap=hush_secure_zero. */
void __real_hush_secure_zero(void *buf, size_t n);
void __wrap_hush_secure_zero(void *buf, size_t n);

static int g_wipe_observe;

void __wrap_hush_secure_zero(void *buf, size_t n)
{
    if (g_wipe_observe)
        wipe_log_hook(buf, n);
    __real_hush_secure_zero(buf, n);
}

static void wipe_log_install(void)
{
    wipe_log_reset();
    g_wipe_observe = 1;
}

static void wipe_log_clear(void)
{
    g_wipe_observe = 0;
    wipe_log_reset();
}

/* W2 + W24: adopted (non-minted) close must full-wipe the pass token. */
static void test_adopted_close_wipes_token(void)
{
    char dir[TEST_DIR_MAX];
    hush_vault_session_t session;
    hush_vault_reply_t replies[1];
    hush_vault_ntfy_t cred;

    snprintf(dir, sizeof(dir), "/tmp/hush-vault-adopt-%ld", (long)getpid());
    use_fake_pass(dir);
    expect(hush_pass_save(HUSH_VAULT_PASS_OPENER, "hvs.adopted-wipe") == HUSH_OK,
           "save adopted");
    memset(replies, 0, sizeof(replies));
    fill_secret(&replies[0], "https://ntfy.onerelay.app");
    hush_vault_session_init(&session);
    hush_vault_session_use_fixture(&session, replies, 1);
    expect(hush_vault_open(&session, &cred) == HUSH_OK, "open adopted");
    expect(session.minted == 0, "not minted");
    expect(strcmp(session.token, "hvs.adopted-wipe") == 0, "token present");
    expect(hush_vault_close(&session) == HUSH_OK, "close adopted");
    expect(buffer_all_zero(session.token, sizeof(session.token)),
           "adopted token fully wiped");
    expect(buffer_all_zero(session.jwt, sizeof(session.jwt)), "jwt wiped");
    expect(session.minted == 0, "still not minted");
    hush_pass_set_helper(NULL);
}

/* W9: revoke itself full-buffer zeros the token (not only token[0]). */
static void test_revoke_full_zeros_token(void)
{
    hush_vault_session_t session;
    hush_vault_reply_t replies[2];

    memset(replies, 0, sizeof(replies));
    fill_auth(&replies[0], "short-lived-token", 300, "[\"eggdrop-ntfy\"]");
    replies[1].http_status = 204;
    hush_vault_session_init(&session);
    hush_vault_session_use_fixture(&session, replies, 2);
    expect(hush_vault_login(&session, "fresh.identity.0") == HUSH_OK, "login");
    /* Poison beyond the NUL so token[0]-only clear would leave residue. */
    memset(session.token + strlen(session.token) + 1, 'Z',
           sizeof(session.token) - strlen(session.token) - 1);
    expect(hush_vault_revoke(&session) == HUSH_OK, "revoke");
    expect(buffer_all_zero(session.token, sizeof(session.token)),
           "revoke full-zeros token");
}

/* W10, W11, W17: fixture login / denied login wipe next, http, login body. */
static void test_login_path_wipes(void)
{
    hush_vault_session_t session;
    hush_vault_reply_t ok;
    hush_vault_reply_t wide;

    wipe_log_install();
    memset(&ok, 0, sizeof(ok));
    fill_auth(&ok, "short-lived-token", 300, "[\"eggdrop-ntfy\"]");
    hush_vault_session_init(&session);
    hush_vault_session_use_fixture(&session, &ok, 1);
    expect(hush_vault_login(&session, "fresh.identity.0") == HUSH_OK, "login ok");
    expect(wipe_log_saw((size_t)HUSH_VAULT_BODY_MAX, "fresh.identity.0"),
           "W17 login body wiped");
    expect(wipe_log_saw((size_t)HUSH_VAULT_BODY_MAX, "short-lived-token"),
           "W11 post_auth http wiped");
    expect(wipe_log_saw((size_t)HUSH_VAULT_TOKEN_MAX, "short-lived-token"),
           "W10 accept_auth next wiped");

    wipe_log_reset();
    memset(&wide, 0, sizeof(wide));
    fill_auth(&wide, "wide-token-value", 300, "[\"default\",\"eggdrop-ntfy\"]");
    hush_vault_session_init(&session);
    hush_vault_session_use_fixture(&session, &wide, 1);
    expect(hush_vault_login(&session, "fresh.identity.0") == HUSH_ERR_DENIED,
           "wide denied");
    expect(wipe_log_saw((size_t)HUSH_VAULT_TOKEN_MAX, "wide-token-value"),
           "W10 next wiped on deny");
    expect(wipe_log_saw((size_t)HUSH_VAULT_BODY_MAX, "wide-token-value"),
           "W11 http wiped on deny");
    wipe_log_clear();
}

/* W12: health wipes http body. */
static void test_health_wipes_http(void)
{
    hush_vault_session_t session;
    hush_vault_reply_t reply;
    int32_t status = 0;

    wipe_log_install();
    memset(&reply, 0, sizeof(reply));
    reply.http_status = 200;
    snprintf(reply.body, sizeof(reply.body),
             "{\"initialized\":true,\"marker\":\"health-wipe-marker\"}");
    hush_vault_session_init(&session);
    hush_vault_session_use_fixture(&session, &reply, 1);
    expect(hush_vault_health(&session, &status) == HUSH_OK, "health");
    expect(wipe_log_saw((size_t)HUSH_VAULT_BODY_MAX, "health-wipe-marker"),
           "W12 health http wiped");
    wipe_log_clear();
}

/* W13: lookup_self wipes http body. */
static void test_lookup_wipes_http(void)
{
    hush_vault_session_t session;
    hush_vault_reply_t replies[2];
    int32_t status = 0;

    wipe_log_install();
    memset(replies, 0, sizeof(replies));
    fill_auth(&replies[0], "short-lived-token", 300, "[\"eggdrop-ntfy\"]");
    replies[1].http_status = 200;
    snprintf(replies[1].body, sizeof(replies[1].body),
             "{\"data\":{\"marker\":\"lookup-wipe-marker\"}}");
    hush_vault_session_init(&session);
    hush_vault_session_use_fixture(&session, replies, 2);
    expect(hush_vault_login(&session, "fresh.identity.0") == HUSH_OK, "login");
    wipe_log_reset();
    expect(hush_vault_lookup_self(&session, &status) == HUSH_OK, "lookup");
    expect(wipe_log_saw((size_t)HUSH_VAULT_BODY_MAX, "lookup-wipe-marker"),
           "W13 lookup http wiped");
    wipe_log_clear();
}

/* W15: read_ntfy final http wipe. */
static void test_read_ntfy_wipes_http(void)
{
    hush_vault_session_t session;
    hush_vault_reply_t replies[2];
    hush_vault_ntfy_t cred;

    wipe_log_install();
    memset(replies, 0, sizeof(replies));
    fill_auth(&replies[0], "short-lived-token", 300, "[\"eggdrop-ntfy\"]");
    fill_secret(&replies[1], "https://ntfy.onerelay.app");
    hush_vault_session_init(&session);
    hush_vault_session_use_fixture(&session, replies, 2);
    expect(hush_vault_login(&session, "fresh.identity.0") == HUSH_OK, "login");
    wipe_log_reset();
    expect(hush_vault_read_ntfy(&session, &cred) == HUSH_OK, "read");
    expect(wipe_log_saw((size_t)HUSH_VAULT_BODY_MAX, "private-test-password"),
           "W15 read_ntfy http wiped");
    wipe_log_clear();
}

/* W18: open wipes opener (JWT). */
static void test_open_wipes_opener(void)
{
    char dir[TEST_DIR_MAX];
    hush_vault_session_t session;
    hush_vault_reply_t replies[2];
    hush_vault_ntfy_t cred;

    wipe_log_install();
    snprintf(dir, sizeof(dir), "/tmp/hush-vault-opener-%ld", (long)getpid());
    use_fake_pass(dir);
    expect(hush_pass_save(HUSH_VAULT_PASS_OPENER, "fresh.identity.0") == HUSH_OK,
           "save jwt");
    memset(replies, 0, sizeof(replies));
    fill_auth(&replies[0], "short-lived-token", 300, "[\"eggdrop-ntfy\"]");
    fill_secret(&replies[1], "https://ntfy.onerelay.app");
    hush_vault_session_init(&session);
    hush_vault_session_use_fixture(&session, replies, 2);
    expect(hush_vault_open(&session, &cred) == HUSH_OK, "open");
    expect(wipe_log_saw((size_t)HUSH_VAULT_JWT_MAX, "fresh.identity.0"),
           "W18 opener wiped");
    wipe_log_clear();
    hush_pass_set_helper(NULL);
}

/* W19 + V20h + N2: curl path via fake libcurl (no fixture). */
static void test_curl_path_wipes(void)
{
    hush_vault_session_t session;
    char auth[512];
    char secret[512];
    hush_vault_ntfy_t cred;

    wipe_log_install();
    fake_curl_reset_slist_stats();
    snprintf(auth, sizeof(auth),
             "{\"auth\":{\"client_token\":\"short-lived-token\","
             "\"lease_duration\":300,\"policies\":[\"eggdrop-ntfy\"]}}");
    snprintf(secret, sizeof(secret),
             "{\"data\":{\"data\":{\"server\":\"https://ntfy.onerelay.app\","
             "\"username\":\"eggdrop\",\"password\":\"private-test-password\"}}}");

    hush_vault_session_init(&session);
    /* replies NULL => libcurl path (fake). */
    fake_curl_set_reply(200, auth);
    expect(hush_vault_login(&session, "fresh.identity.0") == HUSH_OK,
           "curl login");
    /* Login has no X-Vault-Token; W19 needs a token-bearing call next. */
    wipe_log_reset();
    fake_curl_reset_slist_stats();
    fake_curl_set_reply(200, secret);
    expect(hush_vault_read_ntfy(&session, &cred) == HUSH_OK, "curl read");
    expect(wipe_log_saw((size_t)(HUSH_VAULT_TOKEN_MAX + 16), "X-Vault-Token:"),
           "W19 token_header wiped");
    /* storage wipe + http wipe both BODY_MAX with the secret JSON. */
    expect(wipe_log_count((size_t)HUSH_VAULT_BODY_MAX,
                          "private-test-password") >= 2,
           "V20h storage+http wiped");
    /* N2: slist free must not see a live token (wiped before free). */
    expect(fake_curl_slist_freed_token_header(), "slist had token header");
    expect(fake_curl_slist_token_hdr_wiped(), "N2 slist data wiped before free");
    expect(!fake_curl_slist_saw_live_token(), "N2 no live token at free");
    wipe_log_clear();
}

int main(void)
{
    test_contract_literals();
    test_health_and_login_contract();
    test_refuses_broad_or_long_token();
    test_renew_revoke_and_lookup();
    test_lookup_self_contract();
    test_secret_shape_and_revoked_retry();
    test_open_from_pass_keeps_store();
    test_missing_opener();
    test_close_wipes_secrets();
    test_adopted_close_wipes_token();
    test_revoke_full_zeros_token();
    test_login_path_wipes();
    test_health_wipes_http();
    test_lookup_wipes_http();
    test_read_ntfy_wipes_http();
    test_open_wipes_opener();
    test_curl_path_wipes();
    if (g_fail) {
        fprintf(stderr, "test_vault failed\n");
        return 1;
    }
    printf("test_vault ok\n");
    return 0;
}
