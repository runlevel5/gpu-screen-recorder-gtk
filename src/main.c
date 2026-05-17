/*
 * gpu-screen-recorder-motif — Motif/X11 frontend (C99).
 *
 * Orchestrates: config load/save, Xt init, page wiring, subprocess poll +
 * hotkey drain timers, demo hotkey, clean WM_DELETE_WINDOW handling.
 *
 * The original GTK src/main.cpp / src/config.hpp remain on disk on this
 * branch as a porting reference; they are not compiled.
 */

#include <Xm/Form.h>
#include <Xm/MainW.h>
#include <Xm/Protocols.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/SeparatoG.h>
#include <Xm/Xm.h>
/* XmRendition*, XmRenderTable*, XmFONT_IS_XFT are declared by <Xm/Xm.h>;
 * OpenMotif does not ship a separate Xm/RenderT.h. */

#include <X11/Intrinsic.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_state.h"
#include "audio_devices.h"
#include "capabilities.h"
#include "notifications.h"
#include "recorder_args.h"
#include "recorder_process.h"
#include "tray.h"
#include "ui/page_common_settings.h"
#include "ui/page_recording.h"
#include "ui/page_replay.h"
#include "ui/page_streaming.h"
#include "ui/ui_nav.h"
#include "x11_hotkeys.h"

#ifndef GSR_VERSION
#define GSR_VERSION "unknown"
#endif

#define POLL_RECORDER_INTERVAL_MS 500
#define HOTKEY_DRAIN_INTERVAL_MS   50

typedef struct {
    XtAppContext app;
    Display     *display;
    Widget       toplevel;
    Widget       page_host;
    Widget       pages[PAGE_COUNT];
    PageId       current_page;

    PageCommonSettings common;
    PageReplay         replay;
    PageRecording      recording;
    PageStreaming      streaming;

    Config          config;
    GsrCapabilities caps;
    ConfigHotkey    test_hotkey;       /* Ctrl+Alt+R demo — validates root-window key drain */
    bool            running;

    /* Recorder session state. */
    RecorderMode    active_mode;       /* meaningful only when recorder_active */
    bool            recorder_active;
    bool            recorder_paused;
    unsigned long   selected_window;   /* set by the window picker */

    /* Hotkeys currently grabbed for the visible spoke page. Page-change
     * ungrabs the old set and grabs the new set. */
#define GSR_MAX_GRABBED_HOTKEYS 6
    struct {
        ConfigHotkey  hotkey;
        SessionAction action;
        PageId        source;
    } grabbed_hotkeys[GSR_MAX_GRABBED_HOTKEYS];
    size_t grabbed_hotkey_count;

    Tray   *tray;          /* may be NULL if no _NET_SYSTEM_TRAY owner */
    bool    window_hidden;

    Widget  tray_menu;
    Widget  menu_show_hide_btn;
    Widget  menu_stop_btn;
    Widget  menu_pause_btn;
    Widget  menu_save_btn;
    Widget  menu_exit_btn;
} AppCtx;

static const char *page_id_to_mode_name(PageId p)
{
    switch(p) {
    case PAGE_RECORDING: return "record";
    case PAGE_REPLAY:    return "replay";
    case PAGE_STREAMING: return "stream";
    default:             return "?";
    }
}

static bool page_id_to_mode(PageId p, RecorderMode *out)
{
    switch(p) {
    case PAGE_RECORDING: *out = RECORDER_MODE_RECORD; return true;
    case PAGE_REPLAY:    *out = RECORDER_MODE_REPLAY; return true;
    case PAGE_STREAMING: *out = RECORDER_MODE_STREAM; return true;
    default: return false;
    }
}

static void sync_tray_state(AppCtx *ctx);
static void sync_button_labels(AppCtx *ctx);
static void set_btn_label(Widget btn, const char *text);

/* --- Page switching + commit --------------------------------------- */

static void commit_current_page(AppCtx *ctx)
{
    switch(ctx->current_page) {
    case PAGE_COMMON_SETTINGS:
        page_common_settings_commit(&ctx->common, &ctx->config); break;
    case PAGE_REPLAY:
        page_replay_commit(&ctx->replay, &ctx->config); break;
    case PAGE_RECORDING:
        page_recording_commit(&ctx->recording, &ctx->config); break;
    case PAGE_STREAMING:
        page_streaming_commit(&ctx->streaming, &ctx->config); break;
    case PAGE_COUNT:
        break;
    }
}

static void ungrab_page_hotkeys(AppCtx *ctx);
static void grab_page_hotkeys(AppCtx *ctx, PageId page);

/* Per-page preferred dimensions. The shell has XmNallowShellResize=True,
 * so resizing page_host while a new page is being managed will cause the
 * toplevel to follow. Common settings is denser; spoke pages are tight. */
typedef struct { int w, h; } PageSize;
static const PageSize k_page_sizes[PAGE_COUNT] = {
    [PAGE_COMMON_SETTINGS] = { 540, 650 },
    [PAGE_REPLAY]          = { 313, 348 },
    [PAGE_RECORDING]       = { 337, 377 },
    [PAGE_STREAMING]       = { 313, 348 },
};

