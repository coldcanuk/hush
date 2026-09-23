/* hush_build.h: build-stamped version and commit for the main display. */

#ifndef HUSH_BUILD_H
#define HUSH_BUILD_H

/* Baked at build time by ./configure (config.mk -D flags) from git describe
 * and the short HEAD, so an installed binary reports its own build without
 * reading git at runtime. Fallbacks keep non-configure builds working. */
#ifndef HUSH_BUILD_VERSION
#define HUSH_BUILD_VERSION "0.0.1"
#endif
#ifndef HUSH_BUILD_SHA
#define HUSH_BUILD_SHA "unknown"
#endif

#endif /* HUSH_BUILD_H */
