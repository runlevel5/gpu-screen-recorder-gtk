#include "page_common_settings.h"

#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>

#include <stdlib.h>

typedef struct {
    page_nav_cb nav;
    void       *user;
    PageId      target;
} NavClosure;

static void nav_button_cb(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)call;
    NavClosure *c = (NavClosure *)client;
    if(c->nav)
        c->nav(c->target, c->user);
}

static Widget add_nav_button(Widget parent, const char *label,
                             page_nav_cb nav, void *user, PageId target)
{
    XmString xms = XmStringCreateLocalized((char *)label);
    Widget   b = XtVaCreateManagedWidget(
        label,
        xmPushButtonWidgetClass, parent,
        XmNlabelString, xms,
        NULL);
    XmStringFree(xms);

    /* Closure leaks once at process exit — acceptable for nav buttons. */
    NavClosure *c = (NavClosure *)malloc(sizeof(*c));
    c->nav = nav;
    c->user = user;
    c->target = target;
    XtAddCallback(b, XmNactivateCallback, nav_button_cb, c);
    return b;
}

void page_common_settings_create(Widget parent, PageCommonSettings *out,
                                 page_nav_cb nav, void *user_data)
{
    out->root = XtVaCreateWidget(
        "common_settings_page",
        xmFormWidgetClass, parent,
        XmNtopAttachment,    XmATTACH_FORM,
        XmNleftAttachment,   XmATTACH_FORM,
        XmNrightAttachment,  XmATTACH_FORM,
        XmNbottomAttachment, XmATTACH_FORM,
        NULL);

    XmString header_xms = XmStringCreateLocalized(
        (char *)"GPU Screen Recorder — Common Settings\n"
                "(Phase 4 stub; real settings land in Phase 5)");
    Widget header = XtVaCreateManagedWidget(
        "header",
        xmLabelWidgetClass, out->root,
        XmNlabelString,     header_xms,
        XmNtopAttachment,   XmATTACH_FORM,
        XmNleftAttachment,  XmATTACH_FORM,
        XmNrightAttachment, XmATTACH_FORM,
        XmNtopOffset,       16,
        XmNleftOffset,      16,
        XmNrightOffset,     16,
        NULL);
    XmStringFree(header_xms);

    Widget button_box = XtVaCreateManagedWidget(
        "button_box",
        xmRowColumnWidgetClass, out->root,
        XmNorientation,      XmHORIZONTAL,
        XmNpacking,          XmPACK_TIGHT,
        XmNspacing,          12,
        XmNtopAttachment,    XmATTACH_WIDGET,
        XmNtopWidget,        header,
        XmNtopOffset,        24,
        XmNleftAttachment,   XmATTACH_FORM,
        XmNleftOffset,       16,
        NULL);

    out->stream_btn = add_nav_button(button_box, "Stream", nav, user_data, PAGE_STREAMING);
    out->record_btn = add_nav_button(button_box, "Record", nav, user_data, PAGE_RECORDING);
    out->replay_btn = add_nav_button(button_box, "Replay", nav, user_data, PAGE_REPLAY);
}
