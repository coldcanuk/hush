#ifndef HUSH_CODEX_H
#define HUSH_CODEX_H

#include "hush_status.h"

/* Exposes the installed C skill under a borrowed working directory.
 * Rejects NULL/empty cwd with HUSH_ERR_ARG; filesystem failures return HUSH_ERR_IO.
 * Creates directories and a symlink, without replacing existing content. */
hush_status_t hush_codex_prepare_skills(const char *cwd);

#endif /* HUSH_CODEX_H */
