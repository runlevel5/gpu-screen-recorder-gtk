#ifndef GSR_TRAY_H
#define GSR_TRAY_H

/*
 * XEmbed / _NET_SYSTEM_TRAY client. Docks a small window into the
 * desktop's system tray (KDE/XFCE/MATE/etc.), paints a coloured-square
 * icon reflecting current recorder state, and dispatches user clicks.
 *
 * Real PNG icons land in Pass B; for now a state-coloured 22x22 square
 * is drawn so the visual feedback is functional without an image-loading
 * dependency.
 *
 * Graceful fallback: if no _NET_SYSTEM_TRAY selection owner is found,
 * tray_create() returns NULL with an stderr warning and the caller
 * carries on without a tray.
 */

#include <X11/Xlib.h>
#include <Xm/Xm.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TRAY_STATE_IDLE = 0,
    TRAY_STATE_RECORDING,
    TRAY_STATE_PAUSED,
    TRAY_STATE_STREAMING,
} TrayState;

typedef enum {
    TRAY_CLICK_LEFT = 0,
    TRAY_CLICK_RIGHT,
} TrayClick;

typedef void (*tray_click_cb)(TrayClick click, int x_root, int y_root, void *user_data);

typedef struct Tray Tray;

/* Returns NULL if no tray is available (logs to stderr). */
Tray *tray_create(XtAppContext app, Display *display, int screen,
                  tray_click_cb cb, void *user_data);

void  tray_destroy(Tray *tray);
void  tray_set_state(Tray *tray, TrayState state);

#ifdef __cplusplus
}
#endif

#endif /* GSR_TRAY_H */
