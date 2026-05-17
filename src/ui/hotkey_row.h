#ifndef GSR_UI_HOTKEY_ROW_H
#define GSR_UI_HOTKEY_ROW_H

/*
 * "Label: [binding button]" row. The button shows the currently-bound
 * combination (e.g. "Ctrl + Alt + 1") and clicking it opens the
 * hotkey-capture modal. Updates `*target` on accept / clear.
 */

#include <X11/Xlib.h>
#include <Xm/Xm.h>

#include "../app_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    Widget        row;
    Widget        button;
    ConfigHotkey *target;     /* updated in place on accept/clear */
    Display      *display;
    XIC           xic;
} HotkeyRow;

void hotkey_row_create(Widget parent, HotkeyRow *out,
                       const char *label_text,
                       ConfigHotkey *target,
                       Display *display, XIC xic);

/* Refresh the button label from `*target` (use after manual config edits). */
void hotkey_row_refresh(HotkeyRow *row);

#ifdef __cplusplus
}
#endif

#endif /* GSR_UI_HOTKEY_ROW_H */
