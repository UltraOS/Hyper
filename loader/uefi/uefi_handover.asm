section .text
BITS 64

EFLAGS_RESERVED_BIT: equ (1 << 1)
PAE_BIT:             equ (1 << 5)
EFER_NUMBER:         equ 0xC0000080
LONG_MODE_BIT:       equ (1 << 8)
PAGING_BIT:          equ (1 << 31)
DIRECT_MAP_BASE:     equ 0xFFFF800000000000

; NORETURN
; void kernel_handover64(u64 entrypoint, u64 rsp, u64 cr3, u64 arg0,
;                        u64 arg1, bool unmap_lower_half)
; RSP + 48 [unmap_lower_half]
; RSP + 40 [arg1]
; R9       [arg0]
; R8       [cr3]
; RDX      [rsp]
; RCX      [entrypoint]
global kernel_handover64
kernel_handover64:
    cli

    mov rax, gdt_ptr
    lgdt [rax]

    push qword gdt_struct.code64
    mov  rax, .loader_gdt
    push rax
    retfq

.loader_gdt:
    mov ax, gdt_struct.data32
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; arg0 & arg1
    mov rdi, r9
    mov rsi, [rsp + 40]

    mov r10b, [rsp + 48]

    mov cr3, r8
    mov rsp, rdx

    mov rax, DIRECT_MAP_BASE
    mov rbx, .higher_half
    add rax, rbx
    jmp rax

.higher_half:
    test r10b, r10b
    jz .unmap_done

    mov rax, cr3
    mov qword [rax], 0x0000000000000000
    mov cr3, rax

.unmap_done:
    push qword 0x0000000000000000 | EFLAGS_RESERVED_BIT
    popfq

    push qword 0x0000000000000000 ; fake ret address
    push rcx                      ; kernel entry

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

; NORETURN void do_kernel_handover32(u32 esp)
; RCX [esp]
global do_kernel_handover32
do_kernel_handover32:
    cli
    mov rax, gdt_ptr
    lgdt [rax]

    push qword gdt_struct.code32
    mov  rax, .loader_gdt
    push rax
    retfq

BITS 32
.loader_gdt:
    mov ax, gdt_struct.data32
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov eax, cr0
    and eax, ~PAGING_BIT
    mov cr0, eax

    mov esp, ecx

    mov ecx, EFER_NUMBER
    rdmsr
    and eax, ~LONG_MODE_BIT
    wrmsr

    mov eax, cr4
    and eax, ~PAE_BIT
    mov cr4, eax

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

; our own GDT
align 16
section .data

global gdt_ptr
gdt_ptr:
    dw gdt_struct_end - gdt_struct - 1
    dq gdt_struct

; access
READWRITE:    equ (1 << 1)
EXECUTABLE:   equ (1 << 3)
CODE_OR_DATA: equ (1 << 4)
PRESENT:      equ (1 << 7)

; flags
MODE_32BIT:       equ (1 << 6)
MODE_64BIT:       equ (1 << 5)
PAGE_GRANULARITY: equ (1 << 7)

align 16
global gdt_struct
gdt_struct:
    .null: equ $ - gdt_struct
    dq 0x0000000000000000

    .code32: equ $ - gdt_struct
    ; 32 bit code segment descriptor
    dw 0xFFFF ; limit
    dw 0x0000 ; base
    db 0x00   ; base
    db READWRITE | EXECUTABLE | CODE_OR_DATA | PRESENT
    db MODE_32BIT | PAGE_GRANULARITY | 0x0F ; 4 bits of flags + 4 bits of limit
    db 0x00   ; base

    .data32: equ $ - gdt_struct
    ; 32 bit data segment descriptor
    dw 0xFFFF ; limit
    dw 0x0000 ; base
    db 0x00   ; base
    db READWRITE | CODE_OR_DATA | PRESENT
    db MODE_32BIT | PAGE_GRANULARITY | 0x0F ; 4 bits of flags + 4 bits of limit
    db 0x00   ; base

    .code64: equ $ - gdt_struct
    ; 64 bit code segment descriptor
    dw 0xFFFF ; limit
    dw 0x0000 ; base
    db 0x00   ; base
    db READWRITE | EXECUTABLE | CODE_OR_DATA | PRESENT
    db MODE_64BIT | PAGE_GRANULARITY | 0x0F ; 4 bits of flags + 4 bits of limit
    db 0x00   ; base
gdt_struct_end:
