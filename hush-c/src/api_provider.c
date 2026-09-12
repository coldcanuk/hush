/* api_provider.c: owns provider CRUD, scan, and login routes. */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hush_http_internal.h"
#include "hush_provider.h"

typedef struct {
    char api_key[HUSH_PROVIDER_KEY_MAX];
    char username[HUSH_PROVIDER_KEY_MAX];
    char password[HUSH_PROVIDER_KEY_MAX];
    char token[HUSH_PROVIDER_KEY_MAX];
    char passkey[HUSH_PROVIDER_KEY_MAX];
} hush_http_provider_buf_t;

enum {
    HUSH_HTTP_JSON_MAX = 65536,
    HUSH_HTTP_LOGIN_REPLY_MAX = 384
};

/* Copies host, model, use_home, and optional secrets from body into in. */
static void hush_http_fill_provider_in(hush_provider_in_t *in,
                                       hush_http_provider_buf_t *buf,
                                       const char *body);

static void hush_http_append_provider(char *body, size_t bodysz, size_t *n,
                                      const hush_provider_status_t *st,
                                      int first);
static void hush_http_fill_provider_in(hush_provider_in_t *in,
                                       hush_http_provider_buf_t *buf,
                                       const char *body);
static void hush_http_take_secret(const char **dst, char *buf,
                                  const char *body, const char *kind);
static void hush_http_reply_scan(int fd, const hush_provider_scan_t *scan,
                                 hush_status_t st);

static void hush_http_append_provider(char *body, size_t bodysz, size_t *n,
                                      const hush_provider_status_t *st,
                                      int first)
{
    char host[HUSH_PROVIDER_HOST_MAX * 2];
    char model[HUSH_PROVIDER_MODEL_MAX * 2];
    char home_model[HUSH_PROVIDER_MODEL_MAX * 2];
    int wr;
    assert(body != NULL && n != NULL && st != NULL);
    hush_json_escape(st->host, host, sizeof(host));
    hush_json_escape(st->model, model, sizeof(model));
    hush_json_escape(st->home_model, home_model, sizeof(home_model));
    wr = snprintf(body + *n, bodysz - *n,
                  "%s\"%s\":{\"label\":\"%s\",\"family\":\"%s\","
                  "\"has_binary\":%s,\"has_home\":%s,\"has_key\":%s,"
                  "\"has_username\":%s,\"has_password\":%s,"
                  "\"has_token\":%s,\"has_passkey\":%s,"
                  "\"use_home\":%s,\"host\":\"%s\",\"model\":\"%s\","
                  "\"home_model\":\"%s\",\"caps\":%u,\"flags\":%u,"
                  "\"configured\":%s,\"ready\":%s}",
                  first ? "" : ",",
                  st->id, st->label, st->family,
                  st->has_binary ? "true" : "false",
                  st->has_home ? "true" : "false",
                  st->has_key ? "true" : "false",
                  st->has_username ? "true" : "false",
                  st->has_password ? "true" : "false",
                  st->has_token ? "true" : "false",
                  st->has_passkey ? "true" : "false",
                  st->use_home ? "true" : "false",
                  host, model, home_model,
                  st->caps,
                  st->flags,
                  st->configured ? "true" : "false",
                  hush_provider_ready(st) ? "true" : "false");
    if (wr > 0 && (size_t)wr < bodysz - *n)
        *n += (size_t)wr;
}

hush_status_t hush_http_serve_provider_get(int fd)
{
    hush_provider_status_t all[HUSH_PROVIDER_COUNT];
    char body[HUSH_HTTP_JSON_MAX];
    size_t n = 0;
    size_t count = 0;
    size_t i;
    int wr;

    if (hush_provider_status_all(all, &count) != HUSH_OK) {
        hush_http_reply(fd, "500 Internal Server Error", "text/plain",
                        "io error\n", 9);
        return HUSH_ERR_IO;
    }
    wr = snprintf(body, sizeof(body), "{\"ok\":true,\"providers\":{");
    if (wr > 0)
        n = (size_t)wr;
    for (i = 0; i < count; i++)
        hush_http_append_provider(body, sizeof(body), &n, &all[i], i == 0);
    if (n + 3 < sizeof(body)) {
        memcpy(body + n, "}}\n", 3);
        n += 3;
    }
    hush_http_reply(fd, "200 OK", "application/json", body, n);
    return HUSH_OK;
}

static void hush_http_take_secret(const char **dst, char *buf,
                                  const char *body, const char *kind)
{
    assert(dst != NULL);
    assert(buf != NULL);
    if (hush_http_json_field(body, kind, buf, HUSH_PROVIDER_KEY_MAX))
        *dst = buf;
}

static void hush_http_fill_provider_in(hush_provider_in_t *in,
                                       hush_http_provider_buf_t *buf,
                                       const char *body)
{
    char flag[8];

    assert(in != NULL);
    assert(buf != NULL);
    memset(buf, 0, sizeof(*buf));
    (void)hush_http_json_field(body, "host", in->host, sizeof(in->host));
    (void)hush_http_json_field(body, "model", in->model, sizeof(in->model));
    hush_http_take_secret(&in->api_key, buf->api_key, body,
                          HUSH_PROVIDER_SECRET_API_KEY);
    hush_http_take_secret(&in->username, buf->username, body,
                          HUSH_PROVIDER_SECRET_USERNAME);
    hush_http_take_secret(&in->password, buf->password, body,
                          HUSH_PROVIDER_SECRET_PASSWORD);
    hush_http_take_secret(&in->token, buf->token, body,
                          HUSH_PROVIDER_SECRET_TOKEN);
    hush_http_take_secret(&in->passkey, buf->passkey, body,
                          HUSH_PROVIDER_SECRET_PASSKEY);
    if (hush_http_json_field(body, "use_home", flag, sizeof(flag)))
        in->use_home = strcmp(flag, "true") == 0 || strcmp(flag, "1") == 0;
}

