/* hush_mem.c: one wipe helper for leftover secret copies. */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

/* Apple memset_s lives behind this feature macro. */
#ifndef __STDC_WANT_LIB_EXT1__
#define __STDC_WANT_LIB_EXT1__ 1
#endif

#include <string.h>

#include "hush_mem.h"

#if defined(__GLIBC__) || defined(__FreeBSD__) || defined(__OpenBSD__)
#define HUSH_HAVE_EXPLICIT_BZERO 1
#elif defined(__NetBSD__)
#define HUSH_HAVE_EXPLICIT_MEMSET 1
#elif defined(__APPLE__)
#define HUSH_HAVE_MEMSET_S 1
#endif

#if !defined(HUSH_HAVE_EXPLICIT_BZERO) \
    && !defined(HUSH_HAVE_EXPLICIT_MEMSET) \
    && !defined(HUSH_HAVE_MEMSET_S)
/* Reloaded on every call so a dead-store pass cannot drop the memset. */
static void *(*const volatile hush_memset_keep)(void *, int, size_t) = memset;
#endif

void hush_secure_zero(void *buf, size_t n)
{
    if (buf == NULL || n == 0)
        return;
#if defined(HUSH_HAVE_EXPLICIT_BZERO)
    explicit_bzero(buf, n);
#elif defined(HUSH_HAVE_EXPLICIT_MEMSET)
    (void)explicit_memset(buf, 0, n);
#elif defined(HUSH_HAVE_MEMSET_S)
    (void)memset_s(buf, n, 0, n);
#else
    (void)hush_memset_keep(buf, 0, n);
#endif
}
