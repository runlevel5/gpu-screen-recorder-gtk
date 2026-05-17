#include "hotkey_capture.h"

#include <Xm/DialogS.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/Xm.h>

#include <X11/XKBlib.h>
#include <X11/keysym.h>

#include <stdio.h>
#include <stdlib.h>

#include "../x11_hotkeys.h"

typedef struct {
    Widget                  dialog;
    Display                *display;
    XIC                     xic;
    hotkey_capture_done_cb  done;
    void                   *user;
    bool                    finished;
} CaptureCtx;

static void finish(CaptureCtx *c, HotkeyCaptureResult result, ConfigHotkey hk)
{
    if(c->finished)
        return;
    c->finished = true;
    XUngrabKeyboard(c->display, CurrentTime);
    XSync(c->display, False);

    if(c->done)
        c->done(result, hk, c->user);

    XtUnmanageChild(c->dialog);
    XtDestroyWidget(c->dialog);
    free(c);
}

static void key_event_handler(Widget w, XtPointer client_data,
                              XEvent *event, Boolean *cont)
{
    (void)w; (void)cont;
    CaptureCtx *c = (CaptureCtx *)client_data;
    if(event->type != KeyPress)
        return;

    KeySym ks = XkbKeycodeToKeysym(c->display, event->xkey.keycode, 0, 0);

    if(ks == XK_Escape) {
        ConfigHotkey blank = { 0, 0 };
        finish(c, HOTKEY_CAPTURE_CANCELLED, blank);
        return;
    }
    if(ks == XK_BackSpace) {
        ConfigHotkey blank = { 0, 0 };
        finish(c, HOTKEY_CAPTURE_CLEARED, blank);
        return;
    }
    if(gsr_key_is_modifier(ks))
        return;  /* wait for the non-modifier key */

    ConfigHotkey hk;
    hk.keysym    = (int64_t)ks;
    hk.modifiers = gsr_x11_mask_to_gsr_mod(
        gsr_key_state_without_locks(event->xkey.state));
    finish(c, HOTKEY_CAPTURE_OK, hk);
}

void hotkey_capture_run(Widget parent, Display *display, XIC xic,
                        hotkey_capture_done_cb done, void *user_data)
{
    CaptureCtx *c = (CaptureCtx *)calloc(1, sizeof(*c));
    c->display = display;
    c->xic     = xic;
    c->done    = done;
    c->user    = user_data;

    XmString xms_title = XmStringCreateLocalized((char *)"Set hotkey");
    Arg args[8];
    int n = 0;
    XtSetArg(args[n], XmNtitle,          "Set hotkey");        ++n;
    XtSetArg(args[n], XmNdialogStyle,    XmDIALOG_FULL_APPLICATION_MODAL); ++n;
    XtSetArg(args[n], XmNallowShellResize, True);              ++n;
    c->dialog = XmCreateDialogShell(parent, (char *)"hotkey_capture", args, n);
    XmStringFree(xms_title);

    Widget form = XtVaCreateManagedWidget("form",
        xmFormWidgetClass, c->dialog,
        XmNwidth,  420,
        XmNheight, 80,
        NULL);

    XmString xms = XmStringCreateLocalized(
        (char *)"Press a key combination.\n"
                "Backspace to clear, Esc to cancel.");
    XtVaCreateManagedWidget("prompt",
        xmLabelWidgetClass, form,
        XmNlabelString,      xms,
        XmNtopAttachment,    XmATTACH_FORM,
        XmNleftAttachment,   XmATTACH_FORM,
        XmNrightAttachment,  XmATTACH_FORM,
        XmNbottomAttachment, XmATTACH_FORM,
        XmNtopOffset,        8,
        XmNleftOffset,       12,
        XmNrightOffset,      12,
        XmNbottomOffset,     8,
        NULL);
    XmStringFree(xms);

    XtRealizeWidget(c->dialog);
    XtManageChild(form);

    /* The dialog shell window now exists. Listen for key events and grab
     * the keyboard so the user can't activate other widgets mid-capture. */
    XtAddEventHandler(c->dialog,
                      KeyPressMask | KeyReleaseMask,
                      False, key_event_handler, c);

    Window dw = XtWindow(c->dialog);
    int grab = XGrabKeyboard(c->display, dw, True,
                             GrabModeAsync, GrabModeAsync, CurrentTime);
    if(grab != GrabSuccess) {
        fprintf(stderr, "[hotkey_capture] XGrabKeyboard failed (%d); aborting\n", grab);
        ConfigHotkey blank = { 0, 0 };
        finish(c, HOTKEY_CAPTURE_CANCELLED, blank);
        return;
    }
}
