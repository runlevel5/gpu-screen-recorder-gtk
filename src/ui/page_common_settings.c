#include "page_common_settings.h"
#include "dialogs.h"
#include "widgets.h"
#include "window_picker.h"

#include <Xm/ComboBox.h>
#include <Xm/Form.h>
#include <Xm/Frame.h>
#include <Xm/Label.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/ScrolledW.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../audio_devices.h"
#include "../str_util.h"

typedef struct {
    page_nav_cb              nav;
    void                    *user;
    PageCommonSettings      *page;
    Config                  *config;
    const GsrCapabilities   *caps;
    PageId                   target;        /* hub buttons */
} NavCtx;

typedef struct {
    PageCommonSettings    *page;
    const GsrCapabilities *caps;
} VisCtx;

/* --- dynamic audio rows ----------------------------------------------
 *
 * Each row in the audio frame represents one entry in
 * config.main_config.audio_input. The page tracks rows in a dynamic
 * AudioRow array; commit walks them and writes the StringArray. The
 * encoding matches the GTK port:
 *   AUDIO_ROW_DEVICE     -> "device:<pulse device name>"
 *   AUDIO_ROW_APP        -> "app:<application name>"
 *   AUDIO_ROW_APP_CUSTOM -> "app:<user-typed text>"
 */
typedef enum {
    AUDIO_ROW_DEVICE = 0,
    AUDIO_ROW_APP,
    AUDIO_ROW_APP_CUSTOM,
} AudioRowKind;

typedef struct {
    AudioRowKind kind;
    Widget       row;        /* XmRowColumn parent of value + remove */
    Widget       value;      /* combo or text field */
    Widget       remove_btn;
    PageCommonSettings *owner;
} AudioRow;

typedef struct {
    AudioRow **items;
    size_t     len;
    size_t     cap;
} AudioRowList;

static AudioRowList *rows_get(PageCommonSettings *p)
{
    return (AudioRowList *)p->audio_rows;
}

static void rows_push(AudioRowList *list, AudioRow *r)
{
    if(list->len == list->cap) {
        size_t cap = list->cap ? list->cap * 2 : 4;
        AudioRow **n = (AudioRow **)realloc(list->items, cap * sizeof(*n));
        assert(n);
        list->items = n;
        list->cap = cap;
    }
    list->items[list->len++] = r;
}

static void rows_erase(AudioRowList *list, AudioRow *r)
{
    for(size_t i = 0; i < list->len; ++i) {
        if(list->items[i] == r) {
            memmove(list->items + i, list->items + i + 1,
                    (list->len - i - 1) * sizeof(*list->items));
            --list->len;
            return;
        }
    }
}

/* --- static option tables (filtered against caps at build time) ---- */
static const char *k_view_items[]     = { "simple", "advanced", NULL };
/* All codec ids in display order. Filtered by caps in build_codec_items(). */
static const char *k_all_codecs[]     = {
    "auto", "h264", "hevc", "hevc_10bit", "hevc_hdr",
    "av1", "av1_10bit", "av1_hdr", "vp8", "vp9", "h264_software", NULL
};
static const char *k_audio_codecs[]   = { "opus", "aac", NULL };
static const char *k_quality_items[]  = { "constant_bitrate", "medium", "high", "very_high", "ultra", NULL };
static const char *k_color_range[]    = { "limited", "full", NULL };
static const char *k_framerate_mode[] = { "auto", "constant", "variable", NULL };

/* --- titled-frame helper ------------------------------------------- */

/* Titled-frame helper lives in widgets.c as gsr_w_titled_frame now;
 * keep this thin wrapper so the existing call sites read unchanged. */
static Widget make_titled_frame(Widget parent, const char *title, Widget *out_frame)
{
    return gsr_w_titled_frame(parent, title, out_frame);
}

static Widget make_hrow(Widget parent)
{
    return XtVaCreateManagedWidget("hrow",
        xmRowColumnWidgetClass, parent,
        XmNorientation, XmHORIZONTAL,
        XmNpacking,     XmPACK_TIGHT,
        XmNspacing,     6,
        NULL);
}

/* Build a NULL-terminated array of codec ids the host actually supports.
 * Caller frees the array (not the strings themselves; they're string literals). */
