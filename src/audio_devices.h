#ifndef GSR_AUDIO_DEVICES_H
#define GSR_AUDIO_DEVICES_H

#include "str_util.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *name;         /* owned */
    char *description;  /* owned */
} AudioDevice;

typedef struct {
    AudioDevice *items;
    size_t       len;
    size_t       cap;
} AudioDeviceList;

void audio_device_list_init(AudioDeviceList *list);
void audio_device_list_free(AudioDeviceList *list);

/* Runs `gpu-screen-recorder --list-audio-devices`, parsing each line as
 * `name|description`. Returns true if the subprocess was launched and read
 * without I/O error (an empty result is still success). */
bool audio_devices_query_inputs(AudioDeviceList *out);

/* Runs `gpu-screen-recorder --list-application-audio`. One entry per line. */
bool audio_devices_query_applications(StringArray *out_apps);

#ifdef __cplusplus
}
#endif

#endif /* GSR_AUDIO_DEVICES_H */
