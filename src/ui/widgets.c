#include "widgets.h"

#include <Xm/CascadeB.h>
#include <Xm/ComboBox.h>   /* not used anymore but retained in case re-enabled */
#include <Xm/Frame.h>
#include <Xm/Label.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>  /* XmCreatePulldownMenu, XmCreateOptionMenu */
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

/* --- XmOptionMenu-backed combo ----------------------------------------
 *
 * Visually identical to dtcm / dtterm / dtfile dropdowns (a label and a
 * "selection display" button that pops up a menu on click). The previous
 * XmComboBox path was a Motif 2.1+ widget with its own text-entry child
 * — a different look that stood out from native CDE apps.
 *
 * Layout:
 *   parent
 *     └── XmOptionMenu (returned)
 *           ├── (internal) XmLabel + XmCascadeButton
 *           └── XmNsubMenuId -> XmPulldownMenu
 *                                 ├── XmPushButton (items[0])
 *                                 ├── XmPushButton (items[1])
 *                                 └── ...
 *
 * Current selection is the option menu's XmNmenuHistory resource — a
 * pointer to one of the pulldown's PushButton children. */

Widget gsr_w_combo(Widget parent, const char *const *items, const char *initial_value)
{
    /* The pulldown menu must be parented under the same widget as the
     * option menu (Motif requirement). */
    Widget pulldown = XmCreatePulldownMenu(parent, (char *)"combo_pulldown", NULL, 0);

    Widget initial_widget = NULL;
    for(int i = 0; items && items[i]; ++i) {
        XmString xms = XmStringCreateLocalized((char *)items[i]);
        Widget btn = XtVaCreateManagedWidget("combo_item",
            xmPushButtonWidgetClass, pulldown,
            XmNlabelString, xms,
            NULL);
        XmStringFree(xms);
        if(initial_value && strcmp(items[i], initial_value) == 0)
            initial_widget = btn;
        else if(!initial_widget && i == 0)
            initial_widget = btn;
    }

    Arg args[4];
    int n = 0;
    XtSetArg(args[n], XmNsubMenuId, pulldown); ++n;
    Widget option = XmCreateOptionMenu(parent, (char *)"combo", args, n);
    XtManageChild(option);

    if(initial_widget)
        XtVaSetValues(option, XmNmenuHistory, initial_widget, NULL);

    return option;
}

int gsr_w_combo_get_index(Widget combo)
{
    Widget hist = NULL;
    Widget pulldown = NULL;
    XtVaGetValues(combo, XmNmenuHistory, &hist, XmNsubMenuId, &pulldown, NULL);
    if(!hist || !pulldown) return -1;

    WidgetList children = NULL;
    Cardinal   nch = 0;
    XtVaGetValues(pulldown, XmNchildren, &children, XmNnumChildren, &nch, NULL);
    for(Cardinal i = 0; i < nch; ++i)
        if(children[i] == hist) return (int)i;
    return -1;
}

void gsr_w_combo_set_index(Widget combo, int index)
{
    Widget pulldown = NULL;
    XtVaGetValues(combo, XmNsubMenuId, &pulldown, NULL);
    if(!pulldown) return;
    WidgetList children = NULL;
    Cardinal   nch = 0;
    XtVaGetValues(pulldown, XmNchildren, &children, XmNnumChildren, &nch, NULL);
    if(index >= 0 && (Cardinal)index < nch)
        XtVaSetValues(combo, XmNmenuHistory, children[index], NULL);
}

char *gsr_w_combo_get_text(Widget combo)
{
    Widget hist = NULL;
    XtVaGetValues(combo, XmNmenuHistory, &hist, NULL);
    if(!hist) return NULL;
    XmString xms = NULL;
    XtVaGetValues(hist, XmNlabelString, &xms, NULL);
    if(!xms) return NULL;
    char *raw = NULL;
    if(!XmStringGetLtoR(xms, XmFONTLIST_DEFAULT_TAG, &raw))
        return NULL;
    return raw;
}

void gsr_w_combo_select_text(Widget combo, const char *value)
{
    if(!value) return;
    Widget pulldown = NULL;
    XtVaGetValues(combo, XmNsubMenuId, &pulldown, NULL);
    if(!pulldown) return;
    WidgetList children = NULL;
    Cardinal   nch = 0;
    XtVaGetValues(pulldown, XmNchildren, &children, XmNnumChildren, &nch, NULL);
    for(Cardinal i = 0; i < nch; ++i) {
        XmString xms = NULL;
        XtVaGetValues(children[i], XmNlabelString, &xms, NULL);
        if(!xms) continue;
        char *raw = NULL;
        if(XmStringGetLtoR(xms, XmFONTLIST_DEFAULT_TAG, &raw)) {
            int eq = strcmp(raw, value);
            XtFree(raw);
            if(eq == 0) {
                XtVaSetValues(combo, XmNmenuHistory, children[i], NULL);
                return;
            }
        }
    }
}

/* Closure tying a pulldown item to the user's change callback. */
typedef struct {
    gsr_combo_change_cb cb;
    void               *user;
    Widget              combo;
    int                 index;
} ComboItemCtx;

static void combo_item_activate(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)call;
    ComboItemCtx *c = (ComboItemCtx *)client;
    if(c->cb) c->cb(c->combo, c->index, c->user);
}

void gsr_w_combo_on_change(Widget combo, gsr_combo_change_cb cb, void *user_data)
{
    Widget pulldown = NULL;
    XtVaGetValues(combo, XmNsubMenuId, &pulldown, NULL);
    if(!pulldown) return;
    WidgetList children = NULL;
    Cardinal   nch = 0;
    XtVaGetValues(pulldown, XmNchildren, &children, XmNnumChildren, &nch, NULL);
    for(Cardinal i = 0; i < nch; ++i) {
        ComboItemCtx *c = (ComboItemCtx *)malloc(sizeof(*c));
        c->cb    = cb;
        c->user  = user_data;
        c->combo = combo;
        c->index = (int)i;
        XtAddCallback(children[i], XmNactivateCallback, combo_item_activate, c);
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

Widget gsr_w_titled_frame(Widget parent, const char *title, Widget *out_frame)
{
    Widget frame = XtVaCreateManagedWidget("frame",
        xmFrameWidgetClass, parent,
        XmNshadowType, XmSHADOW_ETCHED_IN,
        NULL);
    if(out_frame) *out_frame = frame;

    XmString xms = XmStringCreateLocalized((char *)title);
    XtVaCreateManagedWidget("frame_title",
        xmLabelWidgetClass, frame,
        XmNlabelString,              xms,
        XmNchildType,                XmFRAME_TITLE_CHILD,
        XmNchildHorizontalAlignment, XmALIGNMENT_BEGINNING,
        NULL);
    XmStringFree(xms);

    return XtVaCreateManagedWidget("frame_rc",
        xmRowColumnWidgetClass, frame,
        XmNorientation, XmVERTICAL,
        XmNpacking,     XmPACK_TIGHT,
        XmNspacing,     4,
        XmNchildType,   XmFRAME_WORKAREA_CHILD,
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