static const char **build_supported_codec_list(const GsrCapabilities *caps, size_t *out_count)
{
    size_t n = 0;
    for(size_t i = 0; k_all_codecs[i]; ++i)
        if(gsr_capabilities_codec_supported(caps, k_all_codecs[i]))
            ++n;

    const char **out = (const char **)malloc((n + 1) * sizeof(*out));
    size_t       w = 0;
    for(size_t i = 0; k_all_codecs[i]; ++i)
        if(gsr_capabilities_codec_supported(caps, k_all_codecs[i]))
            out[w++] = k_all_codecs[i];
    out[w] = NULL;
    *out_count = n;
    return out;
}

/* Build a NULL-terminated array of record-area option ids: any of
 * window/follow_focused/portal supported, plus every detected monitor name.
 * Returns a malloc'd array; per-entry strings are either string literals or
 * pointers into caps->capture_options.monitors[].name (also owned by caps),
 * so do not free them — only free the array itself. */
static const char **build_record_area_list(const GsrCapabilities *caps, size_t *out_count)
{
    size_t monitor_count = caps->capture_options.monitors.len;
    size_t cap = 4 + monitor_count;
    const char **out = (const char **)malloc((cap + 1) * sizeof(*out));
    size_t w = 0;
    if(caps->capture_options.window)  out[w++] = "window";
    if(caps->capture_options.focused) out[w++] = "focused";
    if(caps->capture_options.portal)  out[w++] = "portal";
    for(size_t i = 0; i < monitor_count; ++i)
        out[w++] = caps->capture_options.monitors.items[i].name;
    out[w] = NULL;
    *out_count = w;
    return out;
}

/* --- audio row helpers ---------------------------------------------- */

static void audio_row_remove_cb(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)call;
    AudioRow *r = (AudioRow *)client;
    rows_erase(rows_get(r->owner), r);
    XtDestroyWidget(r->row);
    free(r);
}

/* `device_names` and `app_names` are NULL-terminated arrays of C strings
 * (caller owns; not copied — must outlive the combo). */
static AudioRow *audio_row_create(PageCommonSettings *p, AudioRowKind kind,
                                  const char *initial_value,
                                  const char *const *device_names,
                                  const char *const *app_names)
{
    AudioRow *r = (AudioRow *)calloc(1, sizeof(*r));
    r->kind  = kind;
    r->owner = p;

    r->row = XtVaCreateManagedWidget("audio_row",
        xmRowColumnWidgetClass, p->audio_rows_box,
        XmNorientation, XmHORIZONTAL,
        XmNpacking,     XmPACK_TIGHT,
        XmNspacing,     6,
        NULL);

    const char *kind_label = (kind == AUDIO_ROW_DEVICE)     ? "Device:"
                            : (kind == AUDIO_ROW_APP)       ? "App:"
                                                            : "Custom app:";
    gsr_w_label(r->row, kind_label);

    switch(kind) {
    case AUDIO_ROW_DEVICE:
        r->value = gsr_w_combo(r->row, device_names, initial_value);
        break;
    case AUDIO_ROW_APP:
        r->value = gsr_w_combo(r->row, app_names, initial_value);
        break;
    case AUDIO_ROW_APP_CUSTOM:
        r->value = gsr_w_text(r->row, initial_value);
        break;
    }

    r->remove_btn = gsr_w_button(r->row, "Remove");
    XtAddCallback(r->remove_btn, XmNactivateCallback, audio_row_remove_cb, r);

    rows_push(rows_get(p), r);
    return r;
}

/* Build a NULL-terminated const char** view over an AudioDeviceList.
 * Caller frees the array (not the names — owned by the AudioDeviceList). */
static const char **device_names_view(const AudioDeviceList *list)
{
    const char **out = (const char **)malloc((list->len + 1) * sizeof(*out));
    for(size_t i = 0; i < list->len; ++i)
        out[i] = list->items[i].name ? list->items[i].name : "";
    out[list->len] = NULL;
    return out;
}

static const char **string_array_view(const StringArray *list)
{
    const char **out = (const char **)malloc((list->len + 1) * sizeof(*out));
    for(size_t i = 0; i < list->len; ++i)
        out[i] = list->items[i] ? list->items[i] : "";
    out[list->len] = NULL;
    return out;
}

