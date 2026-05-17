/*
 * gpu-screen-recorder-gtk — Motif/X11 frontend (C99).
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
#include <Xm/Xm.h>

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
    ConfigHotkey    test_hotkey;       /* Phase 3 demo */
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

static void switch_to_page(AppCtx *ctx, PageId target)
{
    if(target < 0 || target >= PAGE_COUNT || target == ctx->current_page)
        return;
    XtUnmanageChild(ctx->pages[ctx->current_page]);
    XtManageChild(ctx->pages[target]);
    ctx->current_page = target;

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

static void on_tray_click(TrayClick click, void *user_data)
{
    AppCtx *ctx = (AppCtx *)user_data;
    if(click == TRAY_CLICK_LEFT) {
        /* Toggle main window: if mapped, withdraw it; otherwise map+raise. */
        if(ctx->window_hidden) {
            XtMapWidget(ctx->toplevel);
            XRaiseWindow(ctx->display, XtWindow(ctx->toplevel));
            ctx->window_hidden = false;
        } else {
            XtUnmapWidget(ctx->toplevel);
            ctx->window_hidden = true;
        }
    }
    /* Right-click menu lands in Pass B. */
}

/* --- WM_DELETE_WINDOW handler -------------------------------------- */

static void on_window_close(Widget w, XtPointer client_data, XtPointer call)
{
    (void)w; (void)call;
    AppCtx *ctx = (AppCtx *)client_data;
    commit_current_page(ctx);
    app_state_save(&ctx->config);
    ctx->running = false;
    XtAppSetExitFlag(ctx->app);
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
        fprintf(stderr, "[hotkey] grabbed Ctrl+Alt+R as a Phase 3 demo. "
                        "Press it to see drain output.\n");
}

static void register_wm_protocols(AppCtx *ctx)
{
    Atom wm_delete = XmInternAtom(ctx->display, (char *)"WM_DELETE_WINDOW", False);
    XmAddWMProtocolCallback(ctx->toplevel, wm_delete, on_window_close, ctx);
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

    Widget main_w = XtVaCreateManagedWidget(
        "main_w",
        xmMainWindowWidgetClass, ctx.toplevel,
        NULL);

    ctx.page_host = XtVaCreateManagedWidget(
        "page_host",
        xmFormWidgetClass, main_w,
        XmNwidth,  720,
        XmNheight, 600,
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
