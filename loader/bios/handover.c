#include "handover.h"
#include "bios_call.h"

NORETURN
void do_kernel_handover32(u32 entrypoint, u32 esp);

NORETURN
void do_kernel_handover64(u64 entrypoint, u64 rsp, u64 cr3, u64 arg0, u64 arg1);

#define PUSH_DWORD(stack, value) \
    do {                         \
        (stack) -= 4;            \
        *(u32*)(stack) = value;  \
    } while (0);

void kernel_handover32(u32 entrypoint, u32 esp, u32 arg0, u32 arg1)
{
    // make sure the stack is 16 byte aligned pre-call
    PUSH_DWORD(esp, 0x00000000);
    PUSH_DWORD(esp, 0x00000000);
    PUSH_DWORD(esp, arg1);
    PUSH_DWORD(esp, arg0);

    do_kernel_handover32(entrypoint, esp);
}

void kernel_handover64(u64 entrypoint, u64 rsp, u64 cr3, u64 arg0, u64 arg1)
{
    /*
     * AMD Hammer Family Processor BIOS and Kernel Developer’s Guide
     * 12.21 Detect Target Operating Mode Callback
     * The operating system notifies the BIOS what the expected operating mode is with the Detect Target
     * Operating Mode callback (INT 15, function EC00h). Based on the target operating mode, the BIOS
     * can enable or disable mode specific performance and functional optimizations that are not visible to
     * system software.
     */
    struct real_mode_regs regs = {};
    regs.eax = 0xEC00;
    regs.ebx = 0x02;
    bios_call(0x15, &regs, &regs);

    do_kernel_handover64(entrypoint, rsp, cr3, arg0, arg1);
}
