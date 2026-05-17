#ifndef GSR_RECORDER_ARGS_H
#define GSR_RECORDER_ARGS_H

/*
 * Build the argv passed to `gpu-screen-recorder` for record / replay /
 * stream modes from Config + capabilities. Owns no global state — all
 * inputs are explicit so it's testable in isolation.
 *
 * The original argv layout in src/main.cpp lines 1900-1940 (replay),
 * 2108-2147 (recording), and 2286-2301 (streaming) is replicated; only
 * the source of values changes (now Config + caps instead of GTK widgets).
 */

#include "app_state.h"
#include "capabilities.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RECORDER_MODE_RECORD = 0,
    RECORDER_MODE_REPLAY,
    RECORDER_MODE_STREAM,
} RecorderMode;

typedef enum {
    RA_BUILD_OK = 0,
    RA_BUILD_WINDOW_REQUIRED,      /* record_area=window but no picked window */
    RA_BUILD_NO_CAPABLE_CODEC,     /* "auto" requested but no hardware codec available */
    RA_BUILD_OUTPUT_DIR_FAILED,    /* couldn't create the save directory */
    RA_BUILD_OUTPUT_MISSING,       /* output dir / stream URL unset */
} RecorderArgsStatus;

typedef struct {
    RecorderMode            mode;
    const Config           *config;
    const GsrCapabilities  *caps;

    /* For RECORDER_MODE_RECORD/REPLAY: directory to save into.
     * For RECORDER_MODE_STREAM: ignored. Taken from config if NULL. */
    const char             *save_directory;

    /* X11 window ID to capture when config.main_config.record_area_option
     * is "window". 0 if not applicable. (Typed unsigned long to keep this
     * header free of X11 includes; X11's `Window` is also unsigned long.) */
    unsigned long           selected_window;
} RecorderArgsRequest;

/* On success, returns RA_BUILD_OK and *out_argv = NULL-terminated argv
 * (every element malloc'd; free with recorder_args_free). When the chosen
 * mode requires a generated filename (record / replay-save), *out_filepath
 * receives a malloc'd copy of the full output path; otherwise NULL.
 *
 * On failure, returns an error status, *out_argv = NULL. */
RecorderArgsStatus recorder_args_build(const RecorderArgsRequest *req,
                                       char ***out_argv,
                                       char **out_filepath);

void recorder_args_free(char **argv);

const char *recorder_args_status_str(RecorderArgsStatus s);

#ifdef __cplusplus
}
#endif

#endif /* GSR_RECORDER_ARGS_H */
