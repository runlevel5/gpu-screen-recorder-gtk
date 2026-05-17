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

/* Drop-down combo populated with `items` (NULL-terminated). Selected index
 * defaults to 0 unless `initial_value` matches an item. */
Widget gsr_w_combo   (Widget parent, const char *const *items, const char *initial_value);
int    gsr_w_combo_get_index(Widget combo);
void   gsr_w_combo_set_index(Widget combo, int index);

/* Returns the text of the currently selected item, or NULL if none.
 * Caller frees with XtFree. */
char  *gsr_w_combo_get_text(Widget combo);
void   gsr_w_combo_select_text(Widget combo, const char *value);

Widget gsr_w_button  (Widget parent, const char *label);

/* Tight horizontal XmRowColumn — useful for "label + control" pairs. */
Widget gsr_w_hrow(Widget parent);

#ifdef __cplusplus
}
#endif

#endif /* GSR_UI_WIDGETS_H */
