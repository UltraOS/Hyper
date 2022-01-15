#include <stdarg.h>
#include "attributes.h"

int vsnprintf(char *restrict buffer, size_t capacity, const char *fmt, va_list vlist);

static inline int vscnprintf(char *restrict buffer, size_t capacity, const char *fmt, va_list vlist)
{
    int would_have_been_written = vsnprintf(buffer, capacity, fmt, vlist);

    if (would_have_been_written < 0)
        return would_have_been_written;
    if ((size_t)would_have_been_written < capacity)
        return would_have_been_written;

    return capacity ? capacity - 1 : 0;
}

PRINTF_DECL(3, 4)
static inline int snprintf(char *restrict buffer, size_t capacity, const char *fmt, ...)
{
    va_list list;
    int written;
    va_start(list, fmt);
    written = vsnprintf(buffer, capacity, fmt, list);
    va_end(list);

    return written;
}

PRINTF_DECL(3, 4)
static inline int scnprintf(char *restrict buffer, size_t capacity, const char *fmt, ...)
{
    va_list list;
    int written;
    va_start(list, fmt);
    written = vscnprintf(buffer, capacity, fmt, list);
    va_end(list);

    return written;
}
