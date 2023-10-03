#include "common/constants.h"
#include "handover.h"
#include "bios_call.h"
#include "services_impl.h"

void handover_prepare_for(struct handover_info *hi)
{
    UNUSED(hi);
}

static void cr4_prepare(struct handover_info *hi)
{
    u32 cr4 = handover_flags_to_cr4(hi->flags);
    asm volatile("mov %0, %%cr4" ::"r"(cr4));
}

static struct x86_handover_info {
    u64 arg0, arg1;
    u64 entrypoint;
    u64 stack;
    u64 direct_map_base;
    u32 cr3;

    bool is_long_mode;
    bool unmap_lower_half;
    bool is_pae;
} handover_info;

NORETURN
void kernel_handover_x86(struct x86_handover_info *info);

NORETURN
void kernel_handover(struct handover_info *hi)
{
    cr4_prepare(hi);

    if (hi->flags & HO_X86_LME) {
        handover_info.is_long_mode = true;

        /*
         * AMD Hammer Family Processor BIOS and Kernel Developer’s Guide
         * 12.21 Detect Target Operating Mode Callback
         * ---------------------------------------------------------------------
         * The operating system notifies the BIOS what the expected operating
         * mode is with the Detect Target Operating Mode callback (INT 15,
         * function EC00h). Based on the target operating mode, the BIOS can
         * enable or disable mode specific performance and functional
         * optimizations that are not visible to system software.
         */
        struct real_mode_regs regs = {};
        regs.eax = 0xEC00;
        regs.ebx = 0x02;
        bios_call(0x15, &regs, &regs);
    }

    handover_info.arg0 = hi->arg0;
    handover_info.arg1 = hi->arg1;
    handover_info.entrypoint = hi->entrypoint;
    handover_info.stack = hi->stack;
    handover_info.direct_map_base = hi->direct_map_base;
    handover_info.cr3 = pt_get_root(&hi->pt);
    handover_info.unmap_lower_half = hi->flags & HO_HIGHER_HALF_ONLY;
    handover_info.is_pae = hi->flags & HO_X86_PAE;

    kernel_handover_x86(&handover_info);
}
