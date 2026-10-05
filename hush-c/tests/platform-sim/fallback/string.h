/* Fallback-only: plain memset. */
#ifndef HUSH_SIM_FALLBACK_STRING_H
#define HUSH_SIM_FALLBACK_STRING_H
#include <stddef.h>
void *memset(void *, int, size_t);
#endif
