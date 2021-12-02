#pragma once

#include "Types.h"

struct ID {
    u32 a { 0x00000000 };
    u32 b { 0x00000000 };
    u32 c { 0x00000000 };
    u32 d { 0x00000000 };
};

inline ID cpu_id(u32 function)
{
    ID out {};

    asm volatile("cpuid"
        : "=a"(out.a), "=b"(out.b), "=c"(out.c), "=d"(out.d)
        : "a"(function), "c"(0));

    return out;
}

