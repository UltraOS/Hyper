#include "conversions.h"
#include "ctype.h"

static int get_base(struct string_view *str)
{
    if (unlikely(sv_empty(*str)))
        return -1;

    if (sv_starts_with(*str, SV("0x"))) {
        sv_offset_by(str, 2);
        return 16;
    }

    if (sv_starts_with(*str, SV("0b"))) {
        sv_offset_by(str, 2);
        return 2;
    }

    if (sv_starts_with(*str, SV("0"))) {
        sv_offset_by(str, 1);
        return 8;
    }

    if (str->text[0] >= '1' && str->text[0] <= '9')
        return 10;

    return -1;
}

static bool do_str_to_u64_with_base(struct string_view str, u64 *res, unsigned int base)
{
    u64 number = 0;
    u64 next;
    char c;

    while (sv_pop_one(&str, &c)) {
        if (c >= '0' && c <= '9') {
            next = c - '0';
        } else {
            char l = tolower(c);
            if (l < 'a' || l > 'f')
                return false;
            next = 10 + l - 'a';
        }

        next = number * base + next;
        if (next / base != number)
            return false;
        number = next;
    }

    *res = number;
    return true;
}

static bool do_str_to_u64_strict(struct string_view str, u64 *res)
{
    int base = get_base(&str);
    if (base < 0)
        return false;

    return do_str_to_u64_with_base(str, res, base);
}

static bool do_str_to_u64(struct string_view str, u64 *res)
{
    if (sv_starts_with(str, SV("+")))
        sv_offset_by(&str, 1);

    if (sv_starts_with(str, SV("-")))
        return false;

    return do_str_to_u64_strict(str, res);
}

static bool do_str_to_i64(struct string_view str, i64 *res)
{
    u64 ures;

    if (sv_starts_with(str, SV("-"))) {
        sv_offset_by(&str, 1);

        if (!do_str_to_u64_strict(str, &ures))
            return false;
        if ((i64)-ures > 0)
            return false;
    } else {
        if (sv_starts_with(str, SV("+")))
            sv_offset_by(&str, 1);

        if (!do_str_to_u64_strict(str, &ures))
            return false;
        if (ures > (u64)INT64_MAX)
            return false;
    }

    *res = (i64)ures;
    return true;
}

bool str_to_ll(struct string_view str, long long *res)
{
    return do_str_to_i64(str, res);
}

bool str_to_ull(struct string_view str, unsigned long long *res)
{
    return do_str_to_u64(str, res);
}

bool str_to_l(struct string_view str, long *res)
{
    if (sizeof(long) != sizeof(i64) || __alignof__(long) != __alignof__(i64)) {
        i64 ires;
        if (!do_str_to_i64(str, &ires))
            return false;

        if ((long)ires != ires)
            return false;

        *res = (long)ires;
        return true;
    } else {
        return do_str_to_i64(str, (i64*)res);
    }
}

bool str_to_ul(struct string_view str, unsigned long *res)
{
    if (sizeof(unsigned long) != sizeof(u64) || __alignof__(unsigned long) != __alignof__(u64)) {
        u64 ires;
        if (!do_str_to_u64(str, &ires))
            return false;

        if ((unsigned long)ires != ires)
            return false;

        *res = (unsigned long)ires;
        return true;
    } else {
        return do_str_to_u64(str, (u64*)res);
    }
}

bool str_to_i(struct string_view str, int *res)
{
    i64 ires;
    if (!do_str_to_i64(str, &ires))
        return false;

    if ((int)ires != ires)
        return false;

    *res = (int)ires;
    return true;
}

bool str_to_ui(struct string_view str, unsigned int *res)
{
    u64 ures;
    if (!do_str_to_u64(str, &ures))
        return false;

    if ((unsigned int)ures != ures)
        return false;

    *res = (unsigned int)ures;
    return true;
}

bool str_to_s(struct string_view str, short *res)
{
    i64 ires;
    if (!do_str_to_i64(str, &ires))
        return false;

    if ((short)ires != ires)
        return false;

    *res = (short)ires;
    return true;
}

bool str_to_us(struct string_view str, unsigned short *res)
{
    u64 ures;
    if (!do_str_to_u64(str, &ures))
        return false;

    if ((unsigned short)ures != ures)
        return false;

    *res = (unsigned short)ures;
    return true;
}

bool str_to_uc(struct string_view str, unsigned char *res)
{
    u64 ures;
    if (!do_str_to_u64(str, &ures))
        return false;

    if ((unsigned char)ures != ures)
        return false;

    *res = (unsigned char)ures;
    return true;
}

bool str_to_i64(struct string_view str, i64 *res)
{
    return str_to_ll(str, res);
}

bool str_to_u64(struct string_view str, u64 *res)
{
    return str_to_ull(str, res);
}

bool str_to_i32(struct string_view str, i32 *res)
{
    BUILD_BUG_ON(sizeof(i32) != sizeof(int));
    return str_to_i(str, res);
}

bool str_to_u32(struct string_view str, u32 *res)
{
    BUILD_BUG_ON(sizeof(u32) != sizeof(unsigned int));
    return str_to_ui(str, res);
}

bool str_to_i16(struct string_view str, i16 *res)
{
    BUILD_BUG_ON(sizeof(i16) != sizeof(short));
    return str_to_s(str, res);
}

bool str_to_u16(struct string_view str, u16 *res)
{
    BUILD_BUG_ON(sizeof(u16) != sizeof(unsigned short));
    return str_to_us(str, res);
}

bool str_to_i8(struct string_view str, i8 *res)
{
    i64 ires;
    if (!do_str_to_i64(str, &ires))
        return false;

    if ((char)ires != ires)
        return false;

    *res = (char)ires;
    return true;
}

bool str_to_u8(struct string_view str, u8 *res)
{
    return str_to_uc(str, res);
}
