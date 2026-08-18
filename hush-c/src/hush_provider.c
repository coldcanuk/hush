/* hush_provider.c: owns home detect, overlay file, pass secrets, and model scan. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "hush_pass.h"
#include "hush_provider.h"
#include "hush_roster.h"

enum {
    HUSH_PROVIDER_SCAN_BODY_MAX = 65536,
    HUSH_PROVIDER_CURL_TIME_S = 8,
    HUSH_PROVIDER_YAML_LINE_MAX = 256,
    HUSH_PROVIDER_OBJ_MAX = 1024,
    HUSH_PROVIDER_CURL_CFG_MAX = 2048,
    HUSH_PROVIDER_LOGIN_ARGV_MAX = 4,
    HUSH_PROVIDER_LOGIN_CMD_MAX = 96
};

#define HUSH_PROVIDER_SECRET_PATH_FMT "providers/%s/%s"
#define HUSH_PROVIDER_CURL_BIN "curl"
#define HUSH_PROVIDER_ANTHROPIC_VER "2023-06-01"

typedef struct {
    const char *id;
    const char *label;
    const char *family;
    const char *host;
    const char *binary;
} hush_provider_meta_t;

static const hush_provider_meta_t hush_provider_meta[HUSH_PROVIDER_COUNT] = {
    { HUSH_ROSTER_PROVIDER_GOOSE, "Goose", HUSH_PROVIDER_FAMILY_HOME,
      "", "goose" },
    { HUSH_ROSTER_PROVIDER_GROK_BUILD, "Grok Build",
      HUSH_PROVIDER_FAMILY_HOME, "", "grok" },
    { HUSH_ROSTER_PROVIDER_CODEX, "Codex", HUSH_PROVIDER_FAMILY_HOME,
      "", "codex" },
    { HUSH_ROSTER_PROVIDER_CLINE, "Cline", HUSH_PROVIDER_FAMILY_EDITOR,
      "", "cline" },
    { HUSH_ROSTER_PROVIDER_GEMINI, "Gemini API", HUSH_PROVIDER_FAMILY_API,
      HUSH_PROVIDER_HOST_GEMINI, "" },
    { HUSH_ROSTER_PROVIDER_XAI, "xAI API", HUSH_PROVIDER_FAMILY_API,
      HUSH_PROVIDER_HOST_XAI, "" },
    { HUSH_ROSTER_PROVIDER_OPENAI, "OpenAI API", HUSH_PROVIDER_FAMILY_API,
      HUSH_PROVIDER_HOST_OPENAI, "" },
    { HUSH_ROSTER_PROVIDER_ANTHROPIC, "Anthropic API",
      HUSH_PROVIDER_FAMILY_API, HUSH_PROVIDER_HOST_ANTHROPIC, "" }
};

static const char *const hush_provider_secret_kind[HUSH_PROVIDER_SECRET_COUNT] = {
    HUSH_PROVIDER_SECRET_API_KEY,
    HUSH_PROVIDER_SECRET_USERNAME,
    HUSH_PROVIDER_SECRET_PASSWORD,
    HUSH_PROVIDER_SECRET_TOKEN,
    HUSH_PROVIDER_SECRET_PASSKEY
};

static char g_last_error[HUSH_PROVIDER_ERR_MAX];

static void hush_provider_copy(char *dst, size_t dstsz, const char *src);
static hush_status_t hush_provider_fail(const char *msg);
static const hush_provider_meta_t *hush_provider_meta_of(const char *id);
static int hush_provider_path_exists(const char *path);
static int hush_provider_has_binary(const char *name);
static void hush_provider_config_dir(char *out, size_t outsz);
static void hush_provider_file_path(char *out, size_t outsz);
static hush_status_t hush_provider_ensure_dir(void);
static hush_status_t hush_provider_read_file(char *buf, size_t bufsz);
static hush_status_t hush_provider_write_file(const char *buf);
static int hush_provider_json_string(const char *json, const char *key,
                                     char *out, size_t outsz);
static int hush_provider_json_object(const char *json, const char *id,
                                     char *out, size_t outsz);
static void hush_provider_load_overlay(hush_provider_status_t *st,
                                       const char *json);
static void hush_provider_detect_home(hush_provider_status_t *st);
static void hush_provider_home_path(char *out, size_t outsz, const char *id);
static void hush_provider_read_goose_model(char *out, size_t outsz);
static void hush_provider_trim_yaml(char *text);
/* True when kind is one of the five provider secret names. */
static int hush_provider_is_secret_kind(const char *kind);
/* Returns the matching optional secret pointer from in. */
static const char *hush_provider_in_secret(const hush_provider_in_t *in,
                                           const char *kind);
/* True when pass has providers/<id>/<kind>. */
static int hush_provider_has_kind(const char *id, const char *kind);
/* Sets has_* from hush_pass_has for every kind. */
static void hush_provider_fill_has(hush_provider_status_t *st);
/* Writes each non-empty in secret to pass. Soft-fails. */
static void hush_provider_save_secrets(const hush_provider_in_t *in);
/* Copies posted, else pass api_key, else pass token. */
static void hush_provider_load_scan_key(char *out, size_t outsz, const char *id,
                                        const char *posted);