static void add_device_clicked(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)call;
    PageCommonSettings *p = (PageCommonSettings *)client;
    const char **names = device_names_view(&p->detected_devices);
    audio_row_create(p, AUDIO_ROW_DEVICE, NULL, names, NULL);
    free(names);
}

static void add_app_clicked(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)call;
    PageCommonSettings *p = (PageCommonSettings *)client;
    const char **names = string_array_view(&p->detected_apps);
    audio_row_create(p, AUDIO_ROW_APP, NULL, NULL, names);
    free(names);
}

static void add_custom_app_clicked(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)call;
    PageCommonSettings *p = (PageCommonSettings *)client;
    audio_row_create(p, AUDIO_ROW_APP_CUSTOM, NULL, NULL, NULL);
}

/* Populate rows from existing config (config.main_config.audio_input is
 * a StringArray of "device:NAME" / "app:NAME" entries). */
static void rebuild_audio_rows_from_config(PageCommonSettings *p, const Config *config)
{
    const char **dev_names = device_names_view(&p->detected_devices);
    const char **app_names = string_array_view(&p->detected_apps);
    for(size_t i = 0; i < config->main_config.audio_input.len; ++i) {
        const char *entry = config->main_config.audio_input.items[i];
        if(strncmp(entry, "device:", 7) == 0) {
            audio_row_create(p, AUDIO_ROW_DEVICE, entry + 7, dev_names, NULL);
        } else if(strncmp(entry, "app:", 4) == 0) {
            /* Choose APP vs APP_CUSTOM by whether the name matches a
             * detected app. Otherwise treat as a custom entry. */
            const char *value = entry + 4;
            bool found = false;
            for(size_t j = 0; j < p->detected_apps.len; ++j) {
                if(p->detected_apps.items[j] && strcmp(p->detected_apps.items[j], value) == 0) {
                    found = true;
                    break;
                }
            }
            audio_row_create(p, found ? AUDIO_ROW_APP : AUDIO_ROW_APP_CUSTOM,
                             value,
                             found ? NULL : NULL,
                             found ? app_names : NULL);
        }
    }
    free(dev_names);
    free(app_names);
}

/* --- conditional visibility ---------------------------------------- */

static void manage_set(Widget w, bool visible)
{
    if(!w) return;
    if(visible) XtManageChild(w);
    else        XtUnmanageChild(w);
}

static void apply_view_visibility(PageCommonSettings *p, const GsrCapabilities *caps)
{
    char *view = gsr_w_combo_get_text(p->view_combo);
    bool advanced = view && strcmp(view, "advanced") == 0;
    if(view) XtFree(view);

    manage_set(p->color_range_row,    advanced);
    manage_set(p->framerate_mode_row, advanced);
    manage_set(p->notifications_frame, advanced);

    bool overclock_ok = advanced
        && caps && caps->gpu_vendor == GSR_GPU_NVIDIA
        && caps->display_server != GSR_DISPLAY_WAYLAND;
    manage_set(p->overclock_toggle, overclock_ok);
}

static void apply_record_area_visibility(PageCommonSettings *p)
{
    char *area = gsr_w_combo_get_text(p->record_area_combo);
    bool follow_focused = area && strcmp(area, "focused") == 0;
    bool portal         = area && strcmp(area, "portal") == 0;
    bool window         = area && strcmp(area, "window") == 0;
    if(area) XtFree(area);

    manage_set(p->area_size_row,                follow_focused);
    manage_set(p->restore_portal_session_toggle, portal);
    manage_set(p->select_window_row,             window);
}

static void update_selected_window_label(PageCommonSettings *p)
{
    char buf[64];
    if(p->selected_window_slot && *p->selected_window_slot != 0)
        snprintf(buf, sizeof(buf), "Selected: 0x%lx", *p->selected_window_slot);
    else
        snprintf(buf, sizeof(buf), "(no window selected)");
    XmString xms = XmStringCreateLocalized(buf);
    XtVaSetValues(p->selected_window_label, XmNlabelString, xms, NULL);
    XmStringFree(xms);
}

static void on_window_picked(unsigned long w, void *user)
{
    PageCommonSettings *p = (PageCommonSettings *)user;
    if(p->selected_window_slot)
        *p->selected_window_slot = w;
    update_selected_window_label(p);
}

