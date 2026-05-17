#include "app_state.h"
#include "str_util.h"
#include "xdg_paths.h"

#include <inttypes.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FORMAT_I32 "%" PRIi32
#define FORMAT_I64 "%" PRIi64
#define FORMAT_U32 "%" PRIu32

typedef enum {
    CONFIG_TYPE_BOOL,
    CONFIG_TYPE_STRING,
    CONFIG_TYPE_I32,
    CONFIG_TYPE_HOTKEY,
    CONFIG_TYPE_STRING_ARRAY,
} ConfigValueType;

typedef struct {
    const char     *key;
    ConfigValueType type;
    void           *data;   /* resolved against a specific Config* via build_options() */
} ConfigOption;

/* Builds the option table for a specific Config pointer. The resulting
 * pointers reference fields inside *c, so the array must not outlive *c.
 * Caller supplies storage; we return the count. */
static size_t build_options(Config *c, ConfigOption *out)
{
    size_t i = 0;
    /* main.* */
    out[i++] = (ConfigOption){"main.record_area_option",                    CONFIG_TYPE_STRING,       &c->main_config.record_area_option};
    out[i++] = (ConfigOption){"main.record_area_width",                     CONFIG_TYPE_I32,          &c->main_config.record_area_width};
    out[i++] = (ConfigOption){"main.record_area_height",                    CONFIG_TYPE_I32,          &c->main_config.record_area_height};
    out[i++] = (ConfigOption){"main.video_width",                           CONFIG_TYPE_I32,          &c->main_config.video_width};
    out[i++] = (ConfigOption){"main.video_height",                          CONFIG_TYPE_I32,          &c->main_config.video_height};
    out[i++] = (ConfigOption){"main.fps",                                   CONFIG_TYPE_I32,          &c->main_config.fps};
    out[i++] = (ConfigOption){"main.video_bitrate",                         CONFIG_TYPE_I32,          &c->main_config.video_bitrate};
    out[i++] = (ConfigOption){"main.merge_audio_tracks",                    CONFIG_TYPE_BOOL,         &c->main_config.merge_audio_tracks};
    out[i++] = (ConfigOption){"main.record_app_audio_inverted",             CONFIG_TYPE_BOOL,         &c->main_config.record_app_audio_inverted};
    out[i++] = (ConfigOption){"main.change_video_resolution",               CONFIG_TYPE_BOOL,         &c->main_config.change_video_resolution};
    out[i++] = (ConfigOption){"main.audio_input",                           CONFIG_TYPE_STRING_ARRAY, &c->main_config.audio_input};
    out[i++] = (ConfigOption){"main.color_range",                           CONFIG_TYPE_STRING,       &c->main_config.color_range};
    out[i++] = (ConfigOption){"main.quality",                               CONFIG_TYPE_STRING,       &c->main_config.quality};
    out[i++] = (ConfigOption){"main.codec",                                 CONFIG_TYPE_STRING,       &c->main_config.codec};
    out[i++] = (ConfigOption){"main.audio_codec",                           CONFIG_TYPE_STRING,       &c->main_config.audio_codec};
    out[i++] = (ConfigOption){"main.framerate_mode",                        CONFIG_TYPE_STRING,       &c->main_config.framerate_mode};
    out[i++] = (ConfigOption){"main.advanced_view",                         CONFIG_TYPE_BOOL,         &c->main_config.advanced_view};
    out[i++] = (ConfigOption){"main.overclock",                             CONFIG_TYPE_BOOL,         &c->main_config.overclock};
    out[i++] = (ConfigOption){"main.show_recording_started_notifications",  CONFIG_TYPE_BOOL,         &c->main_config.show_recording_started_notifications};
    out[i++] = (ConfigOption){"main.show_recording_stopped_notifications",  CONFIG_TYPE_BOOL,         &c->main_config.show_recording_stopped_notifications};
    out[i++] = (ConfigOption){"main.show_recording_saved_notifications",    CONFIG_TYPE_BOOL,         &c->main_config.show_recording_saved_notifications};
    out[i++] = (ConfigOption){"main.record_cursor",                         CONFIG_TYPE_BOOL,         &c->main_config.record_cursor};
    out[i++] = (ConfigOption){"main.hide_window_when_recording",            CONFIG_TYPE_BOOL,         &c->main_config.hide_window_when_recording};
    out[i++] = (ConfigOption){"main.software_encoding_warning_shown",       CONFIG_TYPE_BOOL,         &c->main_config.software_encoding_warning_shown};
    out[i++] = (ConfigOption){"main.hevc_amd_bug_warning_shown",            CONFIG_TYPE_BOOL,         &c->main_config.hevc_amd_bug_warning_shown};
    out[i++] = (ConfigOption){"main.av1_amd_bug_warning_shown",             CONFIG_TYPE_BOOL,         &c->main_config.av1_amd_bug_warning_shown};
    out[i++] = (ConfigOption){"main.restore_portal_session",                CONFIG_TYPE_BOOL,         &c->main_config.restore_portal_session};
    out[i++] = (ConfigOption){"main.use_new_ui",                            CONFIG_TYPE_BOOL,         &c->main_config.use_new_ui};
    out[i++] = (ConfigOption){"main.installed_gsr_global_hotkeys_version",  CONFIG_TYPE_I32,          &c->main_config.installed_gsr_global_hotkeys_version};
    out[i++] = (ConfigOption){"main.window_x",                              CONFIG_TYPE_I32,          &c->main_config.window_x};
    out[i++] = (ConfigOption){"main.window_y",                              CONFIG_TYPE_I32,          &c->main_config.window_y};
    out[i++] = (ConfigOption){"main.window_width",                          CONFIG_TYPE_I32,          &c->main_config.window_width};
    out[i++] = (ConfigOption){"main.window_height",                         CONFIG_TYPE_I32,          &c->main_config.window_height};

    /* streaming.* */
    out[i++] = (ConfigOption){"streaming.service",                          CONFIG_TYPE_STRING,       &c->streaming_config.streaming_service};
    out[i++] = (ConfigOption){"streaming.youtube.key",                      CONFIG_TYPE_STRING,       &c->streaming_config.youtube.stream_key};
    out[i++] = (ConfigOption){"streaming.twitch.key",                       CONFIG_TYPE_STRING,       &c->streaming_config.twitch.stream_key};
    out[i++] = (ConfigOption){"streaming.custom.url",                       CONFIG_TYPE_STRING,       &c->streaming_config.custom.url};
    out[i++] = (ConfigOption){"streaming.custom.container",                 CONFIG_TYPE_STRING,       &c->streaming_config.custom.container};
    out[i++] = (ConfigOption){"streaming.start_stop_recording_hotkey",      CONFIG_TYPE_HOTKEY,       &c->streaming_config.start_stop_recording_hotkey};

    /* record.* */
    out[i++] = (ConfigOption){"record.save_directory",                      CONFIG_TYPE_STRING,       &c->record_config.save_directory};
    out[i++] = (ConfigOption){"record.container",                           CONFIG_TYPE_STRING,       &c->record_config.container};
    out[i++] = (ConfigOption){"record.start_stop_recording_hotkey",         CONFIG_TYPE_HOTKEY,       &c->record_config.start_stop_recording_hotkey};
    out[i++] = (ConfigOption){"record.pause_unpause_recording_hotkey",      CONFIG_TYPE_HOTKEY,       &c->record_config.pause_unpause_recording_hotkey};

    /* replay.* */
    out[i++] = (ConfigOption){"replay.save_directory",                      CONFIG_TYPE_STRING,       &c->replay_config.save_directory};
    out[i++] = (ConfigOption){"replay.container",                           CONFIG_TYPE_STRING,       &c->replay_config.container};
    out[i++] = (ConfigOption){"replay.time",                                CONFIG_TYPE_I32,          &c->replay_config.replay_time};
    out[i++] = (ConfigOption){"replay.start_stop_recording_hotkey",         CONFIG_TYPE_HOTKEY,       &c->replay_config.start_stop_recording_hotkey};
    out[i++] = (ConfigOption){"replay.save_recording_hotkey",               CONFIG_TYPE_HOTKEY,       &c->replay_config.save_recording_hotkey};

    return i;
}

