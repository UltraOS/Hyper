#include "common/helpers.h"
#include "common/types.h"
#include "ultra_helpers.h"
#include "test_ctl_impl.h"
#include "test_ctl.h"
#include "fb_tty.h"

volatile u8 *g_qemu_uart = (u8*)0x9000000;
bool g_uart_rebased = false;

void test_ctl_init(struct ultra_boot_context *bctx)
{
    typedef struct ultra_platform_info_attribute upia;
    upia *pia = (upia*)find_attr(bctx, ULTRA_ATTRIBUTE_PLATFORM_INFO);

    g_qemu_uart += pia->higher_half_base;
    g_uart_rebased = true;
}

void arch_put_byte(char c)
{
    if (g_uart_rebased)
        *g_qemu_uart = c;
}

void arch_hang_or_shutdown(void)
{
    // Try PSCI SYSTEM_OFF method, then give up
    asm volatile ("movz x0, #0x0008; movk x0, #0x8400, LSL #16; hvc #0");
    for (;;) asm volatile("wfi" ::: "memory");
}