static void switch_to_page(AppCtx *ctx, PageId target)
{
    if(target < 0 || target >= PAGE_COUNT || target == ctx->current_page)
        return;

    /* Order matters: unmanage old, manage new FIRST so the new page's
     * widgets are realised. Then force the size — if we set sizes before
     * managing, XtManageChild's geometry pass re-queries the new page's
     * natural width and (with XmNallowShellResize=True) shrinks the shell
     * down to it, which is how the user saw the Recording page collapse
     * to ~214px instead of 337px. */
    XtUnmanageChild(ctx->pages[ctx->current_page]);
    XtManageChild  (ctx->pages[target]);
    ctx->current_page = target;

    const Dimension w = (Dimension)k_page_sizes[target].w;
    const Dimension h = (Dimension)k_page_sizes[target].h;

    XtVaSetValues(ctx->page_host, XmNwidth, w, XmNheight, h, NULL);
    if(ctx->toplevel) {
        XtVaSetValues(ctx->toplevel, XmNwidth, w, XmNheight, h, NULL);
        /* Belt and braces: tell X directly. Motif's geometry layer can
         * decline a shell resize after-the-fact; XResizeWindow can't be
         * vetoed by the toolkit. The WM may still clamp, but a sane WM
         * will honour any size above its minimum. */
        if(XtIsRealized(ctx->toplevel)) {
            XResizeWindow(ctx->display, XtWindow(ctx->toplevel), w, h);
            XFlush(ctx->display);
        }
    }

    ungrab_page_hotkeys(ctx);
    grab_page_hotkeys(ctx, target);
}

static void on_page_nav(PageId target, void *user_data)
{
    switch_to_page((AppCtx *)user_data, target);
}

/* --- Session dispatch (page Start/Save/Pause → subprocess) --------- */

static void session_start(AppCtx *ctx, RecorderMode mode)
{
    if(ctx->recorder_active) {
        notifications_show_warning(ctx->toplevel, "Already recording",
            "Another mode is already running. Stop it first.");
        return;
    }

    RecorderArgsRequest req;
    memset(&req, 0, sizeof(req));
    req.mode            = mode;
    req.config          = &ctx->config;
    req.caps            = &ctx->caps;
    req.selected_window = ctx->selected_window;

    char **argv = NULL;
    RecorderArgsStatus s = recorder_args_build(&req, &argv, NULL);
    if(s != RA_BUILD_OK) {
        notifications_show_error(ctx->toplevel, "Could not start recorder",
            recorder_args_status_str(s));
        return;
    }

    fprintf(stderr, "[session] spawn:");
    for(size_t i = 0; argv[i]; ++i) fprintf(stderr, " %s", argv[i]);
    fprintf(stderr, "\n");

    bool spawned = recorder_process_spawn((const char *const *)argv);
    recorder_args_free(argv);
    if(!spawned) {
        notifications_show_error(ctx->toplevel, "Could not start recorder",
            "fork/exec failed. Is gpu-screen-recorder installed and on $PATH?");
        return;
    }
    ctx->active_mode     = mode;
    ctx->recorder_active = true;
    ctx->recorder_paused = false;
    sync_tray_state(ctx);
    sync_button_labels(ctx);
}

static void session_stop(AppCtx *ctx)
{
    if(!ctx->recorder_active) {
        fprintf(stderr, "[session] stop ignored: no recorder running\n");
        return;
    }
    /* SIGINT — gpu-screen-recorder finalises and exits cleanly. The
     * polling timer will pick up the exit and clear recorder_active. */
    recorder_process_send_signal(SIGINT);
}

static void ungrab_page_hotkeys(AppCtx *ctx)
{
    for(size_t i = 0; i < ctx->grabbed_hotkey_count; ++i)
        (void)gsr_hotkey_grab(ctx->display, ctx->grabbed_hotkeys[i].hotkey, false);
    ctx->grabbed_hotkey_count = 0;
}

static void try_grab(AppCtx *ctx, ConfigHotkey hk, SessionAction action, PageId source)
{
    if(hk.keysym == 0 && hk.modifiers == 0) return;
    if(ctx->grabbed_hotkey_count >= GSR_MAX_GRABBED_HOTKEYS) return;
    if(!gsr_hotkey_grab(ctx->display, hk, true)) {
        fprintf(stderr, "[hotkey] grab failed for keysym=0x%lx mods=0x%x — "
                        "likely held by another client\n",
                (unsigned long)hk.keysym, (unsigned)hk.modifiers);
        return;
    }
    ctx->grabbed_hotkeys[ctx->grabbed_hotkey_count].hotkey = hk;
    ctx->grabbed_hotkeys[ctx->grabbed_hotkey_count].action = action;
    ctx->grabbed_hotkeys[ctx->grabbed_hotkey_count].source = source;
    ++ctx->grabbed_hotkey_count;
}

