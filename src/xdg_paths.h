#ifndef GSR_XDG_PATHS_H
#define GSR_XDG_PATHS_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* All returned strings are malloc'd. Caller frees. */
char *xdg_paths_home_dir(void);
char *xdg_paths_config_dir(void);   /* ${XDG_CONFIG_HOME:-~/.config}/gpu-screen-recorder */
char *xdg_paths_videos_dir(void);   /* XDG_VIDEOS_DIR (from user-dirs.dirs) or ~/Videos */

/* Reads `filepath` into a freshly malloc'd, NUL-terminated buffer.
 * On success returns true and sets *out / *out_size (size excludes the trailing NUL).
 * On failure returns false; *out=NULL, *out_size=0. */
bool xdg_paths_file_get_content(const char *filepath, char **out, size_t *out_size);

/* mkdir -p with mode 0700. Returns 0 on success, -1 on error (errno set). */
int xdg_paths_create_directory_recursive(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* GSR_XDG_PATHS_H */
