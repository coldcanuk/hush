/* hush_mem.h: wipe bytes the compiler is not allowed to delete. */

#ifndef HUSH_MEM_H
#define HUSH_MEM_H

#include <stddef.h>

/* Overwrites n bytes at buf. buf may be NULL only when n is 0. */
void hush_secure_zero(void *buf, size_t n);

#endif /* HUSH_MEM_H */
