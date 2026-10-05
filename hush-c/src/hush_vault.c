/* hush_vault.c: sends the vault.onerelay.app calls ntfy_client.py already sends. */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <curl/curl.h>

#include "hush_json.h"
#include "hush_mem.h"
#include "hush_pass.h"
#include "hush_vault.h"

enum {
    HUSH_VAULT_METHOD_MAX = 8,
    HUSH_VAULT_HTTP_MIN = 200,
    HUSH_VAULT_HTTP_MAX = 300,
    HUSH_VAULT_HTTP_BAD = 400,
    HUSH_VAULT_HTTP_DENY = 403,
    HUSH_VAULT_HTTP_MISS = 404,
    HUSH_VAULT_TIMEOUT_SEC = 15,
    HUSH_VAULT_PATH_COUNT = 6
};

/* libcurl fills this during one exchange. cap includes the NUL. */
typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} hush_vault_bytes_t;

typedef struct {
    int32_t http_status;
    char body[HUSH_VAULT_BODY_MAX];
} hush_vault_http_t;

/* method/path select a contract call. body is NULL when none is sent.
 * send_token adds X-Vault-Token from the session. */
typedef struct {
    const char *method;
    const char *path;
    const char *body;
    int32_t send_token;
} hush_vault_query_t;

static const char *const HUSH_VAULT_PATHS[HUSH_VAULT_PATH_COUNT] = {
    HUSH_VAULT_HEALTH_PATH,
    HUSH_VAULT_LOGIN_PATH,
    HUSH_VAULT_SECRET_PATH,
    HUSH_VAULT_RENEW_PATH,
    HUSH_VAULT_REVOKE_PATH,
    HUSH_VAULT_LOOKUP_PATH
};

static int hush_vault_path_ok(const char *path);
static int hush_vault_is_jwt(const char *text);
static int hush_vault_token_ok(const char *text);
static void hush_vault_copy(char *dst, size_t dstsz, const char *src);
static hush_status_t hush_vault_url(char *out, size_t outsz, const char *path);
static hush_status_t hush_vault_record(hush_vault_session_t *session,
                                       const hush_vault_query_t *query,
                                       const char *url);
static hush_status_t hush_vault_take_fixture(hush_vault_http_t *out,
                                             hush_vault_session_t *session);
static size_t hush_vault_write_body(char *ptr, size_t size, size_t nmemb,
                                    void *userdata);
static hush_status_t hush_vault_apply_tls(CURL *easy);
static hush_status_t hush_vault_header_list(struct curl_slist **out,
                                            const hush_vault_call_t *call);
static hush_status_t hush_vault_bind_curl(CURL *easy,
                                          const hush_vault_call_t *call,
                                          struct curl_slist *headers,
                                          hush_vault_bytes_t *bytes);
static hush_status_t hush_vault_curl(hush_vault_http_t *out,
                                     const hush_vault_call_t *call);
static hush_status_t hush_vault_exchange(hush_vault_session_t *session,
                                         hush_vault_http_t *out,
                                         const hush_vault_query_t *query);
static hush_status_t hush_vault_map_http(int32_t code);
static hush_status_t hush_vault_take_ttl(int32_t *out_ttl,
                                         const hush_json_value_t *value);
static hush_status_t hush_vault_one_policy(const char *json);
static hush_status_t hush_vault_accept_auth(hush_vault_session_t *session,
                                            const char *json);
static hush_status_t hush_vault_post_auth(hush_vault_session_t *session,
                                          const char *path,
                                          const char *body);
static hush_status_t hush_vault_store_jwt(hush_vault_session_t *session,
                                          const char *jwt);
static hush_status_t hush_vault_adopt_token(hush_vault_session_t *session,
                                            const char *token);
static hush_status_t hush_vault_login_body(char *out, size_t outsz,
                                           const char *jwt);
static hush_status_t hush_vault_strip_slash(char *text);
static hush_status_t hush_vault_take_field(char *out, size_t outsz,
                                           const char *json,
                                           const char *path);
static hush_status_t hush_vault_take_ntfy(hush_vault_ntfy_t *out,
                                          const char *json);