static void grab_page_hotkeys(AppCtx *ctx, PageId page)
{
    switch(page) {
    case PAGE_REPLAY:
        try_grab(ctx, ctx->config.replay_config.start_stop_recording_hotkey,
                 SESSION_TOGGLE_RUN, PAGE_REPLAY);
        try_grab(ctx, ctx->config.replay_config.save_recording_hotkey,
                 SESSION_SAVE, PAGE_REPLAY);
        break;
    case PAGE_RECORDING:
        try_grab(ctx, ctx->config.record_config.start_stop_recording_hotkey,
                 SESSION_TOGGLE_RUN, PAGE_RECORDING);
        try_grab(ctx, ctx->config.record_config.pause_unpause_recording_hotkey,
                 SESSION_PAUSE, PAGE_RECORDING);
        break;
    case PAGE_STREAMING:
        try_grab(ctx, ctx->config.streaming_config.start_stop_recording_hotkey,
                 SESSION_TOGGLE_RUN, PAGE_STREAMING);
        break;
    default:
        break;
    }
}

static void on_page_session(SessionAction action, PageId source, void *user_data)
{
    AppCtx *ctx = (AppCtx *)user_data;
    RecorderMode mode;
    bool have_mode = page_id_to_mode(source, &mode);

    switch(action) {
    case SESSION_TOGGLE_RUN:
        if(!have_mode) return;
        if(ctx->recorder_active) {
            if(ctx->active_mode == mode) {
                session_stop(ctx);
            } else {
                fprintf(stderr, "[session] cannot start %s: %s already running\n",
                        page_id_to_mode_name(source),
                        ctx->active_mode == RECORDER_MODE_RECORD ? "record"
                      : ctx->active_mode == RECORDER_MODE_REPLAY ? "replay" : "stream");
            }
        } else {
            session_start(ctx, mode);
        }
        break;
    case SESSION_PAUSE:
        if(ctx->recorder_active && ctx->active_mode == RECORDER_MODE_RECORD) {
            recorder_process_send_signal(SIGUSR2);
            ctx->recorder_paused = !ctx->recorder_paused;
            sync_tray_state(ctx);
            sync_button_labels(ctx);
        }
        break;
    case SESSION_SAVE:
        if(ctx->recorder_active && ctx->active_mode == RECORDER_MODE_REPLAY)
            recorder_process_send_signal(SIGUSR1);
        break;
    }
}

/* --- Timers -------------------------------------------------------- */

static void poll_recorder_subprocess(XtPointer client_data, XtIntervalId *id)
{
    (void)id;
    AppCtx *ctx = (AppCtx *)client_data;
    int status = 0;
    if(recorder_process_poll(&status)) {
        fprintf(stderr, "[recorder] subprocess exited (mode=%d, status=%d)\n",
                (int)ctx->active_mode, status);
        ctx->recorder_active = false;
        ctx->recorder_paused = false;
        sync_tray_state(ctx);
        sync_button_labels(ctx);
    }
    XtAppAddTimeOut(ctx->app, POLL_RECORDER_INTERVAL_MS,
                    poll_recorder_subprocess, ctx);
}

static void on_hotkey_fired(KeySym ks, unsigned int x11_mods, void *user_data)
{
    AppCtx  *ctx = (AppCtx *)user_data;
    uint32_t gsr_mods = gsr_x11_mask_to_gsr_mod(x11_mods);

    /* Match against currently-grabbed bindings for the visible page. */
    for(size_t i = 0; i < ctx->grabbed_hotkey_count; ++i) {
        const ConfigHotkey *hk = &ctx->grabbed_hotkeys[i].hotkey;
        if((KeySym)hk->keysym == ks && (uint32_t)hk->modifiers == gsr_mods) {
            fprintf(stderr, "[hotkey] fire keysym=0x%lx (%s) -> action=%d source=%d\n",
                    (unsigned long)ks,
                    XKeysymToString(ks) ? XKeysymToString(ks) : "?",
                    (int)ctx->grabbed_hotkeys[i].action,
                    (int)ctx->grabbed_hotkeys[i].source);
            on_page_session(ctx->grabbed_hotkeys[i].action,
                            ctx->grabbed_hotkeys[i].source, ctx);
            return;
        }
    }

    /* Demo (Ctrl+Alt+R) or otherwise-grabbed-but-not-matched key — log only. */
    fprintf(stderr, "[hotkey] unmatched keysym=0x%lx (%s) x11_mods=0x%x\n",
            (unsigned long)ks,
            XKeysymToString(ks) ? XKeysymToString(ks) : "?",
            x11_mods);
}

static void drain_root_hotkeys(XtPointer client_data, XtIntervalId *id)
{
    (void)id;
    AppCtx *ctx = (AppCtx *)client_data;
    gsr_hotkey_drain_root_events(ctx->display, on_hotkey_fired, ctx);
    XtAppAddTimeOut(ctx->app, HOTKEY_DRAIN_INTERVAL_MS,
                    drain_root_hotkeys, ctx);
}

