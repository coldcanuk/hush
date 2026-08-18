/* hush_pass.h: unix `pass` adapter for Hush secrets. */

#ifndef HUSH_PASS_H
#define HUSH_PASS_H

#include <stddef.h>
#include "hush_status.h"

enum {
    HUSH_PASS_PATH_MAX = 128,
    HUSH_PASS_SECRET_MAX = 256,
    HUSH_PASS_CMD_MAX = 512,
    HUSH_PASS_ERR_MAX = 160
};

#define HUSH_PASS_IDENTITY_NSEC "identity/nsec"
#define HUSH_PASS_PAYNE_NSEC "agents/sgt-major-payne/nsec"
#define HUSH_PASS_SHOW_IDENTITY "pass show hush/identity/nsec"
#define HUSH_PASS_SHOW_PAYNE "pass show hush/agents/sgt-major-payne/nsec"

/* Overrides the helper used by save/get/has. Empty or NULL restores default.
 * cmd is borrowed; the caller keeps ownership. */
void hush_pass_set_helper(const char *cmd);

/* Copies the last helper error into out. Empty when the last call succeeded.
 * out may be NULL. */
void hush_pass_last_error(char *out, size_t outsz);

/* Writes secret at hush/<path> via the helper. Fails HUSH_ERR_ARG or
 * HUSH_ERR_IO. Never puts secret on argv. */
hush_status_t hush_pass_save(const char *path, const char *secret);

/* Reads hush/<path> into out. Fails HUSH_ERR_ARG, HUSH_ERR_IO, or
 * HUSH_ERR_NOT_FOUND. */
hush_status_t hush_pass_get(char *out, size_t outsz, const char *path);

/* True when hush/<path> can be shown. */
int hush_pass_has(const char *path);

#endif /* HUSH_PASS_H */