static void hush_provider_mark_configured(hush_provider_status_t *st);
static hush_status_t hush_provider_upsert_json(char *json, size_t jsonsz,
                                               const hush_provider_in_t *in,
                                               int has_key);
static void hush_provider_format_object(char *dst, size_t dstsz,
                                        const hush_provider_in_t *in,
                                        int has_key);
static size_t hush_provider_json_escape(const char *src, char *dst,
                                        size_t dstsz);
static hush_status_t hush_provider_write_curl_cfg(char *path, size_t pathsz,
                                                  const char *id,
                                                  const char *host,
                                                  const char *api_key);
static void hush_provider_fill_curl_url(char *url, size_t urlsz,
                                        const char *id, const char *host,
                                        const char *api_key);
static void hush_provider_fill_curl_cfg(char *dst, size_t dstsz,
                                        const char *id, const char *url,
                                        const char *api_key);
static hush_status_t hush_provider_run_curl(char *body, size_t bodysz,
                                            const char *cfg_path);
static void hush_provider_exec_curl(int write_fd, const char *cfg_path);
static hush_status_t hush_provider_read_curl(char *body, size_t bodysz,
                                             int fd, pid_t pid);
static void hush_provider_parse_models(hush_provider_scan_t *out,
                                       const char *id, const char *body);
static void hush_provider_add_model(hush_provider_scan_t *out,
                                    const char *name);
/* Fills argv for the official login of id. Returns 0 when id has none. */
static int hush_provider_login_argv(char **argv, size_t argvsz, const char *id);
/* Writes "bin login [--oauth]" into out. */
static void hush_provider_fill_login_cmd(char *out, size_t outsz, char **argv);
/* Adapter: execlp term -e cmd. Returns only when exec fails. */
static void hush_provider_exec_term(const char *term, const char *cmd);
/* Opens a terminal when possible, else execvp of argv. Does not return. */
static void hush_provider_exec_login(char **argv);
/* fork + exec login. Parent returns immediately. */
static hush_status_t hush_provider_spawn_login(char **argv);

int hush_provider_is_id(const char *id)
{
    return hush_provider_meta_of(id) != NULL;
}

void hush_provider_default_host(char *out, size_t outsz, const char *id)
{
    const hush_provider_meta_t *meta;

    if (out == NULL || outsz == 0)
        return;
    meta = hush_provider_meta_of(id);
    hush_provider_copy(out, outsz, meta != NULL ? meta->host : "");
}

void hush_provider_family(char *out, size_t outsz, const char *id)
{
    const hush_provider_meta_t *meta;

    if (out == NULL || outsz == 0)
        return;
    meta = hush_provider_meta_of(id);
    hush_provider_copy(out, outsz, meta != NULL ? meta->family : "");
}

hush_status_t hush_provider_status(hush_provider_status_t *out, const char *id)
{
    const hush_provider_meta_t *meta;
    char json[HUSH_PROVIDER_JSON_MAX];

    if (out == NULL)
        return HUSH_ERR_ARG;
    memset(out, 0, sizeof(*out));
    meta = hush_provider_meta_of(id);
    if (meta == NULL)
        return HUSH_ERR_PARSE;
    hush_provider_copy(out->id, sizeof(out->id), meta->id);
    hush_provider_copy(out->label, sizeof(out->label), meta->label);
    hush_provider_copy(out->family, sizeof(out->family), meta->family);
    hush_provider_copy(out->host, sizeof(out->host), meta->host);
    hush_provider_detect_home(out);
    json[0] = '\0';
    (void)hush_provider_read_file(json, sizeof(json));
    hush_provider_load_overlay(out, json);
    hush_provider_fill_has(out);
    hush_provider_mark_configured(out);
    return HUSH_OK;
}

hush_status_t hush_provider_status_all(hush_provider_status_t *out,
                                       size_t *out_n)
{
    size_t i;

    if (out == NULL || out_n == NULL)
        return HUSH_ERR_ARG;
    *out_n = 0;
    for (i = 0; i < (size_t)HUSH_PROVIDER_COUNT; i++) {
        if (hush_provider_status(&out[i], hush_provider_meta[i].id) != HUSH_OK)
            return HUSH_ERR_PARSE;
        *out_n += 1;
    }
    return HUSH_OK;
}

hush_status_t hush_provider_save(const hush_provider_in_t *in)
{
    char json[HUSH_PROVIDER_JSON_MAX];
    char key_path[HUSH_PASS_PATH_MAX];
    int has_key;
    hush_status_t st;

    if (in == NULL || !hush_provider_is_id(in->id))
        return HUSH_ERR_ARG;
    hush_provider_save_secrets(in);
    hush_provider_secret_path(key_path, sizeof(key_path), in->id,
                              HUSH_PROVIDER_SECRET_API_KEY);
    has_key = hush_pass_has(key_path);
    json[0] = '\0';
    (void)hush_provider_read_file(json, sizeof(json));
    st = hush_provider_upsert_json(json, sizeof(json), in, has_key);
    if (st != HUSH_OK)
        return st;
    return hush_provider_write_file(json);
}

