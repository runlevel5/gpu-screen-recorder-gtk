#include "tray.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRAY_W 22
#define TRAY_H 22

/* _NET_SYSTEM_TRAY opcodes — freedesktop.org System Tray Spec. */
#define SYSTEM_TRAY_REQUEST_DOCK   0

/* XEmbed protocol bits. */
#define XEMBED_VERSION             0
#define XEMBED_MAPPED              (1 << 0)
#define XEMBED_EMBEDDED_NOTIFY     0

#define TRAY_POLL_INTERVAL_MS      80

struct Tray {
    XtAppContext   app;
    Display       *display;
    int            screen;
    Window         root;
    Window         window;
    GC             gc;

    /* Atoms — interned once. */
    Atom selection_atom;     /* _NET_SYSTEM_TRAY_S<screen> */
    Atom opcode_atom;        /* _NET_SYSTEM_TRAY_OPCODE   */
    Atom message_data_atom;  /* _NET_SYSTEM_TRAY_MESSAGE_DATA */
    Atom xembed_atom;        /* _XEMBED                   */
    Atom xembed_info_atom;   /* _XEMBED_INFO              */
    Atom manager_atom;       /* MANAGER                   */

    Window tray_manager;     /* selection owner, or None  */

    TrayState     state;
    tray_click_cb click_cb;
    void         *user;
    bool          destroyed;
};

/* --- helpers ------------------------------------------------------- */

static unsigned long color_for_state(Display *d, int screen, TrayState s)
{
    Colormap   cmap = DefaultColormap(d, screen);
    XColor     col, exact;
    const char *name = "gray50";
    switch(s) {
    case TRAY_STATE_RECORDING: name = "#cc0000"; break;
    case TRAY_STATE_PAUSED:    name = "#cccc00"; break;
    case TRAY_STATE_STREAMING: name = "#1e88e5"; break;
    case TRAY_STATE_IDLE:
    default:                   name = "#7f8c8d"; break;
    }
    if(!XAllocNamedColor(d, cmap, name, &col, &exact))
        return WhitePixel(d, screen);
    return col.pixel;
}

static void paint(Tray *t)
{
    if(!t->window || t->destroyed)
        return;
    XClearWindow(t->display, t->window);
    XSetForeground(t->display, t->gc, color_for_state(t->display, t->screen, t->state));
    XFillRectangle(t->display, t->window, t->gc, 3, 3, TRAY_W - 6, TRAY_H - 6);
    /* Outline for visibility on busy backgrounds. */
    XSetForeground(t->display, t->gc, BlackPixel(t->display, t->screen));
    XDrawRectangle(t->display, t->window, t->gc, 3, 3, TRAY_W - 7, TRAY_H - 7);
    XFlush(t->display);
}

/* --- protocol -------------------------------------------------------- */

static void intern_atoms(Tray *t)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "_NET_SYSTEM_TRAY_S%d", t->screen);
    t->selection_atom    = XInternAtom(t->display, buf, False);
    t->opcode_atom       = XInternAtom(t->display, "_NET_SYSTEM_TRAY_OPCODE",       False);
    t->message_data_atom = XInternAtom(t->display, "_NET_SYSTEM_TRAY_MESSAGE_DATA", False);
    t->xembed_atom       = XInternAtom(t->display, "_XEMBED",                       False);
    t->xembed_info_atom  = XInternAtom(t->display, "_XEMBED_INFO",                  False);
    t->manager_atom      = XInternAtom(t->display, "MANAGER",                       False);
}

static void set_xembed_info(Tray *t)
{
    unsigned long data[2] = { XEMBED_VERSION, XEMBED_MAPPED };
    XChangeProperty(t->display, t->window, t->xembed_info_atom,
                    t->xembed_info_atom, 32, PropModeReplace,
                    (unsigned char *)data, 2);
}

static bool send_dock_request(Tray *t)
{
    t->tray_manager = XGetSelectionOwner(t->display, t->selection_atom);
    if(t->tray_manager == None)
        return false;

    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.xclient.type         = ClientMessage;
    ev.xclient.window       = t->tray_manager;
    ev.xclient.message_type = t->opcode_atom;
    ev.xclient.format       = 32;
    ev.xclient.data.l[0]    = CurrentTime;
    ev.xclient.data.l[1]    = SYSTEM_TRAY_REQUEST_DOCK;
    ev.xclient.data.l[2]    = (long)t->window;
    ev.xclient.data.l[3]    = 0;
    ev.xclient.data.l[4]    = 0;
    XSendEvent(t->display, t->tray_manager, False, NoEventMask, &ev);
    XFlush(t->display);
    return true;
}

