/* hush_win.c: X11 iconify, maximize, and Motif undecorate for the --app window. */

#include <assert.h>
#ifdef HUSH_HAVE_X11
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "hush_status.h"
#include "hush_win.h"

#ifdef HUSH_HAVE_X11

enum {
    HUSH_WIN_CLIENT_MAX = 256,
    HUSH_WIN_DESKTOP_MAX = 256,
    HUSH_WIN_PROTOCOL_MAX = 64,
    HUSH_WIN_MWM_DECOR  = 2,
    HUSH_WIN_MWM_BORDER = 1 << 1,
    HUSH_WIN_MWM_RESIZE = 1 << 2,
    HUSH_WIN_MWM_FIELDS = 5,
    HUSH_WIN_PROPERTY_BITS = 32,
    HUSH_WIN_NET_TOGGLE = 2,
    HUSH_WIN_NET_SOURCE = 1
};

#define HUSH_WIN_CLASS_NAME        "hush-relay"
#define HUSH_WIN_ATOM_NET_CLIENT   "_NET_CLIENT_LIST"
#define HUSH_WIN_ATOM_NET_STATE    "_NET_WM_STATE"
#define HUSH_WIN_ATOM_NET_MAX_VERT "_NET_WM_STATE_MAXIMIZED_VERT"
#define HUSH_WIN_ATOM_NET_MAX_HORZ "_NET_WM_STATE_MAXIMIZED_HORZ"
#define HUSH_WIN_ATOM_MOTIF        "_MOTIF_WM_HINTS"
#define HUSH_WIN_ATOM_RESIZE_SYNC  "_NET_WM_SYNC_REQUEST"
#define HUSH_WIN_DESKTOP_ENV       "XDG_CURRENT_DESKTOP"
#define HUSH_WIN_COSMIC_DESKTOP    "COSMIC"

typedef struct {
    unsigned long flags;
    unsigned long functions;
    unsigned long decorations;
    long input_mode;
    unsigned long status;
} hush_win_mwm_t;

/* Opens $DISPLAY. Caller XCloseDisplay. NULL when no display. */
static Display *hush_win_open(void);

/* Writes the first matching client into *out. Fails HUSH_ERR_NOT_FOUND. */
static hush_status_t hush_win_find(Display *dpy, Window *out);

/* Reads _NET_CLIENT_LIST into wins[0..*out_n). Bounded. */
static hush_status_t hush_win_list(Display *dpy, Window *wins, size_t *out_n);

/* True when WM_CLASS instance or class is hush-relay. */
static int hush_win_class_is_ours(Display *dpy, Window win);

/* Iconify win. */
static hush_status_t hush_win_do_min(Display *dpy, Window win);

/* Toggle maximized vert+horz. */
static hush_status_t hush_win_do_max(Display *dpy, Window win);

/* Requests border/resize decorations on borrowed non-NULL display and nonzero window.
 * HUSH_ERR_IO when Xlib cannot queue the property change. */
static hush_status_t hush_win_do_bare(Display *display, Window window);

/* True when the colon-separated desktop environment contains the COSMIC token.
 * Missing or unrecognized environment is false. No failure or ownership transfer. */
static bool hush_win_is_cosmic_session(void);

/* Removes the optional resize handshake on borrowed display/window.
 * Owns the Xlib protocol list until release. Propagates IO/FULL status. */
static hush_status_t hush_win_disable_resize_sync(Display *display, Window window);

/* Rewrites borrowed protocols for non-NULL display/nonzero window.
 * protocols may be NULL only at count zero. Fails FULL above the bounded limit. */
static hush_status_t hush_win_rewrite_protocols(Display *display, Window window,
                                               Atom *protocols, size_t count);

/* Compacts borrowed protocols in place; returns the retained count, no failure.
 * protocols may be NULL only at count zero; count is at most PROTOCOL_MAX. */
static size_t hush_win_filter_protocols(Atom *protocols, size_t count, Atom excluded);

/* Adapter queues borrowed protocols for non-NULL display/nonzero window.
 * count fits PROTOCOL_MAX; protocols may be NULL at zero. Fails HUSH_ERR_IO. */
static hush_status_t hush_win_store_protocols(Display *display, Window window,
                                             Atom *protocols, size_t count);

/* Sends one _NET_WM_STATE client message with two atoms. */
static void hush_win_send_state(Display *dpy, Window win, Atom first, Atom second);

hush_status_t hush_win_minimize(void)
{
    Display *dpy;
    Window win;
    hush_status_t st;

    dpy = hush_win_open();
    if (dpy == NULL)
        return HUSH_ERR_IO;
    st = hush_win_find(dpy, &win);
    if (st == HUSH_OK)
        st = hush_win_do_min(dpy, win);
    XCloseDisplay(dpy);
    return st;
}

