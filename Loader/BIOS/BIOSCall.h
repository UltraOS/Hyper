#pragma once

#include "Types.h"

struct RealModeRegisterState {
    u32 eax;
    u32 ebx;
    u32 ecx;
    u32 edx;
    u32 esi;
    u32 edi;
    u32 ebp;
    u16 gs;
    u16 fs;
    u16 es;
    u16 ds;
    u32 flags;

    [[nodiscard]] bool is_carry_set() { return flags & 1; }
};

extern "C" void bios_call(u32 number, const RealModeRegisterState* in, RealModeRegisterState* out);
