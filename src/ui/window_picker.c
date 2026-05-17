#include "window_picker.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <Xm/Xm.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define PICK_POLL_INTERVAL_MS 50

typedef struct {
    XtAppContext      app;
    Display          *display;
    Cursor            cursor;
    bool              active;
    window_picked_cb  cb;
    void             *user;
} PickCtx;

static void cleanup(PickCtx *ctx)
{
    if(!ctx->active)
        return;
    ctx->active = false;
    XUngrabPointer(ctx->display, CurrentTime);
    if(ctx->cursor != None)
        XFreeCursor(ctx->display, ctx->cursor);
    XSync(ctx->display, False);
}

/* Walk up from `start` until we find a window with the WM_STATE property
 * set — that's the toplevel client window the WM tracks. Falls back to
 * the original window if traversal fails. */
static Window find_top_level(Display *d, Window start)
{
    if(start == None)
        return start;
    Atom wm_state = XInternAtom(d, "WM_STATE", True);
    if(wm_state == None)
        return start;

    Window cur = start;
    while(cur != None) {
        Atom           actual_type = None;
        int            actual_format = 0;
        unsigned long  nitems = 0, after = 0;
        unsigned char *prop = NULL;
        if(XGetWindowProperty(d, cur, wm_state, 0, 1, False, AnyPropertyType,
                              &actual_type, &actual_format,
                              &nitems, &after, &prop) == Success && prop) {
            XFree(prop);
            return cur;
        }
        if(prop) XFree(prop);

        Window root, parent;
        Window *children = NULL;
        unsigned int nchildren = 0;
        if(!XQueryTree(d, cur, &root, &parent, &children, &nchildren))
            return start;
        if(children) XFree(children);
        if(parent == None || parent == root)
            return cur;
        cur = parent;
    }
    return start;
}

static void poll_picker(XtPointer client, XtIntervalId *id)
{
    (void)id;
    PickCtx *ctx = (PickCtx *)client;
    if(!ctx->active) {
        free(ctx);
        return;
    }

    XEvent ev;
    bool   resolved = false;
    unsigned long picked = 0;
    while(XCheckMaskEvent(ctx->display, ButtonPressMask, &ev)) {
        if(ev.xbutton.button == Button1) {
            Window w = ev.xbutton.subwindow != None
                ? ev.xbutton.subwindow
                : ev.xbutton.window;
            picked = (unsigned long)find_top_level(ctx->display, w);
        } else {
            picked = 0;  /* right/middle click cancels */
        }
        resolved = true;
        break;
    }

    if(resolved) {
        cleanup(ctx);
        if(ctx->cb)
            ctx->cb(picked, ctx->user);
        free(ctx);
        return;
    }

    XtAppAddTimeOut(ctx->app, PICK_POLL_INTERVAL_MS, poll_picker, ctx);
}

void window_picker_run(Widget parent, window_picked_cb cb, void *user_data)
{
    PickCtx *ctx = (PickCtx *)calloc(1, sizeof(*ctx));
    ctx->app     = XtWidgetToApplicationContext(parent);
    ctx->display = XtDisplay(parent);
    ctx->cb      = cb;
    ctx->user    = user_data;

    ctx->cursor = XCreateFontCursor(ctx->display, XC_crosshair);

    int g = XGrabPointer(ctx->display, DefaultRootWindow(ctx->display), False,
                         ButtonPressMask, GrabModeAsync, GrabModeAsync,
                         None, ctx->cursor, CurrentTime);
    if(g != GrabSuccess) {
        fprintf(stderr, "[window_picker] XGrabPointer failed (%d)\n", g);
        if(ctx->cursor != None) XFreeCursor(ctx->display, ctx->cursor);
        if(cb) cb(0, user_data);
        free(ctx);
        return;
    }
    ctx->active = true;
    XtAppAddTimeOut(ctx->app, PICK_POLL_INTERVAL_MS, poll_picker, ctx);
}