static hush_status_t hush_vault_fetch_secret(hush_vault_session_t *session,
                                              hush_vault_http_t *out);
static hush_status_t hush_vault_use_opener(hush_vault_session_t *session,
                                           const char *opener);
static void hush_vault_wipe_http(hush_vault_http_t *http);
static void hush_vault_wipe_call_secrets(hush_vault_call_t *call);
static void hush_vault_wipe_session_secrets(hush_vault_session_t *session);

/* libcurl requires this pointer. It is not a Hush dispatch table. */
static size_t hush_vault_write_body(char *ptr, size_t size, size_t nmemb,
                                    void *userdata)
{
    hush_vault_bytes_t *bytes = userdata;
    size_t chunk = 0;

    assert(bytes != NULL);
    assert(bytes->buf != NULL);
    assert(bytes->cap > 0);
    if (ptr == NULL)
        return 0;
    if (size != 0 && nmemb > (bytes->cap - 1) / size)
        return 0;
    chunk = size * nmemb;
    if (bytes->len + chunk >= bytes->cap)
        return 0;
    memcpy(bytes->buf + bytes->len, ptr, chunk);
    bytes->len += chunk;
    bytes->buf[bytes->len] = '\0';
    assert(bytes->len < bytes->cap);
    return chunk;
}

void hush_vault_session_init(hush_vault_session_t *session)
{
    if (session == NULL)
        return;
    memset(session, 0, sizeof(*session));
}

void hush_vault_session_use_fixture(hush_vault_session_t *session,
                                    const hush_vault_reply_t *replies,
                                    size_t reply_count)
{
    if (session == NULL)
        return;
    session->replies = replies;
    session->reply_count = reply_count;
    session->reply_index = 0;
    session->call_count = 0;
}

hush_status_t hush_vault_session_call(hush_vault_call_t *out,
                                      const hush_vault_session_t *session,
                                      size_t idx)
{
    if (out == NULL || session == NULL)
        return HUSH_ERR_ARG;
    if (idx >= session->call_count)
        return HUSH_ERR_NOT_FOUND;
    *out = session->calls[idx];
    return HUSH_OK;
}


static void hush_vault_wipe_http(hush_vault_http_t *http)
{
    assert(http != NULL);
    hush_secure_zero(http->body, sizeof(http->body));
    http->http_status = 0;
}

static void hush_vault_wipe_call_secrets(hush_vault_call_t *call)
{
    assert(call != NULL);
    hush_secure_zero(call->body, sizeof(call->body));
    hush_secure_zero(call->token, sizeof(call->token));
    call->has_body = 0;
    call->has_token = 0;
}

/* Full-size wipe of token, JWT, and every call-log secret copy. Keeps
 * recorded method/url so contract tests can still name the path. */
static void hush_vault_wipe_session_secrets(hush_vault_session_t *session)
{
    size_t idx = 0;

    assert(session != NULL);
    hush_secure_zero(session->token, sizeof(session->token));
    hush_secure_zero(session->jwt, sizeof(session->jwt));
    session->ttl_seconds = 0;
    session->minted = 0;
    for (idx = 0; idx < (size_t)HUSH_VAULT_CALL_MAX; ++idx)
        hush_vault_wipe_call_secrets(&session->calls[idx]);
}

static int hush_vault_path_ok(const char *path)
{
    size_t idx = 0;

    assert(path != NULL);
    for (idx = 0; idx < (size_t)HUSH_VAULT_PATH_COUNT; ++idx) {
        if (strcmp(path, HUSH_VAULT_PATHS[idx]) == 0)
            return 1;
    }
    return 0;
}

static int hush_vault_is_jwt(const char *text)
{
    size_t idx = 0;
    int32_t dots = 0;

    assert(text != NULL);
    for (idx = 0; idx < (size_t)HUSH_VAULT_JWT_MAX && text[idx] != '\0'; ++idx) {
        char ch = text[idx];
        int base64 = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')
            || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
        if (ch == '.')
            dots++;
        else if (!base64)
            return 0;
    }
    if (idx == 0 || text[idx] != '\0')
        return 0;
    return dots == 2;
}

