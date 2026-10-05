/* hush_mem.c: one wipe helper for leftover secret copies. */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <string.h>

#include "hush_mem.h"

#if defined(__GLIBC__) || defined(__FreeBSD__) || defined(__OpenBSD__) \
    || defined(__NetBSD__) || defined(__APPLE__)
#define HUSH_HAVE_EXPLICIT_BZERO 1
#endif

#if !defined(HUSH_HAVE_EXPLICIT_BZERO)
/* Reloaded on every call so a dead-store pass cannot drop the memset. */
static void *(*const volatile hush_memset_keep)(void *, int, size_t) = memset;
#endif

void hush_secure_zero(void *buf, size_t n)
{
    if (buf == NULL || n == 0)
        return;
#if defined(HUSH_HAVE_EXPLICIT_BZERO)
    explicit_bzero(buf, n);
#else
    (void)hush_memset_keep(buf, 0, n);
#endif
}
