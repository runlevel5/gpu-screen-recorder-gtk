#ifndef GSR_UI_WINDOW_PICKER_H
#define GSR_UI_WINDOW_PICKER_H

/*
 * X11 window picker: grabs the pointer with a crosshair cursor, waits for
 * the user to click a window, walks up the tree to find the toplevel
 * client window, and reports its X window ID via callback. Right-click
 * cancels (reports 0).
 */

#include <Xm/Xm.h>

#ifdef __cplusplus
extern "C" {
#endif

/* `window_id` is 0 if the user cancelled. (unsigned long matches X11's
 * `Window` typedef without requiring callers to include Xlib.) */
typedef void (*window_picked_cb)(unsigned long window_id, void *user_data);

void window_picker_run(Widget parent, window_picked_cb cb, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* GSR_UI_WINDOW_PICKER_H */