static int hush_vault_token_ok(const char *text)
{
    size_t idx = 0;

    assert(text != NULL);
    for (idx = 0; idx < (size_t)HUSH_VAULT_TOKEN_MAX && text[idx] != '\0'; ++idx) {
        unsigned char ch = (unsigned char)text[idx];
        if (ch <= 0x20 || ch >= 0x7f)
            return 0;
    }
    return idx > 0 && text[idx] == '\0';
}

static void hush_vault_copy(char *dst, size_t dstsz, const char *src)
{
    size_t len = 0;

    assert(dst != NULL);
    assert(dstsz > 0);
    if (src == NULL)
        src = "";
    len = strlen(src);
    if (len + 1 > dstsz)
        len = dstsz - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
    assert(dst[len] == '\0');
}

static hush_status_t hush_vault_url(char *out, size_t outsz, const char *path)
{
    int wrote = 0;

    assert(out != NULL);
    assert(outsz > 0);
    assert(hush_vault_path_ok(path));
    wrote = snprintf(out, outsz, "%s%s", HUSH_VAULT_ORIGIN, path);
    if (wrote < 0 || (size_t)wrote >= outsz)
        return HUSH_ERR_FULL;
    assert(strncmp(out, HUSH_VAULT_ORIGIN, strlen(HUSH_VAULT_ORIGIN)) == 0);
    return HUSH_OK;
}

static hush_status_t hush_vault_record(hush_vault_session_t *session,
                                       const hush_vault_query_t *query,
                                       const char *url)
{
    hush_vault_call_t *call = NULL;

    assert(session != NULL);
    assert(query != NULL);
    assert(url != NULL);
    if (session->call_count >= (size_t)HUSH_VAULT_CALL_MAX)
        return HUSH_ERR_FULL;
    call = &session->calls[session->call_count];
    memset(call, 0, sizeof(*call));
    hush_vault_copy(call->method, sizeof(call->method), query->method);
    hush_vault_copy(call->url, sizeof(call->url), url);
    if (query->body != NULL) {
        hush_vault_copy(call->body, sizeof(call->body), query->body);
        call->has_body = 1;
    }
    if (query->send_token) {
        hush_vault_copy(call->token, sizeof(call->token), session->token);
        call->has_token = 1;
    }
    session->call_count++;
    assert(session->call_count <= (size_t)HUSH_VAULT_CALL_MAX);
    return HUSH_OK;
}

static hush_status_t hush_vault_take_fixture(hush_vault_http_t *out,
                                             hush_vault_session_t *session)
{
    const hush_vault_reply_t *reply = NULL;

    assert(out != NULL);
    assert(session != NULL);
    assert(session->replies != NULL);
    if (session->reply_index >= session->reply_count)
        return HUSH_ERR_IO;
    reply = &session->replies[session->reply_index];
    session->reply_index++;
    out->http_status = reply->http_status;
    hush_vault_copy(out->body, sizeof(out->body), reply->body);
    assert(session->reply_index <= session->reply_count);
    return HUSH_OK;
}

static hush_status_t hush_vault_apply_tls(CURL *easy)
{
    assert(easy != NULL);
    if (curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 1L) != CURLE_OK)
        return HUSH_ERR_IO;
    if (curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 2L) != CURLE_OK)
        return HUSH_ERR_IO;
    if (curl_easy_setopt(easy, CURLOPT_SSLVERSION,
                         (long)CURL_SSLVERSION_TLSv1_2) != CURLE_OK)
        return HUSH_ERR_IO;
    if (curl_easy_setopt(easy, CURLOPT_SSL_EC_CURVES, "X25519") != CURLE_OK)
        return HUSH_ERR_IO;
    if (curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 0L) != CURLE_OK)
        return HUSH_ERR_IO;
    if (curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "https") != CURLE_OK)
        return HUSH_ERR_IO;
    if (curl_easy_setopt(easy, CURLOPT_REDIR_PROTOCOLS_STR, "https") != CURLE_OK)
        return HUSH_ERR_IO;
    if (curl_easy_setopt(easy, CURLOPT_TIMEOUT,
                         (long)HUSH_VAULT_TIMEOUT_SEC) != CURLE_OK)
        return HUSH_ERR_IO;
    assert(easy != NULL);
    return HUSH_OK;
}

