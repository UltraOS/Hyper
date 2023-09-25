#pragma once
#include "types.h"

u32 read_u32(void *ptr);
u64 read_u32_zero_extend(void *ptr);
u64 read_u64(void *ptr);

void write_u32(void *ptr, u32 val);
void write_u64(void *ptr, u64 val);

void write_u32_u64(void *ptr, u64 val);
void write_u32_checked_u64(void *ptr, u64 val);
