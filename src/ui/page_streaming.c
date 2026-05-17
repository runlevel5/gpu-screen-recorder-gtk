#include "page_streaming.h"
#include "widgets.h"

#include <Xm/ComboBox.h>
#include <Xm/Form.h>
#include <Xm/RowColumn.h>

#include <stdlib.h>
#include <string.h>

#include "../str_util.h"

typedef struct {
    page_nav_cb     nav;
    page_session_cb session;
    void           *user;
    PageStreaming  *page;
    Config         *config;
} PageCtx;

/* Item index in the service combo. */
typedef enum { SVC_TWITCH = 0, SVC_YOUTUBE, SVC_CUSTOM } ServiceIdx;

static const char *k_services[] = { "twitch", "youtube", "custom", NULL };
/* Custom-stream container choices — full list; sensitivity gating in Pass B. */
static const char *k_containers[] = { "flv", "mkv", "ts", "mp4", "mov", "webm", NULL };

static const char *service_id_from_index(int idx)
{
    return (idx >= 0 && idx < (int)(sizeof(k_services)/sizeof(*k_services)) - 1)
        ? k_services[idx] : "twitch";
}

static int service_index_from_id(const char *id)
{
    if(!id) return 0;
    for(int i = 0; k_services[i]; ++i)
        if(strcmp(id, k_services[i]) == 0)
            return i;
    return 0;
}

static void apply_service_visibility(PageStreaming *p, int service_idx)
{
    XmString lbl = XmStringCreateLocalized(
        service_idx == SVC_CUSTOM ? (char *)"Stream URL:" : (char *)"Stream key:");
    XtVaSetValues(p->stream_key_label, XmNlabelString, lbl, NULL);
    XmStringFree(lbl);

    if(service_idx == SVC_YOUTUBE) { XtManageChild(p->youtube_key_text); }
    else                            { XtUnmanageChild(p->youtube_key_text); }

    if(service_idx == SVC_TWITCH)  { XtManageChild(p->twitch_key_text); }
    else                            { XtUnmanageChild(p->twitch_key_text); }

    if(service_idx == SVC_CUSTOM)  {
        XtManageChild(p->custom_url_text);
        XtManageChild(p->custom_container_row);
    } else {
        XtUnmanageChild(p->custom_url_text);
        XtUnmanageChild(p->custom_container_row);
    }
}

static void service_changed_cb(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)call;
    PageCtx *c = (PageCtx *)client;
    apply_service_visibility(c->page, gsr_w_combo_get_index(c->page->service_combo));
}

static void back_cb(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)call;
    PageCtx *c = (PageCtx *)client;
    page_streaming_commit(c->page, c->config);
    if(c->nav) c->nav(PAGE_COMMON_SETTINGS, c->user);
}

static void start_cb(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)call;
    PageCtx *c = (PageCtx *)client;
    page_streaming_commit(c->page, c->config);
    app_state_save(c->config);
    if(c->session) c->session(SESSION_TOGGLE_RUN, PAGE_STREAMING, c->user);
}

void page_streaming_create(Widget parent, PageStreaming *out,
                           Config *config,
                           Display *display, XIC xic,
                           page_nav_cb nav, page_session_cb session,
                           void *user_data)
{
    out->root = XtVaCreateWidget("streaming_page",
        xmFormWidgetClass, parent,
        XmNtopAttachment,    XmATTACH_FORM,
        XmNleftAttachment,   XmATTACH_FORM,
        XmNrightAttachment,  XmATTACH_FORM,
        XmNbottomAttachment, XmATTACH_FORM,
        NULL);

    /* Wrap the page content in a titled XmFrame ("Streaming"). Same pattern
     * as page_recording / page_replay. */
    Widget frame = NULL;
    Widget rc = gsr_w_titled_frame(out->root, "Streaming", &frame);
    XtVaSetValues(frame,
        XmNtopAttachment,    XmATTACH_FORM,
        XmNleftAttachment,   XmATTACH_FORM,
        XmNrightAttachment,  XmATTACH_FORM,
        XmNtopOffset,        12,
        XmNleftOffset,       12,
        XmNrightOffset,      12,
        NULL);
    XtVaSetValues(rc,
        XmNspacing, 6,
        NULL);

    gsr_w_label(rc, "Stream service:");
    out->service_combo = gsr_w_combo(rc, k_services,
        config->streaming_config.streaming_service);

    out->stream_key_label = gsr_w_label(rc, "Stream key:");

    /* Three peer text fields; only the one matching the current service is
     * managed. We create them all up front so their state persists across
     * service switches. */
    out->youtube_key_text = gsr_w_text(rc, config->streaming_config.youtube.stream_key);
    out->twitch_key_text  = gsr_w_text(rc, config->streaming_config.twitch.stream_key);
    out->custom_url_text  = gsr_w_text(rc, config->streaming_config.custom.url);

    hotkey_row_create(rc, &out->start_stop_hotkey, "Start/stop hotkey:",
                      &config->streaming_config.start_stop_recording_hotkey,
                      display, xic);

    out->custom_container_row = XtVaCreateWidget("custom_row",
        xmRowColumnWidgetClass, rc,
        XmNorientation, XmVERTICAL,
        XmNpacking,     XmPACK_TIGHT,
        XmNspacing,     4,
        NULL);
    gsr_w_label(out->custom_container_row, "Custom container:");
    out->custom_container_combo = gsr_w_combo(out->custom_container_row,
        k_containers, config->streaming_config.custom.container);

    /* Bottom buttons. */
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
    out->back_btn  = gsr_w_button_m(btn_row, "Back",            'B');
    out->start_btn = gsr_w_button_m(btn_row, "Start streaming", 'S');

    PageCtx *c = (PageCtx *)malloc(sizeof(*c));
    c->nav     = nav;
    c->session = session;
    c->user    = user_data;
    c->page    = out;
    c->config  = (Config *)config;

    XtAddCallback(out->back_btn,      XmNactivateCallback,  back_cb,           c);
    XtAddCallback(out->start_btn,     XmNactivateCallback,  start_cb,          c);
    XtAddCallback(out->service_combo, XmNselectionCallback, service_changed_cb, c);

    apply_service_visibility(out,
        service_index_from_id(config->streaming_config.streaming_service));
}

void page_streaming_commit(const PageStreaming *p, Config *config)
{
    /* Service. */
    int svc = gsr_w_combo_get_index(p->service_combo);
    xfree_set(&config->streaming_config.streaming_service,
              xstrdup(service_id_from_index(svc)));

    /* Always commit all three text fields so values persist across service
     * switches (mirrors GTK behaviour). */
    char *yk = gsr_w_text_get(p->youtube_key_text);
    xfree_set(&config->streaming_config.youtube.stream_key, xstrdup(yk));
    XtFree(yk);

    char *tk = gsr_w_text_get(p->twitch_key_text);
    xfree_set(&config->streaming_config.twitch.stream_key, xstrdup(tk));
    XtFree(tk);

    char *cu = gsr_w_text_get(p->custom_url_text);
    xfree_set(&config->streaming_config.custom.url, xstrdup(cu));
    XtFree(cu);

    char *cc = gsr_w_combo_get_text(p->custom_container_combo);
    if(cc) {
        xfree_set(&config->streaming_config.custom.container, xstrdup(cc));
        XtFree(cc);
    }
}
