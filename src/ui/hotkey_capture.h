#ifndef GSR_UI_HOTKEY_CAPTURE_H
#define GSR_UI_HOTKEY_CAPTURE_H

/*
 * Modal hotkey-capture dialog.
 *
 * Pops up a small "Press a key combination, Esc to cancel" prompt, grabs
 * the keyboard, and listens for KeyPress events. When the user presses a
 * non-modifier key the combination is captured and passed back. Esc
 * cancels; Backspace clears the binding (keysym=0, modifiers=0).
 */

#include <X11/Xlib.h>
#include <Xm/Xm.h>

#include "../app_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HOTKEY_CAPTURE_OK = 0,
    HOTKEY_CAPTURE_CANCELLED,
    HOTKEY_CAPTURE_CLEARED,
} HotkeyCaptureResult;

typedef void (*hotkey_capture_done_cb)(HotkeyCaptureResult result,
                                       ConfigHotkey new_hotkey,
                                       void *user_data);

/* Show the modal. Returns immediately; the result arrives via `done` once
 * the user accepts/cancels/clears. */
void hotkey_capture_run(Widget parent, Display *display, XIC xic,
                        hotkey_capture_done_cb done, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* GSR_UI_HOTKEY_CAPTURE_H */
