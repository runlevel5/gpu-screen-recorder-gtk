#include "page_common_settings.h"
#include "widgets.h"

#include <Xm/Form.h>
#include <Xm/Frame.h>
#include <Xm/Label.h>
#include <Xm/RowColumn.h>
#include <Xm/ScrolledW.h>

#include <stdlib.h>
#include <string.h>

#include "../str_util.h"

typedef struct {
    page_nav_cb         nav;
    void               *user;
    PageCommonSettings *page;
    Config             *config;
    PageId              target;        /* for hub buttons */
} NavCtx;

/* --- combo item lists. Capability gating lands in Pass B. --- */
static const char *k_view_items[]       = { "simple", "advanced", NULL };
static const char *k_record_area[]      = { "window", "follow_focused", "portal", "monitor", NULL };
static const char *k_audio_codecs[]     = { "opus", "aac", NULL };
static const char *k_quality_items[]    = { "constant_bitrate", "medium", "high", "very_high", "ultra", NULL };
static const char *k_codec_items[]      = { "auto", "h264", "hevc", "hevc_10bit", "hevc_hdr", "av1", "av1_10bit", "av1_hdr", "vp8", "vp9", "h264_software", NULL };
static const char *k_color_range[]      = { "limited", "full", NULL };
static const char *k_framerate_mode[]   = { "auto", "constant", "variable", NULL };

/* --- titled-frame helper -------------------------------------------- */

static Widget make_titled_frame(Widget parent, const char *title)
{
    Widget frame = XtVaCreateManagedWidget("frame",
        xmFrameWidgetClass, parent,
        XmNshadowType, XmSHADOW_ETCHED_IN,
        NULL);

    XmString xms = XmStringCreateLocalized((char *)title);
    XtVaCreateManagedWidget("frame_title",
        xmLabelWidgetClass, frame,
        XmNlabelString,    xms,
        XmNchildType,      XmFRAME_TITLE_CHILD,
        XmNchildHorizontalAlignment, XmALIGNMENT_BEGINNING,
        NULL);
    XmStringFree(xms);

    Widget rc = XtVaCreateManagedWidget("frame_rc",
        xmRowColumnWidgetClass, frame,
        XmNorientation, XmVERTICAL,
        XmNpacking,     XmPACK_TIGHT,
        XmNspacing,     4,
        XmNchildType,   XmFRAME_WORKAREA_CHILD,
        NULL);
    return rc;
}

/* horizontal row inside a frame for "Label: [widget] Label: [widget]" pairs */
static Widget make_hrow(Widget parent)
{
    return XtVaCreateManagedWidget("hrow",
        xmRowColumnWidgetClass, parent,
        XmNorientation, XmHORIZONTAL,
        XmNpacking,     XmPACK_TIGHT,
        XmNspacing,     6,
        NULL);
}

/* --- callbacks ------------------------------------------------------- */

static void hub_cb(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)call;
    NavCtx *c = (NavCtx *)client;
    page_common_settings_commit(c->page, c->config);
    app_state_save(c->config);
    if(c->nav) c->nav(c->target, c->user);
}

static NavCtx *make_nav_ctx(PageCommonSettings *p, Config *config,
                            page_nav_cb nav, void *user, PageId target)
{
    NavCtx *c = (NavCtx *)malloc(sizeof(*c));
    c->nav    = nav;
    c->user   = user;
    c->page   = p;
    c->config = config;
    c->target = target;
    return c;
}

/* --- page construction ---------------------------------------------- */

