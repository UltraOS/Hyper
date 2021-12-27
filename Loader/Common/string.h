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

static inline void *memmove(void *dest, const void *src, size_t count)
{
    char *cd = dest;
    const char *cs = src;

    if (src < dest) {
        cs += count;
        cd += count;

        while (count--)
            *--cd = *--cs;
    } else {
        memcpy(dest, src, count);
    }

    return dest;
}

static inline void *memset(void *dest, int ch, size_t count)
{
    unsigned char fill = ch;
    unsigned char *cdest = dest;

    while (count--)
        *cdest++ = fill;

    return dest;
}

static inline void *memzero(void *dest, size_t count)
{
    return memset(dest, 0, count);
}

static inline int memcmp(const void *lhs, const void *rhs, size_t count)
{
    const u8 *byte_lhs = lhs;
    const u8 *byte_rhs = rhs;
    size_t i;

    for (i = 0; i < count; ++i) {
        if (byte_lhs[i] != byte_rhs[i])
            return byte_lhs[i] - byte_rhs[i];
    }

    return 0;
}
