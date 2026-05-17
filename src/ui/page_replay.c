#include "page_replay.h"
#include "widgets.h"

#include <Xm/Form.h>
#include <Xm/Frame.h>
#include <Xm/RowColumn.h>

#include <stdlib.h>
#include <string.h>

#include "../str_util.h"

typedef struct {
    page_nav_cb     nav;
    page_session_cb session;
    void           *user;
    PageReplay     *page;
    Config         *config;
} PageCtx;

static const char *k_containers[] = { "mp4", "mkv", "mov", "webm", "flv", "ts", NULL };

static void back_cb(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)call;
    PageCtx *c = (PageCtx *)client;
    page_replay_commit(c->page, c->config);
    if(c->nav) c->nav(PAGE_COMMON_SETTINGS, c->user);
}

static void start_cb(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)call;
    PageCtx *c = (PageCtx *)client;
    page_replay_commit(c->page, c->config);
    app_state_save(c->config);
    if(c->session) c->session(SESSION_TOGGLE_RUN, PAGE_REPLAY, c->user);
}

static void save_cb(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)call;
    PageCtx *c = (PageCtx *)client;
    if(c->session) c->session(SESSION_SAVE, PAGE_REPLAY, c->user);
}

void page_replay_create(Widget parent, PageReplay *out,
                        const Config *config,
                        page_nav_cb nav, page_session_cb session,
                        void *user_data)
{
    out->root = XtVaCreateWidget("replay_page",
        xmFormWidgetClass, parent,
        XmNtopAttachment,    XmATTACH_FORM,
        XmNleftAttachment,   XmATTACH_FORM,
        XmNrightAttachment,  XmATTACH_FORM,
        XmNbottomAttachment, XmATTACH_FORM,
        NULL);

    Widget header = gsr_w_label_at(out->root, "Replay", NULL);

    /* Save dir + container + replay time stacked in a RowColumn. */
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

    gsr_w_label(rc, "Where do you want to save the replays?");
    out->save_dir_text = gsr_w_text(rc, config->replay_config.save_directory);

    gsr_w_label(rc, "Container:");
    out->container_combo = gsr_w_combo(rc, k_containers,
                                       config->replay_config.container);

    gsr_w_label(rc, "Replay time (seconds):");
    out->replay_time_spin = gsr_w_spin_int(rc, 5, 1200,
                                           config->replay_config.replay_time);

    /* Bottom button row. */
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
    out->start_btn = gsr_w_button(btn_row, "Start replay");
    out->save_btn  = gsr_w_button(btn_row, "Save replay");

    PageCtx *c = (PageCtx *)malloc(sizeof(*c));
    c->nav     = nav;
    c->session = session;
    c->user    = user_data;
    c->page    = out;
    c->config  = (Config *)config;

    XtAddCallback(out->back_btn,  XmNactivateCallback, back_cb,  c);
    XtAddCallback(out->start_btn, XmNactivateCallback, start_cb, c);
    XtAddCallback(out->save_btn,  XmNactivateCallback, save_cb,  c);
}

void page_replay_commit(const PageReplay *p, Config *config)
{
    char *dir = gsr_w_text_get(p->save_dir_text);
    xfree_set(&config->replay_config.save_directory, xstrdup(dir));
    XtFree(dir);

    char *container = gsr_w_combo_get_text(p->container_combo);
    if(container) {
        xfree_set(&config->replay_config.container, xstrdup(container));
        XtFree(container);
    }

    config->replay_config.replay_time = gsr_w_spin_get(p->replay_time_spin);
}
