#include "string.h"

#ifdef HARDENED_STRING
#include "log.h"
#include "services.h"

void die_on_runtime_oob(const char *func, const char *file, size_t line,
                        size_t size, size_t dst_size, size_t src_size)
{
    print_err("BUG: out of bounds %s() call at %s:%zu: %zu bytes with dst=%zu",
              func, file, line, size, dst_size);

    if (src_size)
        print_err(" src=%zu", src_size);

    print_err("\n");
    loader_abort();
}
#endif

#ifdef PLATFORM_WANTS_GENERIC_STRING

#ifdef memmove
#undef memmove
#endif

void *MEMMOVE_FUNC(void *dest, const void *src, size_t count)
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

#ifdef memcpy
#undef memcpy
#endif

void *MEMCPY_FUNC(void *dest, const void *src, size_t count)
{
    char *cd = dest;
    const char *cs = src;

    while (count--)
        *cd++ = *cs++;

    return dest;
}

#ifdef memset
#undef memset
#endif

void *MEMSET_FUNC(void *dest, int ch, size_t count)
{
    unsigned char fill = ch;
    unsigned char *cdest = dest;

    while (count--)
        *cdest++ = fill;

    return dest;
}

#ifdef memcmp
#undef memcmp
#endif

int MEMCMP_FUNC(const void *lhs, const void *rhs, size_t count)
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

#ifdef strlen
#undef strlen
#endif

size_t STRLEN_FUNC(const char *str)
{
    const char *str1;

    for (str1 = str; *str1; str1++);

    return str1 - str;
}
#endif