#define MAX_OPTIONS 64  /* current count is 44; padded for headroom */

void app_state_init(Config *config)
{
    memset(config, 0, sizeof(*config));
    /* Non-zero defaults — mirrored from src/config.hpp. */
    config->main_config.fps                                     = 60;
    config->main_config.video_bitrate                           = 15000;
    config->main_config.merge_audio_tracks                      = true;
    config->main_config.show_recording_saved_notifications      = true;
    config->main_config.record_cursor                           = true;
    config->main_config.restore_portal_session                  = true;
    config->replay_config.replay_time                           = 30;
    string_array_init(&config->main_config.audio_input);
}

void app_state_free(Config *config)
{
    free(config->main_config.record_area_option);
    free(config->main_config.color_range);
    free(config->main_config.quality);
    free(config->main_config.codec);
    free(config->main_config.audio_codec);
    free(config->main_config.framerate_mode);
    string_array_free(&config->main_config.audio_input);

    free(config->streaming_config.streaming_service);
    free(config->streaming_config.youtube.stream_key);
    free(config->streaming_config.twitch.stream_key);
    free(config->streaming_config.custom.url);
    free(config->streaming_config.custom.container);

    free(config->record_config.save_directory);
    free(config->record_config.container);

    free(config->replay_config.save_directory);
    free(config->replay_config.container);

    memset(config, 0, sizeof(*config));
}

