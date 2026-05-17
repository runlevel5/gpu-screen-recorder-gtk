#ifndef GSR_UI_PAGE_RECORDING_H
#define GSR_UI_PAGE_RECORDING_H

#include <Xm/Xm.h>

#include "../app_state.h"
#include "ui_nav.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    Widget root;
    Widget save_dir_text;
    Widget container_combo;
    Widget back_btn;
    Widget start_btn;
    Widget pause_btn;
} PageRecording;

void page_recording_create(Widget parent, PageRecording *out,
                           const Config *config,
                           page_nav_cb nav, page_session_cb session,
                           void *user_data);

void page_recording_commit(const PageRecording *p, Config *config);

#ifdef __cplusplus
}
#endif

#endif /* GSR_UI_PAGE_RECORDING_H */
