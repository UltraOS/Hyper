#include "common/string.h"
#include "common/attributes.h"

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

/*
 * string.h remaps these to the __builtin_* variants for regular callers; undo
 * that here so the definitions below produce the actual out-of-line symbols.
 */
#ifdef memmove
#undef memmove
#endif
#ifdef memcpy
#undef memcpy
#endif
#ifdef memset
#undef memset
#endif
#ifdef memcmp
#undef memcmp
#endif
#ifdef strlen
#undef strlen
#endif

/*
 * Word-at-a-time primitives shared by the portable fallbacks and strlen. The
 * word is the native register width (4 bytes on i686, 8 on amd64/aarch64), so
 * the bulk loops move a full register per iteration instead of a single byte.
 */
typedef size_t word_t;
#define WORD_SIZE sizeof(word_t)
#define WORD_ALIGN_MASK (WORD_SIZE - 1)
#define WORD_IS_ALIGNED(p) (((uintptr_t)(p) & WORD_ALIGN_MASK) == 0)

// 0x0101..01 and 0x8080..80 sized to word_t, for the zero-byte search trick.
#define WORD_ONES ((word_t)-1 / 0xFF)
#define WORD_HIGHS (WORD_ONES * 0x80)
#define WORD_HAS_ZERO(w) (((w) - WORD_ONES) & ~(w) & WORD_HIGHS)

#if defined(__i386__) || defined(__x86_64__)

/*
 * x86 exposes microcoded block operations (rep movsb/stosb/cmpsb) that the CPU
 * runs far faster than any hand-rolled loop, especially with ERMS on modern
 * parts. As a bonus they're written in assembly, so the compiler's loop-idiom
 * pass can never rewrite them into a (recursive) call to memcpy()/memset().
 */

void *memcpy(void *dest, const void *src, size_t count)
{
    void *d = dest;

    asm volatile(
        "rep movsb" : "+D"(d), "+S"(src), "+c"(count)
        :: "memory"
    );
    return dest;
}

void *memset(void *dest, int ch, size_t count)
{
    void *d = dest;

    asm volatile(
        "rep stosb" : "+D"(d), "+c"(count) : "a"((unsigned char)ch)
        : "memory"
    );
    return dest;
}

void *memmove(void *dest, const void *src, size_t count)
{
    void *d = dest;

    if ((uintptr_t)dest < (uintptr_t)src) {
        asm volatile(
            "rep movsb" : "+D"(d), "+S"(src), "+c"(count)
            :: "memory"
        );
    } else {
        d = (u8*)dest + count - 1;
        src = (const u8*)src + count - 1;

        asm volatile(
            "std; rep movsb; cld" : "+D"(d), "+S"(src), "+c"(count)
            :: "memory"
        );
    }

    return dest;
}

int memcmp(const void *lhs, const void *rhs, size_t count)
{
    const u8 *l = lhs, *r = rhs;

    if (!count)
        return 0;

    asm volatile(
        "repe cmpsb" : "+S"(l), "+D"(r), "+c"(count)
        :: "memory", "cc"
    );

    // Both pointers end up one past the last byte that was compared.
    return (int)l[-1] - (int)r[-1];
}

#else // Portable word-at-a-time implementation (e.g. aarch64).

static ALWAYS_INLINE void *forward_copy(
    void *dest, const void *src, size_t count
)
{
    u8 *d = dest;
    const u8 *s = src;

    if (count < WORD_SIZE)
        goto out_by_byte;

    // Word copies only pay off when both sides share the same misalignment
    if ((((uintptr_t)d ^ (uintptr_t)s) & WORD_ALIGN_MASK) != 0)
        goto out_by_byte;

    while (!WORD_IS_ALIGNED(d)) {
        *d++ = *s++;
        count--;
    }

    for (; count >= WORD_SIZE; count -= WORD_SIZE) {
        *(word_t*)d = *(const word_t*)s;
        d += WORD_SIZE;
        s += WORD_SIZE;
    }

out_by_byte:
    while (count--)
        *d++ = *s++;

    return dest;
}

void *memcpy(void *dest, const void *src, size_t count)
{
    return forward_copy(dest, src, count);
}

void *memset(void *dest, int ch, size_t count)
{
    u8 *d = dest;
    u8 fill = ch;

    if (count >= WORD_SIZE) {
        word_t wfill = WORD_ONES * fill;

        while (!WORD_IS_ALIGNED(d)) {
            *d++ = fill;
            count--;
        }

        for (; count >= WORD_SIZE; count -= WORD_SIZE) {
            *(word_t*)d = wfill;
            d += WORD_SIZE;
        }
    }

    while (count--)
        *d++ = fill;

    return dest;
}

void *memmove(void *dest, const void *src, size_t count)
{
    u8 *d;
    const u8 *s;

    // Front-to-back is safe unless the destination overlaps and sits higher.
    if ((uintptr_t)dest < (uintptr_t)src)
        return forward_copy(dest, src, count);

    d = (u8*)dest + count;
    s = (const u8*)src + count;

    while (count--)
        *--d = *--s;

    return dest;
}

int memcmp(const void *lhs, const void *rhs, size_t count)
{
    const u8 *l = lhs, *r = rhs;

    if (count < WORD_SIZE)
        goto out_by_byte;
    if ((((uintptr_t)l ^ (uintptr_t)r) & WORD_ALIGN_MASK) != 0)
        goto out_by_byte;

    while (!WORD_IS_ALIGNED(l)) {
        if (*l != *r)
            return (int)*l - (int)*r;

        l++, r++, count--;
    }

    /*
     * Skip whole matching words, then let the byte loop pinpoint the
     * first mismatch within the differing word.
     */
    for (; count >= WORD_SIZE; count -= WORD_SIZE) {
        if (*(const word_t*)l != *(const word_t*)r)
            break;

        l += WORD_SIZE;
        r += WORD_SIZE;
    }

out_by_byte:
    for (; count; l++, r++, count--) {
        if (*l != *r)
            return (int)*l - (int)*r;
    }

    return 0;
}

#endif

size_t strlen(const char *str)
{
    const char *s = str;

    // Handle the unaligned head so the word scan below can't straddle a page.
    while (!WORD_IS_ALIGNED(s)) {
        if (!*s)
            return s - str;
        s++;
    }

    // Scan a word at a time until one of its bytes is zero.
    while (!WORD_HAS_ZERO(*(const word_t*)s))
        s += WORD_SIZE;

    while (*s)
        s++;

    return s - str;
}