/* --- Tray --------------------------------------------------------- */

static TrayState session_to_tray_state(const AppCtx *ctx)
{
    if(!ctx->recorder_active) return TRAY_STATE_IDLE;
    if(ctx->active_mode == RECORDER_MODE_STREAM) return TRAY_STATE_STREAMING;
    if(ctx->active_mode == RECORDER_MODE_RECORD && ctx->recorder_paused)
        return TRAY_STATE_PAUSED;
    return TRAY_STATE_RECORDING;
}

static void sync_tray_state(AppCtx *ctx)
{
    if(ctx->tray)
        tray_set_state(ctx->tray, session_to_tray_state(ctx));
}

/* Update each spoke page's Start/Pause button labels to reflect the
 * current recorder state. Matches the GTK port's behaviour: "Start
 * recording" flips to "Stop recording" while running, and the pause
 * button flips between "Pause recording" and "Unpause recording". */
static void sync_button_labels(AppCtx *ctx)
{
    bool rec_active    = ctx->recorder_active && ctx->active_mode == RECORDER_MODE_RECORD;
    bool replay_active = ctx->recorder_active && ctx->active_mode == RECORDER_MODE_REPLAY;
    bool strm_active   = ctx->recorder_active && ctx->active_mode == RECORDER_MODE_STREAM;

    set_btn_label(ctx->recording.start_btn,
                  rec_active ? "Stop recording" : "Start recording");
    set_btn_label(ctx->recording.pause_btn,
                  (rec_active && ctx->recorder_paused) ? "Unpause recording"
                                                       : "Pause recording");

    set_btn_label(ctx->replay.start_btn,
                  replay_active ? "Stop replay" : "Start replay");

    set_btn_label(ctx->streaming.start_btn,
                  strm_active ? "Stop streaming" : "Start streaming");
}

static void toggle_main_window(AppCtx *ctx)
{
    if(ctx->window_hidden) {
        XtMapWidget(ctx->toplevel);
        XRaiseWindow(ctx->display, XtWindow(ctx->toplevel));
        ctx->window_hidden = false;
    } else {
        XtUnmapWidget(ctx->toplevel);
        ctx->window_hidden = true;
    }
}

static void capture_geometry(AppCtx *ctx);  /* fwd-decl, defined below */

static void exit_app(AppCtx *ctx)
{
    commit_current_page(ctx);
    capture_geometry(ctx);
    app_state_save(&ctx->config);
    ctx->running = false;
    XtAppSetExitFlag(ctx->app);
}

/* Menu item callbacks. */
static void menu_show_hide_cb(Widget w, XtPointer client, XtPointer call)
{ (void)w; (void)call; toggle_main_window((AppCtx *)client); }
static void menu_stop_cb(Widget w, XtPointer client, XtPointer call)
{ (void)w; (void)call; AppCtx *c = (AppCtx *)client; if(c->recorder_active) session_stop(c); }
static void menu_pause_cb(Widget w, XtPointer client, XtPointer call)
{ (void)w; (void)call; on_page_session(SESSION_PAUSE, PAGE_RECORDING, client); }
static void menu_save_cb(Widget w, XtPointer client, XtPointer call)
{ (void)w; (void)call; on_page_session(SESSION_SAVE, PAGE_REPLAY, client); }
static void menu_exit_cb(Widget w, XtPointer client, XtPointer call)
{ (void)w; (void)call; exit_app((AppCtx *)client); }

static void set_btn_label(Widget btn, const char *text)
{
    XmString xms = XmStringCreateLocalized((char *)text);
    XtVaSetValues(btn, XmNlabelString, xms, NULL);
    XmStringFree(xms);
}

static void manage_set_w(Widget w, bool visible)
{
    if(!w) return;
    if(visible) XtManageChild(w);
    else        XtUnmanageChild(w);
}

static void show_tray_menu(AppCtx *ctx, int x_root, int y_root)
{
    if(!ctx->tray_menu) return;

    set_btn_label(ctx->menu_show_hide_btn,
                  ctx->window_hidden ? "Show window" : "Hide window");

    /* Stop / Pause / Save only when relevant. */
    bool stop_avail  = ctx->recorder_active;
    bool pause_avail = ctx->recorder_active && ctx->active_mode == RECORDER_MODE_RECORD;
    bool save_avail  = ctx->recorder_active && ctx->active_mode == RECORDER_MODE_REPLAY;

    if(stop_avail) {
        const char *label =
            ctx->active_mode == RECORDER_MODE_RECORD ? "Stop recording"
          : ctx->active_mode == RECORDER_MODE_REPLAY ? "Stop replay"
                                                     : "Stop streaming";
        set_btn_label(ctx->menu_stop_btn, label);
    }
    if(pause_avail) {
        set_btn_label(ctx->menu_pause_btn,
                      ctx->recorder_paused ? "Unpause recording" : "Pause recording");
    }

    manage_set_w(ctx->menu_stop_btn,  stop_avail);
    manage_set_w(ctx->menu_pause_btn, pause_avail);
    manage_set_w(ctx->menu_save_btn,  save_avail);

    /* Fabricate a button event for XmMenuPosition. */
    XButtonPressedEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type    = ButtonPress;
    ev.display = ctx->display;
    ev.window  = XtWindow(ctx->toplevel);
    ev.x_root  = x_root;
    ev.y_root  = y_root;
    ev.button  = Button3;
    ev.time    = CurrentTime;
    XmMenuPosition(ctx->tray_menu, &ev);
    XtManageChild(ctx->tray_menu);
}

