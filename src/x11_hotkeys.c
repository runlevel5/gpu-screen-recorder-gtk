#include "x11_hotkeys.h"

#include <X11/XKBlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

bool gsr_key_is_modifier(KeySym key_sym)
{
    return key_sym >= XK_Shift_L && key_sym <= XK_Super_R
        && key_sym != XK_Caps_Lock && key_sym != XK_Shift_Lock;
}

uint32_t gsr_modkey_to_mask(KeySym key_sym)
{
    assert(gsr_key_is_modifier(key_sym));
    return (uint32_t)1u << (uint32_t)(key_sym - XK_Shift_L);
}

uint32_t gsr_key_mod_mask_to_x11(uint32_t mask)
{
    uint32_t x = 0;
    if(mask & (gsr_modkey_to_mask(XK_Control_L) | gsr_modkey_to_mask(XK_Control_R)))
        x |= ControlMask;
    if(mask & (gsr_modkey_to_mask(XK_Alt_L)     | gsr_modkey_to_mask(XK_Alt_R)))
        x |= Mod1Mask;
    if(mask & (gsr_modkey_to_mask(XK_Shift_L)   | gsr_modkey_to_mask(XK_Shift_R)))
        x |= ShiftMask;
    if(mask & (gsr_modkey_to_mask(XK_Super_L)   | gsr_modkey_to_mask(XK_Super_R) |
               gsr_modkey_to_mask(XK_Meta_L)    | gsr_modkey_to_mask(XK_Meta_R)))
        x |= Mod4Mask;
    return x;
}

unsigned int gsr_key_state_without_locks(unsigned int key_state)
{
    return key_state & ~(Mod2Mask | LockMask);
}

uint32_t gsr_x11_mask_to_gsr_mod(unsigned int x11_mask)
{
    uint32_t m = 0;
    if(x11_mask & ControlMask) m |= gsr_modkey_to_mask(XK_Control_L);
    if(x11_mask & Mod1Mask)    m |= gsr_modkey_to_mask(XK_Alt_L);
    if(x11_mask & ShiftMask)   m |= gsr_modkey_to_mask(XK_Shift_L);
    if(x11_mask & Mod4Mask)    m |= gsr_modkey_to_mask(XK_Super_L);
    return m;
}

typedef struct {
    KeySym      key_sym;
    const char *name;
} CustomKeyName;

static const CustomKeyName k_custom_key_names[] = {
    { XK_Caps_Lock,      "Caps Lock"     },
    { XK_Shift_Lock,     "Caps Lock"     },
    { XK_Return,         "Return"        },
    { XK_BackSpace,      "BackSpace"     },
    { XK_Tab,            "Tab"           },
    { XK_Delete,         "Delete"        },
    { XK_dead_acute,     "`"             },
    { XK_dead_diaeresis, "^"             },
    { XK_Prior,          "PageUp"        },
    { XK_Next,           "PageDown"      },
    { ' ',               "Space"         },
    { XK_KP_Insert,      "KeyPad 0"      },
    { XK_KP_End,         "KeyPad 1"      },
    { XK_KP_Down,        "KeyPad 2"      },
    { XK_KP_Next,        "KeyPad 3"      },
    { XK_KP_Left,        "KeyPad 4"      },
    { XK_KP_Begin,       "KeyPad 5"      },
    { XK_KP_Right,       "KeyPad 6"      },
    { XK_KP_Home,        "KeyPad 7"      },
    { XK_KP_Up,          "KeyPad 8"      },
    { XK_KP_Prior,       "KeyPad 9"      },
    { XK_KP_Enter,       "KeyPad Return" },
    { XK_KP_Delete,      "KeyPad Delete" },
};

int gsr_key_get_name(Display *display, XIC xic, KeySym key_sym, char *buffer, int buffer_size)
{
    if(buffer_size == 0 || key_sym == NoSymbol)
        return 0;

    for(size_t i = 0; i < sizeof(k_custom_key_names)/sizeof(k_custom_key_names[0]); ++i) {
        if(key_sym == k_custom_key_names[i].key_sym) {
            int len = (int)strlen(k_custom_key_names[i].name);
            if(buffer_size < len)
                return 0;
            memcpy(buffer, k_custom_key_names[i].name, (size_t)len);
            return len;
        }
    }

    if(xic && display) {
        XKeyPressedEvent event;
        memset(&event, 0, sizeof(event));
        event.type    = KeyPress;
        event.display = display;
        event.state   = 0;
        event.keycode = XKeysymToKeycode(display, key_sym);

        KeySym ignore;
        Status status = 0;
        int    buflen = Xutf8LookupString(xic, &event, buffer, buffer_size, &ignore, &status);
        if(status != XBufferOverflow && buflen > 0)
            return buflen;
    }

    const char *str = XKeysymToString(key_sym);
    if(str) {
        int len = (int)strlen(str);
        if(buffer_size >= len) {
            memcpy(buffer, str, (size_t)len);
            return len;
        }
    }
    return 0;
}

typedef struct { KeySym key_sym; const char *name; } ModkeyName;

static const ModkeyName k_modkey_names[] = {
    { XK_Control_L, "Ctrl"  },
    { XK_Control_R, "Ctrl"  },
    { XK_Super_L,   "Super" },
    { XK_Super_R,   "Super" },
    { XK_Meta_L,    "Super" },
    { XK_Meta_R,    "Super" },
    { XK_Shift_L,   "Shift" },
    { XK_Shift_R,   "Shift" },
    { XK_Alt_L,     "Alt"   },
    { XK_Alt_R,     "Alt"   },
};

