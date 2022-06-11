#pragma once

#include "types.h"
#include "helpers.h"
#include "log.h"
#include "bug.h"

#if __has_include("platform_config.h")
#include "platform_config.h"
#else
#define PLATFORM_WANTS_GENERIC_STRING
#endif

#ifdef PLATFORM_HAS_MEMCPY
#define MEMCPY_FUNC memcpy_generic
#else
#define MEMCPY_FUNC memcpy
#endif

#ifdef PLATFORM_HAS_MEMMOVE
#define MEMMOVE_FUNC memmove_generic
#else
#define MEMMOVE_FUNC memmove
#endif

#ifdef PLATFORM_HAS_MEMSET
#define MEMSET_FUNC memset_generic
#else
#define MEMSET_FUNC memset
#endif

#ifdef PLATFORM_HAS_MEMCMP
#define MEMCMP_FUNC memcmp_generic
#else
#define MEMCMP_FUNC memcmp
#endif

#ifdef PLATFORM_HAS_STRLEN
#define STRLEN_FUNC strlen_generic
#else
#define STRLEN_FUNC strlen
#endif

#ifdef PLATFORM_WANTS_GENERIC_STRING
void *MEMCPY_FUNC(void *dest, const void *src, size_t count);
void *MEMMOVE_FUNC(void *dest, const void *src, size_t count);
void *MEMSET_FUNC(void *dest, int ch, size_t count);
int MEMCMP_FUNC(const void *lhs, const void *rhs, size_t count);
size_t STRLEN_FUNC(const char *str);
#endif

#ifdef HARDENED_STRING
ERROR_EMITTER("attempted an out of bounds read (invalid src size)")
void emit_out_of_bounds_read_compile_error(void);

ERROR_EMITTER("attempted an out of bounds write (invalid dest size)")
void emit_out_of_bounds_write_compile_error(void);

#define OOB_ACCESS_HEADER \
    print_err("BUG: out of bounds %s() call at %s:%zu: ", __FUNCTION__, file, line);

#define HARDENED_CHECK_OOB_DST_OR_SRC()                                  \
    do {                                                                 \
        size_t dest_size = __builtin_object_size(dest, 1);               \
        size_t src_size = __builtin_object_size(src, 1);                 \
                                                                         \
        if (__builtin_constant_p(count)) {                               \
            if (dest_size < count)                                       \
                emit_out_of_bounds_write_compile_error();                \
            if (src_size < count)                                        \
                emit_out_of_bounds_read_compile_error();                 \
        }                                                                \
                                                                         \
        if (dest_size < count || src_size < count) {                     \
            OOB_ACCESS_HEADER                                            \
            print_err("with %zu bytes, dest size: %zu, src size: %zu\n", \
                      count, dest_size, src_size);                       \
            DIE();                                                       \
        }                                                                \
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

    if (dest_size < count) {
        OOB_ACCESS_HEADER
        print_err("with %zu bytes (%d filler), dest size: %zu\n",
                  count, val, dest_size);
        DIE();
    }

    return __builtin_memset(dest, val, count);
}
#define memset(dest, val, count) hardened_memset(dest, val, count, __FILE__, __LINE__)

static ALWAYS_INLINE int hardened_memcmp(const void *dest, const void* src, size_t count,
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
    if (str_size <= ret) {
        OOB_ACCESS_HEADER;
        print_err("not null-terminated string (expected max len %zu, got %zu)\n",
                  str_size, ret);
        DIE();
    }

    return ret;
}
#define strlen(str) hardened_strlen(str, __FILE__, __LINE__)

static ALWAYS_INLINE void *hardened_memzero(void *dest, size_t count, const char *file, size_t line)
{
    return hardened_memset(dest, 0, count, file, line);
}
#define memzero(dest, count) hardened_memzero(dest, count, __FILE__, __LINE__)
#else
#define memcpy __builtin_memcpy
#define memmove __builtin_memmove
#define memset __builtin_memset
#define memcmp __builtin_memcmp
#define strlen __builtin_strlen

static ALWAYS_INLINE void *memzero(void *dest, size_t count)
{
    return memset(dest, 0, count);
}
#endif
