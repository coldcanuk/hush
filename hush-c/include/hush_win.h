/* hush_win.h: optional X11 controls for the hush-relay --app window. */

#ifndef HUSH_WIN_H
#define HUSH_WIN_H

#include "hush_status.h"

/* Iconify the hush-relay --app window. HUSH_ERR_IO without X11 or DISPLAY.
 * HUSH_ERR_NOT_FOUND when no matching window. */
hush_status_t hush_win_minimize(void);

/* Toggle WM maximized (vert+horz) on that window. Same errors. */
hush_status_t hush_win_maximize(void);

/* Request Motif border/resize handles without the title bar. On COSMIC, disable
 * the optional X11 resize-sync handshake that stalls successive native drags.
 * Same errors; HUSH_ERR_FULL for an oversized protocol list. */
hush_status_t hush_win_undecorate(void);

#endif /* HUSH_WIN_H */