static hush_status_t hush_vault_header_list(struct curl_slist **out,
                                            const hush_vault_call_t *call)
{
    struct curl_slist *list = NULL;
    char token_header[HUSH_VAULT_TOKEN_MAX + 16];
    int wrote = 0;
    hush_status_t st = HUSH_OK;

    assert(out != NULL);
    assert(call != NULL);
    token_header[0] = '\0';
    list = curl_slist_append(NULL, "Content-Type: application/json");
    if (list == NULL)
        return HUSH_ERR_IO;
    if (!call->has_token) {
        *out = list;
        return HUSH_OK;
    }
    wrote = snprintf(token_header, sizeof(token_header),
                     "X-Vault-Token: %s", call->token);
    if (wrote < 0 || (size_t)wrote >= sizeof(token_header)) {
        curl_slist_free_all(list);
        st = HUSH_ERR_FULL;
        goto done;
    }
    if (curl_slist_append(list, token_header) == NULL) {
        curl_slist_free_all(list);
        st = HUSH_ERR_IO;
        goto done;
    }
    *out = list;
    assert(*out != NULL);
    st = HUSH_OK;
done:
    hush_secure_zero(token_header, sizeof(token_header));
    return st;
}

static hush_status_t hush_vault_set_body(CURL *easy, const hush_vault_call_t *call)
{
    long length = 0;

    assert(easy != NULL);
    assert(call != NULL);
    if (!call->has_body)
        return HUSH_OK;
    length = (long)strlen(call->body);
    if (curl_easy_setopt(easy, CURLOPT_POSTFIELDS, call->body) != CURLE_OK)
        return HUSH_ERR_IO;
    if (curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, length) != CURLE_OK)
        return HUSH_ERR_IO;
    assert(length >= 0);
    return HUSH_OK;
}

static hush_status_t hush_vault_bind_curl(CURL *easy,
                                          const hush_vault_call_t *call,
                                          struct curl_slist *headers,
                                          hush_vault_bytes_t *bytes)
{
    assert(easy != NULL);
    assert(call != NULL);
    assert(bytes != NULL);
    if (curl_easy_setopt(easy, CURLOPT_URL, call->url) != CURLE_OK)
        return HUSH_ERR_IO;
    if (curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, call->method) != CURLE_OK)
        return HUSH_ERR_IO;
    if (curl_easy_setopt(easy, CURLOPT_USERAGENT, HUSH_VAULT_USER_AGENT) != CURLE_OK)
        return HUSH_ERR_IO;
    if (curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers) != CURLE_OK)
        return HUSH_ERR_IO;
    if (curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, hush_vault_write_body) != CURLE_OK)
        return HUSH_ERR_IO;
    if (curl_easy_setopt(easy, CURLOPT_WRITEDATA, bytes) != CURLE_OK)
        return HUSH_ERR_IO;
    return hush_vault_set_body(easy, call);
}

static hush_status_t hush_vault_curl(hush_vault_http_t *out,
                                     const hush_vault_call_t *call)
{
    CURL *easy = NULL;
    struct curl_slist *headers = NULL;
    char storage[HUSH_VAULT_BODY_MAX];
    hush_vault_bytes_t bytes = {0};
    long code = 0;
    hush_status_t st = HUSH_OK;

    assert(out != NULL);
    assert(call != NULL);
    assert(strncmp(call->url, HUSH_VAULT_ORIGIN "/", strlen(HUSH_VAULT_ORIGIN) + 1) == 0);
    easy = curl_easy_init();
    if (easy == NULL)
        return HUSH_ERR_IO;
    storage[0] = '\0';
    bytes.buf = storage;
    bytes.cap = sizeof(storage);
    st = hush_vault_apply_tls(easy);
    if (st == HUSH_OK)
        st = hush_vault_header_list(&headers, call);
    if (st == HUSH_OK)
        st = hush_vault_bind_curl(easy, call, headers, &bytes);
    if (st == HUSH_OK && curl_easy_perform(easy) != CURLE_OK)
        st = HUSH_ERR_IO;
    if (st == HUSH_OK && curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &code) != CURLE_OK)
        st = HUSH_ERR_IO;
    curl_slist_free_all(headers);
    curl_easy_cleanup(easy);
    if (st == HUSH_OK) {
        out->http_status = (int32_t)code;
        hush_vault_copy(out->body, sizeof(out->body), storage);
        assert(out->http_status >= 0);
    }
    hush_secure_zero(storage, sizeof(storage));
    return st;
}

