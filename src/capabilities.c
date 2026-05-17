#include "capabilities.h"
#include "str_util.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

/* --- struct lifecycle ------------------------------------------------ */

static void monitor_list_init(GsrMonitorList *list)
{
    list->items = NULL;
    list->len = list->cap = 0;
}

static void monitor_list_free(GsrMonitorList *list)
{
    for(size_t i = 0; i < list->len; ++i)
        free(list->items[i].name);
    free(list->items);
    list->items = NULL;
    list->len = list->cap = 0;
}

static void monitor_list_push(GsrMonitorList *list, GsrMonitor m)
{
    if(list->len == list->cap) {
        size_t cap = list->cap ? list->cap * 2 : 4;
        GsrMonitor *n = (GsrMonitor *)realloc(list->items, cap * sizeof(*n));
        assert(n && "OOM in monitor_list_push");
        list->items = n;
        list->cap = cap;
    }
    list->items[list->len++] = m;
}

void gsr_capabilities_init(GsrCapabilities *caps)
{
    memset(caps, 0, sizeof(*caps));
    monitor_list_init(&caps->capture_options.monitors);
}

void gsr_capabilities_free(GsrCapabilities *caps)
{
    monitor_list_free(&caps->capture_options.monitors);
    memset(caps, 0, sizeof(*caps));
}

/* --- parsing --------------------------------------------------------- */

typedef enum {
    SECTION_UNKNOWN = 0,
    SECTION_SYSTEM_INFO,
    SECTION_GPU_INFO,
    SECTION_VIDEO_CODECS,
    SECTION_CAPTURE_OPTIONS,
} Section;

/* Returns offset of `c` in [s..s+n) or n if absent. */
static size_t find_char(const char *s, size_t n, char c)
{
    const void *p = memchr(s, c, n);
    return p ? (size_t)((const char *)p - s) : n;
}

static bool eq_cstr(const char *s, size_t n, const char *c)
{
    size_t cl = strlen(c);
    return n == cl && memcmp(s, c, cl) == 0;
}

static void parse_system_info_line(GsrCapabilities *caps, const char *line, size_t line_len)
{
    size_t bar = find_char(line, line_len, '|');
    if(bar == line_len)
        return;
    const char *name = line;
    size_t      name_len = bar;
    const char *value = line + bar + 1;
    size_t      value_len = line_len - bar - 1;

    if(eq_cstr(name, name_len, "display_server")) {
        if(eq_cstr(value, value_len, "x11"))
            caps->display_server = GSR_DISPLAY_X11;
        else if(eq_cstr(value, value_len, "wayland"))
            caps->display_server = GSR_DISPLAY_WAYLAND;
    } else if(eq_cstr(name, name_len, "is_steam_deck")) {
        caps->is_steam_deck = eq_cstr(value, value_len, "yes");
    } else if(eq_cstr(name, name_len, "supports_app_audio")) {
        caps->supports_app_audio = eq_cstr(value, value_len, "yes");
    }
}

static void parse_gpu_info_line(GsrCapabilities *caps, const char *line, size_t line_len)
{
    size_t bar = find_char(line, line_len, '|');
    if(bar == line_len)
        return;
    const char *name = line;
    size_t      name_len = bar;
    const char *value = line + bar + 1;
    size_t      value_len = line_len - bar - 1;

    if(eq_cstr(name, name_len, "vendor")) {
        if(eq_cstr(value, value_len, "amd"))           caps->gpu_vendor = GSR_GPU_AMD;
        else if(eq_cstr(value, value_len, "intel"))    caps->gpu_vendor = GSR_GPU_INTEL;
        else if(eq_cstr(value, value_len, "nvidia"))   caps->gpu_vendor = GSR_GPU_NVIDIA;
        else if(eq_cstr(value, value_len, "broadcom")) caps->gpu_vendor = GSR_GPU_BROADCOM;
    }
}