static bool try_dock(Tray *t)
{
    if(!send_dock_request(t)) {
        fprintf(stderr, "[tray] no _NET_SYSTEM_TRAY owner — no tray in this session\n");
        return false;
    }
    fprintf(stderr, "[tray] requested dock with tray manager 0x%lx\n",
            (unsigned long)t->tray_manager);
    return true;
}

/* --- event drain ----------------------------------------------------- */

static Bool tray_event_filter(Display *d, XEvent *ev, XPointer arg)
{
    Tray *t = (Tray *)arg;
    (void)d;
    if(ev->type == ClientMessage) {
        if(ev->xclient.window == t->window)            return True;
        if(ev->xclient.message_type == t->manager_atom) return True;
        return False;
    }
    if(ev->type == Expose       || ev->type == ButtonPress ||
       ev->type == ConfigureNotify || ev->type == ReparentNotify ||
       ev->type == DestroyNotify)
        return ev->xany.window == t->window;
    return False;
}

static void handle_event(Tray *t, XEvent *ev)
{
    switch(ev->type) {
    case Expose:
        paint(t);
        break;
    case ButtonPress: {
        if(!t->click_cb) break;
        TrayClick which = (ev->xbutton.button == Button3 || ev->xbutton.button == Button2)
            ? TRAY_CLICK_RIGHT : TRAY_CLICK_LEFT;
        t->click_cb(which, ev->xbutton.x_root, ev->xbutton.y_root, t->user);
        break;
    }
    case ClientMessage:
        if(ev->xclient.message_type == t->manager_atom) {
            /* The tray manager changed. Re-dock to the new owner. */
            Window new_owner = (Window)ev->xclient.data.l[1];
            if(new_owner != None) {
                fprintf(stderr, "[tray] MANAGER change — re-docking\n");
                set_xembed_info(t);
                (void)try_dock(t);
            }
        } else if(ev->xclient.message_type == t->xembed_atom) {
            /* EMBEDDED_NOTIFY etc. — no action needed for our purposes. */
        }
        break;
    case ReparentNotify:
        /* Container parented us. Paint once we're realised inside the tray. */
        paint(t);
        break;
    case ConfigureNotify:
        paint(t);
        break;
    case DestroyNotify:
        t->destroyed = true;
        break;
    }
}

static void drain_timer(XtPointer client, XtIntervalId *id)
{
    (void)id;
    Tray *t = (Tray *)client;
    if(t->destroyed)
        return;
    XEvent ev;
    while(XCheckIfEvent(t->display, &ev, tray_event_filter, (XPointer)t))
        handle_event(t, &ev);

    XtAppAddTimeOut(t->app, TRAY_POLL_INTERVAL_MS, drain_timer, t);
}

/* --- lifecycle -------------------------------------------------------- */

Tray *tray_create(XtAppContext app, Display *display, int screen,
                  tray_click_cb cb, void *user_data)
{
    Tray *t = (Tray *)calloc(1, sizeof(*t));
    t->app      = app;
    t->display  = display;
    t->screen   = screen;
    t->root     = RootWindow(display, screen);
    t->click_cb = cb;
    t->user     = user_data;
    t->state    = TRAY_STATE_IDLE;

    intern_atoms(t);

    /* The tray container will reparent us. We just need a window with
     * the right size and properties. */
    XSetWindowAttributes attrs;
    attrs.event_mask = ExposureMask | ButtonPressMask | StructureNotifyMask;
    /* Override-redirect=False so the WM doesn't try to manage us. */
    t->window = XCreateWindow(display, t->root, 0, 0, TRAY_W, TRAY_H, 0,
        CopyFromParent, InputOutput, CopyFromParent,
        CWEventMask, &attrs);

    /* Window name for accessibility / debugging. */
    XStoreName(display, t->window, "GPU Screen Recorder");

    /* Watch the root for MANAGER ClientMessage broadcasts. */
    XSelectInput(display, t->root, StructureNotifyMask);

    t->gc = XCreateGC(display, t->window, 0, NULL);
    set_xembed_info(t);

    if(!try_dock(t)) {
        tray_destroy(t);
        return NULL;
    }

    XtAppAddTimeOut(app, TRAY_POLL_INTERVAL_MS, drain_timer, t);
    return t;
}

void tray_destroy(Tray *tray)
{
    if(!tray) return;
    tray->destroyed = true;
    if(tray->gc)     XFreeGC(tray->display, tray->gc);
    if(tray->window) XDestroyWindow(tray->display, tray->window);
    XFlush(tray->display);
    free(tray);
}

void tray_set_state(Tray *tray, TrayState state)
{
    if(!tray || tray->state == state) return;
    tray->state = state;
    paint(tray);
}