void hush_provider_secret_path(char *out, size_t outsz,
                               const char *id, const char *kind)
{
    int n;

    if (out == NULL || outsz == 0)
        return;
    out[0] = '\0';
    if (!hush_provider_is_id(id) || !hush_provider_is_secret_kind(kind))
        return;
    n = snprintf(out, outsz, HUSH_PROVIDER_SECRET_PATH_FMT, id, kind);
    if (n <= 0 || (size_t)n >= outsz)
        out[0] = '\0';
}

hush_status_t hush_provider_scan(hush_provider_scan_t *out, const char *id,
                                 const char *host, const char *api_key)
{
    char cfg[HUSH_PROVIDER_PATH_MAX];
    char body[HUSH_PROVIDER_SCAN_BODY_MAX];
    char use_host[HUSH_PROVIDER_HOST_MAX];
    char use_key[HUSH_PROVIDER_KEY_MAX];
    hush_status_t st;

    if (out == NULL || !hush_provider_is_id(id))
        return HUSH_ERR_ARG;
    memset(out, 0, sizeof(*out));
    if (host != NULL && host[0] != '\0')
        hush_provider_copy(use_host, sizeof(use_host), host);
    else
        hush_provider_default_host(use_host, sizeof(use_host), id);
    if (use_host[0] == '\0') {
        hush_provider_copy(out->error, sizeof(out->error), "host required");
        return hush_provider_fail("host required");
    }
    hush_provider_load_scan_key(use_key, sizeof(use_key), id, api_key);
    st = hush_provider_write_curl_cfg(cfg, sizeof(cfg), id, use_host, use_key);
    if (st != HUSH_OK) {
        hush_provider_copy(out->error, sizeof(out->error), g_last_error);
        return st;
    }
    st = hush_provider_run_curl(body, sizeof(body), cfg);
    unlink(cfg);
    if (st != HUSH_OK) {
        hush_provider_copy(out->error, sizeof(out->error), g_last_error);
        return st;
    }
    hush_provider_parse_models(out, id, body);
    if (out->nmodels == 0) {
        hush_provider_copy(out->error, sizeof(out->error), "no models");
        return hush_provider_fail("no models");
    }
    return HUSH_OK;
}

hush_status_t hush_provider_start_login(const char *id)
{
    char *argv[HUSH_PROVIDER_LOGIN_ARGV_MAX];

    hush_provider_copy(g_last_error, sizeof(g_last_error), "");
    if (!hush_provider_is_id(id))
        return hush_provider_fail("unknown provider");
    if (!hush_provider_login_argv(argv, HUSH_PROVIDER_LOGIN_ARGV_MAX, id))
        return hush_provider_fail("login not offered");
    if (!hush_provider_has_binary(argv[0]))
        return hush_provider_fail("binary missing");
    return hush_provider_spawn_login(argv);
}

void hush_provider_last_error(char *out, size_t outsz)
{
    if (out == NULL || outsz == 0)
        return;
    hush_provider_copy(out, outsz, g_last_error);
}

static void hush_provider_copy(char *dst, size_t dstsz, const char *src)
{
    size_t n;

    assert(dst != NULL);
    assert(dstsz > 0);
    if (src == NULL)
        src = "";
    n = strlen(src);
    if (n >= dstsz)
        n = dstsz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static hush_status_t hush_provider_fail(const char *msg)
{
    hush_provider_copy(g_last_error, sizeof(g_last_error), msg);
    return HUSH_ERR_IO;
}

static const hush_provider_meta_t *hush_provider_meta_of(const char *id)
{
    size_t i;

    if (id == NULL || id[0] == '\0')
        return NULL;
    for (i = 0; i < (size_t)HUSH_PROVIDER_COUNT; i++) {
        if (strcmp(id, hush_provider_meta[i].id) == 0)
            return &hush_provider_meta[i];
    }
    return NULL;
}

static int hush_provider_path_exists(const char *path)
{
    struct stat st;

    if (path == NULL || path[0] == '\0')
        return 0;
    return stat(path, &st) == 0;
}

static int hush_provider_has_binary(const char *name)
{
    char *path;
    char *copy;
    char *tok;
    char *save;
    char cand[HUSH_PROVIDER_PATH_MAX];
    int found = 0;

    if (name == NULL || name[0] == '\0')
        return 0;
    path = getenv("PATH");
    if (path == NULL)
        return 0;
    copy = strdup(path);
    if (copy == NULL)
        return 0;
    tok = strtok_r(copy, ":", &save);
    while (tok != NULL) {
        if (snprintf(cand, sizeof(cand), "%s/%s", tok, name) < (int)sizeof(cand)
            && access(cand, X_OK) == 0) {
            found = 1;
            break;
        }
        tok = strtok_r(NULL, ":", &save);
    }
    free(copy);
    return found;
}

static void hush_provider_config_dir(char *out, size_t outsz)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    int n;

    assert(out != NULL);
    assert(outsz > 0);
    if (xdg != NULL && xdg[0] != '\0') {
        n = snprintf(out, outsz, "%s/hush", xdg);
        if (n > 0 && (size_t)n < outsz)
            return;
    }
    if (home != NULL && home[0] != '\0') {
        n = snprintf(out, outsz, "%s/.config/hush", home);
        if (n > 0 && (size_t)n < outsz)
            return;
    }
    hush_provider_copy(out, outsz, "/tmp/hush");
}

