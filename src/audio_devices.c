#include "audio_devices.h"
#include "str_util.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void audio_device_list_init(AudioDeviceList *list)
{
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
}

void audio_device_list_free(AudioDeviceList *list)
{
    for(size_t i = 0; i < list->len; ++i) {
        free(list->items[i].name);
        free(list->items[i].description);
    }
    free(list->items);
    list->items = NULL;
    list->len = list->cap = 0;
}

static void audio_device_list_push(AudioDeviceList *list, AudioDevice dev)
{
    if(list->len == list->cap) {
        size_t cap = list->cap ? list->cap * 2 : 4;
        AudioDevice *n = (AudioDevice *)realloc(list->items, cap * sizeof(*n));
        assert(n && "OOM in audio_device_list_push");
        list->items = n;
        list->cap = cap;
    }
    list->items[list->len++] = dev;
}

/* Read entire process output into a freshly malloc'd, NUL-terminated buffer. */
static bool run_capture_output(const char *cmd, char **out, size_t *out_size)
{
    *out = NULL;
    *out_size = 0;

    FILE *f = popen(cmd, "r");
    if(!f) {
        fprintf(stderr, "error: popen failed: %s\n", cmd);
        return false;
    }

    enum { CHUNK = 8192 };
    size_t cap = CHUNK;
    size_t len = 0;
    char  *buf = (char *)malloc(cap);
    if(!buf) {
        pclose(f);
        return false;
    }

    for(;;) {
        if(cap - len < CHUNK) {
            size_t new_cap = cap * 2;
            char  *nb = (char *)realloc(buf, new_cap);
            if(!nb) {
                free(buf);
                pclose(f);
                return false;
            }
            buf = nb;
            cap = new_cap;
        }
        size_t r = fread(buf + len, 1, cap - len - 1, f);
        len += r;
        if(r == 0)
            break;
    }

    bool io_ok = !ferror(f);
    pclose(f);
    if(!io_ok) {
        fprintf(stderr, "error: read failed for: %s\n", cmd);
        free(buf);
        return false;
    }

    buf[len] = '\0';
    *out = buf;
    *out_size = len;
    return true;
}

static AudioDevice parse_audio_device_line(const char *line, size_t line_len)
{
    AudioDevice dev = {NULL, NULL};
    const void *sep = memchr(line, '|', line_len);
    if(!sep)
        return dev;
    size_t name_len = (size_t)((const char *)sep - line);
    size_t desc_off = name_len + 1;
    size_t desc_len = line_len - desc_off;
    dev.name = xstrndup(line, name_len);
    dev.description = xstrndup(line + desc_off, desc_len);
    return dev;
}

bool audio_devices_query_inputs(AudioDeviceList *out)
{
    char  *content = NULL;
    size_t size = 0;
    if(!run_capture_output("gpu-screen-recorder --list-audio-devices", &content, &size))
        return false;

    size_t i = 0;
    while(i < size) {
        size_t j = i;
        while(j < size && content[j] != '\n')
            ++j;
        if(j > i)
            audio_device_list_push(out, parse_audio_device_line(content + i, j - i));
        i = j + 1;
    }

    free(content);
    return true;
}

bool audio_devices_query_applications(StringArray *out_apps)
{
    char  *content = NULL;
    size_t size = 0;
    if(!run_capture_output("gpu-screen-recorder --list-application-audio", &content, &size))
        return false;

    size_t i = 0;
    while(i < size) {
        size_t j = i;
        while(j < size && content[j] != '\n')
            ++j;
        if(j > i)
            string_array_push(out_apps, xstrndup(content + i, j - i));
        i = j + 1;
    }

    free(content);
    return true;
}
