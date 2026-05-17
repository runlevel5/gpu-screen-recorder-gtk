#ifndef GSR_UI_DIALOGS_H
#define GSR_UI_DIALOGS_H

/*
 * Reusable modal dialogs built on Motif.
 *
 * - `dialogs_pick_directory` — XmFileSelectionDialog restricted to
 *   directories. On OK, the user callback is invoked with a malloc'd
 *   directory path; on Cancel, the dialog is dismissed silently.
 */

#include <Xm/Xm.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Called when the user accepts a chosen directory. `path` is freed by the
 * caller after the callback returns. */
typedef void (*directory_chosen_cb)(const char *path, void *user_data);

void dialogs_pick_directory(Widget parent, const char *initial_dir,
                            directory_chosen_cb cb, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* GSR_UI_DIALOGS_H */
