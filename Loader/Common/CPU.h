#pragma once

#include "Types.h"

namespace cpu {

struct ID {
    u32 a { 0x00000000 };
    u32 b { 0x00000000 };
    u32 c { 0x00000000 };
    u32 d { 0x00000000 };
};

inline ID id(u32 function)
{
    ID out {};

    asm volatile("cpuid"
        : "=a"(out.a), "=b"(out.b), "=c"(out.c), "=d"(out.d)
        : "a"(function), "c"(0));

    return out;
}

inline bool supports_long_mode()
{
    static constexpr u32 highest_implemented_function_number = 0x80000000;
    static constexpr u32 extended_processor_info_function_number = 0x80000001;

    auto highest_function = id(highest_implemented_function_number).a;

    if (highest_function < extended_processor_info_function_number)
        return false;

    static constexpr u32 long_mode_bit = SET_BIT(29);
    return id(extended_processor_info_function_number).d & long_mode_bit;
}

}

