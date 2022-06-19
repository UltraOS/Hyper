#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "helpers.h"

// signed types
typedef int8_t i8;
typedef int16_t i16;
typedef int i32;
typedef int64_t i64;

// unsigned types
typedef uint8_t u8;
typedef uint16_t u16;
typedef unsigned int u32;
typedef uint64_t u64;

typedef size_t ptr_t;

#if defined(__x86_64__) || defined(__aarch64__)
typedef i64 ssize_t;
#elif defined(__i386__)
typedef i32 ssize_t;
#else
#error unknown architecture
#endif

BUILD_BUG_ON(sizeof(i8) != 1);
BUILD_BUG_ON(sizeof(i16) != 2);
BUILD_BUG_ON(sizeof(i32) != 4);
BUILD_BUG_ON(sizeof(i64) != 8);

BUILD_BUG_ON(sizeof(u8) != 1);
BUILD_BUG_ON(sizeof(u16) != 2);
BUILD_BUG_ON(sizeof(u32) != 4);
BUILD_BUG_ON(sizeof(u64) != 8);

BUILD_BUG_ON(sizeof(bool) != 1);
