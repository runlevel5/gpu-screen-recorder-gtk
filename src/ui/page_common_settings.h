#ifndef GSR_UI_PAGE_COMMON_SETTINGS_H
#define GSR_UI_PAGE_COMMON_SETTINGS_H

#include <Xm/Xm.h>

#include "../app_state.h"
#include "../audio_devices.h"
#include "../capabilities.h"
#include "ui_nav.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    Widget root;

    Widget view_combo;

    /* Capture target */
    Widget record_area_combo;
    Widget area_size_row;     /* parent of area_width_spin + area_height_spin */
    Widget area_width_spin;
    Widget area_height_spin;
    Widget video_width_spin;
    Widget video_height_spin;
    Widget change_video_resolution_toggle;
    Widget restore_portal_session_toggle;
    Widget select_window_row;        /* visible only when record_area=window */
    Widget select_window_btn;
    Widget selected_window_label;
    unsigned long *selected_window_slot;  /* writable; lives in AppCtx */

    /* Audio */
    Widget audio_codec_combo;
    Widget merge_audio_toggle;          /* inverted: ON => merge=true */
    Widget record_app_audio_inverted_toggle;

    /* Dynamic audio rows — see page_common_settings.c. Opaque to callers. */
    Widget           audio_rows_box;     /* vertical RowColumn parent of rows */
    Widget           add_device_btn;
    Widget           add_app_btn;
    Widget           add_custom_app_btn;
    AudioDeviceList  detected_devices;   /* `pgr --list-audio-devices` */
    StringArray      detected_apps;      /* `pgr --list-application-audio` */
    /* Rows stored as void* to keep the internal AudioRow type opaque. */
    void            *audio_rows;

    /* Video */
    Widget quality_combo;
    Widget bitrate_spin;
    Widget codec_combo;
    Widget color_range_row;        /* advanced-only */
    Widget color_range_combo;
    Widget fps_spin;
    Widget framerate_mode_row;     /* advanced-only */
    Widget framerate_mode_combo;
    Widget record_cursor_toggle;
    Widget overclock_toggle;       /* advanced + NVIDIA + X11 only */

    /* Notifications */
    Widget notifications_frame;    /* advanced-only */
    Widget notif_started_toggle;
    Widget notif_stopped_toggle;
    Widget notif_saved_toggle;

    /* Hub actions */
    Widget stream_btn;
    Widget record_btn;
    Widget replay_btn;
} PageCommonSettings;

void page_common_settings_create(Widget parent, PageCommonSettings *out,
                                 const Config *config,
                                 const GsrCapabilities *caps,
                                 unsigned long *selected_window_slot,
                                 page_nav_cb nav, void *user_data);

void page_common_settings_commit(const PageCommonSettings *p, Config *config);

#ifdef __cplusplus
}
#endif

#endif /* GSR_UI_PAGE_COMMON_SETTINGS_H */
