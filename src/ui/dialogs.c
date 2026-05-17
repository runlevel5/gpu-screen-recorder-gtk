#include "dialogs.h"

#include <Xm/FileSB.h>
#include <Xm/Xm.h>

#include <stdlib.h>
#include <string.h>

#include "../str_util.h"

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
