/* hush_inference.c: owns API request encoding, private curl transport, and text extraction. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "hush_inference.h"
#include "hush_json.h"
#include "hush_pass.h"
#include "hush_roster.h"

enum {
    HUSH_INFERENCE_BODY_MAX = HUSH_INFERENCE_TEXT_MAX * HUSH_JSON_U_LEN,
    HUSH_INFERENCE_RESPONSE_MAX = 131072,
    /* One SSE line: a full content chunk plus JSON escaping. */
    HUSH_INFERENCE_LINE_MAX = 65536,
    HUSH_INFERENCE_PARTS_MAX = 64,
    HUSH_INFERENCE_PATH_MAX = 96,
    HUSH_INFERENCE_WAIT_MAX = 8,
    HUSH_INFERENCE_EXEC_FAILURE = 127,
    HUSH_INFERENCE_OUTPUT_TOKENS = 2048
};

#define HUSH_INFERENCE_ANTHROPIC_VERSION "2023-06-01"
#define HUSH_INFERENCE_MODEL_CHARS "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-:/"
#define HUSH_INFERENCE_TIMEOUT "80"

/* Buffers contain escaped JSON bodies; all are bounded, owned by the request worker. */
typedef struct {
    char system[HUSH_INFERENCE_BODY_MAX];
    char rules[HUSH_INFERENCE_BODY_MAX];
    char message[HUSH_INFERENCE_BODY_MAX];
    char model[HUSH_PROVIDER_MODEL_MAX * HUSH_JSON_U_LEN];
    char body[HUSH_INFERENCE_BODY_MAX];
    /* Set before encoding so format_body can ask the provider to stream. */
    int stream;
} hush_inference_encoding_t;

/* Resolves required provider credentials into caller-owned secret storage. */
static hush_status_t hush_inference_load_key(char *out, size_t outsz, const char *provider);
/* Escapes required request text into owned encoding. FULL on exceeded budget. */
static hush_status_t hush_inference_encode(hush_inference_encoding_t *encoding,
                                          const hush_provider_status_t *status,
                                          const hush_inference_request_t *request);
/* Selects the required provider's request body format. */
static hush_status_t hush_inference_format_body(hush_inference_encoding_t *encoding,
                                               const char *provider);
/* Forms the required provider's endpoint in caller storage. */
static hush_status_t hush_inference_endpoint(char *out, size_t outsz,
                                            const hush_provider_status_t *status);
/* Writes one escaped curl configuration entry into a required borrowed file. */
static hush_status_t hush_inference_config_value(FILE *file, const char *name, const char *value);
/* Writes secret curl configuration to a required anonymous file. */
static hush_status_t hush_inference_configure(FILE *file, const hush_provider_status_t *status,
                                             const char *body);
/* Runs curl using required anonymous files; returns IO on transport/HTTP failure. */
static hush_status_t hush_inference_run_curl(FILE *configuration, FILE *response);
/* Reads required response file into bounded output; rejects oversized/empty bodies. */
static hush_status_t hush_inference_read_response(FILE *file, char *out, size_t outsz);
/* Owns anonymous transport files for a required request; closes on every path. */
static hush_status_t hush_inference_transport(char *out, size_t outsz,
                                             const hush_provider_status_t *status,
                                             const char *body);
/* Extracts complete assistant text from a required borrowed response. */
static hush_status_t hush_inference_extract(char *out, size_t outsz, const char *provider,
                                           const char *response);
/* Appends one user-facing text part; skips thought blocks, NOT_FOUND past array end. */
static hush_status_t hush_inference_append_part(char *out, size_t outsz, const char *response,
                                               const char *path);
/* Appends bounded text blocks from required response at the given array path. */
static hush_status_t hush_inference_extract_parts(char *out, size_t outsz, const char *response,
                                                 const char *path);

int hush_inference_is_api(const char *provider)
{
    if (provider == NULL)
        return 0;
    char family[HUSH_PROVIDER_FAMILY_MAX] = {0};
    hush_provider_family(family, sizeof(family), provider);
    return strcmp(family, HUSH_PROVIDER_FAMILY_API) == 0;
}

int hush_inference_is_ready(const hush_provider_status_t *status)
{
    return status != NULL && hush_inference_is_api(status->id) && hush_provider_ready(status);
}

