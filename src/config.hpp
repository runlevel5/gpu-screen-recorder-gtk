#pragma once

#include <vector>
#include <string>
#include <string.h>
#include <functional>
#include <map>
#include <unistd.h>
#include <limits.h>
#include <inttypes.h>
#include <libgen.h>
#include <stdio.h>
#include <stdint.h>
#include <pwd.h>
#include <sys/stat.h>

struct ConfigHotkey {
    int64_t keysym = 0;
    uint32_t modifiers = 0;
};

struct MainConfig {
    std::string record_area_option;
    int record_area_width = 0;
    int record_area_height = 0;
    int fps = 60;
    bool merge_audio_tracks = true;
    std::vector<std::string> audio_input;
    std::string color_range;
    std::string quality;
    std::string codec; // Video codec
    std::string audio_codec;
    std::string framerate_mode;
    bool advanced_view = false;
    bool overclock = false;
    bool show_notifications = true;
    bool record_cursor = true;
};

struct YoutubeStreamConfig {
    std::string stream_key;
};

struct TwitchStreamConfig {
    std::string stream_key;
};

struct CustomStreamConfig {
    std::string url;
    std::string container;
};

struct StreamingConfig {
    std::string streaming_service;
    YoutubeStreamConfig youtube;
    TwitchStreamConfig twitch;
    CustomStreamConfig custom;
    ConfigHotkey start_recording_hotkey;
};

struct RecordConfig {
    std::string save_directory;
    std::string container;
    ConfigHotkey start_recording_hotkey;
    ConfigHotkey pause_recording_hotkey;
};

struct ReplayConfig {
    std::string save_directory;
    std::string container;
    int replay_time = 30;
    ConfigHotkey start_recording_hotkey;
    ConfigHotkey save_recording_hotkey;
};

struct Config {
    MainConfig main_config;
    StreamingConfig streaming_config;
    RecordConfig record_config;
    ReplayConfig replay_config;
};

static std::string get_home_dir() {
    const char *home_dir = getenv("HOME");
    if(!home_dir) {
        passwd *pw = getpwuid(getuid());
        home_dir = pw->pw_dir;
    }

    if(!home_dir) {
        fprintf(stderr, "Error: Failed to get home directory of user, using /tmp directory\n");
        home_dir = "/tmp";
    }

    return home_dir;
}

static std::string get_config_dir() {
    std::string config_dir;
    const char *xdg_config_home = getenv("XDG_CONFIG_HOME");
    if(xdg_config_home) {
        config_dir = xdg_config_home;
    } else {
        config_dir = get_home_dir() + "/.config";
    }
    config_dir += "/gpu-screen-recorder";
    return config_dir;
}

// Whoever designed xdg-user-dirs is retarded. Why are some XDG variables environment variables
// while others are in this pseudo shell config file ~/.config/user-dirs.dirs
static std::map<std::string, std::string> get_xdg_variables() {
    std::string user_dirs_filepath;
    const char *xdg_config_home = getenv("XDG_CONFIG_HOME");
    if(xdg_config_home) {
        user_dirs_filepath = xdg_config_home;
    } else {
        user_dirs_filepath = get_home_dir() + "/.config";
    }

    user_dirs_filepath += "/user-dirs.dirs";

    std::map<std::string, std::string> result;
    FILE *f = fopen(user_dirs_filepath.c_str(), "rb");
    if(!f)
        return result;

    char line[PATH_MAX];
    while(fgets(line, sizeof(line), f)) {
        int len = strlen(line);
        if(len < 2)
            continue;

        if(line[0] == '#')
            continue;

        if(line[len - 1] == '\n') {
            line[len - 1] = '\0';
            len--;
        }

        if(line[len - 1] != '"')
            continue;

        line[len - 1] = '\0';
        len--;

        const char *sep = strchr(line, '=');
        if(!sep)
            continue;

        if(sep[1] != '\"')
            continue;

        std::string value(sep + 2);
        if(strncmp(value.c_str(), "$HOME/", 6) == 0)
            value = get_home_dir() + value.substr(5);

        std::string key(line, sep - line);
        result[std::move(key)] = std::move(value);
    }

    fclose(f);
    return result;
}

static std::string get_videos_dir() {
    auto xdg_vars = get_xdg_variables();
    std::string xdg_videos_dir = xdg_vars["XDG_VIDEOS_DIR"];
    if(xdg_videos_dir.empty())
        xdg_videos_dir = get_home_dir() + "/Videos";
    return xdg_videos_dir;
}

