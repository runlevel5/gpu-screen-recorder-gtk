#include "page_replay.h"

#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/PushB.h>

#include <stdlib.h>

typedef struct {
    page_nav_cb nav;
    void       *user;
} BackClosure;

static void back_cb(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)call;
    BackClosure *c = (BackClosure *)client;
    if(c->nav)
        c->nav(PAGE_COMMON_SETTINGS, c->user);
}

void page_replay_create(Widget parent, PageReplay *out,
                        page_nav_cb nav, void *user_data)
{
    out->root = XtVaCreateWidget(
        "replay_page",
        xmFormWidgetClass, parent,
        XmNtopAttachment,    XmATTACH_FORM,
        XmNleftAttachment,   XmATTACH_FORM,
        XmNrightAttachment,  XmATTACH_FORM,
        XmNbottomAttachment, XmATTACH_FORM,
        NULL);

    XmString header_xms = XmStringCreateLocalized(
        (char *)"Replay\n(Phase 4 stub)");
    XtVaCreateManagedWidget(
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

    XmString back_xms = XmStringCreateLocalized((char *)"Back");
    out->back_btn = XtVaCreateManagedWidget(
        "back_btn",
        xmPushButtonWidgetClass, out->root,
        XmNlabelString,      back_xms,
        XmNbottomAttachment, XmATTACH_FORM,
        XmNleftAttachment,   XmATTACH_FORM,
        XmNbottomOffset,     16,
        XmNleftOffset,       16,
        NULL);
    XmStringFree(back_xms);

    BackClosure *c = (BackClosure *)malloc(sizeof(*c));
    c->nav = nav;
    c->user = user_data;
    XtAddCallback(out->back_btn, XmNactivateCallback, back_cb, c);
}