hush_status_t hush_win_maximize(void)
{
    Display *dpy;
    Window win;
    hush_status_t st;

    dpy = hush_win_open();
    if (dpy == NULL)
        return HUSH_ERR_IO;
    st = hush_win_find(dpy, &win);
    if (st == HUSH_OK)
        st = hush_win_do_max(dpy, win);
    XCloseDisplay(dpy);
    return st;
}

hush_status_t hush_win_undecorate(void)
{
    Display *display = hush_win_open();
    if (display == NULL)
        return HUSH_ERR_IO;

    Window window = None;
    hush_status_t status = hush_win_find(display, &window);
    if (status == HUSH_OK)
        status = hush_win_do_bare(display, window);
    if (status == HUSH_OK && hush_win_is_cosmic_session())
        status = hush_win_disable_resize_sync(display, window);
    XCloseDisplay(display);
    return status;
}

static Display *hush_win_open(void)
{
    return XOpenDisplay(NULL);
}

static hush_status_t hush_win_find(Display *dpy, Window *out)
{
    Window wins[HUSH_WIN_CLIENT_MAX];
    size_t n = 0;
    size_t i;
    hush_status_t st;

    assert(dpy != NULL);
    assert(out != NULL);
    st = hush_win_list(dpy, wins, &n);
    if (st != HUSH_OK)
        return st;
    for (i = 0; i < n; ++i) {
        if (!hush_win_class_is_ours(dpy, wins[i]))
            continue;
        *out = wins[i];
        return HUSH_OK;
    }
    return HUSH_ERR_NOT_FOUND;
}

static hush_status_t hush_win_list(Display *dpy, Window *wins, size_t *out_n)
{
    Atom atom;
    Atom actual = None;
    int format = 0;
    unsigned long nitem = 0;
    unsigned long bytes = 0;
    unsigned char *prop = NULL;
    unsigned long i;
    unsigned long take;

    assert(dpy != NULL);
    assert(wins != NULL);
    assert(out_n != NULL);
    atom = XInternAtom(dpy, HUSH_WIN_ATOM_NET_CLIENT, True);
    if (atom == None)
        return HUSH_ERR_NOT_FOUND;
    if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), atom, 0L,
                           (long)HUSH_WIN_CLIENT_MAX, False, XA_WINDOW,
                           &actual, &format, &nitem, &bytes, &prop) != Success)
        return HUSH_ERR_IO;
    if (prop == NULL || nitem == 0) {
        if (prop != NULL)
            XFree(prop);
        return HUSH_ERR_NOT_FOUND;
    }
    take = nitem;
    if (take > (unsigned long)HUSH_WIN_CLIENT_MAX)
        take = (unsigned long)HUSH_WIN_CLIENT_MAX;
    for (i = 0; i < take; ++i)
        wins[i] = ((Window *)(void *)prop)[i];
    *out_n = (size_t)take;
    XFree(prop);
    return HUSH_OK;
}

static int hush_win_class_is_ours(Display *dpy, Window win)
{
    XClassHint hint;
    int ok = 0;

    assert(dpy != NULL);
    memset(&hint, 0, sizeof(hint));
    if (XGetClassHint(dpy, win, &hint) == 0)
        return 0;
    if (hint.res_class != NULL && strcmp(hint.res_class, HUSH_WIN_CLASS_NAME) == 0)
        ok = 1;
    if (hint.res_name != NULL && strcmp(hint.res_name, HUSH_WIN_CLASS_NAME) == 0)
        ok = 1;
    if (hint.res_name != NULL)
        XFree(hint.res_name);
    if (hint.res_class != NULL)
        XFree(hint.res_class);
    return ok;
}

static hush_status_t hush_win_do_min(Display *dpy, Window win)
{
    assert(dpy != NULL);
    if (XIconifyWindow(dpy, win, DefaultScreen(dpy)) == 0)
        return HUSH_ERR_IO;
    XFlush(dpy);
    return HUSH_OK;
}

static hush_status_t hush_win_do_max(Display *dpy, Window win)
{
    Atom vert;
    Atom horz;

    assert(dpy != NULL);
    vert = XInternAtom(dpy, HUSH_WIN_ATOM_NET_MAX_VERT, False);
    horz = XInternAtom(dpy, HUSH_WIN_ATOM_NET_MAX_HORZ, False);
    hush_win_send_state(dpy, win, vert, horz);
    return HUSH_OK;
}

static hush_status_t hush_win_do_bare(Display *display, Window window)
{
    assert(display != NULL);
    assert(window != None);
    hush_win_mwm_t hints = {
        .flags = HUSH_WIN_MWM_DECOR,
        .decorations = HUSH_WIN_MWM_BORDER | HUSH_WIN_MWM_RESIZE
    };
    Atom atom = XInternAtom(display, HUSH_WIN_ATOM_MOTIF, False);
    if (XChangeProperty(display, window, atom, atom, HUSH_WIN_PROPERTY_BITS, PropModeReplace,
                        (unsigned char *)&hints, HUSH_WIN_MWM_FIELDS) == 0)
        return HUSH_ERR_IO;
    XFlush(display);
    return HUSH_OK;
}

