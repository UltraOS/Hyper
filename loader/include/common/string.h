#pragma once

#include "types.h"
#include "attributes.h"

void *memcpy(void *dest, const void *src, size_t count);
void *memmove(void *dest, const void *src, size_t count);
void *memset(void *dest, int ch, size_t count);
int memcmp(const void *lhs, const void *rhs, size_t count);
size_t strlen(const char *str);

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
