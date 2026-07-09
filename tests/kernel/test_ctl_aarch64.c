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
    g_qemu_uart = mmio_map(bctx, QEMU_UART_PHYS, PAGE_SIZE, MMIO_DEVICE);
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
        u64 el;

        /*
         * PSCI SYSTEM_OFF, then give up. QEMU's virt machine routes PSCI
         * through the HVC conduit when the guest runs at EL1, but through SMC
         * once EL2 is enabled, so pick the conduit based on the current EL.
         */
        asm volatile("mrs %0, currentel" : "=r"(el));

        if (((el >> 2) & 0b11) == 2)
            asm volatile("movz x0, #0x0008; movk x0, #0x8400, LSL #16; smc #0");
        else
            asm volatile("movz x0, #0x0008; movk x0, #0x8400, LSL #16; hvc #0");
    }
    for (;;) asm volatile("wfi" ::: "memory");
}
