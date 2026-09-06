/* hush_inference.h: bounded text generation through configured HTTP providers. */
#ifndef HUSH_INFERENCE_H
#define HUSH_INFERENCE_H

#include <stddef.h>

#include "hush_provider.h"
#include "hush_status.h"

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

#endif
