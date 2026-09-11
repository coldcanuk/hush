/* hush_schnorr.h: BIP-340 schnorr verification for wire events. */

#ifndef HUSH_SCHNORR_H
#define HUSH_SCHNORR_H

#include <stddef.h>

#include "hush_status.h"

enum {
    HUSH_SCHNORR_PUBKEY_BYTES = 32,
    HUSH_SCHNORR_SIGNATURE_BYTES = 64
};

/* One verification request. Every pointer is borrowed, required, and stays
 * valid for the call: pubkey is 32 x-only bytes, signature is the 64-byte
 * R.x || s encoding, and message is the event id (32 bytes on the wire). */
typedef struct {
    const unsigned char *pubkey;
    const unsigned char *message;
    size_t message_len;
    const unsigned char *signature;
} hush_schnorr_request_t;

/* Verifies a BIP-340 signature. Returns HUSH_OK when valid, HUSH_ERR_DENIED
 * when the key, signature, or point equation is invalid or non-canonical,
 * HUSH_ERR_ARG on NULL, and HUSH_ERR_CRYPTO on an internal failure. */
hush_status_t hush_schnorr_verify(const hush_schnorr_request_t *request);

#endif /* HUSH_SCHNORR_H */