/* Parse a single "key value" line and apply it against the option table. */
static void apply_line(StringView line, ConfigOption *opts, size_t opt_count)
{
    /* Split on the first space. */
    const void *sp = memchr(line.str, ' ', line.size);
    if(!sp) {
        fprintf(stderr, "Warning: invalid config option format: %.*s\n",
                (int)line.size, line.str);
        return;
    }

    StringView key = { line.str, (size_t)((const char *)sp - line.str) };
    StringView val = { (const char *)sp + 1, line.size - (key.size + 1) };

    if(key.size == 0 || val.size == 0)
        return;

    for(size_t i = 0; i < opt_count; ++i) {
        size_t klen = strlen(opts[i].key);
        if(klen != key.size || memcmp(opts[i].key, key.str, klen) != 0)
            continue;

        switch(opts[i].type) {
        case CONFIG_TYPE_BOOL:
            *(bool *)opts[i].data = string_view_eq_cstr(val, "true");
            return;
        case CONFIG_TYPE_STRING:
            xfree_set((char **)opts[i].data, xstrndup(val.str, val.size));
            return;
        case CONFIG_TYPE_I32: {
            char buf[32];
            size_t n = val.size < sizeof(buf) - 1 ? val.size : sizeof(buf) - 1;
            memcpy(buf, val.str, n);
            buf[n] = '\0';
            int32_t v = 0;
            if(sscanf(buf, FORMAT_I32, &v) != 1) {
                fprintf(stderr, "Warning: invalid config option value for %.*s\n",
                        (int)key.size, key.str);
                v = 0;
            }
            *(int32_t *)opts[i].data = v;
            return;
        }
        case CONFIG_TYPE_HOTKEY: {
            char buf[64];
            size_t n = val.size < sizeof(buf) - 1 ? val.size : sizeof(buf) - 1;
            memcpy(buf, val.str, n);
            buf[n] = '\0';
            ConfigHotkey *h = (ConfigHotkey *)opts[i].data;
            if(sscanf(buf, FORMAT_I64 " " FORMAT_U32, &h->keysym, &h->modifiers) != 2) {
                fprintf(stderr, "Warning: invalid config option value for %.*s\n",
                        (int)key.size, key.str);
                h->keysym = 0;
                h->modifiers = 0;
            }
            return;
        }
        case CONFIG_TYPE_STRING_ARRAY:
            string_array_push((StringArray *)opts[i].data,
                              xstrndup(val.str, val.size));
            return;
        }
    }
    /* Unknown key — silently ignored, matching original. */
}

