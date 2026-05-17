#ifndef GSR_UI_PAGE_COMMON_SETTINGS_H
#define GSR_UI_PAGE_COMMON_SETTINGS_H

#include <Xm/Xm.h>

#include "ui_nav.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    Widget root;          /* XmForm; child of caller's parent */
    Widget stream_btn;
    Widget record_btn;
    Widget replay_btn;
} PageCommonSettings;

/* Phase 4: hub page with three navigation buttons (Stream / Record / Replay).
 * Real settings widgets land in Phase 5. */
void page_common_settings_create(Widget parent, PageCommonSettings *out,
                                 page_nav_cb nav, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* GSR_UI_PAGE_COMMON_SETTINGS_H */
