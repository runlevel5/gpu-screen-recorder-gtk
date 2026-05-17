#ifndef GSR_APP_STATE_H
#define GSR_APP_STATE_H

/*
 * App configuration state. C port of the original src/config.hpp.
 *
 * Ownership: every `char *` field is either NULL ("unset / treat as empty")
 * or malloc'd. Setting a field replaces the previous value via xfree_set().
 * StringArray fields own their entries.
 *
 * On-disk format is `key value\n` per line, identical to the GTK version,
 * so existing ~/.config/gpu-screen-recorder/config files load unchanged.
 */

#include "str_util.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int64_t  keysym;
    uint32_t modifiers;
} ConfigHotkey;

typedef struct {
    char    *record_area_option;
    int32_t  record_area_width;
    int32_t  record_area_height;
    int32_t  video_width;
    int32_t  video_height;
    int32_t  fps;
    int32_t  video_bitrate;
    bool     merge_audio_tracks;
    bool     record_app_audio_inverted;
    bool     change_video_resolution;
    StringArray audio_input;
    char    *color_range;
    char    *quality;
    char    *codec;          /* Video codec */
    char    *audio_codec;
    char    *framerate_mode;
    bool     advanced_view;
    bool     overclock;
    bool     show_recording_started_notifications;
    bool     show_recording_stopped_notifications;
    bool     show_recording_saved_notifications;
    bool     record_cursor;
    bool     hide_window_when_recording;
    bool     software_encoding_warning_shown;
    bool     hevc_amd_bug_warning_shown;
    bool     av1_amd_bug_warning_shown;
    bool     restore_portal_session;
    bool     use_new_ui;
    int32_t  installed_gsr_global_hotkeys_version;
} MainConfig;

typedef struct { char *stream_key; } YoutubeStreamConfig;
typedef struct { char *stream_key; } TwitchStreamConfig;
typedef struct {
    char *url;
    char *container;
} CustomStreamConfig;

typedef struct {
    char               *streaming_service;
    YoutubeStreamConfig youtube;
    TwitchStreamConfig  twitch;
    CustomStreamConfig  custom;
    ConfigHotkey        start_stop_recording_hotkey;
} StreamingConfig;

typedef struct {
    char        *save_directory;
    char        *container;
    ConfigHotkey start_stop_recording_hotkey;
    ConfigHotkey pause_unpause_recording_hotkey;
} RecordConfig;

typedef struct {
    char        *save_directory;
    char        *container;
    int32_t      replay_time;
    ConfigHotkey start_stop_recording_hotkey;
    ConfigHotkey save_recording_hotkey;
} ReplayConfig;

typedef struct {
    MainConfig      main_config;
    StreamingConfig streaming_config;
    RecordConfig    record_config;
    ReplayConfig    replay_config;
} Config;

/* Zero the struct and install defaults (non-zero defaults from the original C++ version). */
void app_state_init(Config *config);

/* Free every owned allocation. Safe to call on an init'd-but-empty struct. */
void app_state_free(Config *config);

/* Loads ~/.config/gpu-screen-recorder/config into `config`.
 * `config` must already have been initialised with app_state_init().
 * Returns true if the file existed and was read; false if missing or unreadable
 * (defaults remain in place either way). */
bool app_state_load(Config *config);

/* Writes `config` back to disk, creating the config directory if needed.
 * Output is alphabetically sorted by key to match the original std::map iteration
 * order, so saved files diff cleanly across runs. */
void app_state_save(const Config *config);

#ifdef __cplusplus
}
#endif

#endif /* GSR_APP_STATE_H */