static hush_status_t hush_vault_exchange(hush_vault_session_t *session,
                                         hush_vault_http_t *out,
                                         const hush_vault_query_t *query)
{
    char url[HUSH_VAULT_URL_MAX];
    hush_status_t st = HUSH_OK;

    assert(session != NULL);
    assert(out != NULL);
    assert(query != NULL);
    assert(query->method != NULL);
    memset(out, 0, sizeof(*out));
    if (!hush_vault_path_ok(query->path))
        return HUSH_ERR_ARG;
    if (query->send_token && session->token[0] == '\0')
        return HUSH_ERR_ARG;
    st = hush_vault_url(url, sizeof(url), query->path);
    if (st != HUSH_OK)
        return st;
    st = hush_vault_record(session, query, url);
    if (st != HUSH_OK)
        return st;
    if (session->replies != NULL)
        return hush_vault_take_fixture(out, session);
    return hush_vault_curl(out, &session->calls[session->call_count - 1]);
}

static hush_status_t hush_vault_map_http(int32_t code)
{
    if (code >= HUSH_VAULT_HTTP_MIN && code < HUSH_VAULT_HTTP_MAX)
        return HUSH_OK;
    if (code == HUSH_VAULT_HTTP_BAD || code == HUSH_VAULT_HTTP_DENY
        || code == HUSH_VAULT_HTTP_MISS)
        return HUSH_ERR_DENIED;
    return HUSH_ERR_IO;
}

static hush_status_t hush_vault_take_ttl(int32_t *out_ttl,
                                         const hush_json_value_t *value)
{
    size_t idx = 0;
    int32_t ttl = 0;

    assert(out_ttl != NULL);
    assert(value != NULL);
    assert(value->start != NULL);
    if (value->len == 0 || value->len > 3)
        return HUSH_ERR_DENIED;
    for (idx = 0; idx < value->len; ++idx) {
        char ch = value->start[idx];
        if (ch < '0' || ch > '9')
            return HUSH_ERR_DENIED;
        ttl = ttl * 10 + (int32_t)(ch - '0');
    }
    if (ttl <= 0 || ttl > HUSH_VAULT_TTL_MAX)
        return HUSH_ERR_DENIED;
    *out_ttl = ttl;
    assert(*out_ttl > 0);
    return HUSH_OK;
}

static hush_status_t hush_vault_one_policy(const char *json)
{
    hush_json_value_t first = {0};
    hush_json_value_t extra = {0};
    char name[HUSH_VAULT_NAME_MAX];
    hush_status_t st = HUSH_OK;

    assert(json != NULL);
    st = hush_json_lookup(&first, json, "/auth/policies/0");
    if (st != HUSH_OK)
        return HUSH_ERR_DENIED;
    if (hush_json_decode(name, sizeof(name), &first) != HUSH_OK)
        return HUSH_ERR_DENIED;
    if (strcmp(name, HUSH_VAULT_POLICY) != 0)
        return HUSH_ERR_DENIED;
    st = hush_json_lookup(&extra, json, "/auth/policies/1");
    if (st != HUSH_ERR_NOT_FOUND)
        return HUSH_ERR_DENIED;
    assert(strcmp(name, HUSH_VAULT_POLICY) == 0);
    return HUSH_OK;
}

