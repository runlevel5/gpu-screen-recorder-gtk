#ifndef GSR_UI_PAGE_REPLAY_H
#define GSR_UI_PAGE_REPLAY_H

#include <Xm/Xm.h>

#include "ui_nav.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    Widget root;
    Widget back_btn;
} PageReplay;

void page_replay_create(Widget parent, PageReplay *out,
                        page_nav_cb nav, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* GSR_UI_PAGE_REPLAY_H */
