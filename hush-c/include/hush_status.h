/* hush_status.h: common status codes for Hush modules. */

#ifndef HUSH_STATUS_H
#define HUSH_STATUS_H

typedef enum {
    HUSH_OK = 0,
    HUSH_ERR_ARG = -1,
    HUSH_ERR_PARSE = -2,
    HUSH_ERR_FULL = -3,
    HUSH_ERR_NOT_FOUND = -4,
    HUSH_ERR_CRYPTO = -5,
    HUSH_ERR_IO = -6,
    HUSH_ERR_DENIED = -7
} hush_status_t;

/* Propagates any non-OK status to the caller.
 * Permitted ONLY in functions that acquire no resources.
 * Sole macro allowed to contain return in this style. */
#define HUSH_TRY(expr)                          \
    do {                                        \
        hush_status_t hush_try_s_ = (expr);     \
        if (hush_try_s_ != HUSH_OK)             \
            return hush_try_s_;                 \
    } while (0)

#endif /* HUSH_STATUS_H */