hush_status_t hush_inference_reply(char *out, size_t outsz,
                                     const hush_inference_request_t *request)
{
    return hush_inference_stream(out, outsz, request, -1, NULL);
}

static hush_status_t hush_inference_load_key(char *out, size_t outsz, const char *provider)
{
    assert(out != NULL && outsz > 0);
    assert(provider != NULL);
    char path[HUSH_PASS_PATH_MAX] = {0};
    hush_provider_secret_path(path, sizeof(path), provider, HUSH_PROVIDER_SECRET_API_KEY);
    if (hush_pass_get(out, outsz, path) == HUSH_OK && out[0] != '\0')
        return HUSH_OK;
    hush_provider_secret_path(path, sizeof(path), provider, HUSH_PROVIDER_SECRET_TOKEN);
    if (hush_pass_get(out, outsz, path) == HUSH_OK && out[0] != '\0')
        return HUSH_OK;
    out[0] = '\0';
    return strcmp(provider, HUSH_ROSTER_PROVIDER_CUSTOM) == 0 ? HUSH_OK : HUSH_ERR_DENIED;
}

static hush_status_t hush_inference_encode(hush_inference_encoding_t *encoding,
                                          const hush_provider_status_t *status,
                                          const hush_inference_request_t *request)
{
    assert(encoding != NULL);
    assert(status != NULL && request != NULL);
    size_t total = strlen(request->system) + strlen(request->rules) + strlen(request->message);
    if (total >= (size_t)HUSH_INFERENCE_TEXT_MAX)
        return HUSH_ERR_FULL;
    (void)hush_json_escape(request->system, encoding->system, sizeof(encoding->system));
    (void)hush_json_escape(request->rules, encoding->rules, sizeof(encoding->rules));
    (void)hush_json_escape(request->message, encoding->message, sizeof(encoding->message));
    (void)hush_json_escape(status->model, encoding->model, sizeof(encoding->model));
    return hush_inference_format_body(encoding, status->id);
}

static hush_status_t hush_inference_format_body(hush_inference_encoding_t *encoding,
                                               const char *provider)
{
    assert(encoding != NULL);
    assert(provider != NULL);
    int written = 0;
    const char *stream = encoding->stream ? "true" : "false";
    if (strcmp(provider, HUSH_ROSTER_PROVIDER_ANTHROPIC) == 0) {
        written = snprintf(encoding->body, sizeof(encoding->body),
            "{\"model\":\"%s\",\"max_tokens\":%d,\"stream\":%s,\"system\":\"%s\\n%s\","
            "\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}]}",
            encoding->model, HUSH_INFERENCE_OUTPUT_TOKENS, stream,
            encoding->system, encoding->rules, encoding->message);
    } else if (strcmp(provider, HUSH_ROSTER_PROVIDER_GEMINI) == 0) {
        written = snprintf(encoding->body, sizeof(encoding->body),
            "{\"systemInstruction\":{\"parts\":[{\"text\":\"%s\\n%s\"}]},"
            "\"contents\":[{\"role\":\"user\",\"parts\":[{\"text\":\"%s\"}]}]}",
            encoding->system, encoding->rules, encoding->message);
    } else {
        written = snprintf(encoding->body, sizeof(encoding->body),
            "{\"model\":\"%s\",\"messages\":[{\"role\":\"%s\",\"content\":\"%s\\n%s\"},"
            "{\"role\":\"user\",\"content\":\"%s\"}],\"stream\":%s}",
            encoding->model, strcmp(provider, HUSH_ROSTER_PROVIDER_OPENAI) == 0
                ? "developer" : "system", encoding->system, encoding->rules,
            encoding->message, stream);
    }
    if (written < 0 || (size_t)written >= sizeof(encoding->body))
        return HUSH_ERR_FULL;
    return HUSH_OK;
}