static void hush_provider_file_path(char *out, size_t outsz)
{
    char dir[HUSH_PROVIDER_PATH_MAX];
    int n;

    hush_provider_config_dir(dir, sizeof(dir));
    n = snprintf(out, outsz, "%s/%s", dir, HUSH_PROVIDER_FILE_NAME);
    if (n <= 0 || (size_t)n >= outsz)
        out[0] = '\0';
}

static hush_status_t hush_provider_ensure_dir(void)
{
    char dir[HUSH_PROVIDER_PATH_MAX];

    hush_provider_config_dir(dir, sizeof(dir));
    if (dir[0] == '\0')
        return hush_provider_fail("config dir");
    if (mkdir(dir, 0700) != 0 && errno != EEXIST)
        return hush_provider_fail("mkdir");
    return HUSH_OK;
}

static hush_status_t hush_provider_read_file(char *buf, size_t bufsz)
{
    char path[HUSH_PROVIDER_PATH_MAX];
    FILE *fp;
    size_t n;

    assert(buf != NULL);
    assert(bufsz > 0);
    buf[0] = '\0';
    hush_provider_file_path(path, sizeof(path));
    if (path[0] == '\0')
        return HUSH_ERR_IO;
    fp = fopen(path, "r");
    if (fp == NULL)
        return HUSH_OK;
    n = fread(buf, 1, bufsz - 1, fp);
    buf[n] = '\0';
    fclose(fp);
    return HUSH_OK;
}

static hush_status_t hush_provider_write_file(const char *buf)
{
    char path[HUSH_PROVIDER_PATH_MAX];
    int fd;
    size_t len;
    ssize_t wr;

    if (buf == NULL)
        return HUSH_ERR_ARG;
    if (hush_provider_ensure_dir() != HUSH_OK)
        return HUSH_ERR_IO;
    hush_provider_file_path(path, sizeof(path));
    if (path[0] == '\0')
        return hush_provider_fail("overlay path");
    fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (fd < 0)
        return hush_provider_fail("open overlay");
    len = strlen(buf);
    wr = write(fd, buf, len);
    close(fd);
    if (wr < 0 || (size_t)wr != len) {
        unlink(path);
        return hush_provider_fail("write overlay");
    }
    return HUSH_OK;
}

static int hush_provider_json_string(const char *json, const char *key,
                                     char *out, size_t outsz)
{
    char quoted[80];
    const char *p;
    size_t i = 0;

    assert(out != NULL);
    assert(outsz > 0);
    out[0] = '\0';
    if (json == NULL || key == NULL)
        return 0;
    if (snprintf(quoted, sizeof(quoted), "\"%s\":\"", key)
        >= (int)sizeof(quoted))
        return 0;
    p = strstr(json, quoted);
    if (p == NULL)
        return 0;
    p += strlen(quoted);
    while (*p != '\0' && *p != '"' && i + 1 < outsz) {
        if (*p == '\\' && p[1] != '\0')
            p++;
        out[i++] = *p++;
    }
    out[i] = '\0';
    return out[0] != '\0';
}

static int hush_provider_json_object(const char *json, const char *id,
                                     char *out, size_t outsz)
{
    char needle[80];
    const char *start;
    const char *end;
    size_t n;

    assert(out != NULL);
    assert(outsz > 0);
    out[0] = '\0';
    if (json == NULL || id == NULL)
        return 0;
    if (snprintf(needle, sizeof(needle), "\"%s\":{", id) >= (int)sizeof(needle))
        return 0;
    start = strstr(json, needle);
    if (start == NULL)
        return 0;
    start += strlen(needle) - 1;
    end = strchr(start, '}');
    if (end == NULL)
        return 0;
    n = (size_t)(end - start + 1);
    if (n >= outsz)
        n = outsz - 1;
    memcpy(out, start, n);
    out[n] = '\0';
    return 1;
}

static void hush_provider_load_overlay(hush_provider_status_t *st,
                                       const char *json)
{
    char obj[HUSH_PROVIDER_OBJ_MAX];
    char host[HUSH_PROVIDER_HOST_MAX];
    char model[HUSH_PROVIDER_MODEL_MAX];
    char flag[8];

    assert(st != NULL);
    if (!hush_provider_json_object(json, st->id, obj, sizeof(obj)))
        return;
    if (hush_provider_json_string(obj, "host", host, sizeof(host)))
        hush_provider_copy(st->host, sizeof(st->host), host);
    if (hush_provider_json_string(obj, "model", model, sizeof(model)))
        hush_provider_copy(st->model, sizeof(st->model), model);
    if (hush_provider_json_string(obj, "use_home", flag, sizeof(flag)))
        st->use_home = strcmp(flag, "true") == 0;
}

