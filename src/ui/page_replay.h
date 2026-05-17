#ifndef GSR_UI_PAGE_REPLAY_H
#define GSR_UI_PAGE_REPLAY_H

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
    Widget replay_time_spin;
    Widget back_btn;
    Widget start_btn;
    Widget save_btn;
} PageReplay;

void page_replay_create(Widget parent, PageReplay *out,
                        const Config *config,
                        page_nav_cb nav, void *user_data);

/* Pull widget values back into the Config. Called on navigation away
 * and on Start click. */
void page_replay_commit(const PageReplay *p, Config *config);

#ifdef __cplusplus
}
#endif

#endif /* GSR_UI_PAGE_REPLAY_H */