static int create_directory_recursive(char *path) {
    int path_len = strlen(path);
    char *p = path;
    char *end = path + path_len;
    for(;;) {
        char *slash_p = strchr(p, '/');

        // Skips first '/', we don't want to try and create the root directory
        if(slash_p == path) {
            ++p;
            continue;
        }

        if(!slash_p)
            slash_p = end;

        char prev_char = *slash_p;
        *slash_p = '\0';
        int err = mkdir(path, S_IRWXU);
        *slash_p = prev_char;

        if(err == -1 && errno != EEXIST)
            return err;

        if(slash_p == end)
            break;
        else
            p = slash_p + 1;
    }
    return 0;
}

static bool file_get_content(const char *filepath, std::string &file_content) {
    file_content.clear();
    bool success = false;

    FILE *file = fopen(filepath, "rb");
    if(!file)
        return success;

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    if(file_size != -1) {
        file_content.resize(file_size);
        fseek(file, 0, SEEK_SET);
        if((long)fread(&file_content[0], 1, file_size, file) == file_size)
            success = true;
    }

    fclose(file);
    return success;
}

struct StringView {
    const char *str = nullptr;
    size_t size = 0;

    bool operator == (const char *other) const {
        int len = strlen(other);
        return (size_t)len == size && memcmp(str, other, size) == 0;
    }

    size_t find(char c) const {
        const void *p = memchr(str, c, size);
        if(!p)
            return std::string::npos;
        return (const char*)p - str;
    }
};

using StringSplitCallback = std::function<bool(StringView line)>;

static void string_split_char(const std::string &str, char delimiter, StringSplitCallback callback_func) {
    size_t index = 0;
    while(index < str.size()) {
        size_t new_index = str.find(delimiter, index);
        if(new_index == std::string::npos)
            new_index = str.size();

        if(!callback_func({str.data() + index, new_index - index}))
            break;

        index = new_index + 1;
    }
}

static bool config_split_key_value(const StringView str, StringView &key, StringView &value) {
    key.str = nullptr;
    key.size = 0;

    value.str = nullptr;
    value.size = 0;

    size_t index = str.find(' ');
    if(index == std::string::npos)
        return std::string::npos;

    key.str = str.str;
    key.size = index;

    value.str = str.str + index + 1;
    value.size = str.size - (index + 1);
    
    return true;
}

static bool string_to_int(std::string str, int &value) {
    value = 0;
    errno = 0;
    char *endptr;
    value = (int)strtoll(str.c_str(), &endptr, 10);
    if(endptr == str.c_str() || errno != 0)
        return false;
    return true;
}

#define FORMAT_I64 "%" PRIi64
#define FORMAT_U32 "%" PRIu32