static void hush_provider_home_path(char *out, size_t outsz, const char *id)
{
    const char *home = getenv("HOME");
    const char *xdg = getenv("XDG_CONFIG_HOME");

    assert(out != NULL);
    assert(outsz > 0);
    out[0] = '\0';
    if (home == NULL)
        home = "";
    if (strcmp(id, HUSH_ROSTER_PROVIDER_GOOSE) == 0) {
        if (xdg != NULL && xdg[0] != '\0')
            snprintf(out, outsz, "%s/goose/config.yaml", xdg);
        else
            snprintf(out, outsz, "%s/.config/goose/config.yaml", home);
        return;
    }
    if (strcmp(id, HUSH_ROSTER_PROVIDER_GROK_BUILD) == 0)
        snprintf(out, outsz, "%s/.grok/auth.json", home);
    else if (strcmp(id, HUSH_ROSTER_PROVIDER_CODEX) == 0)
        snprintf(out, outsz, "%s/.codex", home);
    else if (strcmp(id, HUSH_ROSTER_PROVIDER_CLINE) == 0)
        snprintf(out, outsz, "%s/Documents/Cline", home);
}

static void hush_provider_detect_home(hush_provider_status_t *st)
{
    const hush_provider_meta_t *meta;
    char path[HUSH_PROVIDER_PATH_MAX];
    const char *home;

    assert(st != NULL);
    meta = hush_provider_meta_of(st->id);
    if (meta == NULL)
        return;
    if (meta->binary[0] != '\0')
        st->has_binary = hush_provider_has_binary(meta->binary);
    hush_provider_home_path(path, sizeof(path), st->id);
    st->has_home = hush_provider_path_exists(path);
    if (!st->has_home && strcmp(st->id, HUSH_ROSTER_PROVIDER_GOOSE) == 0) {
        home = getenv("HOME");
        if (home != NULL)
            snprintf(path, sizeof(path), "%s/.goose/config.yaml", home);
        st->has_home = hush_provider_path_exists(path);
    }
    if (!st->has_home && strcmp(st->id, HUSH_ROSTER_PROVIDER_CLINE) == 0) {
        home = getenv("HOME");
        if (home != NULL)
            snprintf(path, sizeof(path), "%s/.cline", home);
        st->has_home = hush_provider_path_exists(path);
    }
    if (st->has_home && strcmp(st->id, HUSH_ROSTER_PROVIDER_GOOSE) == 0)
        hush_provider_read_goose_model(st->home_model, sizeof(st->home_model));
}

static void hush_provider_trim_yaml(char *text)
{
    size_t n;

    assert(text != NULL);
    n = strlen(text);
    while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r' ||
                     text[n - 1] == '"' || text[n - 1] == ' ')) {
        text[n - 1] = '\0';
        n--;
    }
}

static void hush_provider_read_goose_model(char *out, size_t outsz)
{
    char path[HUSH_PROVIDER_PATH_MAX];
    FILE *fp;
    char line[HUSH_PROVIDER_YAML_LINE_MAX];
    const char *p;

    assert(out != NULL);
    assert(outsz > 0);
    out[0] = '\0';
    hush_provider_home_path(path, sizeof(path), HUSH_ROSTER_PROVIDER_GOOSE);
    fp = fopen(path, "r");
    if (fp == NULL)
        return;
    while (fgets(line, sizeof(line), fp) != NULL) {
        p = strstr(line, "model:");
        if (p == NULL)
            continue;
        p += 6;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '"')
            p++;
        hush_provider_copy(out, outsz, p);
        hush_provider_trim_yaml(out);
        if (out[0] != '\0')
            break;
    }
    fclose(fp);
}

static int hush_provider_is_secret_kind(const char *kind)
{
    size_t i;

    if (kind == NULL || kind[0] == '\0')
        return 0;
    for (i = 0; i < (size_t)HUSH_PROVIDER_SECRET_COUNT; i++) {
        if (strcmp(kind, hush_provider_secret_kind[i]) == 0)
            return 1;
    }
    return 0;
}

static const char *hush_provider_in_secret(const hush_provider_in_t *in,
                                           const char *kind)
{
    assert(in != NULL);
    assert(kind != NULL);
    if (strcmp(kind, HUSH_PROVIDER_SECRET_API_KEY) == 0)
        return in->api_key;
    if (strcmp(kind, HUSH_PROVIDER_SECRET_USERNAME) == 0)
        return in->username;
    if (strcmp(kind, HUSH_PROVIDER_SECRET_PASSWORD) == 0)
        return in->password;
    if (strcmp(kind, HUSH_PROVIDER_SECRET_TOKEN) == 0)
        return in->token;
    if (strcmp(kind, HUSH_PROVIDER_SECRET_PASSKEY) == 0)
        return in->passkey;
    return NULL;
}

