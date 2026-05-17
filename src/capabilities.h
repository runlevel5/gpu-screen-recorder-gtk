#ifndef GSR_CAPABILITIES_H
#define GSR_CAPABILITIES_H

/*
 * Detected capabilities of the installed `gpu-screen-recorder` binary
 * and the host system. Populated by parsing `gpu-screen-recorder --info`.
 *
 * Pages query this struct to filter combo-box items, control visibility,
 * and (Pass B2) shape the spawned argv.
 */

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GSR_DISPLAY_UNKNOWN = 0,
    GSR_DISPLAY_X11,
    GSR_DISPLAY_WAYLAND,
} GsrDisplayServer;

typedef enum {
    GSR_GPU_UNKNOWN = 0,
    GSR_GPU_AMD,
    GSR_GPU_INTEL,
    GSR_GPU_NVIDIA,
    GSR_GPU_BROADCOM,
} GsrGpuVendor;

typedef struct {
    char *name;     /* owned; e.g. "DP-1" */
    int   width;
    int   height;
} GsrMonitor;

typedef struct {
    GsrMonitor *items;
    size_t      len;
    size_t      cap;
} GsrMonitorList;

typedef struct {
    bool h264;
    bool h264_software;
    bool hevc;
    bool hevc_hdr;
    bool hevc_10bit;
    bool av1;
    bool av1_hdr;
    bool av1_10bit;
    bool vp8;
    bool vp9;
} GsrVideoCodecs;

typedef struct {
    bool             window;
    bool             focused;
    bool             portal;
    GsrMonitorList   monitors;
} GsrCaptureOptions;

typedef enum {
    GSR_INFO_OK = 0,
    GSR_INFO_FAILED_TO_RUN_COMMAND,
    GSR_INFO_OPENGL_FAILED,
    GSR_INFO_NO_DRM_CARD,
} GsrInfoStatus;

typedef struct {
    GsrDisplayServer  display_server;
    bool              supports_app_audio;
    bool              is_steam_deck;
    GsrGpuVendor      gpu_vendor;
    GsrVideoCodecs    video_codecs;
    GsrCaptureOptions capture_options;
} GsrCapabilities;

void          gsr_capabilities_init  (GsrCapabilities *caps);
void          gsr_capabilities_free  (GsrCapabilities *caps);
GsrInfoStatus gsr_capabilities_detect(GsrCapabilities *caps);

/* Returns the user-facing display label for a codec id like "h264" -> "H.264". */
const char *gsr_capabilities_codec_label(const char *codec_id);

/* True iff `codec_id` is supported per detected capabilities. "auto" is always true. */
bool gsr_capabilities_codec_supported(const GsrCapabilities *caps, const char *codec_id);

/* True iff `capture_id` ("window", "focused", "portal") is supported. */
bool gsr_capabilities_capture_supported(const GsrCapabilities *caps, const char *capture_id);

#ifdef __cplusplus
}
#endif

#endif /* GSR_CAPABILITIES_H */
