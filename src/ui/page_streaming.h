#ifndef GSR_UI_PAGE_STREAMING_H
#define GSR_UI_PAGE_STREAMING_H

#include <Xm/Xm.h>

#include "../app_state.h"
#include "ui_nav.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    Widget root;
    Widget service_combo;
    Widget stream_key_label;
    Widget youtube_key_text;
    Widget twitch_key_text;
    Widget custom_url_text;
    Widget custom_container_row;
    Widget custom_container_combo;
    Widget back_btn;
    Widget start_btn;
} PageStreaming;

void page_streaming_create(Widget parent, PageStreaming *out,
                           const Config *config,
                           page_nav_cb nav, void *user_data);

void page_streaming_commit(const PageStreaming *p, Config *config);

#ifdef __cplusplus
}
#endif

#endif /* GSR_UI_PAGE_STREAMING_H */