static int hush_provider_has_kind(const char *id, const char *kind)
{
    char path[HUSH_PASS_PATH_MAX];

    hush_provider_secret_path(path, sizeof(path), id, kind);
    return hush_pass_has(path);
}

static void hush_provider_fill_has(hush_provider_status_t *st)
{
    assert(st != NULL);
    st->has_key = hush_provider_has_kind(st->id, HUSH_PROVIDER_SECRET_API_KEY);
    st->has_username = hush_provider_has_kind(st->id,
                                              HUSH_PROVIDER_SECRET_USERNAME);
    st->has_password = hush_provider_has_kind(st->id,
                                              HUSH_PROVIDER_SECRET_PASSWORD);
    st->has_token = hush_provider_has_kind(st->id, HUSH_PROVIDER_SECRET_TOKEN);
    st->has_passkey = hush_provider_has_kind(st->id,
                                             HUSH_PROVIDER_SECRET_PASSKEY);
}

static void hush_provider_save_secrets(const hush_provider_in_t *in)
{
    size_t i;
    char path[HUSH_PASS_PATH_MAX];
    const char *secret;

    assert(in != NULL);
    for (i = 0; i < (size_t)HUSH_PROVIDER_SECRET_COUNT; i++) {
        secret = hush_provider_in_secret(in, hush_provider_secret_kind[i]);
        if (secret == NULL || secret[0] == '\0')
            continue;
        hush_provider_secret_path(path, sizeof(path), in->id,
                                  hush_provider_secret_kind[i]);
        if (hush_pass_save(path, secret) != HUSH_OK)
            hush_provider_copy(g_last_error, sizeof(g_last_error),
                               "pass save failed");
    }
}

static void hush_provider_load_scan_key(char *out, size_t outsz, const char *id,
                                        const char *posted)
{
    char path[HUSH_PASS_PATH_MAX];

    assert(out != NULL);
    assert(outsz > 0);
    out[0] = '\0';
    if (posted != NULL && posted[0] != '\0') {
        hush_provider_copy(out, outsz, posted);
        return;
    }
    hush_provider_secret_path(path, sizeof(path), id,
                              HUSH_PROVIDER_SECRET_API_KEY);
    if (hush_pass_get(out, outsz, path) == HUSH_OK && out[0] != '\0')
        return;
    hush_provider_secret_path(path, sizeof(path), id,
                              HUSH_PROVIDER_SECRET_TOKEN);
    (void)hush_pass_get(out, outsz, path);
}

static void hush_provider_mark_configured(hush_provider_status_t *st)
{
    assert(st != NULL);
    if (st->use_home && (st->has_home || st->has_binary)) {
        st->configured = 1;
        return;
    }
    if (st->has_key && st->host[0] != '\0') {
        st->configured = 1;
        return;
    }
    if (st->has_home && strcmp(st->family, HUSH_PROVIDER_FAMILY_HOME) == 0)
        st->configured = 1;
}

static hush_status_t hush_provider_upsert_json(char *json, size_t jsonsz,
                                               const hush_provider_in_t *in,
                                               int has_key)
{
    char obj[HUSH_PROVIDER_OBJ_MAX];
    char needle[80];
    char next[HUSH_PROVIDER_JSON_MAX];
    const char *start;
    const char *end;
    size_t n;

    assert(json != NULL);
    assert(in != NULL);
    hush_provider_format_object(obj, sizeof(obj), in, has_key);
    if (json[0] == '\0') {
        if (snprintf(json, jsonsz, "{%s}", obj) >= (int)jsonsz)
            return hush_provider_fail("overlay full");
        return HUSH_OK;
    }
    if (snprintf(needle, sizeof(needle), "\"%s\":{", in->id) >= (int)sizeof(needle))
        return HUSH_ERR_ARG;
    start = strstr(json, needle);
    if (start == NULL) {
        n = strlen(json);
        if (n > 0 && json[n - 1] == '}')
            json[n - 1] = '\0';
        if (snprintf(next, sizeof(next), "%s,%s}", json, obj) >= (int)sizeof(next))
            return hush_provider_fail("overlay full");
        hush_provider_copy(json, jsonsz, next);
        return HUSH_OK;
    }
    end = strchr(start, '}');
    if (end == NULL)
        return hush_provider_fail("overlay parse");
    if (snprintf(next, sizeof(next), "%.*s%s%s",
                 (int)(start - json), json, obj, end + 1) >= (int)sizeof(next))
        return hush_provider_fail("overlay full");
    hush_provider_copy(json, jsonsz, next);
    return HUSH_OK;
}

static void hush_provider_format_object(char *dst, size_t dstsz,
                                        const hush_provider_in_t *in,
                                        int has_key)
{
    char host[HUSH_PROVIDER_HOST_MAX * 2];
    char model[HUSH_PROVIDER_MODEL_MAX * 2];

    assert(dst != NULL);
    assert(in != NULL);
    hush_provider_json_escape(in->host, host, sizeof(host));
    hush_provider_json_escape(in->model, model, sizeof(model));
    snprintf(dst, dstsz,
             "\"%s\":{\"use_home\":\"%s\",\"host\":\"%s\",\"model\":\"%s\","
             "\"has_key\":\"%s\"}",
             in->id, in->use_home ? "true" : "false", host, model,
             has_key ? "true" : "false");
}