static void select_window_clicked(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)call;
    PageCommonSettings *p = (PageCommonSettings *)client;
    window_picker_run(p->root, on_window_picked, p);
}

static void about_clicked(Widget w, XtPointer client, XtPointer call)
{
    (void)client; (void)call;
    dialogs_show_about(w);
}

static void view_changed_cb(Widget combo, int index, void *user_data)
{
    (void)combo; (void)index;
    VisCtx *vc = (VisCtx *)user_data;
    apply_view_visibility(vc->page, vc->caps);
}

static void record_area_changed_cb(Widget combo, int index, void *user_data)
{
    (void)combo; (void)index;
    VisCtx *vc = (VisCtx *)user_data;
    apply_record_area_visibility(vc->page);
}

/* --- hub buttons --------------------------------------------------- */

static void hub_cb(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)call;
    NavCtx *c = (NavCtx *)client;
    page_common_settings_commit(c->page, c->config);
    app_state_save(c->config);
    if(c->nav) c->nav(c->target, c->user);
}

static NavCtx *make_nav_ctx(PageCommonSettings *p, Config *config,
                            const GsrCapabilities *caps,
                            page_nav_cb nav, void *user, PageId target)
{
    NavCtx *c = (NavCtx *)malloc(sizeof(*c));
    c->nav    = nav;
    c->user   = user;
    c->page   = p;
    c->config = config;
    c->caps   = caps;
    c->target = target;
    return c;
}

/* --- page construction ---------------------------------------------- */

