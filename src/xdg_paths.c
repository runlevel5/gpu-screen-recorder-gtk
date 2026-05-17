#include "xdg_paths.h"
#include "str_util.h"

#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

char *xdg_paths_home_dir(void)
{
    const char *home = getenv("HOME");
    if(!home) {
        struct passwd *pw = getpwuid(getuid());
        if(pw)
            home = pw->pw_dir;
    }
    if(!home) {
        fprintf(stderr, "Error: failed to resolve home directory, falling back to /tmp\n");
        home = "/tmp";
    }
    return xstrdup(home);
}

char *xdg_paths_config_dir(void)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if(xdg && *xdg)
        return xasprintf("%s/gpu-screen-recorder", xdg);

    char *home = xdg_paths_home_dir();
    char *r = xasprintf("%s/.config/gpu-screen-recorder", home);
    free(home);
    return r;
}

/* Reads ~/.config/user-dirs.dirs and returns the value of `key` (e.g.
 * "XDG_VIDEOS_DIR"). Returns malloc'd string or NULL if absent. */
static char *read_user_dirs_value(const char *key)
{
    char *config_root = NULL;
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if(xdg && *xdg)
        config_root = xstrdup(xdg);
    else {
        char *home = xdg_paths_home_dir();
        config_root = xasprintf("%s/.config", home);
        free(home);
    }

    char *path = xasprintf("%s/user-dirs.dirs", config_root);
    free(config_root);

    FILE *f = fopen(path, "rb");
    free(path);
    if(!f)
        return NULL;

    char *home = NULL;
    char *result = NULL;
    char line[PATH_MAX];
    size_t key_len = strlen(key);
    while(fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if(len < 2)
            continue;
        if(line[0] == '#')
            continue;
        if(line[len - 1] == '\n') {
            line[len - 1] = '\0';
            --len;
        }
        if(len == 0 || line[len - 1] != '"')
            continue;
        line[len - 1] = '\0';
        --len;

        const char *sep = strchr(line, '=');
        if(!sep || sep[1] != '"')
            continue;

        size_t this_key_len = (size_t)(sep - line);
        if(this_key_len != key_len || memcmp(line, key, key_len) != 0)
            continue;

        const char *val = sep + 2;
        if(strncmp(val, "$HOME/", 6) == 0) {
            if(!home)
                home = xdg_paths_home_dir();
            result = xasprintf("%s/%s", home, val + 6);
        } else {
            result = xstrdup(val);
        }
        break;
    }

    free(home);
    fclose(f);
    return result;
}

char *xdg_paths_videos_dir(void)
{
    char *v = read_user_dirs_value("XDG_VIDEOS_DIR");
    if(v)
        return v;
    char *home = xdg_paths_home_dir();
    char *r = xasprintf("%s/Videos", home);
    free(home);
    return r;
}

bool xdg_paths_file_get_content(const char *filepath, char **out, size_t *out_size)
{
    *out = NULL;
    *out_size = 0;

    FILE *f = fopen(filepath, "rb");
    if(!f)
        return false;

    bool ok = false;
    if(fseek(f, 0, SEEK_END) == 0) {
        long size = ftell(f);
        if(size >= 0 && fseek(f, 0, SEEK_SET) == 0) {
            char *buf = (char *)malloc((size_t)size + 1);
            if(buf) {
                size_t r = fread(buf, 1, (size_t)size, f);
                if(r == (size_t)size) {
                    buf[size] = '\0';
                    *out = buf;
                    *out_size = (size_t)size;
                    ok = true;
                } else {
                    free(buf);
                }
            }
        }
    }

    fclose(f);
    return ok;
}

int xdg_paths_create_directory_recursive(const char *path)
{
    size_t len = strlen(path);
    if(len == 0)
        return 0;

    char *tmp = xstrdup(path);
    char *p = tmp;
    char *end = tmp + len;
    int ret = 0;

    for(;;) {
        char *slash = strchr(p, '/');

        /* Skip the leading slash so we never try to mkdir("/"). */
        if(slash == tmp) {
            ++p;
            continue;
        }

        if(!slash)
            slash = end;

        char prev = *slash;
        *slash = '\0';
        if(mkdir(tmp, S_IRWXU) == -1 && errno != EEXIST) {
            ret = -1;
            *slash = prev;
            break;
        }
        *slash = prev;

        if(slash == end)
            break;
        p = slash + 1;
    }

    free(tmp);
    return ret;
}