static size_t hush_provider_json_escape(const char *src, char *dst,
                                        size_t dstsz)
{
    size_t i = 0;
    size_t o = 0;

    if (dst == NULL || dstsz == 0)
        return 0;
    if (src == NULL)
        src = "";
    while (src[i] != '\0' && o + 2 < dstsz) {
        if (src[i] == '"' || src[i] == '\\')
            dst[o++] = '\\';
        dst[o++] = src[i++];
    }
    dst[o] = '\0';
    return o;
}

static void hush_provider_fill_curl_url(char *url, size_t urlsz,
                                        const char *id, const char *host,
                                        const char *api_key)
{
    assert(url != NULL);
    if (strcmp(id, HUSH_ROSTER_PROVIDER_GEMINI) == 0) {
        if (api_key != NULL && api_key[0] != '\0')
            snprintf(url, urlsz, "%s/v1beta/models?key=%s", host, api_key);
        else
            snprintf(url, urlsz, "%s/v1beta/models", host);
        return;
    }
    snprintf(url, urlsz, "%s/v1/models", host);
}

static void hush_provider_fill_curl_cfg(char *dst, size_t dstsz,
                                        const char *id, const char *url,
                                        const char *api_key)
{
    const char *key = api_key != NULL ? api_key : "";

    assert(dst != NULL);
    if (strcmp(id, HUSH_ROSTER_PROVIDER_ANTHROPIC) == 0) {
        snprintf(dst, dstsz,
                 "silent\nshow-error\nmax-time = %d\nurl = \"%s\"\n"
                 "header = \"x-api-key: %s\"\n"
                 "header = \"anthropic-version: %s\"\n",
                 HUSH_PROVIDER_CURL_TIME_S, url, key,
                 HUSH_PROVIDER_ANTHROPIC_VER);
        return;
    }
    if (strcmp(id, HUSH_ROSTER_PROVIDER_GEMINI) == 0) {
        snprintf(dst, dstsz,
                 "silent\nshow-error\nmax-time = %d\nurl = \"%s\"\n",
                 HUSH_PROVIDER_CURL_TIME_S, url);
        return;
    }
    snprintf(dst, dstsz,
             "silent\nshow-error\nmax-time = %d\nurl = \"%s\"\n"
             "header = \"Authorization: Bearer %s\"\n",
             HUSH_PROVIDER_CURL_TIME_S, url, key);
}

static hush_status_t hush_provider_write_curl_cfg(char *path, size_t pathsz,
                                                  const char *id,
                                                  const char *host,
                                                  const char *api_key)
{
    char url[HUSH_PROVIDER_URL_MAX];
    char body[HUSH_PROVIDER_CURL_CFG_MAX];
    const char *tmpdir = getenv("TMPDIR");
    int fd;
    ssize_t wr;

    assert(path != NULL);
    if (tmpdir == NULL || tmpdir[0] == '\0')
        tmpdir = "/tmp";
    if (snprintf(path, pathsz, "%s/hush-provider-curl-XXXXXX", tmpdir)
        >= (int)pathsz)
        return hush_provider_fail("tmp path");
    fd = mkstemp(path);
    if (fd < 0)
        return hush_provider_fail("mkstemp");
    (void)fchmod(fd, 0600);
    hush_provider_fill_curl_url(url, sizeof(url), id, host, api_key);
    hush_provider_fill_curl_cfg(body, sizeof(body), id, url, api_key);
    wr = write(fd, body, strlen(body));
    close(fd);
    if (wr < 0 || (size_t)wr != strlen(body)) {
        unlink(path);
        return hush_provider_fail("write curl cfg");
    }
    return HUSH_OK;
}

static void hush_provider_exec_curl(int write_fd, const char *cfg_path)
{
    char *argv[5];

    if (dup2(write_fd, STDOUT_FILENO) < 0)
        _exit(127);
    close(write_fd);
    argv[0] = (char *)HUSH_PROVIDER_CURL_BIN;
    argv[1] = (char *)"-sS";
    argv[2] = (char *)"--config";
    argv[3] = (char *)cfg_path;
    argv[4] = NULL;
    execvp(argv[0], argv);
    _exit(127);
}

static hush_status_t hush_provider_read_curl(char *body, size_t bodysz,
                                             int fd, pid_t pid)
{
    ssize_t n;
    size_t got = 0;
    int status;

    while (got + 1 < bodysz) {
        n = read(fd, body + got, bodysz - 1 - got);
        if (n <= 0)
            break;
        got += (size_t)n;
    }
    body[got] = '\0';
    close(fd);
    if (waitpid(pid, &status, 0) < 0)
        return hush_provider_fail("waitpid");
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return hush_provider_fail("curl failed");
    return HUSH_OK;
}