static hush_status_t hush_inference_endpoint(char *out, size_t outsz,
                                            const hush_provider_status_t *status)
{
    assert(out != NULL && outsz > 0);
    assert(status != NULL);
    if (strncmp(status->host, "https://", strlen("https://")) != 0 &&
        strncmp(status->host, "http://", strlen("http://")) != 0)
        return HUSH_ERR_ARG;
    if (strpbrk(status->host, "\r\n\t\"\\?#@") != NULL)
        return HUSH_ERR_ARG;
    size_t len = strlen(status->host);
    for (size_t i = 0; i < sizeof(status->host) && len > 0; ++i) {
        if (status->host[len - 1] != '/') break;
        --len;
    }
    int gemini = strcmp(status->id, HUSH_ROSTER_PROVIDER_GEMINI) == 0;
    const char *version = gemini ? "/v1beta" : "/v1";
    size_t version_len = strlen(version);
    if (len >= version_len && memcmp(status->host + len - version_len, version, version_len) == 0)
        version = "";
    const char *method = strcmp(status->id, HUSH_ROSTER_PROVIDER_ANTHROPIC) == 0
        ? "/messages" : "/chat/completions";
    char generation[HUSH_PROVIDER_MODEL_MAX + HUSH_INFERENCE_PATH_MAX] = {0};
    if (gemini) {
        if (strspn(status->model, HUSH_INFERENCE_MODEL_CHARS) != strlen(status->model))
            return HUSH_ERR_ARG;
        const char *model = status->model;
        if (strncmp(model, "models/", strlen("models/")) == 0) model += strlen("models/");
        int written = snprintf(generation, sizeof(generation), "/models/%s:generateContent", model);
        if (written < 0 || (size_t)written >= sizeof(generation)) return HUSH_ERR_FULL;
        method = generation;
    }
    int written = snprintf(out, outsz, "%.*s%s%s", (int)len, status->host, version, method);
    return written < 0 || (size_t)written >= outsz ? HUSH_ERR_FULL : HUSH_OK;
}

static hush_status_t hush_inference_config_value(FILE *file, const char *name, const char *value)
{
    assert(file != NULL);
    assert(name != NULL && value != NULL);
    size_t capacity = strlen(value) * HUSH_JSON_U_LEN + 1;
    char *escaped = calloc(capacity, 1);
    if (escaped == NULL)
        return HUSH_ERR_IO;
    (void)hush_json_escape(value, escaped, capacity);
    int written = fprintf(file, "%s = \"%s\"\n", name, escaped);
    free(escaped);
    return written < 0 ? HUSH_ERR_IO : HUSH_OK;
}

static hush_status_t hush_inference_configure(FILE *file, const hush_provider_status_t *status,
                                             const char *body)
{
    assert(file != NULL);
    assert(status != NULL && body != NULL);
    char url[HUSH_PROVIDER_URL_MAX] = {0};
    HUSH_TRY(hush_inference_endpoint(url, sizeof(url), status));
    char key[HUSH_PROVIDER_KEY_MAX] = {0};
    HUSH_TRY(hush_inference_load_key(key, sizeof(key), status->id));
    if (strpbrk(key, "\r\n") != NULL)
        return HUSH_ERR_ARG;
    HUSH_TRY(hush_inference_config_value(file, "url", url));
    HUSH_TRY(hush_inference_config_value(file, "header", "Content-Type: application/json"));
    const char *prefix = "Authorization: Bearer ";
    if (strcmp(status->id, HUSH_ROSTER_PROVIDER_GEMINI) == 0) prefix = "x-goog-api-key: ";
    if (strcmp(status->id, HUSH_ROSTER_PROVIDER_ANTHROPIC) == 0) prefix = "x-api-key: ";
    char header[HUSH_PROVIDER_KEY_MAX + HUSH_INFERENCE_PATH_MAX] = {0};
    int written = snprintf(header, sizeof(header), "%s%s", prefix, key);
    if (written < 0 || (size_t)written >= sizeof(header)) return HUSH_ERR_FULL;
    if (key[0] != '\0') HUSH_TRY(hush_inference_config_value(file, "header", header));
    if (strcmp(status->id, HUSH_ROSTER_PROVIDER_ANTHROPIC) == 0)
        HUSH_TRY(hush_inference_config_value(file, "header",
            "anthropic-version: " HUSH_INFERENCE_ANTHROPIC_VERSION));
    HUSH_TRY(hush_inference_config_value(file, "data", body));
    if (fflush(file) != 0 || fseek(file, 0, SEEK_SET) != 0)
        return HUSH_ERR_IO;
    return HUSH_OK;
}