static bool hush_win_is_cosmic_session(void)
{
    const char *desktop = getenv(HUSH_WIN_DESKTOP_ENV);
    if (desktop == NULL)
        return false;

    const size_t token_len = sizeof(HUSH_WIN_COSMIC_DESKTOP) - 1;
    for (size_t idx = 0; idx < HUSH_WIN_DESKTOP_MAX; ++idx) {
        if (desktop[idx] == '\0')
            return false;
        if (idx > 0 && desktop[idx - 1] != ':')
            continue;
        if (strncmp(desktop + idx, HUSH_WIN_COSMIC_DESKTOP, token_len) != 0)
            continue;
        if (desktop[idx + token_len] == '\0' || desktop[idx + token_len] == ':')
            return true;
    }
    return false;
}

static hush_status_t hush_win_disable_resize_sync(Display *display, Window window)
{
    assert(display != NULL);
    assert(window != None);
    Atom *protocols = NULL;
    /* XGetWMProtocols requires an int output for the list length. */
    int count = 0;
    if (XGetWMProtocols(display, window, &protocols, &count) == 0)
        return HUSH_OK; /* No valid protocol list means no advertised handshake. */

    assert(count >= 0);
    hush_status_t status = hush_win_rewrite_protocols(display, window, protocols, (size_t)count);
    XFree(protocols);
    return status;
}

static hush_status_t hush_win_rewrite_protocols(Display *display, Window window,
                                               Atom *protocols, size_t count)
{
    assert(display != NULL);
    assert(window != None);
    assert(protocols != NULL || count == 0);
    if (count > HUSH_WIN_PROTOCOL_MAX)
        return HUSH_ERR_FULL;

    /* COSMIC/Xwayland can stall on Chromium's resize acknowledgements after
     * the first drag. Keep ordinary asynchronous resizing for this session;
     * leave close, ping, custom protocols and the browser's counter intact. */
    Atom excluded = XInternAtom(display, HUSH_WIN_ATOM_RESIZE_SYNC, True);
    if (excluded == None)
        return HUSH_OK;
    size_t retained = hush_win_filter_protocols(protocols, count, excluded);
    if (retained == count)
        return HUSH_OK;
    return hush_win_store_protocols(display, window, protocols, retained);
}

static size_t hush_win_filter_protocols(Atom *protocols, size_t count, Atom excluded)
{
    assert(protocols != NULL || count == 0);
    assert(count <= HUSH_WIN_PROTOCOL_MAX);
    size_t retained = 0;
    for (size_t idx = 0; idx < HUSH_WIN_PROTOCOL_MAX && idx < count; ++idx) {
        if (protocols[idx] == excluded)
            continue;
        protocols[retained] = protocols[idx];
        ++retained;
    }
    assert(retained <= count);
    return retained;
}

static hush_status_t hush_win_store_protocols(Display *display, Window window,
                                             Atom *protocols, size_t count)
{
    assert(display != NULL);
    assert(window != None);
    assert(count <= HUSH_WIN_PROTOCOL_MAX);
    assert(protocols != NULL || count == 0);
    /* XSetWMProtocols requires an int length; the bounded count fits. */
    if (XSetWMProtocols(display, window, protocols, (int)count) == 0)
        return HUSH_ERR_IO;
    return HUSH_OK;
}

static void hush_win_send_state(Display *dpy, Window win, Atom first, Atom second)
{
    XEvent ev;
    Atom state;
    long mask;

    assert(dpy != NULL);
    memset(&ev, 0, sizeof(ev));
    state = XInternAtom(dpy, HUSH_WIN_ATOM_NET_STATE, False);
    ev.xclient.type = ClientMessage;
    ev.xclient.window = win;
    ev.xclient.message_type = state;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = HUSH_WIN_NET_TOGGLE;
    ev.xclient.data.l[1] = (long)first;
    ev.xclient.data.l[2] = (long)second;
    ev.xclient.data.l[3] = HUSH_WIN_NET_SOURCE;
    mask = SubstructureRedirectMask | SubstructureNotifyMask;
    XSendEvent(dpy, DefaultRootWindow(dpy), False, mask, &ev);
    XFlush(dpy);
}

#else /* !HUSH_HAVE_X11 */

hush_status_t hush_win_minimize(void)
{
    return HUSH_ERR_IO;
}

hush_status_t hush_win_maximize(void)
{
    return HUSH_ERR_IO;
}

hush_status_t hush_win_undecorate(void)
{
    return HUSH_ERR_IO;
}

#endif /* HUSH_HAVE_X11 */
