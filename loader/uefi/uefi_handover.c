#include "handover.h"

NORETURN
void do_kernel_handover32(u32 esp);

void kernel_handover32(u32 entrypoint, u32 esp, u32 arg0, u32 arg1)
{
    // make sure the stack is 16 byte aligned pre-call
    STACK_PUSH_DWORD(esp, 0x00000000);
    STACK_PUSH_DWORD(esp, 0x00000000);
    STACK_PUSH_DWORD(esp, arg1);
    STACK_PUSH_DWORD(esp, arg0);
    STACK_PUSH_DWORD(esp, 0x00000000); // Fake return address
    STACK_PUSH_DWORD(esp, entrypoint);

    do_kernel_handover32(esp);
}
