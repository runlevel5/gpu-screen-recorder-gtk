#ifndef GSR_NOTIFICATIONS_H
#define GSR_NOTIFICATIONS_H

/*
 * Toolkit-agnostic notifications facade.
 *
 * Phase 2 ships header-only stubs that just log to stderr. Phase 6 replaces
 * the implementations with XmMessageDialog popups (error/warning/info).
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Parent widget pointer (Widget = Xt opaque). Passed as void* so callers don't
 * have to drag in Xt headers; the Motif backend casts it back. */
typedef void *NotificationParent;

void notifications_show_error  (NotificationParent parent, const char *title, const char *message);
void notifications_show_warning(NotificationParent parent, const char *title, const char *message);
void notifications_show_info   (NotificationParent parent, const char *title, const char *message);

#ifdef __cplusplus
}
#endif

#endif /* GSR_NOTIFICATIONS_H */
