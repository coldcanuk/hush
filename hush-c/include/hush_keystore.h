/* hush_keystore.h: unlock private keys from pass, 1Password, or secret-tool. */

#ifndef HUSH_KEYSTORE_H
#define HUSH_KEYSTORE_H

#include <stddef.h>

#include "hush_identity.h"
#include "hush_status.h"

typedef enum {
    HUSH_KEYSTORE_NONE = 0,
    HUSH_KEYSTORE_PASS = 1,
    HUSH_KEYSTORE_OP = 2,
    HUSH_KEYSTORE_SECRET = 3
} hush_keystore_kind;

/* Test double for the `op` binary. Unset uses `op` on PATH. */
#define HUSH_KEYSTORE_ENV_OP "HUSH_OP_HELPER"

/* Test double for the `secret-tool` binary. Unset uses `secret-tool` on PATH. */
#define HUSH_KEYSTORE_ENV_SECRET "HUSH_SECRET_TOOL_HELPER"

/* 1Password vault. Unset uses the vault named Hush. */
#define HUSH_KEYSTORE_ENV_VAULT "HUSH_OP_VAULT"

/* True when kind can be executed. A pass helper override keeps live
 * `op` or `secret-tool` from running unless that helper env is set.
 * Runs nothing. */
int hush_keystore_ready(hush_keystore_kind kind);

/* Writes secret into one store. Never writes a hush-home file.
 * Fails HUSH_ERR_ARG or HUSH_ERR_IO. Secret stays off argv. */
hush_status_t hush_keystore_save(hush_keystore_kind kind, const char *path,
                                 const char *secret);

/* Offers secret to one ready store. Uses pass when use_pass is set.
 * Otherwise tries op, then secret-tool. A miss leaves the secret
 * in the caller's memory. Never writes a plain file. */
void hush_keystore_offer(int use_pass, const char *path, const char *secret);

/* Removes path from every ready store (pass, op, secret-tool).
 * A missing entry in a store is OK. Tries every ready store even when
 * one fails. Returns HUSH_OK when every attempt succeeded or found
 * nothing; HUSH_ERR_IO when at least one ready store failed a remove;
 * HUSH_ERR_ARG on a bad path. */
hush_status_t hush_keystore_remove(const char *path);

/* Removes path from one store. Missing is HUSH_OK. Not ready is
 * HUSH_OK (nothing to clear). Fails HUSH_ERR_ARG or HUSH_ERR_IO. */
hush_status_t hush_keystore_remove_kind(hush_keystore_kind kind,
                                        const char *path);

/* Reads one store into out. A missing tool is HUSH_ERR_NOT_FOUND.
 * Also fails HUSH_ERR_ARG or HUSH_ERR_IO. */
hush_status_t hush_keystore_load_kind(hush_keystore_kind kind, char *out,
                                     size_t outsz, const char *path);

/* Reads pass, then 1Password, then secret-tool. First hit wins.
 * A miss is HUSH_ERR_NOT_FOUND. Does not mint a key. */
hush_status_t hush_keystore_load(char *out, size_t outsz, const char *path);

/* Like hush_keystore_load. found receives the store that hit.
 * found may be NULL when the caller does not need the kind. */
hush_status_t hush_keystore_load_from(hush_keystore_kind *found, char *out,
                                     size_t outsz, const char *path);

/* Imports the first stored secret into id. On a miss, clears id.
 * Does not generate a replacement identity. */
hush_status_t hush_keystore_import(hush_identity_t *id, const char *path);

#endif /* HUSH_KEYSTORE_H */
