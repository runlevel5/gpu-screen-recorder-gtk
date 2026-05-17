#ifndef GSR_X11_HOTKEYS_H
#define GSR_X11_HOTKEYS_H

/*
 * X11 global-hotkey primitives. Toolkit-agnostic: takes a Display* and
 * leaves dispatch policy (which hotkey -> which action on which page) to
 * the caller in the page code.
 *
 * Modifier mask convention matches the original GTK port: a bitmap of
 * `1 << (keysym - XK_Shift_L)` values, translated to the X11 modifier mask
 * (ControlMask | Mod1Mask | etc.) at grab time.
 */

#include <X11/Xlib.h>
#include <stdbool.h>
#include <stdint.h>

#include "app_state.h"  /* ConfigHotkey */

#ifdef __cplusplus
extern "C" {
#endif

bool     gsr_key_is_modifier   (KeySym key_sym);
uint32_t gsr_modkey_to_mask    (KeySym key_sym);   /* requires gsr_key_is_modifier(key_sym) */
uint32_t gsr_key_mod_mask_to_x11(uint32_t mask);   /* internal bitmap -> X11 ModXMask */
unsigned int gsr_key_state_without_locks(unsigned int key_state);

/* Look up a human-readable name for `key_sym` into `buffer`. Returns bytes
 * written (without trailing NUL). Returns 0 on failure / unknown / buffer too
 * small. `xic` may be NULL (loses UTF-8 names; falls back to XKeysymToString).
 */
int gsr_key_get_name(Display *display, XIC xic, KeySym key_sym, char *buffer, int buffer_size);

/* Format a hotkey for display, e.g. "Ctrl + Shift + A". Writes up to
 * buffer_size-1 bytes plus NUL. Returns characters written. */
size_t gsr_hotkey_format(Display *display, XIC xic, ConfigHotkey hotkey, char *buffer, size_t buffer_size);

/* Grab (or ungrab) a hotkey on the root window. Returns true if the grab
 * succeeded (no BadAccess from X). On a failed grab, partial state is rolled
 * back. Safe to call with keysym=0 and modifiers=0 (no-op, returns true). */
bool gsr_hotkey_grab(Display *display, ConfigHotkey hotkey, bool grab);

/* Callback invoked for each KeyPress on the root window that the drain picks
 * up. `x11_modifiers` is the raw X state with NumLock/CapsLock cleared. */
typedef void (*gsr_hotkey_callback)(KeySym key_sym, unsigned int x11_modifiers, void *user_data);

/* Drains pending root-window key events and dispatches them via `callback`.
 * Returns the number of events dispatched. Designed to be called from a
 * short-interval timer (~50ms): Xt does not natively dispatch root-window
 * events to widget callbacks, so we pull them out of the queue manually
 * with XCheckIfEvent. */
int gsr_hotkey_drain_root_events(Display *display, gsr_hotkey_callback callback, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* GSR_X11_HOTKEYS_H */
