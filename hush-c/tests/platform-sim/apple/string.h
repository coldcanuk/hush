/* Apple-like: memset_s yes, explicit_bzero no. */
#ifndef HUSH_SIM_APPLE_STRING_H
#define HUSH_SIM_APPLE_STRING_H
#include <stddef.h>
typedef int errno_t;
typedef size_t rsize_t;
void *memset(void *, int, size_t);
errno_t memset_s(void *s, rsize_t smax, int c, rsize_t n);
#endif
