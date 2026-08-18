/* hush_identity.h: Nostr secp256k1 identity generate/import for Hush. */

#ifndef HUSH_IDENTITY_H
#define HUSH_IDENTITY_H

#include <stddef.h>
#include "hush_bech32.h"
#include "hush_status.h"

enum {
    HUSH_IDENTITY_HEX_LEN = 64,
    HUSH_IDENTITY_NSEC_MAX = HUSH_BECH32_TEXT_MAX,
    HUSH_IDENTITY_NPUB_MAX = HUSH_BECH32_TEXT_MAX
};

typedef struct {
    unsigned char seckey[HUSH_BECH32_DATA_LEN];
    unsigned char pubkey[HUSH_BECH32_DATA_LEN];
    char nsec[HUSH_IDENTITY_NSEC_MAX];
    char npub[HUSH_IDENTITY_NPUB_MAX];
    char pubkey_hex[HUSH_IDENTITY_HEX_LEN + 1];
} hush_identity_t;

/* Fills id with a fresh secp256k1 keypair. Fails HUSH_ERR_CRYPTO. */
hush_status_t hush_identity_generate(hush_identity_t *id);

/* Accepts nsec1… or 64-char hex. Fails HUSH_ERR_PARSE or HUSH_ERR_CRYPTO. */
hush_status_t hush_identity_import(hush_identity_t *id, const char *secret);

/* Overwrites secret material in id. Safe on NULL. */
void hush_identity_clear(hush_identity_t *id);

#endif /* HUSH_IDENTITY_H */
