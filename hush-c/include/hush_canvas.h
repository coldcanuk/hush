/* hush_canvas.h: Fill-in-the-Middle jobs for the code canvas. */

#ifndef HUSH_CANVAS_H
#define HUSH_CANVAS_H

#include <stddef.h>

#include "hush_status.h"

enum {
    HUSH_CANVAS_PRED_MAX = 512,
    HUSH_CANVAS_TOKEN_MAX = 16
};

/* Zeros the single job slot. Safe to call twice. */
void hush_canvas_init(void);

/* Kills a live job and closes its pipe. Safe on an empty slot. */
void hush_canvas_shutdown(void);

/* Reaps the FIM child. Does not sleep. */
void hush_canvas_poll(void);

/* Starts a grok FIM job. Writes a token. A new start replaces any
 * prior job. prefix and suffix may be empty; both are copied.
 * Fails with HUSH_ERR_ARG or HUSH_ERR_IO. */
hush_status_t hush_canvas_start(char *token, size_t tokensz,
                                const char *prefix,
                                const char *suffix);

/* True when token names the in-flight job. */
int hush_canvas_is_busy(const char *token);

/* Copies a finished middle into out (at most HUSH_CANVAS_PRED_MAX).
 * HUSH_OK when ready, HUSH_ERR_NOT_FOUND while busy or unknown,
 * HUSH_ERR_IO on fail. */
hush_status_t hush_canvas_take(const char *token, char *out, size_t outsz);

#endif /* HUSH_CANVAS_H */
