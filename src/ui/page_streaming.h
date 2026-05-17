#ifndef GSR_UI_PAGE_STREAMING_H
#define GSR_UI_PAGE_STREAMING_H

#include <Xm/Xm.h>

#include "ui_nav.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    Widget root;
    Widget back_btn;
} PageStreaming;

void page_streaming_create(Widget parent, PageStreaming *out,
                           page_nav_cb nav, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* GSR_UI_PAGE_STREAMING_H */