void page_common_settings_create(Widget parent, PageCommonSettings *out,
                                 const Config *config,
                                 const GsrCapabilities *caps,
                                 unsigned long *selected_window_slot,
                                 page_nav_cb nav, void *user_data)
{
    out->selected_window_slot = selected_window_slot;
    /* Detect audio devices + running apps once. Quiet failures — both
     * lists may be empty if the recorder binary is missing or the
     * pipewire build doesn't support app audio. */
    audio_device_list_init(&out->detected_devices);
    string_array_init(&out->detected_apps);
    out->audio_rows = calloc(1, sizeof(AudioRowList));
    (void)audio_devices_query_inputs(&out->detected_devices);
    if(caps && caps->supports_app_audio)
        (void)audio_devices_query_applications(&out->detected_apps);

    out->root = XtVaCreateWidget("common_settings_page",
        xmFormWidgetClass, parent,
        XmNtopAttachment,    XmATTACH_FORM,
        XmNleftAttachment,   XmATTACH_FORM,
        XmNrightAttachment,  XmATTACH_FORM,
        XmNbottomAttachment, XmATTACH_FORM,
        NULL);

    /* Bottom button strip: hub buttons (Stream/Record/Replay) left-anchored
     * in a tight RowColumn; About button right-anchored in the same strip. */
    Widget btn_strip = XtVaCreateManagedWidget("btn_strip",
        xmFormWidgetClass, out->root,
        XmNbottomAttachment, XmATTACH_FORM,
        XmNleftAttachment,   XmATTACH_FORM,
        XmNrightAttachment,  XmATTACH_FORM,
        XmNbottomOffset,     12,
        XmNleftOffset,       12,
        XmNrightOffset,      12,
        NULL);

    Widget hub_group = XtVaCreateManagedWidget("hub_group",
        xmRowColumnWidgetClass, btn_strip,
        XmNorientation,      XmHORIZONTAL,
        XmNpacking,          XmPACK_TIGHT,
        XmNspacing,          12,
        XmNtopAttachment,    XmATTACH_FORM,
        XmNbottomAttachment, XmATTACH_FORM,
        XmNleftAttachment,   XmATTACH_FORM,
        NULL);
    /* Mnemonics: Alt+S Stream, Alt+R Record, Alt+P Replay (P avoids
     * conflict with R; Replay's R is already used as Record's mnemonic). */
    out->stream_btn = gsr_w_button_m(hub_group, "Stream", 'S');
    out->record_btn = gsr_w_button_m(hub_group, "Record", 'R');
    out->replay_btn = gsr_w_button_m(hub_group, "Replay", 'p');

    /* About button: same strip, right-aligned. */
    XmString about_xms = XmStringCreateLocalized((char *)"About");
    out->about_btn = XtVaCreateManagedWidget("about",
        xmPushButtonWidgetClass, btn_strip,
        XmNlabelString,      about_xms,
        XmNtopAttachment,    XmATTACH_FORM,
        XmNbottomAttachment, XmATTACH_FORM,
        XmNrightAttachment,  XmATTACH_FORM,
        XmNmnemonic,         (KeySym)'A',
        NULL);
    XmStringFree(about_xms);
    XtAddCallback(out->about_btn, XmNactivateCallback, about_clicked, out);

    /* Wrap the scrolled area in an XmFrame so it has the same etched-in
     * border dtterm uses for its scrolled terminal area
     * (TermView.c:1010 — "dtTermScrolledWindowFrame"). */
    Widget sw_frame = XtVaCreateManagedWidget("sw_frame",
        xmFrameWidgetClass, out->root,
        XmNshadowType,        XmSHADOW_ETCHED_IN,
        XmNtopAttachment,     XmATTACH_FORM,
        XmNleftAttachment,    XmATTACH_FORM,
        XmNrightAttachment,   XmATTACH_FORM,
        XmNbottomAttachment,  XmATTACH_WIDGET,
        XmNbottomWidget,      btn_strip,
        XmNtopOffset,         4,
        XmNleftOffset,        4,
        XmNrightOffset,       4,
        XmNbottomOffset,      4,
        NULL);

    /* Scrolled-window policies mirror dtterm's "dtTermScrolledWindow"
     * (TermView.c:1018-1027). Notable departures from our previous setup:
     *   - XmSTATIC scrollbar display: scrollbars are *always* visible,
     *     matching the CDE convention. Previously XmAS_NEEDED hid them
     *     when content fit, which feels foreign in a CDE session.
     *   - XmVARIABLE visualPolicy lets the scrolled window resize to
     *     fit its child rather than clip aggressively.
     *   - XmAUTOMATIC scrollingPolicy is retained — dtterm uses
     *     XmAPPLICATION_DEFINED because the term widget manages its own
     *     scrolling, which we don't. */
    Widget sw = XtVaCreateManagedWidget("sw",
        xmScrolledWindowWidgetClass, sw_frame,
        XmNscrollingPolicy,        XmAUTOMATIC,
        XmNvisualPolicy,           XmVARIABLE,
        XmNscrollBarDisplayPolicy, XmSTATIC,
        NULL);

    Widget content = XtVaCreateManagedWidget("content",
        xmRowColumnWidgetClass, sw,
        XmNorientation, XmVERTICAL,
        XmNpacking,     XmPACK_TIGHT,
        XmNspacing,     8,
        NULL);

    /* --- View frame --- */
    {
        Widget rc = make_titled_frame(content, "View", NULL);
        Widget row = make_hrow(rc);
        gsr_w_label(row, "View:");
        const char *initial = config->main_config.advanced_view ? "advanced" : "simple";
        out->view_combo = gsr_w_combo(row, k_view_items, initial);
    }

    /* --- Capture target frame --- */
    {
        Widget rc = make_titled_frame(content, "Capture target", NULL);
        Widget r0 = make_hrow(rc);
        gsr_w_label(r0, "Record area:");

        size_t       area_count = 0;
        const char **areas = build_record_area_list(caps, &area_count);
        out->record_area_combo = gsr_w_combo(r0, areas,
            config->main_config.record_area_option);
        free(areas);

        /* Area size row — only visible when record_area=follow_focused. */
        out->area_size_row = make_hrow(rc);
        gsr_w_label(out->area_size_row, "Area W:");
        out->area_width_spin  = gsr_w_spin_int(out->area_size_row, 5, 10000,
            config->main_config.record_area_width ?
            config->main_config.record_area_width : 1920);
        gsr_w_label(out->area_size_row, "H:");
        out->area_height_spin = gsr_w_spin_int(out->area_size_row, 5, 10000,
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

        /* Visible only when record_area=portal. */
        out->restore_portal_session_toggle = gsr_w_toggle(rc,
            "Restore portal session",
            config->main_config.restore_portal_session);

        /* Visible only when record_area=window. */
        out->select_window_row    = gsr_w_hrow(rc);
        out->select_window_btn    = gsr_w_button(out->select_window_row, "Select window...");
        out->selected_window_label = gsr_w_label(out->select_window_row, "(no window selected)");
        XtAddCallback(out->select_window_btn, XmNactivateCallback,
                      select_window_clicked, out);
        update_selected_window_label(out);
    }

    /* --- Audio frame --- */
    {
        Widget rc = make_titled_frame(content, "Audio", NULL);
        Widget r0 = make_hrow(rc);
        gsr_w_label(r0, "Audio codec:");
        out->audio_codec_combo = gsr_w_combo(r0, k_audio_codecs,
            config->main_config.audio_codec);

        out->merge_audio_toggle = gsr_w_toggle(rc,
            "Merge all audio sources into a single track",
            config->main_config.merge_audio_tracks);
        out->record_app_audio_inverted_toggle = gsr_w_toggle(rc,
            "Record audio from all applications except the selected ones",
            config->main_config.record_app_audio_inverted);

        if(caps && !caps->supports_app_audio)
            XtSetSensitive(out->record_app_audio_inverted_toggle, False);

        /* Audio devices list — populated below from config; rows
         * added/removed dynamically via the three Add buttons. */
        Widget add_row = make_hrow(rc);
        out->add_device_btn     = gsr_w_button(add_row, "Add audio device");
        out->add_app_btn        = gsr_w_button(add_row, "Add application audio");
        out->add_custom_app_btn = gsr_w_button(add_row, "Add custom application audio");

        out->audio_rows_box = XtVaCreateManagedWidget("audio_rows_box",
            xmRowColumnWidgetClass, rc,
            XmNorientation, XmVERTICAL,
            XmNpacking,     XmPACK_TIGHT,
            XmNspacing,     2,
            NULL);

        if(caps && !caps->supports_app_audio) {
            XtSetSensitive(out->add_app_btn, False);
            XtSetSensitive(out->add_custom_app_btn, False);
        }

        XtAddCallback(out->add_device_btn,     XmNactivateCallback, add_device_clicked,     out);
        XtAddCallback(out->add_app_btn,        XmNactivateCallback, add_app_clicked,        out);
        XtAddCallback(out->add_custom_app_btn, XmNactivateCallback, add_custom_app_clicked, out);

        rebuild_audio_rows_from_config(out, config);
    }

    /* --- Video frame --- */
    {
        Widget rc = make_titled_frame(content, "Video", NULL);

        Widget r0 = make_hrow(rc);
        gsr_w_label(r0, "Quality:");
        out->quality_combo = gsr_w_combo(r0, k_quality_items,
            config->main_config.quality);

        Widget r1 = make_hrow(rc);
        gsr_w_label(r1, "Bitrate (kbps):");
        out->bitrate_spin = gsr_w_spin_int(r1, 1, 500000,
            config->main_config.video_bitrate);

        /* Codec combo only lists detected codecs (plus "auto"). */
        Widget r2 = make_hrow(rc);
        gsr_w_label(r2, "Codec:");
        size_t       codec_count = 0;
        const char **codecs = build_supported_codec_list(caps, &codec_count);
        out->codec_combo = gsr_w_combo(r2, codecs, config->main_config.codec);
        free(codecs);

        /* Advanced-only: color range. */
        out->color_range_row = make_hrow(rc);
        gsr_w_label(out->color_range_row, "Color range:");
        out->color_range_combo = gsr_w_combo(out->color_range_row, k_color_range,
            config->main_config.color_range);

        Widget r4 = make_hrow(rc);
        gsr_w_label(r4, "Frame rate:");
        out->fps_spin = gsr_w_spin_int(r4, 1, 500, config->main_config.fps);

        /* Advanced-only: frame rate mode. */
        out->framerate_mode_row = make_hrow(rc);
        gsr_w_label(out->framerate_mode_row, "Frame rate mode:");
        out->framerate_mode_combo = gsr_w_combo(out->framerate_mode_row,
            k_framerate_mode, config->main_config.framerate_mode);

        out->record_cursor_toggle = gsr_w_toggle(rc,
            "Record cursor", config->main_config.record_cursor);
        out->overclock_toggle = gsr_w_toggle(rc,
            "Overclock memory transfer rate (NVIDIA workaround)",
            config->main_config.overclock);
    }

    /* --- Notifications frame (advanced-only) --- */
    {
        Widget rc = make_titled_frame(content, "Notifications", &out->notifications_frame);
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

    /* Visibility callbacks (after all widgets exist). */
    VisCtx *vc = (VisCtx *)malloc(sizeof(*vc));
    vc->page = out;
    vc->caps = caps;
    gsr_w_combo_on_change(out->view_combo,        view_changed_cb,        vc);
    gsr_w_combo_on_change(out->record_area_combo, record_area_changed_cb, vc);

    apply_view_visibility(out, caps);
    apply_record_area_visibility(out);

    /* Hub buttons. */
    XtAddCallback(out->stream_btn, XmNactivateCallback, hub_cb,
                  make_nav_ctx(out, (Config *)config, caps, nav, user_data, PAGE_STREAMING));
    XtAddCallback(out->record_btn, XmNactivateCallback, hub_cb,
                  make_nav_ctx(out, (Config *)config, caps, nav, user_data, PAGE_RECORDING));
    XtAddCallback(out->replay_btn, XmNactivateCallback, hub_cb,
                  make_nav_ctx(out, (Config *)config, caps, nav, user_data, PAGE_REPLAY));
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
    {
        char *raw = gsr_w_combo_get_text(p->view_combo);
        if(raw) {
            config->main_config.advanced_view = strcmp(raw, "advanced") == 0;
            XtFree(raw);
        }
    }

    commit_combo_str(p->record_area_combo, &config->main_config.record_area_option);
    config->main_config.record_area_width  = gsr_w_spin_get(p->area_width_spin);
    config->main_config.record_area_height = gsr_w_spin_get(p->area_height_spin);
    config->main_config.video_width        = gsr_w_spin_get(p->video_width_spin);
    config->main_config.video_height       = gsr_w_spin_get(p->video_height_spin);
    config->main_config.change_video_resolution = gsr_w_toggle_get(p->change_video_resolution_toggle);
    config->main_config.restore_portal_session  = gsr_w_toggle_get(p->restore_portal_session_toggle);

    commit_combo_str(p->audio_codec_combo, &config->main_config.audio_codec);
    config->main_config.merge_audio_tracks        = gsr_w_toggle_get(p->merge_audio_toggle);
    config->main_config.record_app_audio_inverted = gsr_w_toggle_get(p->record_app_audio_inverted_toggle);

    /* Rebuild audio_input from rows. */
    string_array_clear(&config->main_config.audio_input);
    AudioRowList *rows = (AudioRowList *)p->audio_rows;
    for(size_t i = 0; rows && i < rows->len; ++i) {
        AudioRow *r = rows->items[i];
        char *raw = NULL;
        if(r->kind == AUDIO_ROW_APP_CUSTOM)
            raw = gsr_w_text_get(r->value);
        else
            raw = gsr_w_combo_get_text(r->value);
        if(raw && raw[0]) {
            const char *prefix = (r->kind == AUDIO_ROW_DEVICE) ? "device:" : "app:";
            string_array_push(&config->main_config.audio_input,
                              xasprintf("%s%s", prefix, raw));
        }
        if(raw) XtFree(raw);
    }

    commit_combo_str(p->quality_combo,        &config->main_config.quality);
    config->main_config.video_bitrate = gsr_w_spin_get(p->bitrate_spin);
    commit_combo_str(p->codec_combo,          &config->main_config.codec);
    commit_combo_str(p->color_range_combo,    &config->main_config.color_range);
    config->main_config.fps = gsr_w_spin_get(p->fps_spin);
    commit_combo_str(p->framerate_mode_combo, &config->main_config.framerate_mode);
    config->main_config.record_cursor = gsr_w_toggle_get(p->record_cursor_toggle);
    config->main_config.overclock     = gsr_w_toggle_get(p->overclock_toggle);

    config->main_config.show_recording_started_notifications = gsr_w_toggle_get(p->notif_started_toggle);
    config->main_config.show_recording_stopped_notifications = gsr_w_toggle_get(p->notif_stopped_toggle);
    config->main_config.show_recording_saved_notifications   = gsr_w_toggle_get(p->notif_saved_toggle);
}