size_t gsr_hotkey_format(Display *display, XIC xic, ConfigHotkey hotkey, char *buffer, size_t buffer_size)
{
    if(buffer_size == 0)
        return 0;
    buffer[0] = '\0';

    size_t written = 0;
    bool   first = true;

    for(size_t i = 0; i < sizeof(k_modkey_names)/sizeof(k_modkey_names[0]); ++i) {
        if((uint32_t)hotkey.modifiers & gsr_modkey_to_mask(k_modkey_names[i].key_sym)) {
            int n = snprintf(buffer + written, buffer_size - written,
                             "%s%s", first ? "" : " + ", k_modkey_names[i].name);
            if(n < 0 || (size_t)n >= buffer_size - written)
                return written;
            written += (size_t)n;
            first = false;
        }
    }

    if(hotkey.keysym != 0 && written + 3 < buffer_size) {
        if(!first) {
            int n = snprintf(buffer + written, buffer_size - written, " + ");
            if(n < 0 || (size_t)n >= buffer_size - written)
                return written;
            written += (size_t)n;
        }
        char name[128];
        int  name_len = gsr_key_get_name(display, xic, (KeySym)hotkey.keysym, name, sizeof(name));
        if(name_len > 0 && written + (size_t)name_len < buffer_size) {
            memcpy(buffer + written, name, (size_t)name_len);
            written += (size_t)name_len;
            buffer[written] = '\0';
        }
    }

    return written;
}

/* --- Grab implementation ------------------------------------------------- */

static int   s_xerror_dummy(Display *d, XErrorEvent *e) { (void)d; (void)e; return 0; }
static int   s_x_failed = 0;
static int   s_xerror_grab(Display *d, XErrorEvent *e)  { (void)d; (void)e; s_x_failed = 1; return 0; }

/* Find which X11 modifier index NumLock occupies (varies per server). */
static unsigned int detect_numlock_mask(Display *display)
{
    unsigned int mask = 0;
    KeyCode      nl_kc = XKeysymToKeycode(display, XK_Num_Lock);
    XModifierKeymap *modmap = XGetModifierMapping(display);
    if(modmap) {
        for(int i = 0; i < 8; ++i) {
            for(int j = 0; j < modmap->max_keypermod; ++j) {
                if(modmap->modifiermap[i * modmap->max_keypermod + j] == nl_kc)
                    mask = (unsigned int)(1 << i);
            }
        }
        XFreeModifiermap(modmap);
    }
    return mask;
}

bool gsr_hotkey_grab(Display *display, ConfigHotkey hotkey, bool grab)
{
    if(hotkey.keysym == 0 && hotkey.modifiers == 0)
        return true;

    unsigned int numlock_mask = detect_numlock_mask(display);
    uint32_t     key_mod_masks = 0;
    KeySym       key_sym = (KeySym)hotkey.keysym;

    if(key_sym != 0)
        key_mod_masks = gsr_key_mod_mask_to_x11((uint32_t)hotkey.modifiers);

    XSync(display, False);
    s_x_failed = 0;
    XErrorHandler prev = XSetErrorHandler(s_xerror_grab);

    Window       root = DefaultRootWindow(display);
    unsigned int variants[] = { 0, LockMask, numlock_mask, numlock_mask | LockMask };

    if(key_sym != 0) {
        for(int i = 0; i < 4; ++i) {
            if(grab)
                XGrabKey(display, XKeysymToKeycode(display, key_sym),
                         (unsigned int)key_mod_masks | variants[i],
                         root, False, GrabModeAsync, GrabModeAsync);
            else
                XUngrabKey(display, XKeysymToKeycode(display, key_sym),
                           (unsigned int)key_mod_masks | variants[i], root);
        }
    }
    XSync(display, False);

    bool ok = (s_x_failed == 0);

    /* If a grab failed partway, roll back the partial grab to leave a clean state. */
    if(!ok && key_sym != 0) {
        for(int i = 0; i < 4; ++i) {
            XUngrabKey(display, XKeysymToKeycode(display, key_sym),
                       (unsigned int)key_mod_masks | variants[i], root);
        }
    }
    XSync(display, False);

    XSetErrorHandler(prev);
    return ok;
}

/* --- Root-window event drain -------------------------------------------- */

typedef struct {
    Display *display;
    Window   root;
} DrainCtx;

static Bool s_match_root_key(Display *d, XEvent *ev, XPointer arg)
{
    DrainCtx *ctx = (DrainCtx *)arg;
    if(d != ctx->display)
        return False;
    if(ev->type != KeyPress && ev->type != KeyRelease)
        return False;
    /* xkey.window is the event window (root, for root-grabbed keys); xkey.root
     * is always the root window. Match either to be tolerant of WM behavior. */
    return ev->xkey.window == ctx->root || ev->xkey.root == ctx->root;
}

int gsr_hotkey_drain_root_events(Display *display, gsr_hotkey_callback callback, void *user_data)
{
    DrainCtx ctx = { display, DefaultRootWindow(display) };
    XEvent ev;
    int    dispatched = 0;
    while(XCheckIfEvent(display, &ev, s_match_root_key, (XPointer)&ctx)) {
        if(ev.type == KeyPress && callback) {
            KeySym ks = XkbKeycodeToKeysym(display, ev.xkey.keycode, 0, 0);
            unsigned int mods = gsr_key_state_without_locks(ev.xkey.state);
            callback(ks, mods, user_data);
        }
        ++dispatched;
    }
    return dispatched;
}