static hush_status_t hush_vault_accept_auth(hush_vault_session_t *session,
                                            const char *json)
{
    hush_json_value_t token = {0};
    hush_json_value_t ttl = {0};
    char next[HUSH_VAULT_TOKEN_MAX];
    int32_t seconds = 0;
    hush_status_t st = HUSH_OK;

    assert(session != NULL);
    assert(json != NULL);
    next[0] = '\0';
    hush_secure_zero(session->token, sizeof(session->token));
    session->ttl_seconds = 0;
    if (hush_json_lookup(&token, json, "/auth/client_token") != HUSH_OK) {
        st = HUSH_ERR_DENIED;
        goto done;
    }
    if (hush_json_decode(next, sizeof(next), &token) != HUSH_OK || next[0] == '\0') {
        st = HUSH_ERR_DENIED;
        goto done;
    }
    if (hush_json_lookup(&ttl, json, "/auth/lease_duration") != HUSH_OK) {
        st = HUSH_ERR_DENIED;
        goto done;
    }
    if (hush_vault_take_ttl(&seconds, &ttl) != HUSH_OK) {
        st = HUSH_ERR_DENIED;
        goto done;
    }
    if (hush_vault_one_policy(json) != HUSH_OK) {
        st = HUSH_ERR_DENIED;
        goto done;
    }
    hush_vault_copy(session->token, sizeof(session->token), next);
    session->ttl_seconds = seconds;
    assert(session->token[0] != '\0');
    st = HUSH_OK;
done:
    hush_secure_zero(next, sizeof(next));
    return st;
}

static hush_status_t hush_vault_post_auth(hush_vault_session_t *session,
                                          const char *path,
                                          const char *body)
{
    hush_vault_http_t http = {0};
    hush_vault_query_t query = {0};
    hush_status_t st = HUSH_OK;

    assert(session != NULL);
    assert(path != NULL);
    assert(body != NULL);
    query.method = "POST";
    query.path = path;
    query.body = body;
    query.send_token = strcmp(path, HUSH_VAULT_LOGIN_PATH) != 0;
    st = hush_vault_exchange(session, &http, &query);
    if (st == HUSH_OK)
        st = hush_vault_map_http(http.http_status);
    if (st == HUSH_OK)
        st = hush_vault_accept_auth(session, http.body);
    hush_vault_wipe_http(&http);
    return st;
}

static hush_status_t hush_vault_store_jwt(hush_vault_session_t *session,
                                          const char *jwt)
{
    size_t len = 0;

    assert(session != NULL);
    assert(hush_vault_is_jwt(jwt));
    len = strlen(jwt);
    if (len + 1 > sizeof(session->jwt))
        return HUSH_ERR_FULL;
    memcpy(session->jwt, jwt, len + 1);
    assert(session->jwt[len] == '\0');
    return HUSH_OK;
}

static hush_status_t hush_vault_adopt_token(hush_vault_session_t *session,
                                            const char *token)
{
    size_t len = 0;

    assert(session != NULL);
    assert(hush_vault_token_ok(token));
    len = strlen(token);
    if (len + 1 > sizeof(session->token))
        return HUSH_ERR_FULL;
    memcpy(session->token, token, len + 1);
    hush_secure_zero(session->jwt, sizeof(session->jwt));
    session->ttl_seconds = 0;
    session->minted = 0;
    assert(session->minted == 0);
    return HUSH_OK;
}

static hush_status_t hush_vault_login_body(char *out, size_t outsz,
                                           const char *jwt)
{
    int wrote = 0;

    assert(out != NULL);
    assert(jwt != NULL);
    wrote = snprintf(out, outsz,
                     "{\"role\":\"%s\",\"jwt\":\"%s\"}",
                     HUSH_VAULT_ROLE, jwt);
    if (wrote < 0 || (size_t)wrote >= outsz)
        return HUSH_ERR_FULL;
    assert(strstr(out, "\"role\":\"eggdrop\"") != NULL);
    return HUSH_OK;
}

hush_status_t hush_vault_health(hush_vault_session_t *session,
                                int32_t *out_status)
{
    hush_vault_http_t http = {0};
    hush_vault_query_t query = {0};
    hush_status_t st = HUSH_OK;

    if (session == NULL || out_status == NULL)
        return HUSH_ERR_ARG;
    *out_status = 0;
    query.method = "GET";
    query.path = HUSH_VAULT_HEALTH_PATH;
    st = hush_vault_exchange(session, &http, &query);
    if (st == HUSH_OK) {
        *out_status = http.http_status;
        st = hush_vault_map_http(http.http_status);
    }
    hush_vault_wipe_http(&http);
    return st;
}

