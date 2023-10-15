#pragma once

#include "common/types.h"
#include "common/constants.h"
#include "common/bug.h"

struct real_mode_regs {
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
};

struct real_mode_addr {
    u16 segment;
    u16 offset;
};

static inline bool is_carry_set(const struct real_mode_regs *regs)
{
    return regs->flags & (1 << 0);
}

static inline bool is_zero_set(const struct real_mode_regs *regs)
{
    return regs->flags & (1 << 6);
}

u32 bios_read_bda(u16 offset, u8 width);

NORETURN
void bios_jmp_to_reset_vector(void);

void bios_call(u32 number, const struct real_mode_regs *in, struct real_mode_regs *out);

static inline void* from_real_mode_addr(u16 segment, u16 offset)
{
    return (void*)(((u32)segment << 4) + offset);
}

static inline void as_real_mode_addr(ptr_t addr, struct real_mode_addr *rm_addr)
{
    BUG_ON(addr > (1 * MB));

    *rm_addr = (struct real_mode_addr) {
        .offset = addr & 0xF,
        .segment = (addr & 0xFFFF0) >> 4
    };
}
