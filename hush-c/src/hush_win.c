/* hush_win.c: X11 iconify, maximize, and Motif undecorate for the --app window. */

#include <assert.h>
#ifdef HUSH_HAVE_X11
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif
#include <string.h>

#include "hush_status.h"
#include "hush_win.h"

#ifdef HUSH_HAVE_X11

enum {
    HUSH_WIN_CLIENT_MAX = 256,
    HUSH_WIN_MWM_DECOR  = 2,
    HUSH_WIN_MWM_FIELDS = 5,
    HUSH_WIN_NET_TOGGLE = 2,
    HUSH_WIN_NET_SOURCE = 1
};

#define HUSH_WIN_CLASS_NAME        "hush-relay"
#define HUSH_WIN_ATOM_NET_CLIENT   "_NET_CLIENT_LIST"
#define HUSH_WIN_ATOM_NET_STATE    "_NET_WM_STATE"
#define HUSH_WIN_ATOM_NET_MAX_VERT "_NET_WM_STATE_MAXIMIZED_VERT"
#define HUSH_WIN_ATOM_NET_MAX_HORZ "_NET_WM_STATE_MAXIMIZED_HORZ"
#define HUSH_WIN_ATOM_MOTIF        "_MOTIF_WM_HINTS"

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

/* Motif decorations = 0. */
static hush_status_t hush_win_do_bare(Display *dpy, Window win);

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
    Display *dpy;
    Window win;
    hush_status_t st;

    dpy = hush_win_open();
    if (dpy == NULL)
        return HUSH_ERR_IO;
    st = hush_win_find(dpy, &win);
    if (st == HUSH_OK)
        st = hush_win_do_bare(dpy, win);
    XCloseDisplay(dpy);
    return st;
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

static hush_status_t hush_win_do_bare(Display *dpy, Window win)
{
    hush_win_mwm_t mwm;
    Atom atom;

    assert(dpy != NULL);
    memset(&mwm, 0, sizeof(mwm));
    mwm.flags = (unsigned long)HUSH_WIN_MWM_DECOR;
    mwm.decorations = 0;
    atom = XInternAtom(dpy, HUSH_WIN_ATOM_MOTIF, False);
    XChangeProperty(dpy, win, atom, atom, 32, PropModeReplace,
                    (unsigned char *)&mwm, HUSH_WIN_MWM_FIELDS);
    XFlush(dpy);
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