hush_status_t hush_vault_login(hush_vault_session_t *session, const char *jwt)
{
    char body[HUSH_VAULT_BODY_MAX];
    hush_status_t st = HUSH_OK;

    if (session == NULL || jwt == NULL || !hush_vault_is_jwt(jwt))
        return HUSH_ERR_ARG;
    body[0] = '\0';
    st = hush_vault_store_jwt(session, jwt);
    if (st != HUSH_OK)
        return st;
    hush_secure_zero(session->token, sizeof(session->token));
    session->minted = 0;
    st = hush_vault_login_body(body, sizeof(body), session->jwt);
    if (st == HUSH_OK)
        st = hush_vault_post_auth(session, HUSH_VAULT_LOGIN_PATH, body);
    hush_secure_zero(body, sizeof(body));
    if (st != HUSH_OK)
        return st;
    session->minted = 1;
    assert(session->minted == 1);
    return HUSH_OK;
}

hush_status_t hush_vault_renew(hush_vault_session_t *session)
{
    hush_status_t st = HUSH_OK;

    if (session == NULL || session->token[0] == '\0')
        return HUSH_ERR_ARG;
    st = hush_vault_post_auth(session, HUSH_VAULT_RENEW_PATH, HUSH_VAULT_RENEW_BODY);
    if (st == HUSH_OK)
        return HUSH_OK;
    if (st != HUSH_ERR_DENIED || session->jwt[0] == '\0')
        return st;
    return hush_vault_login(session, session->jwt);
}

hush_status_t hush_vault_revoke(hush_vault_session_t *session)
{
    hush_vault_http_t http = {0};
    hush_vault_query_t query = {0};
    hush_status_t st = HUSH_OK;

    if (session == NULL || session->token[0] == '\0')
        return HUSH_ERR_ARG;
    query.method = "POST";
    query.path = HUSH_VAULT_REVOKE_PATH;
    query.body = HUSH_VAULT_REVOKE_BODY;
    query.send_token = 1;
    st = hush_vault_exchange(session, &http, &query);
    hush_secure_zero(session->token, sizeof(session->token));
    session->ttl_seconds = 0;
    session->minted = 0;
    if (st == HUSH_OK)
        st = hush_vault_map_http(http.http_status);
    hush_vault_wipe_http(&http);
    return st;
}

hush_status_t hush_vault_lookup_self(hush_vault_session_t *session,
                                     int32_t *out_status)
{
    hush_vault_http_t http = {0};
    hush_vault_query_t query = {0};
    hush_status_t st = HUSH_OK;

    if (session == NULL || out_status == NULL || session->token[0] == '\0')
        return HUSH_ERR_ARG;
    *out_status = 0;
    query.method = "GET";
    query.path = HUSH_VAULT_LOOKUP_PATH;
    query.send_token = 1;
    st = hush_vault_exchange(session, &http, &query);
    if (st == HUSH_OK) {
        *out_status = http.http_status;
        st = hush_vault_map_http(http.http_status);
    }
    hush_vault_wipe_http(&http);
    return st;
}

static hush_status_t hush_vault_strip_slash(char *text)
{
    size_t len = 0;

    assert(text != NULL);
    len = strlen(text);
    while (len > 0 && text[len - 1] == '/') {
        text[len - 1] = '\0';
        len--;
    }
    assert(len == 0 || text[len - 1] != '/');
    return HUSH_OK;
}

static hush_status_t hush_vault_take_field(char *out, size_t outsz,
                                           const char *json,
                                           const char *path)
{
    hush_json_value_t value = {0};

    assert(out != NULL);
    assert(json != NULL);
    assert(path != NULL);
    if (hush_json_lookup(&value, json, path) != HUSH_OK)
        return HUSH_ERR_DENIED;
    if (hush_json_decode(out, outsz, &value) != HUSH_OK)
        return HUSH_ERR_DENIED;
    if (out[0] == '\0')
        return HUSH_ERR_DENIED;
    return HUSH_OK;
}