void page_common_settings_create(Widget parent, PageCommonSettings *out,
                                 const Config *config,
                                 page_nav_cb nav, void *user_data)
{
    out->root = XtVaCreateWidget("common_settings_page",
        xmFormWidgetClass, parent,
        XmNtopAttachment,    XmATTACH_FORM,
        XmNleftAttachment,   XmATTACH_FORM,
        XmNrightAttachment,  XmATTACH_FORM,
        XmNbottomAttachment, XmATTACH_FORM,
        NULL);

    /* Bottom action row is anchored to form bottom. */
    Widget btn_row = XtVaCreateManagedWidget("btn_row",
        xmRowColumnWidgetClass, out->root,
        XmNorientation,      XmHORIZONTAL,
        XmNpacking,          XmPACK_TIGHT,
        XmNspacing,          12,
        XmNbottomAttachment, XmATTACH_FORM,
        XmNleftAttachment,   XmATTACH_FORM,
        XmNrightAttachment,  XmATTACH_FORM,
        XmNbottomOffset,     12,
        XmNleftOffset,       12,
        XmNrightOffset,      12,
        NULL);
    out->stream_btn = gsr_w_button(btn_row, "Stream");
    out->record_btn = gsr_w_button(btn_row, "Record");
    out->replay_btn = gsr_w_button(btn_row, "Replay");

    /* Scrollable content fills the rest. */
    Widget sw = XtVaCreateManagedWidget("sw",
        xmScrolledWindowWidgetClass, out->root,
        XmNscrollingPolicy,        XmAUTOMATIC,
        XmNscrollBarDisplayPolicy, XmAS_NEEDED,
        XmNtopAttachment,          XmATTACH_FORM,
        XmNleftAttachment,         XmATTACH_FORM,
        XmNrightAttachment,        XmATTACH_FORM,
        XmNbottomAttachment,       XmATTACH_WIDGET,
        XmNbottomWidget,           btn_row,
        XmNtopOffset,              4,
        XmNleftOffset,             4,
        XmNrightOffset,            4,
        XmNbottomOffset,           4,
        NULL);

    Widget content = XtVaCreateManagedWidget("content",
        xmRowColumnWidgetClass, sw,
        XmNorientation, XmVERTICAL,
        XmNpacking,     XmPACK_TIGHT,
        XmNspacing,     8,
        NULL);

    /* --- View frame --- */
    {
        Widget rc = make_titled_frame(content, "View");
        Widget row = make_hrow(rc);
        gsr_w_label(row, "View:");
        const char *initial = config->main_config.advanced_view ? "advanced" : "simple";
        out->view_combo = gsr_w_combo(row, k_view_items, initial);
    }

    /* --- Capture target frame --- */
    {
        Widget rc = make_titled_frame(content, "Capture target");
        Widget r0 = make_hrow(rc);
        gsr_w_label(r0, "Record area:");
        out->record_area_combo = gsr_w_combo(r0, k_record_area,
            config->main_config.record_area_option);

        Widget r1 = make_hrow(rc);
        gsr_w_label(r1, "Area W:");
        out->area_width_spin  = gsr_w_spin_int(r1, 5, 10000,
            config->main_config.record_area_width ?
            config->main_config.record_area_width : 1920);
        gsr_w_label(r1, "H:");
        out->area_height_spin = gsr_w_spin_int(r1, 5, 10000,
            config->main_config.record_area_height ?
            config->main_config.record_area_height : 1080);

        Widget r2 = make_hrow(rc);
        gsr_w_label(r2, "Video W:");
        out->video_width_spin  = gsr_w_spin_int(r2, 5, 10000,
            config->main_config.video_width ?
            config->main_config.video_width : 1920);
        gsr_w_label(r2, "H:");
        out->video_height_spin = gsr_w_spin_int(r2, 5, 10000,
            config->main_config.video_height ?
            config->main_config.video_height : 1080);

        out->change_video_resolution_toggle = gsr_w_toggle(rc,
            "Change video resolution",
            config->main_config.change_video_resolution);
        out->restore_portal_session_toggle = gsr_w_toggle(rc,
            "Restore portal session",
            config->main_config.restore_portal_session);
    }

    /* --- Audio frame --- */
    {
        Widget rc = make_titled_frame(content, "Audio");
        Widget r0 = make_hrow(rc);
        gsr_w_label(r0, "Audio codec:");
        out->audio_codec_combo = gsr_w_combo(r0, k_audio_codecs,
            config->main_config.audio_codec);

        /* GTK shows a "Split each device/app audio into separate tracks"
         * toggle whose ON state means merge=false. Mirror that inversion. */
        out->merge_audio_toggle = gsr_w_toggle(rc,
            "Merge all audio sources into a single track",
            config->main_config.merge_audio_tracks);
        out->record_app_audio_inverted_toggle = gsr_w_toggle(rc,
            "Record audio from all applications except the selected ones",
            config->main_config.record_app_audio_inverted);

        /* Audio device list — populated in Pass B (needs capability detection
         * + dynamic widget management). */
        gsr_w_label(rc, "(Audio devices list — coming in Pass B)");
    }

    /* --- Video frame --- */
    {
        Widget rc = make_titled_frame(content, "Video");
        Widget r0 = make_hrow(rc);
        gsr_w_label(r0, "Quality:");
        out->quality_combo = gsr_w_combo(r0, k_quality_items,
            config->main_config.quality);

        Widget r1 = make_hrow(rc);
        gsr_w_label(r1, "Bitrate (kbps):");
        out->bitrate_spin = gsr_w_spin_int(r1, 1, 500000,
            config->main_config.video_bitrate);

        Widget r2 = make_hrow(rc);
        gsr_w_label(r2, "Codec:");
        out->codec_combo = gsr_w_combo(r2, k_codec_items,
            config->main_config.codec);

        Widget r3 = make_hrow(rc);
        gsr_w_label(r3, "Color range:");
        out->color_range_combo = gsr_w_combo(r3, k_color_range,
            config->main_config.color_range);

        Widget r4 = make_hrow(rc);
        gsr_w_label(r4, "Frame rate:");
        out->fps_spin = gsr_w_spin_int(r4, 1, 500,
            config->main_config.fps);

        Widget r5 = make_hrow(rc);
        gsr_w_label(r5, "Frame rate mode:");
        out->framerate_mode_combo = gsr_w_combo(r5, k_framerate_mode,
            config->main_config.framerate_mode);

        out->record_cursor_toggle = gsr_w_toggle(rc,
            "Record cursor",
            config->main_config.record_cursor);
        out->overclock_toggle = gsr_w_toggle(rc,
            "Overclock memory transfer rate (NVIDIA workaround)",
            config->main_config.overclock);
    }

    /* --- Notifications frame --- */
    {
        Widget rc = make_titled_frame(content, "Notifications");
        out->notif_started_toggle = gsr_w_toggle(rc,
            "Show recording/streaming/replay started notification",
            config->main_config.show_recording_started_notifications);
        out->notif_stopped_toggle = gsr_w_toggle(rc,
            "Show streaming/replay stopped notification",
            config->main_config.show_recording_stopped_notifications);
        out->notif_saved_toggle = gsr_w_toggle(rc,
            "Show video saved notification",
            config->main_config.show_recording_saved_notifications);
    }

    /* Hub button callbacks. */
    XtAddCallback(out->stream_btn, XmNactivateCallback, hub_cb,
                  make_nav_ctx(out, (Config *)config, nav, user_data, PAGE_STREAMING));
    XtAddCallback(out->record_btn, XmNactivateCallback, hub_cb,
                  make_nav_ctx(out, (Config *)config, nav, user_data, PAGE_RECORDING));
    XtAddCallback(out->replay_btn, XmNactivateCallback, hub_cb,
                  make_nav_ctx(out, (Config *)config, nav, user_data, PAGE_REPLAY));
}

