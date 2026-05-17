#include "notifications.h"

#include <stdio.h>

/* Phase 2 placeholder implementations. Real Motif dialogs land in Phase 6. */

void notifications_show_error(NotificationParent parent, const char *title, const char *message)
{
    (void)parent;
    fprintf(stderr, "ERROR: %s — %s\n", title ? title : "(no title)", message ? message : "(no message)");
}

void notifications_show_warning(NotificationParent parent, const char *title, const char *message)
{
    (void)parent;
    fprintf(stderr, "WARNING: %s — %s\n", title ? title : "(no title)", message ? message : "(no message)");
}

void notifications_show_info(NotificationParent parent, const char *title, const char *message)
{
    (void)parent;
    fprintf(stderr, "INFO: %s — %s\n", title ? title : "(no title)", message ? message : "(no message)");
}
