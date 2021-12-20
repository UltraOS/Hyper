#pragma once

static inline size_t strlen(const char *str)
{
    const char *str1;

    for (str1 = str; *str1; str1++);

    return str1 - str;
}

static inline void *memcpy(void *dest, const void *src, size_t count)
{
    char *cd = dest;
    const char *cs = src;

    while (count--)
        *cd++ = *cs++;

    return dest;
}

static inline void *memset(void *dest, int ch, size_t count)
{
    unsigned char fill = ch;
    char *cdest = cdest;

    while (count--)
        *cdest++ = fill;

    return cdest;
}

static inline void *memzero(void *dest, size_t count)
{
    return memset(dest, 0, count);
}