static void on_tray_click(TrayClick click, int x_root, int y_root, void *user_data)
{
    AppCtx *ctx = (AppCtx *)user_data;
    if(click == TRAY_CLICK_LEFT)
        toggle_main_window(ctx);
    else
        show_tray_menu(ctx, x_root, y_root);
}

static void build_tray_menu(AppCtx *ctx)
{
    ctx->tray_menu = XmCreatePopupMenu(ctx->toplevel, (char *)"tray_menu", NULL, 0);

    XmString xms = XmStringCreateLocalized((char *)"Show window");
    ctx->menu_show_hide_btn = XtVaCreateManagedWidget("show_hide",
        xmPushButtonWidgetClass, ctx->tray_menu, XmNlabelString, xms, NULL);
    XmStringFree(xms);

    XtVaCreateManagedWidget("sep1",
        xmSeparatorGadgetClass, ctx->tray_menu, NULL);

    xms = XmStringCreateLocalized((char *)"Stop");
    ctx->menu_stop_btn = XtVaCreateWidget("stop",
        xmPushButtonWidgetClass, ctx->tray_menu, XmNlabelString, xms, NULL);
    XmStringFree(xms);

    xms = XmStringCreateLocalized((char *)"Pause recording");
    ctx->menu_pause_btn = XtVaCreateWidget("pause",
        xmPushButtonWidgetClass, ctx->tray_menu, XmNlabelString, xms, NULL);
    XmStringFree(xms);

    xms = XmStringCreateLocalized((char *)"Save replay");
    ctx->menu_save_btn = XtVaCreateWidget("save",
        xmPushButtonWidgetClass, ctx->tray_menu, XmNlabelString, xms, NULL);
    XmStringFree(xms);

    XtVaCreateManagedWidget("sep2",
        xmSeparatorGadgetClass, ctx->tray_menu, NULL);

    xms = XmStringCreateLocalized((char *)"Exit");
    ctx->menu_exit_btn = XtVaCreateManagedWidget("exit",
        xmPushButtonWidgetClass, ctx->tray_menu, XmNlabelString, xms, NULL);
    XmStringFree(xms);

    XtAddCallback(ctx->menu_show_hide_btn, XmNactivateCallback, menu_show_hide_cb, ctx);
    XtAddCallback(ctx->menu_stop_btn,      XmNactivateCallback, menu_stop_cb,      ctx);
    XtAddCallback(ctx->menu_pause_btn,     XmNactivateCallback, menu_pause_cb,     ctx);
    XtAddCallback(ctx->menu_save_btn,      XmNactivateCallback, menu_save_cb,      ctx);
    XtAddCallback(ctx->menu_exit_btn,      XmNactivateCallback, menu_exit_cb,      ctx);
}

/* --- WM_DELETE_WINDOW handler -------------------------------------- */

static void on_window_close(Widget w, XtPointer client_data, XtPointer call)
{
    (void)w; (void)call;
    exit_app((AppCtx *)client_data);
}

/* --- Setup --------------------------------------------------------- */

static void log_loaded_config(const Config *c)
{
    fprintf(stderr, "Loaded config:\n");
    fprintf(stderr, "  main.codec               = %s\n", c->main_config.codec ? c->main_config.codec : "(unset)");
    fprintf(stderr, "  main.fps                 = %d\n", c->main_config.fps);
    fprintf(stderr, "  main.video_bitrate       = %d\n", c->main_config.video_bitrate);
    fprintf(stderr, "  main.advanced_view       = %s\n", c->main_config.advanced_view ? "true" : "false");
    fprintf(stderr, "  main.audio_input entries = %zu\n", c->main_config.audio_input.len);
    fprintf(stderr, "  record.save_directory    = %s\n", c->record_config.save_directory ? c->record_config.save_directory : "(unset)");
    fprintf(stderr, "  replay.replay_time       = %d\n", c->replay_config.replay_time);
    fprintf(stderr, "  streaming.service        = %s\n", c->streaming_config.streaming_service ? c->streaming_config.streaming_service : "(unset)");
}