static hush_status_t hush_provider_run_curl(char *body, size_t bodysz,
                                            const char *cfg_path)
{
    int out_pipe[2];
    pid_t pid;

    assert(body != NULL);
    assert(bodysz > 0);
    body[0] = '\0';
    if (!hush_provider_has_binary(HUSH_PROVIDER_CURL_BIN))
        return hush_provider_fail("curl missing");
    if (pipe(out_pipe) != 0)
        return hush_provider_fail("pipe");
    pid = fork();
    if (pid < 0) {
        close(out_pipe[0]);
        close(out_pipe[1]);
        return hush_provider_fail("fork");
    }
    if (pid == 0) {
        close(out_pipe[0]);
        hush_provider_exec_curl(out_pipe[1], cfg_path);
    }
    close(out_pipe[1]);
    return hush_provider_read_curl(body, bodysz, out_pipe[0], pid);
}

static void hush_provider_parse_models(hush_provider_scan_t *out,
                                       const char *id, const char *body)
{
    const char *p;
    const char *needle;
    size_t skip;

    assert(out != NULL);
    out->nmodels = 0;
    if (body == NULL)
        return;
    if (strcmp(id, HUSH_ROSTER_PROVIDER_GEMINI) == 0) {
        needle = "\"name\":\"models/";
        skip = 15;
    } else {
        needle = "\"id\":\"";
        skip = 6;
    }
    p = body;
    while ((p = strstr(p, needle)) != NULL &&
           out->nmodels < (size_t)HUSH_PROVIDER_MODELS_MAX) {
        char name[HUSH_PROVIDER_MODEL_MAX];
        size_t i = 0;

        p += skip;
        while (*p != '\0' && *p != '"' && i + 1 < sizeof(name))
            name[i++] = *p++;
        name[i] = '\0';
        if (name[0] != '\0')
            hush_provider_add_model(out, name);
    }
}

static void hush_provider_add_model(hush_provider_scan_t *out, const char *name)
{
    size_t i;

    assert(out != NULL);
    assert(name != NULL);
    for (i = 0; i < out->nmodels; i++) {
        if (strcmp(out->models[i], name) == 0)
            return;
    }
    if (out->nmodels >= (size_t)HUSH_PROVIDER_MODELS_MAX)
        return;
    hush_provider_copy(out->models[out->nmodels], HUSH_PROVIDER_MODEL_MAX, name);
    out->nmodels += 1;
}

static int hush_provider_login_argv(char **argv, size_t argvsz, const char *id)
{
    assert(argv != NULL);
    assert(argvsz >= (size_t)HUSH_PROVIDER_LOGIN_ARGV_MAX);
    assert(id != NULL);
    memset(argv, 0, argvsz * sizeof(*argv));
    if (strcmp(id, HUSH_ROSTER_PROVIDER_GROK_BUILD) == 0) {
        argv[0] = (char *)"grok";
        argv[1] = (char *)"login";
        argv[2] = (char *)"--oauth";
        return 1;
    }
    if (strcmp(id, HUSH_ROSTER_PROVIDER_CODEX) == 0) {
        argv[0] = (char *)"codex";
        argv[1] = (char *)"login";
        return 1;
    }
    return 0;
}

static void hush_provider_fill_login_cmd(char *out, size_t outsz, char **argv)
{
    assert(out != NULL);
    assert(outsz > 0);
    assert(argv != NULL);
    assert(argv[0] != NULL);
    if (argv[2] != NULL)
        snprintf(out, outsz, "%s %s %s", argv[0], argv[1], argv[2]);
    else if (argv[1] != NULL)
        snprintf(out, outsz, "%s %s", argv[0], argv[1]);
    else
        snprintf(out, outsz, "%s", argv[0]);
}

static void hush_provider_exec_term(const char *term, const char *cmd)
{
    assert(term != NULL);
    assert(cmd != NULL);
    if (term[0] == '\0')
        return;
    execlp(term, term, "-e", cmd, (char *)NULL);
}

static void hush_provider_exec_login(char **argv)
{
    char cmd[HUSH_PROVIDER_LOGIN_CMD_MAX];
    const char *term;
    const char *display;

    assert(argv != NULL);
    assert(argv[0] != NULL);
    hush_provider_fill_login_cmd(cmd, sizeof(cmd), argv);
    term = getenv("HUSH_PROVIDER_TERM");
    if (term != NULL && term[0] != '\0') {
        hush_provider_exec_term(term, cmd);
        _exit(127);
    }
    display = getenv("DISPLAY");
    if (display != NULL && display[0] != '\0') {
        hush_provider_exec_term("x-terminal-emulator", cmd);
        hush_provider_exec_term("xterm", cmd);
    }
    execvp(argv[0], argv);
    _exit(127);
}

static hush_status_t hush_provider_spawn_login(char **argv)
{
    pid_t pid;

    assert(argv != NULL);
    assert(argv[0] != NULL);
    pid = fork();
    if (pid < 0)
        return hush_provider_fail("fork");
    if (pid == 0)
        hush_provider_exec_login(argv);
    return HUSH_OK;
}
