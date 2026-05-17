#ifndef GSR_UI_WIDGETS_H
#define GSR_UI_WIDGETS_H

/*
 * Thin Motif-widget helpers. Keep page code dense and intent-revealing.
 * All returned widgets are managed unless otherwise noted.
 */

#include <Xm/Xm.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

Widget gsr_w_label   (Widget parent, const char *text);
Widget gsr_w_label_at(Widget parent, const char *text, Widget above);

Widget gsr_w_toggle  (Widget parent, const char *text, bool initial);
bool   gsr_w_toggle_get(Widget toggle);
void   gsr_w_toggle_set(Widget toggle, bool active);

Widget gsr_w_text    (Widget parent, const char *initial);
char  *gsr_w_text_get(Widget text);  /* caller frees with XtFree */
void   gsr_w_text_set(Widget text, const char *value);

/* Integer spin box (XmSimpleSpinBox, NUMERIC).
 * Read/write current value via gsr_w_spin_get/set. */
Widget gsr_w_spin_int(Widget parent, int min, int max, int initial);
int    gsr_w_spin_get(Widget spin);
void   gsr_w_spin_set(Widget spin, int value);

/* XmOptionMenu-backed dropdown (matches the classic CDE/dtterm/dtcm look:
 * label + click-to-pop-menu, no text entry). Items are NULL-terminated.
 * Initial selection defaults to index 0 unless `initial_value` matches.
 *
 * The returned widget is the XmOptionMenu itself. Get/set helpers use
 * its XmNmenuHistory resource under the hood. */
Widget gsr_w_combo   (Widget parent, const char *const *items, const char *initial_value);
int    gsr_w_combo_get_index(Widget combo);
void   gsr_w_combo_set_index(Widget combo, int index);
char  *gsr_w_combo_get_text(Widget combo);   /* XtFree to release */
void   gsr_w_combo_select_text(Widget combo, const char *value);

/* Register a change callback. Invoked when the user selects an item.
 * `index` is the 0-based item position. */
typedef void (*gsr_combo_change_cb)(Widget combo, int index, void *user_data);
void gsr_w_combo_on_change(Widget combo, gsr_combo_change_cb cb, void *user_data);

Widget gsr_w_button  (Widget parent, const char *label);

/* Same, but also installs an Alt-mnemonic. `mnemonic` should appear in
 * `label` (case-insensitive) and is set as XmNmnemonic. */
Widget gsr_w_button_m(Widget parent, const char *label, char mnemonic);

/* Tight horizontal XmRowColumn — useful for "label + control" pairs. */
Widget gsr_w_hrow(Widget parent);

/* XmFrame with an ETCHED_IN border and a left-aligned title label as the
 * frame's title child. Returns the inner XmRowColumn (vertical, packed
 * tight) that should be used as the parent for the frame's content. If
 * `out_frame` is non-NULL it receives the frame widget itself (useful
 * for manage/unmanage). */
Widget gsr_w_titled_frame(Widget parent, const char *title, Widget *out_frame);

#ifdef __cplusplus
}
#endif

#endif /* GSR_UI_WIDGETS_H */