static void build_pages(AppCtx *ctx)
{
    page_common_settings_create(ctx->page_host, &ctx->common,    &ctx->config, &ctx->caps, &ctx->selected_window, on_page_nav, ctx);
    page_replay_create         (ctx->page_host, &ctx->replay,    &ctx->config, ctx->display, NULL, on_page_nav, on_page_session, ctx);
    page_recording_create      (ctx->page_host, &ctx->recording, &ctx->config, ctx->display, NULL, on_page_nav, on_page_session, ctx);
    page_streaming_create      (ctx->page_host, &ctx->streaming, &ctx->config, ctx->display, NULL, on_page_nav, on_page_session, ctx);

    ctx->pages[PAGE_COMMON_SETTINGS] = ctx->common.root;
    ctx->pages[PAGE_REPLAY]          = ctx->replay.root;
    ctx->pages[PAGE_RECORDING]       = ctx->recording.root;
    ctx->pages[PAGE_STREAMING]       = ctx->streaming.root;

    ctx->current_page = PAGE_COMMON_SETTINGS;
    XtManageChild(ctx->pages[PAGE_COMMON_SETTINGS]);
}

static void register_demo_hotkey(AppCtx *ctx)
{
    ctx->test_hotkey.keysym    = XK_r;
    ctx->test_hotkey.modifiers = gsr_modkey_to_mask(XK_Control_L) |
                                 gsr_modkey_to_mask(XK_Alt_L);
    if(!gsr_hotkey_grab(ctx->display, ctx->test_hotkey, true))
        fprintf(stderr, "[hotkey] WARNING: failed to grab Ctrl+Alt+R "
                        "(another client likely holds it)\n");
    else
        fprintf(stderr, "[hotkey] grabbed Ctrl+Alt+R as a demo. "
                        "Press it to see drain output.\n");
}

static void register_wm_protocols(AppCtx *ctx)
{
    Atom wm_delete = XmInternAtom(ctx->display, (char *)"WM_DELETE_WINDOW", False);
    XmAddWMProtocolCallback(ctx->toplevel, wm_delete, on_window_close, ctx);
}

#ifdef GSR_CDE_PALETTE
/* Resolve the user's CDE palette and apply it to the toplevel.
 *
 * CDE's session resources contain two parallel forms:
 *   1.  Dt*background: ~c      <- palette indirection scoped to the Dt
 *                                  class (only resolves for CDE apps that
 *                                  register the Dt color converter)
 *   2.  *background: #63637e...  <- resolved RGB written by dtsession,
 *                                  applies to any app via loose binding
 *
 * Query the loose-bound form via XrmGetResource against our own class
 * hierarchy ("GpuScreenRecorder.background"). That walks the resource
 * rules and picks up "*background" without colliding with "Dt*"-only
 * indirections we can't resolve.
 *
 * Propagate the result via XmChangeColor — Motif computes top/bottom
 * shadow + arm + select shades from the base background and cascades
 * to every descendant widget. */
/* Parse the live RESOURCE_MANAGER property each time. XtDatabase() returns
 * Xt's snapshot taken at XtVaAppInitialize, which on a CDE session can be
 * out of sync with what dtsession later wrote to the root window — and
 * Xt's snapshot stores some values as binary blobs rather than strings,
 * which is what gave us the "\xXX~c" garbage prefix. Reading the live
 * string property and parsing it via XrmGetStringDatabase matches what
 * xrdb -query reports. */
static bool query_resource(Display *d, const char *name, const char *class_,
                           char *out, size_t out_size)
{
    const char *rms = XResourceManagerString(d);
    if(!rms) return false;
    XrmDatabase db = XrmGetStringDatabase(rms);
    if(!db) return false;

    XrmValue val;
    char    *type = NULL;
    bool     ok   = XrmGetResource(db, name, class_, &type, &val)
                 && val.addr && val.size > 0;
    bool     produced = false;
    if(ok) {
        /* CDE palette indirection — can't resolve without the Dt converter. */
        if(val.addr[0] != '~') {
            size_t n = val.size < out_size - 1 ? val.size : out_size - 1;
            /* val.size from XrmGetStringDatabase usually excludes the
             * trailing NUL but defensively trim any embedded NULs. */
            while(n > 0 && ((unsigned char *)val.addr)[n - 1] == '\0') --n;
            memcpy(out, val.addr, n);
            out[n] = '\0';
            produced = out[0] != '\0';
        }
    }
    XrmDestroyDatabase(db);
    return produced;
}

