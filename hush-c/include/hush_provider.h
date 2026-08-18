/* hush_provider.h: detect home configs, overlay file, pass keys, model scan. */

#ifndef HUSH_PROVIDER_H
#define HUSH_PROVIDER_H

#include <stddef.h>
#include "hush_status.h"

enum {
    HUSH_PROVIDER_ID_MAX = 32,
    HUSH_PROVIDER_LABEL_MAX = 32,
    HUSH_PROVIDER_FAMILY_MAX = 16,
    HUSH_PROVIDER_HOST_MAX = 256,
    HUSH_PROVIDER_MODEL_MAX = 64,
    HUSH_PROVIDER_PATH_MAX = 256,
    HUSH_PROVIDER_MODELS_MAX = 32,
    HUSH_PROVIDER_JSON_MAX = 8192,
    HUSH_PROVIDER_ERR_MAX = 160,
    HUSH_PROVIDER_COUNT = 8,
    HUSH_PROVIDER_KEY_MAX = 512,
    HUSH_PROVIDER_URL_MAX = 1024,
    HUSH_PROVIDER_SECRET_COUNT = 5
};

#define HUSH_PROVIDER_FAMILY_HOME "home"
#define HUSH_PROVIDER_FAMILY_API "api"
#define HUSH_PROVIDER_FAMILY_EDITOR "editor"

#define HUSH_PROVIDER_SECRET_API_KEY "api_key"
#define HUSH_PROVIDER_SECRET_USERNAME "username"
#define HUSH_PROVIDER_SECRET_PASSWORD "password"
#define HUSH_PROVIDER_SECRET_TOKEN "token"
#define HUSH_PROVIDER_SECRET_PASSKEY "passkey"

#define HUSH_PROVIDER_HOST_OPENAI "https://api.openai.com"
#define HUSH_PROVIDER_HOST_XAI "https://api.x.ai"
#define HUSH_PROVIDER_HOST_ANTHROPIC "https://api.anthropic.com"
#define HUSH_PROVIDER_HOST_GEMINI "https://generativelanguage.googleapis.com"

#define HUSH_PROVIDER_FILE_NAME "providers.json"

typedef struct {
    char id[HUSH_PROVIDER_ID_MAX];
    char label[HUSH_PROVIDER_LABEL_MAX];
    char family[HUSH_PROVIDER_FAMILY_MAX];
    char host[HUSH_PROVIDER_HOST_MAX];
    char model[HUSH_PROVIDER_MODEL_MAX];
    char home_model[HUSH_PROVIDER_MODEL_MAX];
    int has_binary;
    int has_home;
    int has_key;
    int has_username;
    int has_password;
    int has_token;
    int has_passkey;
    int use_home;
    int configured;
} hush_provider_status_t;

typedef struct {
    char id[HUSH_PROVIDER_ID_MAX];
    char host[HUSH_PROVIDER_HOST_MAX];
    char model[HUSH_PROVIDER_MODEL_MAX];
    const char *api_key;
    const char *username;
    const char *password;
    const char *token;
    const char *passkey;
    int use_home;
} hush_provider_in_t;

typedef struct {
    char models[HUSH_PROVIDER_MODELS_MAX][HUSH_PROVIDER_MODEL_MAX];
    size_t nmodels;
    char error[HUSH_PROVIDER_ERR_MAX];
} hush_provider_scan_t;

/* True when id is one of the eight named runtimes. */
int hush_provider_is_id(const char *id);

/* Copies the default host for id. Empty for home and editor families. */
void hush_provider_default_host(char *out, size_t outsz, const char *id);

/* Copies the family name for id. Empty when id is unknown. */
void hush_provider_family(char *out, size_t outsz, const char *id);

/* Fills status from home detect, overlay file, and pass. */
hush_status_t hush_provider_status(hush_provider_status_t *out, const char *id);

/* Fills one status per known id. out must have HUSH_PROVIDER_COUNT slots. */
hush_status_t hush_provider_status_all(hush_provider_status_t *out, size_t *out_n);

/* Writes overlay host, model, and use_home. Optional secrets go to pass. */
hush_status_t hush_provider_save(const hush_provider_in_t *in);

/* Writes providers/<id>/<kind> (no hush/ prefix). Empty on a bad id or kind. */
void hush_provider_secret_path(char *out, size_t outsz,
                               const char *id, const char *kind);

/* Lists models via curl. Fills out->error on failure. */
hush_status_t hush_provider_scan(hush_provider_scan_t *out, const char *id,
                                 const char *host, const char *api_key);

/* Copies the last module error. out may be NULL. */
void hush_provider_last_error(char *out, size_t outsz);

#endif /* HUSH_PROVIDER_H */
