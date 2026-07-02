#include "common/helpers.h"
#include "common/types.h"
#include "arch/constants.h"
#include "mmio.h"
#include "ultra_helpers.h"
#include "test_ctl_impl.h"
#include "test_ctl.h"
#include "fb_tty.h"

#define QEMU_UART_PHYS 0x9000000

volatile u8 *g_qemu_uart;
bool g_uart_rebased = false;

void arch_test_ctl_init(struct ultra_boot_context *bctx)
{
    /* The loader only direct-maps RAM, so map the UART (device memory) here */
    g_qemu_uart = mmio_map(bctx, QEMU_UART_PHYS, PAGE_SIZE);
    g_uart_rebased = true;
}

void arch_put_byte(char c)
{
    if (g_uart_rebased)
        *g_qemu_uart = c;
}

void arch_hang_or_shutdown(void)
{
    if (g_should_shutdown) {
        // Try PSCI SYSTEM_OFF method, then give up
        asm volatile ("movz x0, #0x0008; movk x0, #0x8400, LSL #16; hvc #0");
    }
    for (;;) asm volatile("wfi" ::: "memory");
}