static void apply_cde_palette(AppCtx *ctx)
{
    char bg[64] = {0};
    char fg[64] = {0};
    bool have_bg = query_resource(ctx->display,
        "GpuScreenRecorder.background", "GpuScreenRecorder.Background",
        bg, sizeof(bg));
    bool have_fg = query_resource(ctx->display,
        "GpuScreenRecorder.foreground", "GpuScreenRecorder.Foreground",
        fg, sizeof(fg));

    if(!have_bg && !have_fg) {
        fprintf(stderr, "[cde] no usable *background/*foreground in resource DB; "
                        "using Motif defaults\n");
        return;
    }

    Colormap cmap = DefaultColormap(ctx->display, DefaultScreen(ctx->display));

    if(have_bg) {
        XColor col, exact;
        if(XAllocNamedColor(ctx->display, cmap, bg, &col, &exact)) {
            XmChangeColor(ctx->toplevel, col.pixel);
            fprintf(stderr, "[cde] inherited *background=%s\n", bg);
        } else {
            fprintf(stderr, "[cde] could not allocate *background='%s'\n", bg);
        }
    }
    if(have_fg) {
        XColor col, exact;
        if(XAllocNamedColor(ctx->display, cmap, fg, &col, &exact)) {
            XtVaSetValues(ctx->toplevel, XmNforeground, col.pixel, NULL);
            fprintf(stderr, "[cde] inherited *foreground=%s\n", fg);
        } else {
            fprintf(stderr, "[cde] could not allocate *foreground='%s'\n", fg);
        }
    }
}
#endif

/* Try to load the session's *FontList XLFD as a single XFontStruct
 * (XmFONT_IS_FONT) rather than a FontSet. This sidesteps the FontSet
 * charset-coverage check that fails under en_US.UTF-8 because CDE's
 * bitmap fonts don't ship iso10646 variants — but Xlib will happily
 * resolve the wildcard XLFD to a concrete iso8859-1 or similar variant
 * via XLoadQueryFont, which is what every other CDE app on the box is
 * also doing. Returns true if the font was loaded and applied. */
static bool install_cde_fonts(AppCtx *ctx)
{
    char xlfd[256] = {0};
    if(!query_resource(ctx->display, "GpuScreenRecorder.fontList",
                       "GpuScreenRecorder.FontList", xlfd, sizeof(xlfd))) {
        return false;
    }

    /* CDE's *FontList resource value is in Motif fontList list syntax:
     *   "<xlfd1>:<xlfd2>:..."
     * Even with a single entry it ends with a trailing ':'. Strip the
     * trailing separator + whitespace so XmFontListEntryLoad sees a clean
     * XLFD when we load it as XmFONT_IS_FONT. Also stop at the first ':'
     * to ignore extra specs we don't need for a single XFontStruct. */
    char *colon = strchr(xlfd, ':');
    if(colon) *colon = '\0';
    size_t len = strlen(xlfd);
    while(len > 0 && (xlfd[len - 1] == ' ' || xlfd[len - 1] == '\t')) {
        xlfd[--len] = '\0';
    }
    if(len == 0)
        return false;

    /* Resolve the XLFD wildcard explicitly via XLoadQueryFont. If the
     * wildcard doesn't resolve (no matching font installed), bail out
     * cleanly so the caller can fall through to Xft. */
    XFontStruct *fs = XLoadQueryFont(ctx->display, xlfd);
    if(!fs) {
        fprintf(stderr, "[cde] no font matched XLFD '%s'\n", xlfd);
        return false;
    }
    /* We don't need the XFontStruct ourselves — XmFontListEntryLoad will
     * load its own. Free this probe. */
    XFreeFont(ctx->display, fs);

    XmFontListEntry entry = XmFontListEntryLoad(ctx->display, xlfd,
        XmFONT_IS_FONT, XmFONTLIST_DEFAULT_TAG);
    if(!entry) {
        fprintf(stderr, "[cde] XmFontListEntryLoad failed for '%s'\n", xlfd);
        return false;
    }
    XmFontList fl = XmFontListAppendEntry(NULL, entry);
    XmFontListEntryFree(&entry);
    XtVaSetValues(ctx->toplevel, XmNfontList, fl, NULL);
    XmFontListFree(fl);
    fprintf(stderr, "[cde] inherited *FontList=%s\n", xlfd);
    return true;
}

/* Install an Xft-based render table on the toplevel so all descendant
 * widgets get anti-aliased text instead of Motif's default bitmap fonts.
 * Must be called BEFORE XtRealizeWidget so children inherit.
 *
 * Fallback path for non-CDE sessions, or for CDE sessions where the
 * font wildcard fails to resolve. */
static void install_xft_fonts(AppCtx *ctx)
{
    Arg args[4];
    int n = 0;
    XtSetArg(args[n], XmNfontName, (XtPointer)"Sans:size=10"); ++n;
    XtSetArg(args[n], XmNfontType, XmFONT_IS_XFT);             ++n;
    XmRendition r = XmRenditionCreate(ctx->toplevel, (XmStringTag)"", args, n);
    if(!r) {
        fprintf(stderr, "[fonts] XmRenditionCreate failed; using Motif defaults\n");
        return;
    }
    XmRenderTable rt = XmRenderTableAddRenditions(NULL, &r, 1, XmMERGE_REPLACE);
    XmRenditionFree(r);
    XtVaSetValues(ctx->toplevel, XmNrenderTable, rt, NULL);
    /* RenderTable is now owned by the shell. */
}

