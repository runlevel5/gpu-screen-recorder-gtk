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

/* Page Start/Stop/Save/Pause buttons funnel through this. The orchestrator
 * looks at the current recorder state and dispatches:
 *   SESSION_TOGGLE_RUN  — start if idle for `source`, SIGINT if running.
 *   SESSION_PAUSE       — SIGUSR2 (recording only).
 *   SESSION_SAVE        — SIGUSR1 (replay only).
 */
typedef enum {
    SESSION_TOGGLE_RUN = 0,
    SESSION_PAUSE,
    SESSION_SAVE,
} SessionAction;

typedef void (*page_session_cb)(SessionAction action, PageId source, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* GSR_UI_NAV_H */
