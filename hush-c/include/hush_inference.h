/* hush_inference.h: bounded text generation through configured HTTP providers. */
#ifndef HUSH_INFERENCE_H
#define HUSH_INFERENCE_H

#include <stddef.h>

#include "hush_provider.h"
#include "hush_status.h"

enum {
    /* Largest provider answer the extractor will assemble. Callers that store
     * into a smaller field must truncate, not fail the job. */
    HUSH_INFERENCE_TEXT_MAX = 32768
};

/* All strings are borrowed, required, and NUL-terminated. No secret is supplied
 * by callers: credentials are retrieved from the provider's pass namespace. */
typedef struct {
    const char *provider;
    const char *system;
    const char *rules;
    const char *message;
} hush_inference_request_t;

/* True for HTTP text providers with an implemented wire protocol. */
int hush_inference_is_api(const char *provider);

/* True when required borrowed status has a host, model, and credentials
 * (credentials are optional only for a Custom endpoint). */
int hush_inference_is_ready(const hush_provider_status_t *status);

/* Generates text in a worker process. Required output/request; no secrets in
 * argv/logs. Returns ARG, IO, PARSE, FULL, or DENIED; never substitutes providers.
 * Blocking curl is isolated from the relay's poll loop by the agent worker. */
hush_status_t hush_inference_reply(char *out, size_t outsz,
                                    const hush_inference_request_t *request);

/* Generates text like hush_inference_reply while forwarding every provider
 * delta to stream_fd as it arrives (pass -1 for buffered only). Providers
 * without a streaming wire format ignore the request and answer whole.
 * When a delta was forwarded, *streamed is set to 1 so the caller does not
 * repeat the text; streamed may be NULL. */
hush_status_t hush_inference_stream(char *out, size_t outsz,
                                    const hush_inference_request_t *request,
                                    int stream_fd, int *streamed);

#endif
