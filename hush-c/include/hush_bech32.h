/* hush_bech32.h: NIP-19 bech32 encode/decode for 32-byte keys. */

#ifndef HUSH_BECH32_H
#define HUSH_BECH32_H

#include <stddef.h>
#include "hush_status.h"

enum {
    HUSH_BECH32_HRP_MAX = 8,
    HUSH_BECH32_DATA_LEN = 32,
    HUSH_BECH32_TEXT_MAX = 128
};

#define HUSH_BECH32_HRP_NPUB "npub"
#define HUSH_BECH32_HRP_NSEC "nsec"

/* Writes NUL-terminated bech32 (hrp + 32-byte payload) to out.
 * Fails HUSH_ERR_ARG on NULL, empty hrp, or a buffer smaller than needed. */
hush_status_t hush_bech32_encode(char *out, size_t outsz,
                                 const char *hrp, const unsigned char *data32);

/* Writes 32 payload bytes and a NUL-terminated HRP.
 * Fails HUSH_ERR_PARSE on a bad checksum or length. */
hush_status_t hush_bech32_decode(unsigned char *out32,
                                 char *hrp_out, size_t hrp_outsz,
                                 const char *text);

#endif /* HUSH_BECH32_H */