static void parse_video_codecs_line(GsrCapabilities *caps, const char *line, size_t line_len)
{
    GsrVideoCodecs *vc = &caps->video_codecs;
    if(eq_cstr(line, line_len, "h264"))               vc->h264 = true;
    else if(eq_cstr(line, line_len, "h264_software")) vc->h264_software = true;
    else if(eq_cstr(line, line_len, "hevc"))          vc->hevc = true;
    else if(eq_cstr(line, line_len, "hevc_hdr"))      vc->hevc_hdr = true;
    else if(eq_cstr(line, line_len, "hevc_10bit"))    vc->hevc_10bit = true;
    else if(eq_cstr(line, line_len, "av1"))           vc->av1 = true;
    else if(eq_cstr(line, line_len, "av1_hdr"))       vc->av1_hdr = true;
    else if(eq_cstr(line, line_len, "av1_10bit"))     vc->av1_10bit = true;
    else if(eq_cstr(line, line_len, "vp8"))           vc->vp8 = true;
    else if(eq_cstr(line, line_len, "vp9"))           vc->vp9 = true;
}

static void parse_capture_options_line(GsrCapabilities *caps, const char *line, size_t line_len)
{
    GsrCaptureOptions *co = &caps->capture_options;
    if(eq_cstr(line, line_len, "window"))       { co->window = true;  return; }
    if(eq_cstr(line, line_len, "focused"))      { co->focused = true; return; }
    if(eq_cstr(line, line_len, "portal"))       { co->portal = true;  return; }
    if(eq_cstr(line, line_len, "region"))       return;   /* unsupported in this UI */
    if(line_len == 0 || line[0] == '/')         return;   /* skip empty / paths */

    /* Monitor entry: "name|WxH" */
    size_t bar = find_char(line, line_len, '|');
    GsrMonitor m = {NULL, 0, 0};
    if(bar < line_len) {
        m.name = xstrndup(line, bar);
        char dims[64];
        size_t dlen = line_len - bar - 1;
        if(dlen < sizeof(dims)) {
            memcpy(dims, line + bar + 1, dlen);
            dims[dlen] = '\0';
            if(sscanf(dims, "%dx%d", &m.width, &m.height) != 2) {
                m.width = 0;
                m.height = 0;
            }
        }
    } else {
        m.name = xstrndup(line, line_len);
    }
    monitor_list_push(&co->monitors, m);
}

/* Drain entire popen() into a malloc'd NUL-terminated buffer. */
static bool slurp_popen(const char *cmd, char **out, size_t *out_size, int *out_status)
{
    *out = NULL;
    *out_size = 0;
    if(out_status) *out_status = -1;

    FILE *f = popen(cmd, "r");
    if(!f)
        return false;

    enum { CHUNK = 4096 };
    size_t cap = CHUNK;
    size_t len = 0;
    char  *buf = (char *)malloc(cap);
    if(!buf) { pclose(f); return false; }

    for(;;) {
        if(cap - len < CHUNK) {
            size_t new_cap = cap * 2;
            char  *nb = (char *)realloc(buf, new_cap);
            if(!nb) { free(buf); pclose(f); return false; }
            buf = nb;
            cap = new_cap;
        }
        size_t r = fread(buf + len, 1, cap - len - 1, f);
        len += r;
        if(r == 0)
            break;
    }

    bool io_ok = !ferror(f);
    int status = pclose(f);
    if(out_status) *out_status = status;

    if(!io_ok) {
        free(buf);
        return false;
    }
    buf[len] = '\0';
    *out = buf;
    *out_size = len;
    return true;
}

