section .text

PAE_BIT:                 equ 1 << 5
EFER_NUMBER:             equ 0xC0000080
LONG_MODE_BIT:           equ 1 << 8
PAGING_BIT:              equ 1 << 31
LONG_MODE_CODE_SELECTOR: equ 0x28

; [[noreturn]] void do_kernel_handover32(u32 entrypoint, u32 esp)
; esp + 8 [esp]
; esp + 4 [entrypoint]
; esp + 0 [ret]
global do_kernel_handover32
do_kernel_handover32:
    mov eax, [esp + 4]
    mov esp, [esp + 8]

    call eax

; [[noreturn]] void do_kernel_handover64(u64 entrypoint, u64 rsp, u64 cr3, u64 arg0, u64 arg1)
; esp + 36 [arg1]
; esp + 28 [arg0]
; esp + 20 [cr3]
; esp + 12 [rsp]
; esp + 4  [entrypoint]
; esp + 0  [ret]
global do_kernel_handover64
do_kernel_handover64:
    mov eax, [esp + 20]
    mov cr3, eax

    mov eax, cr4
    or  eax, PAE_BIT
    mov cr4, eax

    mov ecx, EFER_NUMBER
    rdmsr
    or eax, LONG_MODE_BIT
    wrmsr

    mov eax, cr0
    or  eax, PAGING_BIT
    mov cr0, eax

    jmp LONG_MODE_CODE_SELECTOR:.long_mode

BITS 64
.long_mode:
    mov rax, [rsp + 4]
    mov rdi, [rsp + 28]
    mov rsi, [rsp + 36]
    mov rsp, [rsp + 12]

    call rax