static hush_status_t hush_vault_take_ntfy(hush_vault_ntfy_t *out,
                                          const char *json)
{
    hush_status_t st = HUSH_OK;

    assert(out != NULL);
    assert(json != NULL);
    memset(out, 0, sizeof(*out));
    st = hush_vault_take_field(out->server, sizeof(out->server), json,
                               "/data/data/server");
    if (st != HUSH_OK)
        return st;
    st = hush_vault_strip_slash(out->server);
    if (st != HUSH_OK)
        return st;
    if (strcmp(out->server, HUSH_VAULT_NTFY_ORIGIN) != 0)
        return HUSH_ERR_DENIED;
    st = hush_vault_take_field(out->username, sizeof(out->username), json,
                               "/data/data/username");
    if (st != HUSH_OK)
        return st;
    if (strcmp(out->username, HUSH_VAULT_NTFY_USER) != 0)
        return HUSH_ERR_DENIED;
    return hush_vault_take_field(out->password, sizeof(out->password), json,
                                 "/data/data/password");
}

static hush_status_t hush_vault_fetch_secret(hush_vault_session_t *session,
                                              hush_vault_http_t *out)
{
    hush_vault_query_t query = {0};

    assert(session != NULL);
    assert(out != NULL);
    assert(session->token[0] != '\0');
    query.method = "GET";
    query.path = HUSH_VAULT_SECRET_PATH;
    query.send_token = 1;
    return hush_vault_exchange(session, out, &query);
}

static hush_status_t hush_vault_read_body(hush_vault_ntfy_t *out,
                                          const hush_vault_http_t *http)
{
    hush_status_t st = HUSH_OK;

    assert(out != NULL);
    assert(http != NULL);
    st = hush_vault_map_http(http->http_status);
    if (st != HUSH_OK)
        return st;
    return hush_vault_take_ntfy(out, http->body);
}

hush_status_t hush_vault_read_ntfy(hush_vault_session_t *session,
                                   hush_vault_ntfy_t *out)
{
    hush_vault_http_t http = {0};
    hush_status_t st = HUSH_OK;

    if (session == NULL || out == NULL || session->token[0] == '\0')
        return HUSH_ERR_ARG;
    memset(out, 0, sizeof(*out));
    st = hush_vault_fetch_secret(session, &http);
    if (st == HUSH_OK && http.http_status == HUSH_VAULT_HTTP_DENY
        && session->jwt[0] != '\0') {
        hush_vault_wipe_http(&http);
        st = hush_vault_login(session, session->jwt);
        if (st == HUSH_OK)
            st = hush_vault_fetch_secret(session, &http);
    }
    if (st == HUSH_OK)
        st = hush_vault_read_body(out, &http);
    hush_vault_wipe_http(&http);
    return st;
}

static hush_status_t hush_vault_use_opener(hush_vault_session_t *session,
                                           const char *opener)
{
    assert(session != NULL);
    assert(opener != NULL);
    if (hush_vault_is_jwt(opener))
        return hush_vault_login(session, opener);
    if (!hush_vault_token_ok(opener))
        return HUSH_ERR_ARG;
    return hush_vault_adopt_token(session, opener);
}

hush_status_t hush_vault_open(hush_vault_session_t *session,
                              hush_vault_ntfy_t *out)
{
    char opener[HUSH_VAULT_JWT_MAX];
    hush_status_t st = HUSH_OK;

    if (session == NULL || out == NULL)
        return HUSH_ERR_ARG;
    memset(out, 0, sizeof(*out));
    opener[0] = '\0';
    if (!hush_pass_has(HUSH_VAULT_PASS_OPENER))
        return HUSH_ERR_NOT_FOUND;
    st = hush_pass_get(opener, sizeof(opener), HUSH_VAULT_PASS_OPENER);
    if (st != HUSH_OK)
        return st;
    st = hush_vault_use_opener(session, opener);
    hush_secure_zero(opener, sizeof(opener));
    if (st != HUSH_OK)
        return st;
    return hush_vault_read_ntfy(session, out);
}

hush_status_t hush_vault_close(hush_vault_session_t *session)
{
    hush_status_t st = HUSH_OK;

    if (session == NULL)
        return HUSH_ERR_ARG;
    if (session->minted)
        st = hush_vault_revoke(session);
    hush_vault_wipe_session_secrets(session);
    return st;
}
