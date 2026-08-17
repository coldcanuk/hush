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
    HUSH_ERR_IO = -6
} hush_status_t;

#endif /* HUSH_STATUS_H */