/* --- commit ---------------------------------------------------------- */

static void commit_combo_str(Widget combo, char **slot)
{
    char *raw = gsr_w_combo_get_text(combo);
    if(raw) {
        xfree_set(slot, xstrdup(raw));
        XtFree(raw);
    }
}

void page_common_settings_commit(const PageCommonSettings *p, Config *config)
{
    /* View. */
    {
        char *raw = gsr_w_combo_get_text(p->view_combo);
        if(raw) {
            config->main_config.advanced_view = strcmp(raw, "advanced") == 0;
            XtFree(raw);
        }
    }

    /* Capture target. */
    commit_combo_str(p->record_area_combo, &config->main_config.record_area_option);
    config->main_config.record_area_width  = gsr_w_spin_get(p->area_width_spin);
    config->main_config.record_area_height = gsr_w_spin_get(p->area_height_spin);
    config->main_config.video_width        = gsr_w_spin_get(p->video_width_spin);
    config->main_config.video_height       = gsr_w_spin_get(p->video_height_spin);
    config->main_config.change_video_resolution = gsr_w_toggle_get(p->change_video_resolution_toggle);
    config->main_config.restore_portal_session  = gsr_w_toggle_get(p->restore_portal_session_toggle);

    /* Audio. */
    commit_combo_str(p->audio_codec_combo, &config->main_config.audio_codec);
    config->main_config.merge_audio_tracks        = gsr_w_toggle_get(p->merge_audio_toggle);
    config->main_config.record_app_audio_inverted = gsr_w_toggle_get(p->record_app_audio_inverted_toggle);

    /* Video. */
    commit_combo_str(p->quality_combo,        &config->main_config.quality);
    config->main_config.video_bitrate = gsr_w_spin_get(p->bitrate_spin);
    commit_combo_str(p->codec_combo,          &config->main_config.codec);
    commit_combo_str(p->color_range_combo,    &config->main_config.color_range);
    config->main_config.fps = gsr_w_spin_get(p->fps_spin);
    commit_combo_str(p->framerate_mode_combo, &config->main_config.framerate_mode);
    config->main_config.record_cursor = gsr_w_toggle_get(p->record_cursor_toggle);
    config->main_config.overclock     = gsr_w_toggle_get(p->overclock_toggle);

    /* Notifications. */
    config->main_config.show_recording_started_notifications = gsr_w_toggle_get(p->notif_started_toggle);
    config->main_config.show_recording_stopped_notifications = gsr_w_toggle_get(p->notif_stopped_toggle);
    config->main_config.show_recording_saved_notifications   = gsr_w_toggle_get(p->notif_saved_toggle);
}
