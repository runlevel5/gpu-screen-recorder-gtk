#ifndef GSR_UI_PAGE_RECORDING_H
#define GSR_UI_PAGE_RECORDING_H

#include <Xm/Xm.h>

#include "ui_nav.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    Widget root;
    Widget back_btn;
} PageRecording;

void page_recording_create(Widget parent, PageRecording *out,
                           page_nav_cb nav, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* GSR_UI_PAGE_RECORDING_H */
