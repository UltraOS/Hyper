#include "Handover.h"
#include "BIOSCall.h"

extern "C" [[noreturn]] void do_kernel_handover32(u32 esp, u32 entrypoint);
extern "C" [[noreturn]] void do_kernel_handover64(u64 entrypoint, u64 rsp, u64 cr3, u64 arg0, u64 arg1);

void kernel_handover32(u32 entrypoint, u32 esp, u32 arg0, u32 arg1)
{
    auto stack_push = [&esp] (u32 value) {
        esp -= 4;
        *reinterpret_cast<u32*>(esp) = value;
    };

    // make sure the stack is 16 byte aligned pre-call
    stack_push(0);
    stack_push(0);
    stack_push(arg1);
    stack_push(arg0);

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
    RealModeRegisterState registers {};
    registers.eax = 0xEC00;
    registers.ebx = 0x02;
    bios_call(0x15, &registers, &registers);

    do_kernel_handover64(entrypoint, rsp, cr3, arg0, arg1);
}
