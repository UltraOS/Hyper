#pragma once

#include "string_view.h"
#include "types.h"

bool str_to_i64_with_base(struct string_view str, i64 *res, unsigned int base);
bool str_to_u64_with_base(struct string_view str, u64 *res, unsigned int base);
bool str_to_i32_with_base(struct string_view str, i32 *res, unsigned int base);
bool str_to_u32_with_base(struct string_view str, u32 *res, unsigned int base);
bool str_to_i16_with_base(struct string_view str, i16 *res, unsigned int base);
bool str_to_u16_with_base(struct string_view str, u16 *res, unsigned int base);
bool str_to_i8_with_base(struct string_view str, i8 *res, unsigned int base);
bool str_to_u8_with_base(struct string_view str, u8 *res, unsigned int base);

static inline bool str_to_i64(struct string_view str, i64 *res)
{
    return str_to_i64_with_base(str, res, 0);
}

static inline bool str_to_u64(struct string_view str, u64 *res)
{
    return str_to_u64_with_base(str, res, 0);
}

static inline bool str_to_i32(struct string_view str, i32 *res)
{
    return str_to_i32_with_base(str, res, 0);
}

static inline bool str_to_u32(struct string_view str, u32 *res)
{
    return str_to_u32_with_base(str, res, 0);
}

static inline bool str_to_i16(struct string_view str, i16 *res)
{
    return str_to_i16_with_base(str, res, 0);
}

static inline bool str_to_u16(struct string_view str, u16 *res)
{
    return str_to_u16_with_base(str, res, 0);
}

static inline bool str_to_i8(struct string_view str, i8 *res)
{
    return str_to_i8_with_base(str, res, 0);
}

static inline bool str_to_u8(struct string_view str, u8 *res)
{
    return str_to_u8_with_base(str, res, 0);
}
