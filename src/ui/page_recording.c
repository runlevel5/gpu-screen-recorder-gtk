#include "page_recording.h"
#include "dialogs.h"
#include "widgets.h"

#include <Xm/Form.h>
#include <Xm/RowColumn.h>

#include <stdlib.h>
#include <string.h>

#include "../str_util.h"

typedef struct {
    page_nav_cb     nav;
    page_session_cb session;
    void           *user;
    PageRecording  *page;
    Config         *config;
} PageCtx;

static const char *k_containers[] = { "mp4", "mkv", "mov", "webm", "flv", "ts", NULL };

static void back_cb(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)call;
    PageCtx *c = (PageCtx *)client;
    page_recording_commit(c->page, c->config);
    if(c->nav) c->nav(PAGE_COMMON_SETTINGS, c->user);
}

static void start_cb(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)call;
    PageCtx *c = (PageCtx *)client;
    page_recording_commit(c->page, c->config);
    app_state_save(c->config);
    if(c->session) c->session(SESSION_TOGGLE_RUN, PAGE_RECORDING, c->user);
}

static void pause_cb(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)call;
    PageCtx *c = (PageCtx *)client;
    if(c->session) c->session(SESSION_PAUSE, PAGE_RECORDING, c->user);
}

static void on_dir_chosen(const char *path, void *user)
{
    PageCtx *c = (PageCtx *)user;
    gsr_w_text_set(c->page->save_dir_text, path);
}

static void browse_cb(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)call;
    PageCtx *c = (PageCtx *)client;
    char *current = gsr_w_text_get(c->page->save_dir_text);
    dialogs_pick_directory(c->page->root, current, on_dir_chosen, c);
    if(current) XtFree(current);
}

void page_recording_create(Widget parent, PageRecording *out,
                           const Config *config,
                           page_nav_cb nav, page_session_cb session,
                           void *user_data)
{
    out->root = XtVaCreateWidget("recording_page",
        xmFormWidgetClass, parent,
        XmNtopAttachment,    XmATTACH_FORM,
        XmNleftAttachment,   XmATTACH_FORM,
        XmNrightAttachment,  XmATTACH_FORM,
        XmNbottomAttachment, XmATTACH_FORM,
        NULL);

    Widget header = gsr_w_label_at(out->root, "Recording", NULL);

    Widget rc = XtVaCreateManagedWidget("rc",
        xmRowColumnWidgetClass, out->root,
        XmNorientation,      XmVERTICAL,
        XmNpacking,          XmPACK_TIGHT,
        XmNspacing,          6,
        XmNtopAttachment,    XmATTACH_WIDGET,
        XmNtopWidget,        header,
        XmNtopOffset,        16,
        XmNleftAttachment,   XmATTACH_FORM,
        XmNrightAttachment,  XmATTACH_FORM,
        XmNleftOffset,       12,
        XmNrightOffset,      12,
        NULL);

    gsr_w_label(rc, "Where do you want to save the video?");
    Widget dir_row = gsr_w_hrow(rc);
    out->save_dir_text       = gsr_w_text(dir_row, config->record_config.save_directory);
    out->save_dir_browse_btn = gsr_w_button(dir_row, "Browse...");

    gsr_w_label(rc, "Container:");
    out->container_combo = gsr_w_combo(rc, k_containers,
                                       config->record_config.container);

    Widget btn_row = XtVaCreateManagedWidget("btn_row",
        xmRowColumnWidgetClass, out->root,
        XmNorientation,      XmHORIZONTAL,
        XmNpacking,          XmPACK_TIGHT,
        XmNspacing,          12,
        XmNbottomAttachment, XmATTACH_FORM,
        XmNleftAttachment,   XmATTACH_FORM,
        XmNbottomOffset,     12,
        XmNleftOffset,       12,
        NULL);

    out->back_btn  = gsr_w_button(btn_row, "Back");
    out->start_btn = gsr_w_button(btn_row, "Start recording");
    out->pause_btn = gsr_w_button(btn_row, "Pause recording");

    PageCtx *c = (PageCtx *)malloc(sizeof(*c));
    c->nav     = nav;
    c->session = session;
    c->user    = user_data;
    c->page    = out;
    c->config  = (Config *)config;

    XtAddCallback(out->back_btn,            XmNactivateCallback, back_cb,   c);
    XtAddCallback(out->start_btn,           XmNactivateCallback, start_cb,  c);
    XtAddCallback(out->pause_btn,           XmNactivateCallback, pause_cb,  c);
    XtAddCallback(out->save_dir_browse_btn, XmNactivateCallback, browse_cb, c);
}

void page_recording_commit(const PageRecording *p, Config *config)
{
    char *dir = gsr_w_text_get(p->save_dir_text);
    xfree_set(&config->record_config.save_directory, xstrdup(dir));
    XtFree(dir);

    char *container = gsr_w_combo_get_text(p->container_combo);
    if(container) {
        xfree_set(&config->record_config.container, xstrdup(container));
        XtFree(container);
    }
}