static Config read_config(bool &config_empty) {
    Config config;

    const std::string config_path = get_config_dir() + "/config";
    std::string file_content;
    if(!file_get_content(config_path.c_str(), file_content)) {
        fprintf(stderr, "Warning: Failed to read config file: %s\n", config_path.c_str());
        config_empty = true;
        return config;
    }

    string_split_char(file_content, '\n', [&](StringView line) {
        StringView key, value;
        if(!config_split_key_value(line, key, value)) {
            fprintf(stderr, "Warning: Invalid config option format: %.*s\n", (int)line.size, line.str);
            return true;
        }

        if(key == "main.record_area_option") {
            config.main_config.record_area_option.assign(value.str, value.size);
        } else if(key == "main.record_area_width") {
            if(!string_to_int(std::string(value.str, value.size), config.main_config.record_area_width)) {
                fprintf(stderr, "Warning: Invalid config option value for main.record_area_width\n");
                config.main_config.record_area_width = 0;
            }
        } else if(key == "main.record_area_height") {
            if(!string_to_int(std::string(value.str, value.size), config.main_config.record_area_height)) {
                fprintf(stderr, "Warning: Invalid config option value for main.record_area_height\n");
                config.main_config.record_area_height = 0;
            }
        } else if(key == "main.fps") {
            if(!string_to_int(std::string(value.str, value.size), config.main_config.fps)) {
                fprintf(stderr, "Warning: Invalid config option value for main.fps\n");
                config.main_config.fps = 60;
            }
        } else if(key == "main.merge_audio_tracks") {
            if(value == "true")
                config.main_config.merge_audio_tracks = true;
            else if(value == "false")
                config.main_config.merge_audio_tracks = false;
        } else if(key == "main.audio_input") {
            config.main_config.audio_input.emplace_back(value.str, value.size);
        } else if(key == "main.color_range") {
            config.main_config.color_range.assign(value.str, value.size);
        } else if(key == "main.quality") {
            config.main_config.quality.assign(value.str, value.size);
        } else if(key == "main.codec") {
            config.main_config.codec.assign(value.str, value.size);
        } else if(key == "main.audio_codec") {
            config.main_config.audio_codec.assign(value.str, value.size);
        } else if(key == "main.framerate_mode") {
            config.main_config.framerate_mode.assign(value.str, value.size);
        } else if(key == "main.advanced_view") {
            if(value == "true")
                config.main_config.advanced_view = true;
            else if(value == "false")
                config.main_config.advanced_view = false;
        } else if(key == "main.overclock") {
            if(value == "true")
                config.main_config.overclock = true;
            else if(value == "false")
                config.main_config.overclock = false;
        } else if(key == "main.show_notifications") {
            if(value == "true")
                config.main_config.show_notifications = true;
            else if(value == "false")
                config.main_config.show_notifications = false;
        } else if(key == "main.record_cursor") {
            if(value == "true")
                config.main_config.record_cursor = true;
            else if(value == "false")
                config.main_config.record_cursor = false;
        } else if(key == "streaming.service") {
            config.streaming_config.streaming_service.assign(value.str, value.size);
        } else if(key == "streaming.youtube.key") {
            config.streaming_config.youtube.stream_key.assign(value.str, value.size);
        } else if(key == "streaming.twitch.key") {
            config.streaming_config.twitch.stream_key.assign(value.str, value.size);
        } else if(key == "streaming.custom.url") {
            config.streaming_config.custom.url.assign(value.str, value.size);
        } else if(key == "streaming.custom.container") {
            config.streaming_config.custom.container.assign(value.str, value.size);
        } else if(key == "streaming.start_recording_hotkey") {
            std::string value_str(value.str, value.size);
            if(sscanf(value_str.c_str(), FORMAT_I64 " " FORMAT_U32, &config.streaming_config.start_recording_hotkey.keysym, &config.streaming_config.start_recording_hotkey.modifiers) != 2) {
                fprintf(stderr, "Warning: Invalid config option value for streaming.start_recording_hotkey\n");
                config.streaming_config.start_recording_hotkey.keysym = 0;
                config.streaming_config.start_recording_hotkey.modifiers = 0;
            }
        } else if(key == "record.save_directory") {
            config.record_config.save_directory.assign(value.str, value.size);
        } else if(key == "record.container") {
            config.record_config.container.assign(value.str, value.size);
        } else if(key == "record.start_recording_hotkey") {
            std::string value_str(value.str, value.size);
            if(sscanf(value_str.c_str(), FORMAT_I64 " " FORMAT_U32, &config.record_config.start_recording_hotkey.keysym, &config.record_config.start_recording_hotkey.modifiers) != 2) {
                fprintf(stderr, "Warning: Invalid config option value for record.start_recording_hotkey\n");
                config.record_config.start_recording_hotkey.keysym = 0;
                config.record_config.start_recording_hotkey.modifiers = 0;
            }
        } else if(key == "record.pause_recording_hotkey") {
            std::string value_str(value.str, value.size);
            if(sscanf(value_str.c_str(), FORMAT_I64 " " FORMAT_U32, &config.record_config.pause_recording_hotkey.keysym, &config.record_config.pause_recording_hotkey.modifiers) != 2) {
                fprintf(stderr, "Warning: Invalid config option value for record.pause_recording_hotkey\n");
                config.record_config.pause_recording_hotkey.keysym = 0;
                config.record_config.pause_recording_hotkey.modifiers = 0;
            }
        } else if(key == "replay.save_directory") {
            config.replay_config.save_directory.assign(value.str, value.size);
        } else if(key == "replay.container") {
            config.replay_config.container.assign(value.str, value.size);
        } else if(key == "replay.time") {
            if(!string_to_int(std::string(value.str, value.size), config.replay_config.replay_time)) {
                fprintf(stderr, "Warning: Invalid config option value for replay.time\n");
                config.replay_config.replay_time = 30;
            }
        } else if(key == "replay.start_recording_hotkey") {
            std::string value_str(value.str, value.size);
            if(sscanf(value_str.c_str(), FORMAT_I64 " " FORMAT_U32, &config.replay_config.start_recording_hotkey.keysym, &config.replay_config.start_recording_hotkey.modifiers) != 2) {
                fprintf(stderr, "Warning: Invalid config option value for replay.start_recording_hotkey\n");
                config.replay_config.start_recording_hotkey.keysym = 0;
                config.replay_config.start_recording_hotkey.modifiers = 0;
            }
        } else if(key == "replay.save_recording_hotkey") {
            std::string value_str(value.str, value.size);
            if(sscanf(value_str.c_str(), FORMAT_I64 " " FORMAT_U32, &config.replay_config.save_recording_hotkey.keysym, &config.replay_config.save_recording_hotkey.modifiers) != 2) {
                fprintf(stderr, "Warning: Invalid config option value for replay.save_recording_hotkey\n");
                config.replay_config.save_recording_hotkey.keysym = 0;
                config.replay_config.save_recording_hotkey.modifiers = 0;
            }
        } else {
            fprintf(stderr, "Warning: Invalid config option: %.*s\n", (int)line.size, line.str);
        }

        return true;
    });

    return config;
}

