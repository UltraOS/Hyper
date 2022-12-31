#include "common/conversions.h"
#include "common/ctype.h"

static unsigned int consume_base(struct string_view *str)
{
    if (unlikely(sv_empty(*str)))
        return 0;

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

    return 0;
}

static bool do_str_to_u64_unchecked(struct string_view str, u64 *res, unsigned int base)
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

static bool do_str_to_u64(struct string_view str, u64 *res, unsigned int base)
{
    unsigned int cb = consume_base(&str);
    if (!base && !cb)
        return false;

    return do_str_to_u64_unchecked(str, res, base ?: cb);
}

bool str_to_i64_with_base(struct string_view str, i64 *res, unsigned int base)
{
    u64 ures;

    if (sv_starts_with(str, SV("-"))) {
        sv_offset_by(&str, 1);

        if (!do_str_to_u64(str, &ures, base))
            return false;
        if ((i64)-ures > 0)
            return false;
    } else {
        if (sv_starts_with(str, SV("+")))
            sv_offset_by(&str, 1);

        if (!do_str_to_u64(str, &ures, base))
            return false;
        if (ures > (u64)INT64_MAX)
            return false;
    }

    *res = (i64)ures;
    return true;
}

bool str_to_u64_with_base(struct string_view str, u64 *res, unsigned int base)
{
    if (sv_starts_with(str, SV("+")))
        sv_offset_by(&str, 1);

    if (sv_starts_with(str, SV("-")))
        return false;

    return do_str_to_u64(str, res, base);
}

bool str_to_i32_with_base(struct string_view str, i32 *res, unsigned int base)
{
    i64 ires;
    if (!str_to_i64_with_base(str, &ires, base))
        return false;

    if ((i32)ires != ires)
        return false;

    *res = (i32)ires;
    return true;
}

bool str_to_u32_with_base(struct string_view str, u32 *res, unsigned int base)
{
    u64 ures;
    if (!str_to_u64_with_base(str, &ures, base))
        return false;

    if ((u32)ures != ures)
        return false;

    *res = (u32)ures;
    return true;
}

bool str_to_i16_with_base(struct string_view str, i16 *res, unsigned int base)
{
    i64 ires;
    if (!str_to_i64_with_base(str, &ires, base))
        return false;

    if ((i16)ires != ires)
        return false;

    *res = (i16)ires;
    return true;
}

bool str_to_u16_with_base(struct string_view str, u16 *res, unsigned int base)
{
    u64 ures;
    if (!str_to_u64_with_base(str, &ures, base))
        return false;

    if ((u16)ures != ures)
        return false;

    *res = (u16)ures;
    return true;
}

bool str_to_i8_with_base(struct string_view str, i8 *res, unsigned int base)
{
    i64 ires;
    if (!str_to_i64_with_base(str, &ires, base))
        return false;

    if ((i8)ires != ires)
        return false;

    *res = (i8)ires;
    return true;
}

bool str_to_u8_with_base(struct string_view str, u8 *res, unsigned int base)
{
    u64 ures;
    if (!str_to_u64_with_base(str, &ures, base))
        return false;

    if ((u8)ures != ures)
        return false;

    *res = (u8)ures;
    return true;
}
