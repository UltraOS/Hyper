section .text

PAE_BIT:                 equ (1 << 5)
EFER_NUMBER:             equ 0xC0000080
LONG_MODE_BIT:           equ (1 << 8)
PAGING_BIT:              equ (1 << 31)
LONG_MODE_CODE_SELECTOR: equ 0x28
EFLAGS_RESERVED_BIT:     equ (1 << 1)
DIRECT_MAP_BASE:         equ 0xFFFF800000000000

; NORETURN void do_kernel_handover32(u32 esp)
; esp + 4 [esp]
; esp + 0 [ret]
global do_kernel_handover32
do_kernel_handover32:
    mov esp, [esp + 4]

    push dword 0x00000000 | EFLAGS_RESERVED_BIT
    popfd

    xor eax, eax
    xor ecx, ecx
    xor edx, edx
    xor ebx, ebx
    xor ebp, ebp
    xor esi, esi
    xor edi, edi

    ret

; NOTERUN void do_kernel_handover64(u64 entrypoint, u64 rsp, u64 cr3, u64 arg0,
;                                   u64 arg1, bool unmap_lower_half)
; esp + 44 [unmap_lower_half]
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
    mov rax, DIRECT_MAP_BASE
    add rax, .higher_half
    jmp rax

.higher_half:
    mov r8,  [rsp + 4]
    mov rdi, [rsp + 28]
    mov rsi, [rsp + 36]
    mov al,  [rsp + 44]

    mov rsp, [rsp + 12]

    test al, al
    jz .unmap_done

    ; unmap lower half
    mov rax, cr3
    mov qword [rax], 0x0000000000000000
    mov cr3, rax

.unmap_done:
    push qword 0x0000000000000000 | EFLAGS_RESERVED_BIT
    popfq

    push qword 0x0000000000000000 ; fake ret address
    push r8                       ; kernel entry

    xor rax, rax
    xor rcx, rcx
    xor rdx, rdx
    xor rbx, rbx
    xor rbp, rbp
    xor r8, r8
    xor r9, r9
    xor r10, r10
    xor r11, r11
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15

    ret
