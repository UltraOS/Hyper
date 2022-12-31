#pragma once

#include "types.h"
#include "attributes.h"

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
#include "hardened_string.h"
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