static hush_status_t hush_inference_run_curl(FILE *configuration, FILE *response)
{
    assert(configuration != NULL);
    assert(response != NULL);
    /* This worker owns its curl child; the relay itself retains its SIGCHLD policy. */
    if (signal(SIGCHLD, SIG_DFL) == SIG_ERR)
        return HUSH_ERR_IO;
    pid_t child = fork();
    if (child < 0) return HUSH_ERR_IO;
    if (child == 0) {
        if (dup2(fileno(configuration), STDIN_FILENO) < 0 ||
            dup2(fileno(response), STDOUT_FILENO) < 0)
            _exit(HUSH_INFERENCE_EXEC_FAILURE);
        execlp("curl", "curl", "--disable", "--silent", "--fail", "--max-time",
               HUSH_INFERENCE_TIMEOUT, "--max-filesize", "131072", "--proto", "=http,https", "--config", "-", (char *)NULL);
        _exit(HUSH_INFERENCE_EXEC_FAILURE);
    }
    int status = 0;
    for (size_t i = 0; i < (size_t)HUSH_INFERENCE_WAIT_MAX; ++i) {
        pid_t result = waitpid(child, &status, 0);
        if (result == child)
            return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? HUSH_OK : HUSH_ERR_IO;
        if (errno != EINTR) return HUSH_ERR_IO;
    }
    if (kill(child, SIGKILL) != 0 && errno != ESRCH)
        return HUSH_ERR_IO;
    return HUSH_ERR_IO;
}

static hush_status_t hush_inference_read_response(FILE *file, char *out, size_t outsz)
{
    assert(file != NULL);
    assert(out != NULL && outsz > 0);
    if (fseek(file, 0, SEEK_SET) != 0)
        return HUSH_ERR_IO;
    size_t count = fread(out, 1, outsz - 1, file);
    out[count] = '\0';
    if (ferror(file)) return HUSH_ERR_IO;
    if (fgetc(file) != EOF) return HUSH_ERR_FULL;
    return count == 0 ? HUSH_ERR_PARSE : HUSH_OK;
}

static hush_status_t hush_inference_transport(char *out, size_t outsz,
                                             const hush_provider_status_t *status,
                                             const char *body)
{
    assert(out != NULL && status != NULL);
    assert(body != NULL);
    FILE *configuration = tmpfile();
    if (configuration == NULL) return HUSH_ERR_IO;
    FILE *response = tmpfile();
    if (response == NULL) {
        if (fclose(configuration) != 0) return HUSH_ERR_IO;
        return HUSH_ERR_IO;
    }
    hush_status_t result = hush_inference_configure(configuration, status, body);
    if (result == HUSH_OK) result = hush_inference_run_curl(configuration, response);
    if (result == HUSH_OK) result = hush_inference_read_response(response, out, outsz);
    if (fclose(response) != 0) result = HUSH_ERR_IO;
    if (fclose(configuration) != 0) result = HUSH_ERR_IO;
    return result;
}

static hush_status_t hush_inference_extract(char *out, size_t outsz, const char *provider,
                                           const char *response)
{
    assert(out != NULL && provider != NULL);
    assert(response != NULL);
    if (strcmp(provider, HUSH_ROSTER_PROVIDER_ANTHROPIC) == 0)
        return hush_inference_extract_parts(out, outsz, response, "/content");
    if (strcmp(provider, HUSH_ROSTER_PROVIDER_GEMINI) == 0)
        return hush_inference_extract_parts(out, outsz, response, "/candidates/0/content/parts");
    hush_json_value_t value = {0};
    HUSH_TRY(hush_json_lookup(&value, response, "/choices/0/message/content"));
    HUSH_TRY(hush_json_decode(out, outsz, &value));
    return out[0] == '\0' ? HUSH_ERR_PARSE : HUSH_OK;
}

static hush_status_t hush_inference_append_part(char *out, size_t outsz, const char *response,
                                               const char *path)
{
    assert(out != NULL && outsz > 0 && response != NULL && path != NULL);
    hush_json_value_t part = {0};
    HUSH_TRY(hush_json_lookup(&part, response, path));
    char field[HUSH_INFERENCE_PATH_MAX] = {0};
    int written = snprintf(field, sizeof(field), "%s/thought", path);
    if (written < 0 || (size_t)written >= sizeof(field)) return HUSH_ERR_FULL;
    hush_status_t status = hush_json_lookup(&part, response, field);
    if (status == HUSH_OK && part.len == strlen("true") &&
        memcmp(part.start, "true", part.len) == 0) return HUSH_OK;
    if (status != HUSH_OK && status != HUSH_ERR_NOT_FOUND) return status;
    written = snprintf(field, sizeof(field), "%s/text", path);
    if (written < 0 || (size_t)written >= sizeof(field)) return HUSH_ERR_FULL;
    status = hush_json_lookup(&part, response, field);
    if (status == HUSH_ERR_NOT_FOUND) return HUSH_OK;
    if (status != HUSH_OK) return status;
    size_t used = strlen(out);
    return hush_json_decode(out + used, outsz - used, &part);
}

