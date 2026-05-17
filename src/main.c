/*
 * gpu-screen-recorder-gtk — Motif/X11 frontend (C99).
 *
 * Phase 3 + 4: wires the Xt event loop (subprocess polling timer + root-window
 * hotkey drain timer) and a hub-and-spoke navigation between four page forms.
 *
 * The original GTK source remains at src/main.cpp / src/config.hpp on this
 * branch as a porting reference; it is not compiled.
 */

#include <Xm/Form.h>
#include <Xm/MainW.h>
#include <Xm/Xm.h>

#include <X11/Intrinsic.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_state.h"
#include "audio_devices.h"
#include "recorder_process.h"
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
    Widget       page_host;       /* XmForm containing all four page forms */
    Widget       pages[PAGE_COUNT];
    PageId       current_page;

    PageCommonSettings common;
    PageReplay         replay;
    PageRecording      recording;
    PageStreaming      streaming;

    Config       config;

    /* Phase 3 demo hotkey: validates the root-window key drain path end-to-end. */
    ConfigHotkey test_hotkey;
} AppCtx;

static void switch_to_page(AppCtx *ctx, PageId target)
{
    if(target < 0 || target >= PAGE_COUNT)
        return;
    if(target == ctx->current_page)
        return;
    XtUnmanageChild(ctx->pages[ctx->current_page]);
    XtManageChild(ctx->pages[target]);
    ctx->current_page = target;
}

static void on_page_nav(PageId target, void *user_data)
{
    switch_to_page((AppCtx *)user_data, target);
}

/* --- Timers ------------------------------------------------------------- */

static void poll_recorder_subprocess(XtPointer client_data, XtIntervalId *id)
{
    (void)id;
    AppCtx *ctx = (AppCtx *)client_data;

    int status = 0;
    if(recorder_process_poll(&status))
        fprintf(stderr, "[recorder] subprocess exited, status=%d\n", status);

    XtAppAddTimeOut(ctx->app, POLL_RECORDER_INTERVAL_MS,
                    poll_recorder_subprocess, ctx);
}

static void on_hotkey_fired(KeySym ks, unsigned int mods, void *user_data)
{
    (void)user_data;
    fprintf(stderr, "[hotkey] keysym=0x%lx (%s) x11_mods=0x%x\n",
            (unsigned long)ks,
            XKeysymToString(ks) ? XKeysymToString(ks) : "?",
            mods);
}

static void drain_root_hotkeys(XtPointer client_data, XtIntervalId *id)
{
    (void)id;
    AppCtx *ctx = (AppCtx *)client_data;
    gsr_hotkey_drain_root_events(ctx->display, on_hotkey_fired, ctx);
    XtAppAddTimeOut(ctx->app, HOTKEY_DRAIN_INTERVAL_MS,
                    drain_root_hotkeys, ctx);
}

/* --- Setup -------------------------------------------------------------- */

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
    /* page_host is an XmForm child of XmMainWindow. Each page is an unmanaged
     * XmForm; switch_to_page() manages/unmanages to swap visibility. */
    page_common_settings_create(ctx->page_host, &ctx->common,    on_page_nav, ctx);
    page_replay_create         (ctx->page_host, &ctx->replay,    on_page_nav, ctx);
    page_recording_create      (ctx->page_host, &ctx->recording, on_page_nav, ctx);
    page_streaming_create      (ctx->page_host, &ctx->streaming, on_page_nav, ctx);

    ctx->pages[PAGE_COMMON_SETTINGS] = ctx->common.root;
    ctx->pages[PAGE_REPLAY]          = ctx->replay.root;
    ctx->pages[PAGE_RECORDING]       = ctx->recording.root;
    ctx->pages[PAGE_STREAMING]       = ctx->streaming.root;

    ctx->current_page = PAGE_COMMON_SETTINGS;
    XtManageChild(ctx->pages[PAGE_COMMON_SETTINGS]);
}

static void register_demo_hotkey(AppCtx *ctx)
{
    /* Ctrl+Alt+R — validates the grab+drain pipeline end-to-end.
     * Modifier bitmap follows the ConfigHotkey convention: bits set per
     * gsr_modkey_to_mask(XK_*_L). */
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

int main(int argc, char **argv)
{
    AppCtx ctx;
    memset(&ctx, 0, sizeof(ctx));

    app_state_init(&ctx.config);
    app_state_load(&ctx.config);
    log_loaded_config(&ctx.config);

    recorder_process_init();

    Widget toplevel = XtVaAppInitialize(
        &ctx.app,
        "GpuScreenRecorder",
        NULL, 0,
        &argc, argv,
        NULL,
        XmNtitle,            "GPU Screen Recorder",
        XmNallowShellResize, True,
        NULL);

    ctx.display = XtDisplay(toplevel);

    Widget main_w = XtVaCreateManagedWidget(
        "main_w",
        xmMainWindowWidgetClass, toplevel,
        NULL);

    ctx.page_host = XtVaCreateManagedWidget(
        "page_host",
        xmFormWidgetClass, main_w,
        XmNwidth,  640,
        XmNheight, 480,
        NULL);

    build_pages(&ctx);

    XmMainWindowSetAreas(main_w, NULL, NULL, NULL, NULL, ctx.page_host);

    XtRealizeWidget(toplevel);

    /* Phase 3 wiring — only meaningful once a Display is realized. */
    register_demo_hotkey(&ctx);
    XtAppAddTimeOut(ctx.app, POLL_RECORDER_INTERVAL_MS,
                    poll_recorder_subprocess, &ctx);
    XtAppAddTimeOut(ctx.app, HOTKEY_DRAIN_INTERVAL_MS,
                    drain_root_hotkeys, &ctx);

    XtAppMainLoop(ctx.app);

    /* Unreachable in normal operation; XtAppMainLoop returns only after an
     * explicit XtAppSetExitFlag. Cleanup left here for ASan happiness. */
    if(gsr_hotkey_grab(ctx.display, ctx.test_hotkey, false))
        ; /* silent */
    recorder_process_terminate();
    app_state_free(&ctx.config);
    return 0;
}
