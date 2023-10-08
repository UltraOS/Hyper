#include <stdarg.h>
#include "pio.h"

#include "test_ctl.h"
#include "test_ctl_impl.h"
#include "ultra_protocol.h"
#include "ultra_helpers.h"

static int is_in_hypervisor_state = -1;

#define HYPERVISOR_BIT (1 << 31)

static bool is_in_hypervisor(void)
{
    if (is_in_hypervisor_state == -1) {
        u32 a, b, c, d;

        asm volatile("cpuid"
            : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
            : "a"(1), "c"(0));
        is_in_hypervisor_state = c & HYPERVISOR_BIT;
    }

    return is_in_hypervisor_state;
}

static inline void e9_put_byte(char c)
{
    out8(0xE9, c);
}

void arch_put_byte(char c)
{
    if (!is_in_hypervisor())
        return;

    e9_put_byte(c);
}

void arch_write_string(const char *str, size_t len)
{
    if (!is_in_hypervisor())
        return;

    while (len--)
        e9_put_byte(*str++);
}

// Try various methods, then give up
void arch_hang_or_shutdown()
{
    if (!is_in_hypervisor() || !g_should_shutdown)
        goto hang;

    out16(0xB004, 0x2000);
    out16(0x604,  0x2000);
    out16(0x4004, 0x3400);

hang:
    for (;;) asm volatile("hlt" ::: "memory");
}