static hush_status_t hush_inference_extract_parts(char *out, size_t outsz, const char *response,
                                                 const char *path)
{
    assert(out != NULL && outsz > 0 && response != NULL && path != NULL);
    out[0] = '\0';
    for (size_t i = 0; i <= (size_t)HUSH_INFERENCE_PARTS_MAX; ++i) {
        char part_path[HUSH_INFERENCE_PATH_MAX] = {0};
        int written = snprintf(part_path, sizeof(part_path), "%s/%zu", path, i);
        if (written < 0 || (size_t)written >= sizeof(part_path)) return HUSH_ERR_FULL;
        hush_status_t status = hush_inference_append_part(out, outsz, response, part_path);
        if (status == HUSH_ERR_NOT_FOUND) return out[0] == '\0' ? HUSH_ERR_PARSE : HUSH_OK;
        if (status != HUSH_OK) return status;
    }
    return HUSH_ERR_FULL;
}

/* Writes exactly len bytes, retrying short writes and EINTR. */
static hush_status_t hush_inference_write_all(int fd, const char *text, size_t len)
{
    size_t off = 0;

    assert(text != NULL);
    while (off < len) {
        ssize_t written = write(fd, text + off, len - off);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            return HUSH_ERR_IO;
        }
        off += (size_t)written;
    }
    return HUSH_OK;
}

/* Appends one streamed delta to out and, when asked, to the live stream. */
static hush_status_t hush_inference_take_delta(char *out, size_t outsz, int stream_fd,
                                               const char *provider, const char *payload,
                                               int *streamed)
{
    const char *path;
    hush_json_value_t value = {0};
    char text[HUSH_INFERENCE_TEXT_MAX + 1] = {0};
    size_t used;
    size_t len;

    assert(out != NULL);
    assert(provider != NULL);
    assert(payload != NULL);
    path = strcmp(provider, HUSH_ROSTER_PROVIDER_ANTHROPIC) == 0
        ? "/delta/text" : "/choices/0/delta/content";
    /* Keep-alives, role openers, usage tails and error events carry no text. */
    if (hush_json_lookup(&value, payload, path) != HUSH_OK ||
        hush_json_decode(text, sizeof(text), &value) != HUSH_OK)
        return HUSH_OK;
    len = strlen(text);
    if (len == 0)
        return HUSH_OK;
    used = strlen(out);
    if (used + len >= outsz)
        return HUSH_ERR_FULL;
    memcpy(out + used, text, len + 1);
    if (stream_fd >= 0 && hush_inference_write_all(stream_fd, text, len) != HUSH_OK)
        return HUSH_ERR_IO;
    if (streamed != NULL)
        *streamed = 1;
    return HUSH_OK;
}

