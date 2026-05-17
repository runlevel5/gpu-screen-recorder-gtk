#include "str_util.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *xstrdup(const char *s)
{
    if(!s)
        return NULL;
    size_t n = strlen(s);
    char *r = (char *)malloc(n + 1);
    assert(r && "OOM in xstrdup");
    memcpy(r, s, n + 1);
    return r;
}

char *xstrndup(const char *s, size_t n)
{
    char *r = (char *)malloc(n + 1);
    assert(r && "OOM in xstrndup");
    if(n)
        memcpy(r, s, n);
    r[n] = '\0';
    return r;
}

char *xasprintf(const char *fmt, ...)
{
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    assert(n >= 0 && "vsnprintf format error");
    char *r = (char *)malloc((size_t)n + 1);
    assert(r && "OOM in xasprintf");
    vsnprintf(r, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return r;
}

void xfree_set(char **slot, char *new_value)
{
    free(*slot);
    *slot = new_value;
}

void string_array_init(StringArray *a)
{
    a->items = NULL;
    a->len = 0;
    a->cap = 0;
}

void string_array_push(StringArray *a, char *owned)
{
    if(a->len == a->cap) {
        size_t cap = a->cap ? a->cap * 2 : 4;
        char **n = (char **)realloc(a->items, cap * sizeof(*n));
        assert(n && "OOM in string_array_push");
        a->items = n;
        a->cap = cap;
    }
    a->items[a->len++] = owned;
}

void string_array_clear(StringArray *a)
{
    for(size_t i = 0; i < a->len; ++i)
        free(a->items[i]);
    a->len = 0;
}

void string_array_free(StringArray *a)
{
    string_array_clear(a);
    free(a->items);
    a->items = NULL;
    a->cap = 0;
}

bool string_view_eq_cstr(StringView v, const char *s)
{
    size_t n = strlen(s);
    return v.size == n && memcmp(v.str, s, n) == 0;
}
