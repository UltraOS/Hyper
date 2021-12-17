#pragma once

#include "Types.h"

struct CPUID {
    u32 a;
    u32 b;
    u32 c;
    u32 d;
};

void cpuid(u32 function, struct CPUID *id);
bool cpu_supports_long_mode();