bool app_state_load(Config *config)
{
    char *config_dir = xdg_paths_config_dir();
    char *path = xasprintf("%s/config", config_dir);
    free(config_dir);

    char  *content = NULL;
    size_t size = 0;
    bool   ok = xdg_paths_file_get_content(path, &content, &size);
    if(!ok) {
        fprintf(stderr, "Warning: failed to read config file: %s\n", path);
        free(path);
        return false;
    }
    free(path);

    ConfigOption opts[MAX_OPTIONS];
    size_t opt_count = build_options(config, opts);

    size_t i = 0;
    while(i < size) {
        size_t j = i;
        while(j < size && content[j] != '\n')
            ++j;
        StringView line = { content + i, j - i };
        apply_line(line, opts, opt_count);
        i = j + 1;
    }

    free(content);
    return true;
}

static int compare_option_by_key(const void *a, const void *b)
{
    const ConfigOption *oa = (const ConfigOption *)a;
    const ConfigOption *ob = (const ConfigOption *)b;
    return strcmp(oa->key, ob->key);
}

void app_state_save(const Config *config)
{
    char *config_dir = xdg_paths_config_dir();
    char *path = xasprintf("%s/config", config_dir);

    if(xdg_paths_create_directory_recursive(config_dir) != 0) {
        fprintf(stderr, "Warning: failed to create config directory: %s\n", config_dir);
        free(config_dir);
        free(path);
        return;
    }
    free(config_dir);

    FILE *f = fopen(path, "wb");
    if(!f) {
        fprintf(stderr, "Warning: failed to create config file: %s\n", path);
        free(path);
        return;
    }
    free(path);

    /* build_options() takes a mutable pointer (it stores field addresses).
     * Save is conceptually const, but we need the pointer-into-fields trick.
     * Cast away const for the table build; we never write through these
     * pointers on the save path. */
    ConfigOption opts[MAX_OPTIONS];
    size_t opt_count = build_options((Config *)config, opts);
    qsort(opts, opt_count, sizeof(opts[0]), compare_option_by_key);

    for(size_t i = 0; i < opt_count; ++i) {
        switch(opts[i].type) {
        case CONFIG_TYPE_BOOL:
            fprintf(f, "%s %s\n", opts[i].key,
                    *(const bool *)opts[i].data ? "true" : "false");
            break;
        case CONFIG_TYPE_STRING: {
            const char *s = *(const char *const *)opts[i].data;
            fprintf(f, "%s %s\n", opts[i].key, s ? s : "");
            break;
        }
        case CONFIG_TYPE_I32:
            fprintf(f, "%s " FORMAT_I32 "\n", opts[i].key,
                    *(const int32_t *)opts[i].data);
            break;
        case CONFIG_TYPE_HOTKEY: {
            const ConfigHotkey *h = (const ConfigHotkey *)opts[i].data;
            fprintf(f, "%s " FORMAT_I64 " " FORMAT_U32 "\n",
                    opts[i].key, h->keysym, h->modifiers);
            break;
        }
        case CONFIG_TYPE_STRING_ARRAY: {
            const StringArray *a = (const StringArray *)opts[i].data;
            for(size_t k = 0; k < a->len; ++k)
                fprintf(f, "%s %s\n", opts[i].key, a->items[k]);
            break;
        }
        }
    }

    fclose(f);
}
