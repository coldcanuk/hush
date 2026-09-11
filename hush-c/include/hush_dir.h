/* hush_dir.h: shared private-directory creation for state and cwd owners. */

#ifndef HUSH_DIR_H
#define HUSH_DIR_H

#include "hush_status.h"

enum {
    HUSH_DIR_MODE_PRIVATE = 0700
};

/* Creates path 0700 when missing. When it already exists, requires a directory
 * owned by the current user and not reached through a symlink; anything else
 * fails HUSH_ERR_DENIED. NULL or empty fails HUSH_ERR_ARG. */
hush_status_t hush_dir_ensure_private(const char *path);

#endif /* HUSH_DIR_H */