static void save_config(const Config &config) {
    const std::string config_path = get_config_dir() + "/config";

    char dir_tmp[PATH_MAX];
    strcpy(dir_tmp, config_path.c_str());
    char *dir = dirname(dir_tmp);

    if(create_directory_recursive(dir) != 0) {
        fprintf(stderr, "Warning: Failed to create config directory: %s\n", dir);
        return;
    }

    FILE *file = fopen(config_path.c_str(), "wb");
    if(!file) {
        fprintf(stderr, "Warning: Failed to create config file: %s\n", config_path.c_str());
        return;
    }

    fprintf(file, "main.record_area_option %s\n", config.main_config.record_area_option.c_str());
    fprintf(file, "main.record_area_width %d\n", config.main_config.record_area_width);
    fprintf(file, "main.record_area_height %d\n", config.main_config.record_area_height);
    fprintf(file, "main.fps %d\n", config.main_config.fps);
    fprintf(file, "main.merge_audio_tracks %s\n", config.main_config.merge_audio_tracks ? "true" : "false");
    for(const std::string &audio_input : config.main_config.audio_input) {
        fprintf(file, "main.audio_input %s\n", audio_input.c_str());
    }
    fprintf(file, "main.color_range %s\n", config.main_config.color_range.c_str());
    fprintf(file, "main.quality %s\n", config.main_config.quality.c_str());
    fprintf(file, "main.codec %s\n", config.main_config.codec.c_str());
    fprintf(file, "main.audio_codec %s\n", config.main_config.audio_codec.c_str());
    fprintf(file, "main.framerate_mode %s\n", config.main_config.framerate_mode.c_str());
    fprintf(file, "main.advanced_view %s\n", config.main_config.advanced_view ? "true" : "false");
    fprintf(file, "main.overclock %s\n", config.main_config.overclock ? "true" : "false");
    fprintf(file, "main.show_notifications %s\n", config.main_config.show_notifications ? "true" : "false");
    fprintf(file, "main.record_cursor %s\n", config.main_config.record_cursor ? "true" : "false");

    fprintf(file, "streaming.service %s\n", config.streaming_config.streaming_service.c_str());
    fprintf(file, "streaming.youtube.key %s\n", config.streaming_config.youtube.stream_key.c_str());
    fprintf(file, "streaming.twitch.key %s\n", config.streaming_config.twitch.stream_key.c_str());
    fprintf(file, "streaming.custom.url %s\n", config.streaming_config.custom.url.c_str());
    fprintf(file, "streaming.custom.container %s\n", config.streaming_config.custom.container.c_str());
    fprintf(file, "streaming.start_recording_hotkey " FORMAT_I64 " " FORMAT_U32 "\n", config.streaming_config.start_recording_hotkey.keysym, config.streaming_config.start_recording_hotkey.modifiers);

    fprintf(file, "record.save_directory %s\n", config.record_config.save_directory.c_str());
    fprintf(file, "record.container %s\n", config.record_config.container.c_str());
    fprintf(file, "record.start_recording_hotkey " FORMAT_I64 " " FORMAT_U32 "\n", config.record_config.start_recording_hotkey.keysym, config.record_config.start_recording_hotkey.modifiers);
    fprintf(file, "record.pause_recording_hotkey " FORMAT_I64 " " FORMAT_U32 "\n", config.record_config.pause_recording_hotkey.keysym, config.record_config.pause_recording_hotkey.modifiers);

    fprintf(file, "replay.save_directory %s\n", config.replay_config.save_directory.c_str());
    fprintf(file, "replay.container %s\n", config.replay_config.container.c_str());
    fprintf(file, "replay.time %d\n", config.replay_config.replay_time);
    fprintf(file, "replay.start_recording_hotkey " FORMAT_I64 " " FORMAT_U32 "\n", config.replay_config.start_recording_hotkey.keysym, config.replay_config.start_recording_hotkey.modifiers);
    fprintf(file, "replay.save_recording_hotkey " FORMAT_I64 " " FORMAT_U32 "\n", config.replay_config.save_recording_hotkey.keysym, config.replay_config.save_recording_hotkey.modifiers);

    fclose(file);
}
