#pragma once

#include <stddef.h>
#include <stdint.h>

// signed types
using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

// unsigned types
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using ptr_t = size_t;

#define SIZEOF_I8 1
#define SIZEOF_I16 2
#define SIZEOF_I32 4
#define SIZEOF_I64 8

#define SIZEOF_U8 1
#define SIZEOF_U16 2
#define SIZEOF_U32 4
#define SIZEOF_U64 8

static_assert(sizeof(i8) == SIZEOF_I8, "Incorrect size of 8 bit integer");
static_assert(sizeof(i16) == SIZEOF_I16, "Incorrect size of 16 bit integer");
static_assert(sizeof(i32) == SIZEOF_I32, "Incorrect size of 32 bit integer");
static_assert(sizeof(i64) == SIZEOF_I64, "Incorrect size of 64 bit integer");

static_assert(sizeof(u8) == SIZEOF_U8, "Incorrect size of 8 bit unsigned integer");
static_assert(sizeof(u16) == SIZEOF_U16, "Incorrect size of 16 bit unsigned integer");
static_assert(sizeof(u32) == SIZEOF_U32, "Incorrect size of 32 bit unsigned integer");
static_assert(sizeof(u64) == SIZEOF_U64, "Incorrect size of 64 bit unsigned integer");

#undef SIZEOF_U8
#undef SIZEOF_U16
#undef SIZEOF_U32
#undef SIZEOF_U64

#undef SIZEOF_I8
#undef SIZEOF_I16
#undef SIZEOF_I32
#undef SIZEOF_I64

#define KB (1024ul)
#define MB (1024ul * KB)
#define GB (1024ul * MB)
