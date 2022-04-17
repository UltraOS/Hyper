#pragma once

#include "types.h"
#include "bug.h"

#define LOWER_TO_UPPER_OFFSET ('a' - 'A')
BUILD_BUG_ON(LOWER_TO_UPPER_OFFSET < 0);

static inline bool isupper(char c)
{
    return c >= 'A' && c <= 'Z';
}

static inline bool islower(char c)
{
    return c >= 'a' && c <= 'z';
}

static inline bool isalnum(char c)
{
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9');
}

static inline char tolower(char c)
{
    if (isupper(c))
        return c + LOWER_TO_UPPER_OFFSET;

    return c;
}

static inline void str_tolower(char* str, size_t size)
{
    size_t i;

    for (i = 0; i < size; ++i)
        str[i] = tolower(str[i]);
}

static inline char toupper(char c)
{
    if (islower(c))
        return c - LOWER_TO_UPPER_OFFSET;

    return c;
}

static inline void str_toupper(char* str, size_t size)
{
    size_t i;

    for (i = 0; i < size; ++i)
        str[i] = toupper(str[i]);
}
