#include "dialogs.h"

#include <Xm/DialogS.h>
#include <Xm/FileSB.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/Separator.h>
#include <Xm/Xm.h>

#include <stdlib.h>
#include <string.h>

#include "../str_util.h"

#ifndef GSR_VERSION
#define GSR_VERSION "unknown"
#endif

typedef struct {
    directory_chosen_cb cb;
    void               *user;
} DirCtx;

static void destroy_dialog(Widget dialog)
{
    XtUnmanageChild(dialog);
    XtDestroyWidget(dialog);
}

static void ok_cb(Widget dialog, XtPointer client, XtPointer call)
{
    DirCtx *ctx = (DirCtx *)client;
    XmFileSelectionBoxCallbackStruct *cb_data = (XmFileSelectionBoxCallbackStruct *)call;

    /* When XmNfileTypeMask=XmFILE_DIRECTORY, the selection IS the directory. */
    char *path = NULL;
    if(cb_data && cb_data->value)
        XmStringGetLtoR(cb_data->value, XmFONTLIST_DEFAULT_TAG, &path);

    if(ctx->cb)
        ctx->cb(path ? path : "", ctx->user);

    if(path) XtFree(path);
    free(ctx);
    destroy_dialog(dialog);
}

static void cancel_cb(Widget dialog, XtPointer client, XtPointer call)
{
    (void)call;
    DirCtx *ctx = (DirCtx *)client;
    free(ctx);
    destroy_dialog(dialog);
}

/* --- About dialog ----------------------------------------------------
 *
 * Layout (mirrors the dtcm "About Calendar" dialog from CDE):
 *
 *   +------------------------------------------+
 *   |        GPU Screen Recorder               |
 *   |        ----------------------            |
 *   |        Version 5.7.9                     |
 *   |        Motif/X11 port (C99)              |
 *   |                                          |
 *   |        Original GTK port:                |
 *   |          dec05eba                        |
 *   |                                          |
 *   |        Motif port:                       |
 *   |          Trung Lê                        |
 *   |                                          |
 *   |        [          OK          ]          |
 *   +------------------------------------------+
 */

static void about_ok_cb(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)call;
    Widget shell = (Widget)client;
    XtPopdown(shell);
    XtDestroyWidget(shell);
}

/* Convenience: add a single-line centered label to a RowColumn. */
static Widget about_line(Widget parent, const char *text)
{
    XmString xms = XmStringCreateLocalized((char *)text);
    Widget   w   = XtVaCreateManagedWidget("about_line",
        xmLabelWidgetClass, parent,
        XmNlabelString, xms,
        XmNalignment,   XmALIGNMENT_CENTER,
        NULL);
    XmStringFree(xms);
    return w;
}

void dialogs_show_about(Widget parent)
{
    Widget shell = XtVaCreatePopupShell("about_shell",
        xmDialogShellWidgetClass, parent,
        XmNtitle,            "About",
        XmNallowShellResize, True,
        XmNdeleteResponse,   XmDESTROY,
        NULL);

    Widget form = XtVaCreateManagedWidget("about_form",
        xmFormWidgetClass, shell,
        XmNwidth,  360,
        NULL);

    /* OK button — placed first so the content RowColumn can attach to its top. */
    XmString ok_xms = XmStringCreateLocalized((char *)"OK");
    Widget ok_btn = XtVaCreateManagedWidget("about_ok",
        xmPushButtonWidgetClass, form,
        XmNlabelString,      ok_xms,
        XmNshowAsDefault,    True,
        XmNbottomAttachment, XmATTACH_FORM,
        XmNbottomOffset,     14,
        XmNleftAttachment,   XmATTACH_POSITION,
        XmNleftPosition,     40,
        XmNrightAttachment,  XmATTACH_POSITION,
        XmNrightPosition,    60,
        NULL);
    XmStringFree(ok_xms);

    /* Content stack. */
    Widget rc = XtVaCreateManagedWidget("about_rc",
        xmRowColumnWidgetClass, form,
        XmNorientation,      XmVERTICAL,
        XmNentryAlignment,   XmALIGNMENT_CENTER,
        XmNspacing,          4,
        XmNtopAttachment,    XmATTACH_FORM,
        XmNleftAttachment,   XmATTACH_FORM,
        XmNrightAttachment,  XmATTACH_FORM,
        XmNbottomAttachment, XmATTACH_WIDGET,
        XmNbottomWidget,     ok_btn,
        XmNtopOffset,        20,
        XmNleftOffset,       24,
        XmNrightOffset,      24,
        XmNbottomOffset,     16,
        NULL);

    about_line(rc, "GPU Screen Recorder");
    XtVaCreateManagedWidget("about_sep",
        xmSeparatorWidgetClass, rc, NULL);
    about_line(rc, "Version " GSR_VERSION);
    about_line(rc, "Motif/X11 port (C99)");

    about_line(rc, " ");   /* visual gap */
    about_line(rc, "Original GTK port:");
    about_line(rc, "dec05eba");
    about_line(rc, " ");
    about_line(rc, "Motif port:");
    about_line(rc, "Trung Lê");

    XtAddCallback(ok_btn, XmNactivateCallback, about_ok_cb, (XtPointer)shell);

    XtPopup(shell, XtGrabExclusive);
}

void dialogs_pick_directory(Widget parent, const char *initial_dir,
                            directory_chosen_cb cb, void *user_data)
{
    DirCtx *ctx = (DirCtx *)malloc(sizeof(*ctx));
    ctx->cb   = cb;
    ctx->user = user_data;

    XmString xms_title = XmStringCreateLocalized((char *)"Choose a directory");

    Arg args[8];
    int n = 0;
    XtSetArg(args[n], XmNdialogTitle,  xms_title);          ++n;
    XtSetArg(args[n], XmNfileTypeMask, XmFILE_DIRECTORY);   ++n;
    XtSetArg(args[n], XmNdialogStyle,  XmDIALOG_FULL_APPLICATION_MODAL); ++n;

    Widget dialog = XmCreateFileSelectionDialog(parent,
        (char *)"dir_chooser", args, n);

    XmStringFree(xms_title);

    if(initial_dir && initial_dir[0]) {
        XmString xms_dir = XmStringCreateLocalized((char *)initial_dir);
        XtVaSetValues(dialog, XmNdirectory, xms_dir, NULL);
        XmStringFree(xms_dir);
    }

    /* Hide the filename field — directory selection only. */
    Widget help_btn = XmFileSelectionBoxGetChild(dialog, XmDIALOG_HELP_BUTTON);
    if(help_btn) XtUnmanageChild(help_btn);

    XtAddCallback(dialog, XmNokCallback,     ok_cb,     ctx);
    XtAddCallback(dialog, XmNcancelCallback, cancel_cb, ctx);

    XtManageChild(dialog);
}
