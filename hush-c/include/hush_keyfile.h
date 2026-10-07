/* hush_keyfile.h: local AES-GCM vault for nsecs. Not a plaintext file. */

#ifndef HUSH_KEYFILE_H
#define HUSH_KEYFILE_H

#include <stddef.h>

#include "hush_status.h"

/* Unlock phrase. Unset or empty means the vault is off. */
#define HUSH_KEYFILE_ENV "HUSH_KEY_PASS"

/* Writes secret under path into $HUSH_HOME/keys.vault.
 * A missing phrase is HUSH_OK and writes nothing.
 * A phrase that does not fit is HUSH_ERR_ARG and writes nothing.
 * A phrase that cannot open an existing vault is HUSH_ERR_CRYPTO.
 * That failure leaves the file untouched. */
hush_status_t hush_keyfile_save(const char *path, const char *secret);

/* Reads path from the vault into out.
 * No file, or a missing path inside an open vault, is HUSH_ERR_NOT_FOUND.
 * A file that the current phrase cannot open is HUSH_ERR_CRYPTO. */
hush_status_t hush_keyfile_load(char *out, size_t outsz, const char *path);

#endif /* HUSH_KEYFILE_H */
