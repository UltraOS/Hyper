#pragma once

#include "string_view.h"
#include "types.h"

bool str_to_ll(struct string_view str, long long *res);
bool str_to_ull(struct string_view str, unsigned long long *res);
bool str_to_l(struct string_view str, long *res);
bool str_to_ul(struct string_view str, unsigned long *res);
bool str_to_i(struct string_view str, int *res);
bool str_to_ui(struct string_view str, unsigned int *res);
bool str_to_s(struct string_view str, short *res);
bool str_to_us(struct string_view str, unsigned short *res);
bool str_to_uc(struct string_view str, unsigned char *res);

bool str_to_i64(struct string_view str, i64 *res);
bool str_to_u64(struct string_view str, u64 *res);
bool str_to_i32(struct string_view str, i32 *res);
bool str_to_u32(struct string_view str, u32 *res);
bool str_to_i16(struct string_view str, i16 *res);
bool str_to_u16(struct string_view str, u16 *res);
bool str_to_i8(struct string_view str, i8 *res);
bool str_to_u8(struct string_view str, u8 *res);
