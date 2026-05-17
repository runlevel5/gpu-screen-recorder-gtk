#include "widgets.h"

#include <Xm/ComboBox.h>
#include <Xm/Label.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/SSpinB.h>     /* XmSimpleSpinBox — has its own header in OpenMotif */
#include <Xm/SpinB.h>
#include <Xm/TextF.h>
#include <Xm/ToggleB.h>

#include <stdlib.h>
#include <string.h>

Widget gsr_w_label(Widget parent, const char *text)
{
    XmString xms = XmStringCreateLocalized((char *)text);
    Widget   w = XtVaCreateManagedWidget("label",
        xmLabelWidgetClass, parent,
        XmNlabelString, xms,
        XmNalignment,   XmALIGNMENT_BEGINNING,
        NULL);
    XmStringFree(xms);
    return w;
}

Widget gsr_w_label_at(Widget parent, const char *text, Widget above)
{
    XmString xms = XmStringCreateLocalized((char *)text);
    Widget   w = XtVaCreateManagedWidget("label",
        xmLabelWidgetClass, parent,
        XmNlabelString,   xms,
        XmNalignment,     XmALIGNMENT_BEGINNING,
        XmNtopAttachment, above ? XmATTACH_WIDGET : XmATTACH_FORM,
        XmNtopWidget,     above,
        XmNtopOffset,     above ? 8 : 12,
        XmNleftAttachment, XmATTACH_FORM,
        XmNleftOffset,     12,
        NULL);
    XmStringFree(xms);
    return w;
}

Widget gsr_w_toggle(Widget parent, const char *text, bool initial)
{
    XmString xms = XmStringCreateLocalized((char *)text);
    Widget   w = XtVaCreateManagedWidget("toggle",
        xmToggleButtonWidgetClass, parent,
        XmNlabelString, xms,
        XmNset,         initial ? XmSET : XmUNSET,
        NULL);
    XmStringFree(xms);
    return w;
}

bool gsr_w_toggle_get(Widget toggle)
{
    return XmToggleButtonGetState(toggle) == True;
}

void gsr_w_toggle_set(Widget toggle, bool active)
{
    XmToggleButtonSetState(toggle, active ? True : False, False);
}

Widget gsr_w_text(Widget parent, const char *initial)
{
    Widget w = XtVaCreateManagedWidget("text",
        xmTextFieldWidgetClass, parent,
        XmNcolumns, 24,
        NULL);
    if(initial && *initial)
        XmTextFieldSetString(w, (char *)initial);
    return w;
}

char *gsr_w_text_get(Widget text)
{
    return XmTextFieldGetString(text);
}

void gsr_w_text_set(Widget text, const char *value)
{
    XmTextFieldSetString(text, value ? (char *)value : (char *)"");
}

Widget gsr_w_spin_int(Widget parent, int min, int max, int initial)
{
    if(initial < min) initial = min;
    if(initial > max) initial = max;
    return XtVaCreateManagedWidget("spin",
        xmSimpleSpinBoxWidgetClass, parent,
        XmNspinBoxChildType, XmNUMERIC,
        XmNminimumValue,     min,
        XmNmaximumValue,     max,
        XmNincrementValue,   1,
        XmNposition,         initial,
        XmNcolumns,          8,
        NULL);
}

int gsr_w_spin_get(Widget spin)
{
    int v = 0;
    XtVaGetValues(spin, XmNposition, &v, NULL);
    return v;
}

void gsr_w_spin_set(Widget spin, int value)
{
    XtVaSetValues(spin, XmNposition, value, NULL);
}

Widget gsr_w_combo(Widget parent, const char *const *items, const char *initial_value)
{
    Widget w = XtVaCreateManagedWidget("combo",
        xmComboBoxWidgetClass, parent,
        XmNcomboBoxType, XmDROP_DOWN_LIST,
        NULL);
    int initial_index = 0;
    for(int i = 0; items && items[i]; ++i) {
        XmString xms = XmStringCreateLocalized((char *)items[i]);
        XmComboBoxAddItem(w, xms, 0 /* append */, False);
        XmStringFree(xms);
        if(initial_value && strcmp(items[i], initial_value) == 0)
            initial_index = i;
    }
    XtVaSetValues(w, XmNselectedPosition, initial_index, NULL);
    return w;
}

int gsr_w_combo_get_index(Widget combo)
{
    int p = 0;
    XtVaGetValues(combo, XmNselectedPosition, &p, NULL);
    return p;
}

void gsr_w_combo_set_index(Widget combo, int index)
{
    XtVaSetValues(combo, XmNselectedPosition, index, NULL);
}

/* Reads the selected combo entry by pulling it out of the items list. */
char *gsr_w_combo_get_text(Widget combo)
{
    int          item_count = 0;
    XmStringTable items = NULL;
    int          selected = 0;
    XtVaGetValues(combo,
        XmNitemCount,        &item_count,
        XmNitems,            &items,
        XmNselectedPosition, &selected,
        NULL);
    if(selected < 0 || selected >= item_count || !items)
        return NULL;
    char *raw = NULL;
    if(!XmStringGetLtoR(items[selected], XmFONTLIST_DEFAULT_TAG, &raw))
        return NULL;
    return raw;  /* XtFree */
}

void gsr_w_combo_select_text(Widget combo, const char *value)
{
    if(!value)
        return;
    int          item_count = 0;
    XmStringTable items = NULL;
    XtVaGetValues(combo,
        XmNitemCount, &item_count,
        XmNitems,     &items,
        NULL);
    for(int i = 0; i < item_count; ++i) {
        char *raw = NULL;
        if(XmStringGetLtoR(items[i], XmFONTLIST_DEFAULT_TAG, &raw)) {
            int eq = strcmp(raw, value);
            XtFree(raw);
            if(eq == 0) {
                XtVaSetValues(combo, XmNselectedPosition, i, NULL);
                return;
            }
        }
    }
}

Widget gsr_w_button(Widget parent, const char *label)
{
    XmString xms = XmStringCreateLocalized((char *)label);
    Widget   w = XtVaCreateManagedWidget("btn",
        xmPushButtonWidgetClass, parent,
        XmNlabelString, xms,
        NULL);
    XmStringFree(xms);
    return w;
}

Widget gsr_w_hrow(Widget parent)
{
    return XtVaCreateManagedWidget("hrow",
        xmRowColumnWidgetClass, parent,
        XmNorientation, XmHORIZONTAL,
        XmNpacking,     XmPACK_TIGHT,
        XmNspacing,     6,
        NULL);
}

Widget gsr_w_button_m(Widget parent, const char *label, char mnemonic)
{
    XmString xms = XmStringCreateLocalized((char *)label);
    Widget w = XtVaCreateManagedWidget("btn",
        xmPushButtonWidgetClass, parent,
        XmNlabelString, xms,
        XmNmnemonic,    (KeySym)mnemonic,
        NULL);
    XmStringFree(xms);
    return w;
}
