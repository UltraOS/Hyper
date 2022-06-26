#include "types.h"

static inline void out8(u16 port, u8 data)
{
    asm volatile("outb %0, %1" ::"a"(data), "Nd"(port));
}

static inline void out16(u16 port, u16 data)
{
    asm volatile("outw %0, %1" ::"a"(data), "Nd"(port));
}

static inline void out32(uint16_t port, uint32_t data)
{
    asm volatile("outl %0, %1" ::"a"(data), "Nd"(port));
}
