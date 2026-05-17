#include "hotkey_row.h"
#include "hotkey_capture.h"
#include "widgets.h"

#include <Xm/Label.h>
#include <Xm/RowColumn.h>
#include <Xm/Xm.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../x11_hotkeys.h"

static void format_label(HotkeyRow *row, char *buf, size_t buf_size)
{
    if(row->target->keysym == 0 && row->target->modifiers == 0) {
        snprintf(buf, buf_size, "<click to bind>");
        return;
    }
    char tmp[128];
    size_t n = gsr_hotkey_format(row->display, row->xic, *row->target, tmp, sizeof(tmp));
    if(n == 0)
        snprintf(buf, buf_size, "<unknown>");
    else
        snprintf(buf, buf_size, "%s", tmp);
}

void hotkey_row_refresh(HotkeyRow *row)
{
    char buf[160];
    format_label(row, buf, sizeof(buf));
    XmString xms = XmStringCreateLocalized(buf);
    XtVaSetValues(row->button, XmNlabelString, xms, NULL);
    XmStringFree(xms);
}

static void on_capture_done(HotkeyCaptureResult result,
                            ConfigHotkey new_hotkey,
                            void *user)
{
    HotkeyRow *row = (HotkeyRow *)user;
    switch(result) {
    case HOTKEY_CAPTURE_OK:
        *row->target = new_hotkey;
        break;
    case HOTKEY_CAPTURE_CLEARED:
        row->target->keysym = 0;
        row->target->modifiers = 0;
        break;
    case HOTKEY_CAPTURE_CANCELLED:
        break;
    }
    hotkey_row_refresh(row);
}

static void on_button_click(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)call;
    HotkeyRow *row = (HotkeyRow *)client;
    hotkey_capture_run(row->row, row->display, row->xic, on_capture_done, row);
}

void hotkey_row_create(Widget parent, HotkeyRow *out,
                       const char *label_text,
                       ConfigHotkey *target,
                       Display *display, XIC xic)
{
    out->target  = target;
    out->display = display;
    out->xic     = xic;
    out->row     = gsr_w_hrow(parent);

    gsr_w_label(out->row, label_text);

    char buf[160];
    format_label(out, buf, sizeof(buf));
    out->button = gsr_w_button(out->row, buf);
    XtAddCallback(out->button, XmNactivateCallback, on_button_click, out);
}
