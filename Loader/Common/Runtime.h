#pragma once

#include "Types.h"

#define TO_STRING_HELPER(x) #x
#define TO_STRING(x) TO_STRING_HELPER(x)

#define cli() asm volatile("cli" :: \
                               : "memory")
#define sti() asm volatile("sti" :: \
                               : "memory")
#define hlt() asm volatile("hlt" :: \
                               : "memory")
#define hang() \
    for (;;) { \
        cli(); \
        hlt(); \
    }

[[noreturn]] void on_assertion_failed(const char* message, const char* file, const char* function, u32 line);
[[noreturn]] void panic(const char* message);

#define ASSERT(expression)         \
    (static_cast<bool>(expression) \
            ? static_cast<void>(0) \
            : on_assertion_failed(TO_STRING(expression), __FILE__, __PRETTY_FUNCTION__, __LINE__))
