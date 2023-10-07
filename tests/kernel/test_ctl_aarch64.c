#include "common/helpers.h"
#include "common/types.h"
#include "test_ctl_impl.h"
#include "test_ctl.h"
#include "fb_tty.h"

volatile u8 *g_qemu_uart = (u8*)0x9000000;

void test_ctl_init(struct ultra_boot_context *bctx)
{
    UNUSED(bctx);
}

void arch_put_byte(char c)
{
    *g_qemu_uart = c;
}

void arch_hang_or_shutdown(void)
{
    // Try PSCI SYSTEM_OFF method, then give up
    asm volatile ("movz x0, #0x0008; movk x0, #0x8400, LSL #16; hvc #0");
    for (;;) asm volatile("wfi" ::: "memory");
}
