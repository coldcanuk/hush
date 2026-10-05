/* NetBSD-like: explicit_memset yes, explicit_bzero no. */
#ifndef HUSH_SIM_NETBSD_STRING_H
#define HUSH_SIM_NETBSD_STRING_H
#include <stddef.h>
void *memset(void *, int, size_t);
void *memset_explicit(void *, int, size_t);
void *explicit_memset(void *, int, size_t);
#endif
