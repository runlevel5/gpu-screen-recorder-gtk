#include "notifications.h"

#include <Xm/MessageB.h>
#include <Xm/Xm.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* OK callback destroys the dialog so it doesn't accumulate. */
static void on_ok(Widget w, XtPointer client, XtPointer call)
{
    (void)client; (void)call;
    XtUnmanageChild(w);
    XtDestroyWidget(w);
}

static void show(NotificationParent parent_in, const char *title,
                 const char *message, unsigned char dialog_type)
{
    Widget parent = (Widget)parent_in;
    if(!parent) {
        /* No parent supplied — log instead so we don't crash. */
        const char *t = (dialog_type == XmDIALOG_ERROR)   ? "ERROR"
                     : (dialog_type == XmDIALOG_WARNING) ? "WARNING"
                                                         : "INFO";
        fprintf(stderr, "%s: %s — %s\n",
                t, title ? title : "(no title)",
                message ? message : "(no message)");
        return;
    }

    XmString xms_msg   = XmStringCreateLocalized((char *)(message ? message : ""));
    XmString xms_title = XmStringCreateLocalized((char *)(title   ? title   : "GPU Screen Recorder"));

    Arg args[8];
    int n = 0;
    XtSetArg(args[n], XmNdialogType,    dialog_type); ++n;
    XtSetArg(args[n], XmNmessageString, xms_msg);     ++n;
    XtSetArg(args[n], XmNdialogTitle,   xms_title);   ++n;

    Widget dialog = XmCreateMessageDialog(parent, (char *)"msg_dialog", args, n);

    /* Hide Cancel and Help; OK alone is enough for error / info popups. */
    Widget cancel = XmMessageBoxGetChild(dialog, XmDIALOG_CANCEL_BUTTON);
    Widget help   = XmMessageBoxGetChild(dialog, XmDIALOG_HELP_BUTTON);
    if(cancel) XtUnmanageChild(cancel);
    if(help)   XtUnmanageChild(help);

    XtAddCallback(dialog, XmNokCallback, on_ok, NULL);

    XmStringFree(xms_msg);
    XmStringFree(xms_title);

    XtManageChild(dialog);
}

void notifications_show_error(NotificationParent parent, const char *title, const char *message)
{
    show(parent, title, message, XmDIALOG_ERROR);
}

void notifications_show_warning(NotificationParent parent, const char *title, const char *message)
{
    show(parent, title, message, XmDIALOG_WARNING);
}

void notifications_show_info(NotificationParent parent, const char *title, const char *message)
{
    show(parent, title, message, XmDIALOG_INFORMATION);
}
