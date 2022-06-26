#pragma once
#include "attributes.h"

ERROR_EMITTER("attempted an out of bounds read (invalid src size)")
void emit_out_of_bounds_read_compile_error(void);

ERROR_EMITTER("attempted an out of bounds write (invalid dest size)")
void emit_out_of_bounds_write_compile_error(void);

NORETURN
void die_on_runtime_oob(const char *func, const char *file, size_t line,
                        size_t size, size_t dst_size, size_t src_size);

#define HARDENED_CHECK_OOB_DST_OR_SRC()                         \
    do {                                                        \
        size_t dest_size = __builtin_object_size(dest, 1);      \
        size_t src_size = __builtin_object_size(src, 1);        \
                                                                \
        if (__builtin_constant_p(count)) {                      \
            if (dest_size < count)                              \
                emit_out_of_bounds_write_compile_error();       \
            if (src_size < count)                               \
                emit_out_of_bounds_read_compile_error();        \
        }                                                       \
                                                                \
        if (dest_size < count || src_size < count)              \
            die_on_runtime_oob(__FUNCTION__, file, line, count, \
                               dest_size, src_size);            \
    } while (0)


static ALWAYS_INLINE void *hardened_memcpy(void *dest, const void *src, size_t count,
                                           const char *file, size_t line)
{
    HARDENED_CHECK_OOB_DST_OR_SRC();
    return __builtin_memcpy(dest, src, count);
}
#define memcpy(dest, src, count) hardened_memcpy(dest, src, count, __FILE__, __LINE__)

static ALWAYS_INLINE void *hardened_memmove(void *dest, const void *src, size_t count,
                                            const char *file, size_t line)
{
    HARDENED_CHECK_OOB_DST_OR_SRC();
    return __builtin_memmove(dest, src, count);
}
#define memmove(dest, src, count) hardened_memmove(dest, src, count, __FILE__, __LINE__)

static ALWAYS_INLINE void *hardened_memset(void *dest, int val, size_t count,
                                           const char *file, size_t line)
{
    size_t dest_size = __builtin_object_size(dest, 1);

    if (__builtin_constant_p(count) && dest_size < count)
        emit_out_of_bounds_write_compile_error();

    if (dest_size < count)
        die_on_runtime_oob(__FUNCTION__, file, line, count, dest_size, 0);

    return __builtin_memset(dest, val, count);
}
#define memset(dest, val, count) hardened_memset(dest, val, count, __FILE__, __LINE__)

static ALWAYS_INLINE int hardened_memcmp(const void *dest, const void *src, size_t count,
                                         const char *file, size_t line)
{
    HARDENED_CHECK_OOB_DST_OR_SRC();
    return __builtin_memcmp(dest, src, count);
}
#define memcmp(lhs, rhs, count) hardened_memcmp(lhs, rhs, count, __FILE__, __LINE__)

static ALWAYS_INLINE size_t hardened_strlen(const char *str,
                                            const char *file, size_t line)
{
    size_t str_size, ret;

    str_size = __builtin_object_size(str, 1);
    ret = __builtin_strlen(str);

    // FIXME: use strnlen so that we guarantee no OOB access for known lengths
    if (str_size <= ret)
        die_on_runtime_oob(__FUNCTION__, file, line, ret, str_size, 0);

    return ret;
}
#define strlen(str) hardened_strlen(str, __FILE__, __LINE__)

static ALWAYS_INLINE void *hardened_memzero(void *dest, size_t count,
                                            const char *file, size_t line)
{
    return hardened_memset(dest, 0, count, file, line);
}
#define memzero(dest, count) hardened_memzero(dest, count, __FILE__, __LINE__)