static void apply_saved_geometry(AppCtx *ctx)
{
    /* Only restore window position, not size. Window size is governed by
     * per-page k_page_sizes[] now; restoring a saved size would conflict
     * with the explicit per-page sizing in switch_to_page (and could
     * pin the toplevel small enough to clip a wider page). */
    const MainConfig *m = &ctx->config.main_config;
    if(m->window_x != 0 || m->window_y != 0) {
        XtVaSetValues(ctx->toplevel,
            XmNx, m->window_x,
            XmNy, m->window_y,
            NULL);
    }
}

static void capture_geometry(AppCtx *ctx)
{
    if(!ctx->toplevel) return;
    Position  x = 0, y = 0;
    Dimension w = 0, h = 0;
    XtVaGetValues(ctx->toplevel,
        XmNx, &x, XmNy, &y, XmNwidth, &w, XmNheight, &h, NULL);
    ctx->config.main_config.window_x      = (int32_t)x;
    ctx->config.main_config.window_y      = (int32_t)y;
    ctx->config.main_config.window_width  = (int32_t)w;
    ctx->config.main_config.window_height = (int32_t)h;
}

int main(int argc, char **argv)
{
    AppCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.running = true;

    app_state_init(&ctx.config);
    app_state_load(&ctx.config);
    log_loaded_config(&ctx.config);

    gsr_capabilities_init(&ctx.caps);
    GsrInfoStatus info_status = gsr_capabilities_detect(&ctx.caps);
    if(info_status != GSR_INFO_OK) {
        /* Defer the popup until the toplevel exists. */
        fprintf(stderr, "[caps] WARNING: gpu-screen-recorder --info failed (status %d); "
                        "running with empty capabilities\n", (int)info_status);
    } else
        fprintf(stderr, "[caps] display_server=%d gpu_vendor=%d monitors=%zu "
                        "codecs(h264=%d hevc=%d av1=%d vp9=%d)\n",
                        (int)ctx.caps.display_server, (int)ctx.caps.gpu_vendor,
                        ctx.caps.capture_options.monitors.len,
                        ctx.caps.video_codecs.h264, ctx.caps.video_codecs.hevc,
                        ctx.caps.video_codecs.av1, ctx.caps.video_codecs.vp9);

    recorder_process_init();

    ctx.toplevel = XtVaAppInitialize(
        &ctx.app,
        "GpuScreenRecorder",
        NULL, 0,
        &argc, argv,
        NULL,
        XmNtitle,            "GPU Screen Recorder",
        XmNallowShellResize, True,
        XmNdeleteResponse,   XmDO_NOTHING,   /* WM_DELETE handled via protocol */
        NULL);

    ctx.display = XtDisplay(ctx.toplevel);

#ifdef GSR_CDE_PALETTE
    apply_cde_palette(&ctx);
#endif

    /* Font preference order:
     *   1.  CDE/session *FontList loaded as a single XFontStruct via
     *       XmFontListEntryLoad(..., XmFONT_IS_FONT). This avoids the
     *       FontSet charset-coverage check that fails under en_US.UTF-8.
     *       Picks up exactly what dtsession told other CDE apps to use.
     *   2.  Xft fallback when no *FontList in the resource DB. */
#ifdef GSR_CDE_PALETTE
    bool cde_font_ok = install_cde_fonts(&ctx);
#else
    bool cde_font_ok = false;
#endif
#ifdef GSR_XFT_FONTS
    if(!cde_font_ok)
        install_xft_fonts(&ctx);
#else
    (void)cde_font_ok;
#endif

    apply_saved_geometry(&ctx);

    Widget main_w = XtVaCreateManagedWidget(
        "main_w",
        xmMainWindowWidgetClass, ctx.toplevel,
        NULL);

    ctx.page_host = XtVaCreateManagedWidget(
        "page_host",
        xmFormWidgetClass, main_w,
        XmNwidth,  540,
        XmNheight, 650,
        NULL);

    build_pages(&ctx);

    XmMainWindowSetAreas(main_w, NULL, NULL, NULL, NULL, ctx.page_host);

    XtRealizeWidget(ctx.toplevel);
    register_wm_protocols(&ctx);
    register_demo_hotkey(&ctx);

    XtAppAddTimeOut(ctx.app, POLL_RECORDER_INTERVAL_MS,
                    poll_recorder_subprocess, &ctx);
    XtAppAddTimeOut(ctx.app, HOTKEY_DRAIN_INTERVAL_MS,
                    drain_root_hotkeys, &ctx);

    build_tray_menu(&ctx);
    ctx.tray = tray_create(ctx.app, ctx.display,
                           DefaultScreen(ctx.display),
                           on_tray_click, &ctx);

    XtAppMainLoop(ctx.app);

    /* WM_DELETE_WINDOW path: save was already done in on_window_close.
     * Release the demo grab and the child process so ASan stays quiet. */
    (void)gsr_hotkey_grab(ctx.display, ctx.test_hotkey, false);
    ungrab_page_hotkeys(&ctx);
    if(ctx.tray) tray_destroy(ctx.tray);
    recorder_process_terminate();
    gsr_capabilities_free(&ctx.caps);
    app_state_free(&ctx.config);
    return 0;
}