hush_status_t hush_http_serve_provider_post(int fd, const char *body)
{
    hush_provider_in_t in;
    hush_provider_status_t st;
    hush_http_provider_buf_t buf;
    char reply[2048];
    size_t n = 0;
    int wr;

    memset(&in, 0, sizeof(in));
    if (!hush_http_json_field(body, "provider", in.id, sizeof(in.id))) {
        hush_http_reply(fd, "400 Bad Request", "text/plain", "bad request\n", 12);
        return HUSH_ERR_PARSE;
    }
    hush_http_fill_provider_in(&in, &buf, body);
    if (hush_provider_save(&in) != HUSH_OK) {
        hush_http_reply(fd, "400 Bad Request", "text/plain", "bad request\n", 12);
        return HUSH_ERR_PARSE;
    }
    if (hush_provider_status(&st, in.id) != HUSH_OK)
        return hush_http_serve_provider_get(fd);
    wr = snprintf(reply, sizeof(reply), "{\"ok\":true,\"providers\":{");
    if (wr > 0)
        n = (size_t)wr;
    hush_http_append_provider(reply, sizeof(reply), &n, &st, 1);
    if (n + 3 < sizeof(reply)) {
        memcpy(reply + n, "}}\n", 3);
        n += 3;
    }
    hush_http_reply(fd, "200 OK", "application/json", reply, n);
    return HUSH_OK;
}

static void hush_http_reply_scan(int fd, const hush_provider_scan_t *scan,
                                 hush_status_t st)
{
    char body[HUSH_HTTP_JSON_MAX];
    char err[HUSH_PROVIDER_ERR_MAX * 2];
    size_t n = 0;
    size_t i;
    int wr;

    assert(scan != NULL);
    hush_json_escape(scan->error, err, sizeof(err));
    wr = snprintf(body, sizeof(body),
                  "{\"ok\":%s,\"error\":\"%s\"",
                  st == HUSH_OK ? "true" : "false", err);
    if (wr > 0)
        n = (size_t)wr;
    for (i = 0; i < scan->nmodels; i++) {
        char name[HUSH_PROVIDER_MODEL_MAX * 2];

        hush_json_escape(scan->models[i], name, sizeof(name));
        wr = snprintf(body + n, sizeof(body) - n, ",\"model_%zu\":\"%s\"",
                      i, name);
        if (wr > 0 && (size_t)wr < sizeof(body) - n)
            n += (size_t)wr;
    }
    if (n + 2 < sizeof(body)) {
        memcpy(body + n, "}\n", 2);
        n += 2;
    }
    hush_http_reply(fd, "200 OK", "application/json", body, n);
}

hush_status_t hush_http_serve_provider_scan(int fd, const char *body)
{
    char id[HUSH_PROVIDER_ID_MAX];
    char host[HUSH_PROVIDER_HOST_MAX];
    char key[HUSH_PROVIDER_KEY_MAX];
    hush_provider_scan_t scan;
    hush_status_t st;

    if (!hush_http_json_field(body, "provider", id, sizeof(id))) {
        hush_http_reply(fd, "400 Bad Request", "text/plain", "bad request\n", 12);
        return HUSH_ERR_PARSE;
    }
    host[0] = '\0';
    key[0] = '\0';
    (void)hush_http_json_field(body, "host", host, sizeof(host));
    (void)hush_http_json_field(body, "api_key", key, sizeof(key));
    st = hush_provider_scan(&scan, id, host, key);
    hush_http_reply_scan(fd, &scan, st);
    return HUSH_OK;
}

hush_status_t hush_http_serve_provider_login(int fd, const char *body)
{
    char id[HUSH_PROVIDER_ID_MAX];
    char err[HUSH_PROVIDER_ERR_MAX];
    char reply[HUSH_HTTP_LOGIN_REPLY_MAX];
    char esc[HUSH_PROVIDER_ERR_MAX * 2];
    hush_status_t st;
    int wr;

    if (!hush_http_json_field(body, "provider", id, sizeof(id))) {
        hush_http_reply(fd, "400 Bad Request", "text/plain", "bad request\n", 12);
        return HUSH_ERR_PARSE;
    }
    st = hush_provider_start_login(id);
    hush_provider_last_error(err, sizeof(err));
    hush_json_escape(err, esc, sizeof(esc));
    wr = snprintf(reply, sizeof(reply),
                  "{\"ok\":%s,\"error\":\"%s\"}\n",
                  st == HUSH_OK ? "true" : "false", esc);
    if (wr <= 0 || (size_t)wr >= sizeof(reply))
        return HUSH_ERR_IO;
    hush_http_reply(fd, "200 OK", "application/json", reply, (size_t)wr);
    return HUSH_OK;
}

