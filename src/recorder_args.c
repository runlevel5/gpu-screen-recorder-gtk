#include "recorder_args.h"
#include "str_util.h"
#include "xdg_paths.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* --- argv builder ---------------------------------------------------- */

typedef struct {
    char  **items;
    size_t  len;
    size_t  cap;
} ArgvBuf;

static void argv_init(ArgvBuf *b) { b->items = NULL; b->len = 0; b->cap = 0; }

static void argv_push_owned(ArgvBuf *b, char *owned)
{
    if(b->len + 1 >= b->cap) {  /* +1 for trailing NULL */
        size_t cap = b->cap ? b->cap * 2 : 16;
        char **n = (char **)realloc(b->items, cap * sizeof(*n));
        assert(n && "OOM in argv_push_owned");
        b->items = n;
        b->cap = cap;
    }
    b->items[b->len++] = owned;
}

static void argv_push(ArgvBuf *b, const char *literal)
{
    argv_push_owned(b, xstrdup(literal));
}

static void argv_pushf(ArgvBuf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void argv_pushf(ArgvBuf *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    /* xasprintf is va_list-less, so go through vsnprintf manually. */
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    assert(n >= 0);
    char *s = (char *)malloc((size_t)n + 1);
    assert(s && "OOM in argv_pushf");
    vsnprintf(s, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    argv_push_owned(b, s);
}

void recorder_args_free(char **argv)
{
    if(!argv) return;
    for(size_t i = 0; argv[i]; ++i)
        free(argv[i]);
    free(argv);
}

const char *recorder_args_status_str(RecorderArgsStatus s)
{
    switch(s) {
    case RA_BUILD_OK:                return "ok";
    case RA_BUILD_WINDOW_REQUIRED:   return "record_area=window but no window picked";
    case RA_BUILD_NO_CAPABLE_CODEC:  return "no hardware video codec available; software fallback also unavailable";
    case RA_BUILD_OUTPUT_DIR_FAILED: return "could not create output directory";
    case RA_BUILD_OUTPUT_MISSING:    return "output directory or stream URL is unset";
    }
    return "?";
}

/* --- helpers --------------------------------------------------------- */

static const char *first_usable_hw_codec(const GsrCapabilities *caps)
{
    const GsrVideoCodecs *v = &caps->video_codecs;
    if(v->h264) return "h264";
    if(v->hevc) return "hevc";
    if(v->av1)  return "av1";
    if(v->vp8)  return "vp8";
    if(v->vp9)  return "vp9";
    return NULL;
}

/* Mirrors change_container_if_codec_not_supported() from the GTK port:
 * VP8/VP9 force webm/matroska; webm forces away from non-VPx codecs. */
static const char *adjust_container(const char *codec, const char *container_in)
{
    if(!codec || !container_in)
        return container_in ? container_in : "mp4";
    bool is_vpx = strcmp(codec, "vp8") == 0 || strcmp(codec, "vp9") == 0;
    if(is_vpx) {
        if(strcmp(container_in, "webm") != 0 && strcmp(container_in, "matroska") != 0) {
            fprintf(stderr, "[recorder_args] container '%s' incompatible with %s; forcing webm\n",
                    container_in, codec);
            return "webm";
        }
        return container_in;
    }
    if(strcmp(container_in, "webm") == 0) {
        fprintf(stderr, "[recorder_args] container webm incompatible with %s; forcing mp4\n", codec);
        return "mp4";
    }
    return container_in;
}

/* "matroska" -> "mkv", others usually map 1:1. */
static const char *container_file_ext(const char *container)
{
    if(!container)                              return "mp4";
    if(strcmp(container, "matroska") == 0)      return "mkv";
    if(strcmp(container, "mpegts") == 0)        return "ts";
    if(strcmp(container, "hls") == 0)           return "m3u8";
    return container;
}

/* "2026-05-17_15-23-04" — same shape the GTK port produces. */
static char *make_date_str(void)
{
    time_t  t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", &tm);
    return xstrdup(buf);
}

static char *build_stream_url(const StreamingConfig *sc)
{
    const char *svc = sc->streaming_service ? sc->streaming_service : "twitch";
    if(strcmp(svc, "twitch") == 0)
        return xasprintf("rtmp://live.twitch.tv/app/%s",
                         sc->twitch.stream_key ? sc->twitch.stream_key : "");
    if(strcmp(svc, "youtube") == 0)
        return xasprintf("rtmp://a.rtmp.youtube.com/live2/%s",
                         sc->youtube.stream_key ? sc->youtube.stream_key : "");
    if(strcmp(svc, "custom") == 0) {
        const char *u = sc->custom.url ? sc->custom.url : "";
        /* If no recognised scheme, prefix rtmp:// (mirrors GTK). */
        static const char *schemes[] = {
            "rtmp://", "rtmps://", "rtsp://", "srt://",
            "http://", "https://", "tcp://", "udp://", NULL };
        for(size_t i = 0; schemes[i]; ++i) {
            if(strncmp(u, schemes[i], strlen(schemes[i])) == 0)
                return xstrdup(u);
        }
        return xasprintf("rtmp://%s", u);
    }
    return xstrdup("");
}

/* --- builder --------------------------------------------------------- */

RecorderArgsStatus recorder_args_build(const RecorderArgsRequest *req,
                                       char ***out_argv,
                                       char **out_filepath)
{
    *out_argv = NULL;
    if(out_filepath) *out_filepath = NULL;

    const Config          *c    = req->config;
    const GsrCapabilities *caps = req->caps;
    const MainConfig      *mc   = &c->main_config;

    /* Resolve area / window argument. */
    const char *area_id = mc->record_area_option ? mc->record_area_option : "focused";
    bool follow_focused = strcmp(area_id, "focused") == 0;
    bool is_window      = strcmp(area_id, "window") == 0;
    bool is_portal      = strcmp(area_id, "portal") == 0;

    char *window_arg = NULL;
    if(is_window) {
        if(req->selected_window == 0)
            return RA_BUILD_WINDOW_REQUIRED;
        window_arg = xasprintf("%lu", req->selected_window);
    } else {
        window_arg = xstrdup(area_id);
    }

    /* Resolve codec/encoder ("auto" / software fallback). */
    const char *codec   = mc->codec ? mc->codec : "auto";
    const char *encoder = "gpu";
    if(strcmp(codec, "h264_software") == 0) {
        codec   = "h264";
        encoder = "cpu";
    } else if(strcmp(codec, "auto") == 0) {
        const char *hw = first_usable_hw_codec(caps);
        if(hw) {
            codec = hw;
        } else {
            codec   = "h264";
            encoder = "cpu";
            if(!caps->video_codecs.h264_software) {
                free(window_arg);
                return RA_BUILD_NO_CAPABLE_CODEC;
            }
        }
    }

    /* Container selection — record/replay pull from per-mode config;
     * stream defaults to flv (or custom's container if custom). */
    const char *container = "flv";
    if(req->mode == RECORDER_MODE_RECORD)
        container = c->record_config.container ? c->record_config.container : "mp4";
    else if(req->mode == RECORDER_MODE_REPLAY)
        container = c->replay_config.container ? c->replay_config.container : "mp4";
    else if(req->mode == RECORDER_MODE_STREAM) {
        if(c->streaming_config.streaming_service &&
           strcmp(c->streaming_config.streaming_service, "custom") == 0 &&
           c->streaming_config.custom.container)
            container = c->streaming_config.custom.container;
    }
    container = adjust_container(codec, container);

    /* Output path. */
    char *output = NULL;
    if(req->mode == RECORDER_MODE_STREAM) {
        output = build_stream_url(&c->streaming_config);
        if(!output || output[0] == '\0') {
            free(window_arg);
            free(output);
            return RA_BUILD_OUTPUT_MISSING;
        }
    } else {
        const char *dir = req->save_directory;
        if(!dir)
            dir = (req->mode == RECORDER_MODE_RECORD)
                ? c->record_config.save_directory
                : c->replay_config.save_directory;
        if(!dir || dir[0] == '\0') {
            free(window_arg);
            return RA_BUILD_OUTPUT_MISSING;
        }
        if(xdg_paths_create_directory_recursive(dir) != 0) {
            free(window_arg);
            return RA_BUILD_OUTPUT_DIR_FAILED;
        }
        if(req->mode == RECORDER_MODE_RECORD) {
            char *date = make_date_str();
            output = xasprintf("%s/Video_%s.%s", dir, date, container_file_ext(container));
            free(date);
        } else {
            /* Replay: pass the directory; gpu-screen-recorder names per-save. */
            output = xstrdup(dir);
        }
    }
    if(out_filepath && req->mode != RECORDER_MODE_STREAM)
        *out_filepath = xstrdup(output);

    /* --- assemble argv ---- */

    ArgvBuf b;
    argv_init(&b);

    argv_push (&b, "gpu-screen-recorder");
    argv_push (&b, "-w");            argv_push_owned(&b, window_arg);
    argv_push (&b, "-c");            argv_push(&b, container);
    argv_push (&b, "-k");            argv_push(&b, codec);
    argv_push (&b, "-ac");           argv_push(&b, mc->audio_codec ? mc->audio_codec : "opus");
    argv_push (&b, "-f");            argv_pushf(&b, "%d", mc->fps > 0 ? mc->fps : 60);
    argv_push (&b, "-cursor");       argv_push(&b, mc->record_cursor ? "yes" : "no");
    argv_push (&b, "-restore-portal-session");
    argv_push (&b, is_portal && mc->restore_portal_session ? "yes" : "no");
    argv_push (&b, "-cr");           argv_push(&b, mc->color_range ? mc->color_range : "limited");
    argv_push (&b, "-encoder");      argv_push(&b, encoder);
    argv_push (&b, "-o");            argv_push_owned(&b, output);

    /* Replay: -r <replay_time>. */
    if(req->mode == RECORDER_MODE_REPLAY) {
        argv_push (&b, "-r");
        argv_pushf(&b, "%d", c->replay_config.replay_time > 0 ? c->replay_config.replay_time : 30);
    }

    /* Quality / bitrate. */
    const char *quality = mc->quality ? mc->quality : "very_high";
    if(strcmp(quality, "custom") == 0) {
        argv_push (&b, "-bm"); argv_push(&b, "cbr");
        argv_push (&b, "-q");  argv_pushf(&b, "%d", mc->video_bitrate > 0 ? mc->video_bitrate : 15000);
    } else {
        argv_push (&b, "-q");  argv_push(&b, quality);
    }

    if(mc->overclock) { argv_push(&b, "-oc"); argv_push(&b, "yes"); }

    if(mc->framerate_mode && strcmp(mc->framerate_mode, "auto") != 0) {
        argv_push(&b, "-fm"); argv_push(&b, mc->framerate_mode);
    }

    /* Audio: -a per StringArray entry; if merge, join with '|' and pass once.
     * If record_app_audio_inverted is set, rewrite the "app:" prefix to
     * "app-inverse:" so gpu-screen-recorder captures everything except the
     * named apps. Devices ("device:") pass through unchanged. */
    if(mc->audio_input.len > 0) {
        /* Pre-resolve transformed strings into a temporary array. */
        char **xforms = (char **)malloc(mc->audio_input.len * sizeof(*xforms));
        assert(xforms);
        for(size_t i = 0; i < mc->audio_input.len; ++i) {
            const char *e = mc->audio_input.items[i];
            if(mc->record_app_audio_inverted && strncmp(e, "app:", 4) == 0)
                xforms[i] = xasprintf("app-inverse:%s", e + 4);
            else
                xforms[i] = xstrdup(e);
        }

        if(mc->merge_audio_tracks) {
            size_t total = 0;
            for(size_t i = 0; i < mc->audio_input.len; ++i)
                total += strlen(xforms[i]) + 1;
            char *joined = (char *)malloc(total + 1);
            assert(joined);
            char *pp = joined;
            for(size_t i = 0; i < mc->audio_input.len; ++i) {
                if(i) *pp++ = '|';
                size_t n = strlen(xforms[i]);
                memcpy(pp, xforms[i], n);
                pp += n;
            }
            *pp = '\0';
            argv_push(&b, "-a"); argv_push_owned(&b, joined);
        } else {
            for(size_t i = 0; i < mc->audio_input.len; ++i) {
                argv_push(&b, "-a"); argv_push(&b, xforms[i]);
            }
        }

        for(size_t i = 0; i < mc->audio_input.len; ++i)
            free(xforms[i]);
        free(xforms);
    }

    /* -s <WxH> only if follow_focused or change_video_resolution. */
    if(follow_focused || mc->change_video_resolution) {
        int w = follow_focused
            ? (mc->record_area_width  > 0 ? mc->record_area_width  : 1920)
            : (mc->video_width        > 0 ? mc->video_width        : 1920);
        int h = follow_focused
            ? (mc->record_area_height > 0 ? mc->record_area_height : 1080)
            : (mc->video_height       > 0 ? mc->video_height       : 1080);
        argv_push (&b, "-s");
        argv_pushf(&b, "%dx%d", w, h);
    }

    /* NULL-terminate. */
    if(b.len == b.cap) {
        char **n = (char **)realloc(b.items, (b.cap + 1) * sizeof(*n));
        assert(n);
        b.items = n;
    }
    b.items[b.len] = NULL;

    *out_argv = b.items;
    return RA_BUILD_OK;
}
