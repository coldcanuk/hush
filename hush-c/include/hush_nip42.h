/* hush_nip42.h: NIP-42 AUTH-event validation for wire connections. */

#ifndef HUSH_NIP42_H
#define HUSH_NIP42_H

#include <time.h>

#include "hush_event.h"
#include "hush_status.h"

enum {
    HUSH_NIP42_KIND_AUTH = 22242,
    HUSH_NIP42_CREATED_WINDOW_S = 600,
    HUSH_NIP42_RELAY_HOST_MAX = 64
};

/* One AUTH validation request. Every pointer is borrowed, required, and
 * stays valid for the call. challenge is the per-connection challenge the
 * event must echo; bind_addr is the configured listen address or "". */
typedef struct {
    const hush_event_t *event;
    time_t now;
    const char *challenge;
    const char *bind_addr;
} hush_nip42_request_t;

/* Validates a kind-22242 AUTH event: freshness, challenge and relay tags,
 * then the NIP-01 id and BIP-340 signature via hush_event_verify. reason,
 * when non-NULL, receives a short "invalid: ..." text on HUSH_ERR_DENIED.
 * Returns HUSH_OK when authentic, HUSH_ERR_DENIED when not, HUSH_ERR_ARG
 * on NULL, and HUSH_ERR_CRYPTO on an internal failure. */
hush_status_t hush_nip42_validate(const hush_nip42_request_t *request,
                                  char *reason, size_t reason_len);

/* True when the host part of a relay URL matches the loopback set or the
 * configured bind address. Strips ws://, wss://, http://, https:// and any
 * path. A NULL or empty URL is false. */
int hush_nip42_relay_host_ok(const char *relay_url, const char *bind_addr);

#endif /* HUSH_NIP42_H */
