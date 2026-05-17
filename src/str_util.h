#ifndef GSR_STR_UTIL_H
#define GSR_STR_UTIL_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* xstrdup / xstrndup: abort on OOM. xstrdup(NULL) returns NULL. */
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);

/* asprintf wrapper, aborts on OOM. */
char *xasprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* free *slot, store new_value (which may be NULL or owned). */
void  xfree_set(char **slot, char *new_value);

/* Dynamic, ownership-taking string array. items[i] is malloc'd. */
typedef struct {
    char **items;
    size_t len;
    size_t cap;
} StringArray;

void string_array_init(StringArray *a);
void string_array_push(StringArray *a, char *owned);
void string_array_clear(StringArray *a);
void string_array_free(StringArray *a);

/* Non-owning view used by config parsing. */
typedef struct {
    const char *str;
    size_t      size;
} StringView;

bool string_view_eq_cstr(StringView v, const char *s);

#ifdef __cplusplus
}
#endif

#endif /* GSR_STR_UTIL_H */
