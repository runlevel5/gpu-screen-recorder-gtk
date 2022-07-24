#pragma once

#include <string>
#include <string.h>
#include <functional>
#include <unistd.h>
#include <limits.h>
#include <libgen.h>
#include <stdio.h>
#include <pwd.h>
#include <sys/stat.h>

struct MainConfig {
    std::string record_area_option;
    int fps = 60;
    std::string audio_input;
    std::string quality;
};

struct StreamingConfig {
    std::string streaming_service;
    std::string stream_key;
};

struct RecordConfig {
    std::string save_directory;
};

struct ReplayConfig {
    std::string save_directory;
    int replay_time = 30;
};

struct Config {
    MainConfig main_config;
    StreamingConfig streaming_config;
    RecordConfig record_config;
    ReplayConfig replay_config;
};

std::string get_home_dir() {
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
    const char *str;
    size_t size;

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

static Config read_config() {
    Config config;

    std::string config_path = get_home_dir();
    config_path += "/.config/gpu-screen-recorder/config";

    std::string file_content;
    if(!file_get_content(config_path.c_str(), file_content)) {
        fprintf(stderr, "Warning: Failed to read config file: %s\n", config_path.c_str());
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
        } else if(key == "main.fps") {
            if(!string_to_int(std::string(value.str, value.size), config.main_config.fps)) {
                fprintf(stderr, "Warning: Invalid config option main.fps\n");
                config.main_config.fps = 60;
            }
        } else if(key == "main.audio_input") {
            config.main_config.audio_input.assign(value.str, value.size);
        } else if(key == "main.quality") {
            config.main_config.quality.assign(value.str, value.size);
        } else if(key == "streaming.service") {
            config.streaming_config.streaming_service.assign(value.str, value.size);
        } else if(key == "streaming.key") {
            config.streaming_config.stream_key.assign(value.str, value.size);
        } else if(key == "record.save_directory") {
            config.record_config.save_directory.assign(value.str, value.size);
        } else if(key == "replay.save_directory") {
            config.replay_config.save_directory.assign(value.str, value.size);
        } else if(key == "replay.time") {
            if(!string_to_int(std::string(value.str, value.size), config.replay_config.replay_time)) {
                fprintf(stderr, "Warning: Invalid config option replay.time\n");
                config.replay_config.replay_time = 30;
            }
        } else {
            fprintf(stderr, "Warning: Invalid config option: %.*s\n", (int)line.size, line.str);
        }

        return true;
    });

    return config;
}

static void save_config(const Config &config) {
    std::string config_path = get_home_dir();
    config_path += "/.config/gpu-screen-recorder/config";

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
    fprintf(file, "main.fps %d\n", config.main_config.fps);
    fprintf(file, "main.audio_input %s\n", config.main_config.audio_input.c_str());
    fprintf(file, "main.quality %s\n", config.main_config.quality.c_str());

    fprintf(file, "streaming.service %s\n", config.streaming_config.streaming_service.c_str());
    fprintf(file, "streaming.key %s\n", config.streaming_config.stream_key.c_str());

    fprintf(file, "record.save_directory %s\n", config.record_config.save_directory.c_str());

    fprintf(file, "replay.save_directory %s\n", config.replay_config.save_directory.c_str());
    fprintf(file, "replay.time %d\n", config.replay_config.replay_time);

    fclose(file);
}