/* Runs curl unbuffered, forwarding each SSE delta as it arrives. */
static hush_status_t hush_inference_stream_curl(FILE *configuration, const char *provider,
                                                char *out, size_t outsz, int stream_fd,
                                                int *streamed)
{
    int fds[2];
    pid_t child;
    FILE *response;
    char line[HUSH_INFERENCE_LINE_MAX];
    char raw[HUSH_INFERENCE_RESPONSE_MAX] = {0};
    size_t raw_n = 0;
    hush_status_t result = HUSH_OK;
    int status = 0;

    assert(configuration != NULL);
    assert(provider != NULL);
    assert(out != NULL && outsz > 0);
    if (pipe(fds) != 0)
        return HUSH_ERR_IO;
    if (signal(SIGCHLD, SIG_DFL) == SIG_ERR) {
        (void)close(fds[0]);
        (void)close(fds[1]);
        return HUSH_ERR_IO;
    }
    child = fork();
    if (child < 0) {
        (void)close(fds[0]);
        (void)close(fds[1]);
        return HUSH_ERR_IO;
    }
    if (child == 0) {
        if (close(fds[0]) != 0 ||
            dup2(fileno(configuration), STDIN_FILENO) < 0 ||
            dup2(fds[1], STDOUT_FILENO) < 0)
            _exit(HUSH_INFERENCE_EXEC_FAILURE);
        execlp("curl", "curl", "--disable", "--silent", "--fail", "--no-buffer",
               "--max-time", HUSH_INFERENCE_TIMEOUT, "--max-filesize", "131072",
               "--proto", "=http,https", "--config", "-", (char *)NULL);
        _exit(HUSH_INFERENCE_EXEC_FAILURE);
    }
    if (close(fds[1]) != 0)
        result = HUSH_ERR_IO;
    response = fdopen(fds[0], "r");
    if (response == NULL) {
        (void)close(fds[0]);
        result = HUSH_ERR_IO;
    }
    while (result == HUSH_OK && response != NULL &&
           fgets(line, sizeof(line), response) != NULL) {
        size_t len = strlen(line);

        if (raw_n + len < sizeof(raw)) {
            memcpy(raw + raw_n, line, len);
            raw_n += len;
            raw[raw_n] = '\0';
        }
        if (strncmp(line, "data: ", 6) != 0 || strncmp(line + 6, "[DONE]", 6) == 0)
            continue;
        result = hush_inference_take_delta(out, outsz, stream_fd, provider, line + 6,
                                           streamed);
    }
    if (response != NULL && fclose(response) != 0)
        result = HUSH_ERR_IO;
    if (waitpid(child, &status, 0) != child)
        result = HUSH_ERR_IO;
    else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        result = HUSH_ERR_IO;
    /* A provider that ignored stream:true answered with one whole body. */
    if (result == HUSH_OK && out[0] == '\0' && raw_n > 0)
        result = hush_inference_extract(out, outsz, provider, raw);
    return result;
}

/* Owns the anonymous curl configuration for one streamed request. */
static hush_status_t hush_inference_stream_transport(const hush_provider_status_t *status,
                                                     const char *body, char *out,
                                                     size_t outsz, int stream_fd,
                                                     int *streamed)
{
    FILE *configuration;
    hush_status_t result;

    assert(status != NULL);
    assert(body != NULL);
    configuration = tmpfile();
    if (configuration == NULL)
        return HUSH_ERR_IO;
    result = hush_inference_configure(configuration, status, body);
    if (result == HUSH_OK)
        result = hush_inference_stream_curl(configuration, status->id, out, outsz,
                                            stream_fd, streamed);
    if (fclose(configuration) != 0)
        result = HUSH_ERR_IO;
    return result;
}

hush_status_t hush_inference_stream(char *out, size_t outsz,
                                    const hush_inference_request_t *request,
                                    int stream_fd, int *streamed)
{
    hush_provider_status_t status = {0};
    hush_status_t result;
    hush_inference_encoding_t *encoding;

    if (out == NULL || outsz == 0 || request == NULL || request->provider == NULL ||
        request->system == NULL || request->rules == NULL || request->message == NULL)
        return HUSH_ERR_ARG;
    out[0] = '\0';
    if (streamed != NULL)
        *streamed = 0;
    result = hush_provider_status(&status, request->provider);
    if (result != HUSH_OK)
        return result;
    if (!hush_inference_is_ready(&status))
        return HUSH_ERR_DENIED;
    encoding = calloc(1, sizeof(*encoding));
    if (encoding == NULL)
        return HUSH_ERR_IO;
    /* Gemini answers a streamed call at a different endpoint; keep it whole. */
    encoding->stream = stream_fd >= 0 &&
        strcmp(status.id, HUSH_ROSTER_PROVIDER_GEMINI) != 0;
    result = hush_inference_encode(encoding, &status, request);
    if (result == HUSH_OK && encoding->stream) {
        result = hush_inference_stream_transport(&status, encoding->body, out, outsz,
                                                 stream_fd, streamed);
    } else if (result == HUSH_OK) {
        char response[HUSH_INFERENCE_RESPONSE_MAX] = {0};

        result = hush_inference_transport(response, sizeof(response), &status,
                                          encoding->body);
        if (result == HUSH_OK)
            result = hush_inference_extract(out, outsz, status.id, response);
    }
    free(encoding);
    return result;
}
