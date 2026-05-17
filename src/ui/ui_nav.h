#ifndef GSR_UI_NAV_H
#define GSR_UI_NAV_H

/* Shared navigation enum + callback type used by every page. */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PAGE_COMMON_SETTINGS = 0,
    PAGE_REPLAY,
    PAGE_RECORDING,
    PAGE_STREAMING,
    PAGE_COUNT
} PageId;

/* Pages invoke this to ask the orchestrator to switch the visible page. */
typedef void (*page_nav_cb)(PageId target, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* GSR_UI_NAV_H */