GsrInfoStatus gsr_capabilities_detect(GsrCapabilities *caps)
{
    /* Reset to empty before populating. */
    gsr_capabilities_free(caps);
    gsr_capabilities_init(caps);

    char  *content = NULL;
    size_t size = 0;
    int    status = -1;
    if(!slurp_popen("gpu-screen-recorder --info", &content, &size, &status)) {
        fprintf(stderr, "error: 'gpu-screen-recorder --info' could not be started or read\n");
        return GSR_INFO_FAILED_TO_RUN_COMMAND;
    }

    Section section = SECTION_UNKNOWN;
    size_t  i = 0;
    while(i < size) {
        size_t j = i;
        while(j < size && content[j] != '\n') ++j;
        const char *line = content + i;
        size_t      line_len = j - i;
        i = j + 1;

        /* section=<name> header */
        if(line_len >= 8 && memcmp(line, "section=", 8) == 0) {
            const char *name = line + 8;
            size_t      name_len = line_len - 8;
            if(eq_cstr(name, name_len, "system_info"))          section = SECTION_SYSTEM_INFO;
            else if(eq_cstr(name, name_len, "gpu_info"))        section = SECTION_GPU_INFO;
            else if(eq_cstr(name, name_len, "video_codecs"))    section = SECTION_VIDEO_CODECS;
            else if(eq_cstr(name, name_len, "capture_options")) section = SECTION_CAPTURE_OPTIONS;
            else                                                 section = SECTION_UNKNOWN;
            continue;
        }

        switch(section) {
        case SECTION_UNKNOWN:         break;
        case SECTION_SYSTEM_INFO:     parse_system_info_line   (caps, line, line_len); break;
        case SECTION_GPU_INFO:        parse_gpu_info_line      (caps, line, line_len); break;
        case SECTION_VIDEO_CODECS:    parse_video_codecs_line  (caps, line, line_len); break;
        case SECTION_CAPTURE_OPTIONS: parse_capture_options_line(caps, line, line_len); break;
        }
    }

    free(content);

    if(WIFEXITED(status)) {
        switch(WEXITSTATUS(status)) {
        case 0:  return GSR_INFO_OK;
        case 22: return GSR_INFO_OPENGL_FAILED;
        case 23: return GSR_INFO_NO_DRM_CARD;
        default: return GSR_INFO_FAILED_TO_RUN_COMMAND;
        }
    }
    return GSR_INFO_FAILED_TO_RUN_COMMAND;
}

/* --- accessors -------------------------------------------------------- */

const char *gsr_capabilities_codec_label(const char *codec_id)
{
    if(!codec_id)              return "?";
    if(strcmp(codec_id, "auto") == 0)          return "Auto (Recommended)";
    if(strcmp(codec_id, "h264") == 0)          return "H.264";
    if(strcmp(codec_id, "h264_software") == 0) return "H.264 (software)";
    if(strcmp(codec_id, "hevc") == 0)          return "HEVC";
    if(strcmp(codec_id, "hevc_hdr") == 0)      return "HEVC (HDR)";
    if(strcmp(codec_id, "hevc_10bit") == 0)    return "HEVC (10-bit)";
    if(strcmp(codec_id, "av1") == 0)           return "AV1";
    if(strcmp(codec_id, "av1_hdr") == 0)       return "AV1 (HDR)";
    if(strcmp(codec_id, "av1_10bit") == 0)     return "AV1 (10-bit)";
    if(strcmp(codec_id, "vp8") == 0)           return "VP8";
    if(strcmp(codec_id, "vp9") == 0)           return "VP9";
    return codec_id;
}

bool gsr_capabilities_codec_supported(const GsrCapabilities *caps, const char *codec_id)
{
    if(!codec_id) return false;
    if(strcmp(codec_id, "auto") == 0) return true;
    const GsrVideoCodecs *vc = &caps->video_codecs;
    if(strcmp(codec_id, "h264") == 0)          return vc->h264;
    if(strcmp(codec_id, "h264_software") == 0) return vc->h264_software;
    if(strcmp(codec_id, "hevc") == 0)          return vc->hevc;
    if(strcmp(codec_id, "hevc_hdr") == 0)      return vc->hevc_hdr;
    if(strcmp(codec_id, "hevc_10bit") == 0)    return vc->hevc_10bit;
    if(strcmp(codec_id, "av1") == 0)           return vc->av1;
    if(strcmp(codec_id, "av1_hdr") == 0)       return vc->av1_hdr;
    if(strcmp(codec_id, "av1_10bit") == 0)     return vc->av1_10bit;
    if(strcmp(codec_id, "vp8") == 0)           return vc->vp8;
    if(strcmp(codec_id, "vp9") == 0)           return vc->vp9;
    return false;
}

bool gsr_capabilities_capture_supported(const GsrCapabilities *caps, const char *capture_id)
{
    if(!capture_id) return false;
    const GsrCaptureOptions *co = &caps->capture_options;
    if(strcmp(capture_id, "window") == 0)         return co->window;
    if(strcmp(capture_id, "focused") == 0)        return co->focused;
    if(strcmp(capture_id, "portal") == 0)         return co->portal;
    return false;
}
