section .text
BITS 64

EFLAGS_RESERVED_BIT: equ (1 << 1)
PAE_BIT:             equ (1 << 5)
EFER_NUMBER:         equ 0xC0000080
LONG_MODE_BIT:       equ (1 << 8)
PAGING_BIT:          equ (1 << 31)
DIRECT_MAP_BASE:     equ 0xFFFF800000000000
CR4_PAE_BIT:         equ (1 << 5)

struc x86_handover_info
    .arg0:             resq 1
    .arg1:             resq 1

    .entrypoint:       resq 1
    .stack:            resq 1
    .direct_map_base:  resq 1
    .compat_code_addr: resd 1
    .cr3:              resd 1
    .cr4:              resd 1
    .is_long_mode:     resb 1
    .unmap_lower_half: resb 1
endstruc

; NORETURN
; void kernel_handover_x86(struct x86_handover_info *info)
global kernel_handover_x86
kernel_handover_x86:
     cli
     mov rax, gdt_ptr
     lgdt [rax]

     push qword gdt_struct.code32
     mov eax, [rcx + x86_handover_info.compat_code_addr]
     push qword rax

     retfq

 BITS 32
 ; This is always below 4 or 1 Gib depending on the handover arch
 global kernel_handover_x86_compat_code_begin
 kernel_handover_x86_compat_code_begin:
    mov ax, gdt_struct.data32
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov eax, cr0
    and eax, ~PAGING_BIT
    mov cr0, eax

    mov eax, [ecx + x86_handover_info.cr4]
    mov cr4, eax

    mov eax, [ecx + x86_handover_info.cr3]
    mov cr3, eax

    mov al, [ecx + x86_handover_info.is_long_mode]
    test al, al
    jz handover_i386_begin

; ============================ amd64 handover code ============================
handover_amd64_begin:
    mov eax, cr0
    or eax, PAGING_BIT
    mov cr0, eax

    mov eax, [ecx + x86_handover_info.compat_code_addr]
    add eax, .long_mode_continue - kernel_handover_x86_compat_code_begin
    mov [eax - (.long_mode_continue - .long_mode_jump_fixup)], eax

    db 0xEA
.long_mode_jump_fixup:
    dd 0xCCCCCCCC
    dw gdt_struct.code64

BITS 64
.long_mode_continue:
    add rax, .higher_half - .long_mode_continue
    add rax, [rcx + x86_handover_info.direct_map_base]
    jmp rax

.higher_half:
    mov rsp, [rcx + x86_handover_info.stack]

    push qword 0x0000000000000000 ; fake ret address
    push qword [rcx + x86_handover_info.entrypoint]

    mov rdi, [rcx + x86_handover_info.arg0]
    mov rsi, [rcx + x86_handover_info.arg1]

    mov al, [rcx + x86_handover_info.unmap_lower_half]
    test al, al
    jz .unmap_done

    mov rax, cr3
    mov rbx, [rcx + x86_handover_info.direct_map_base]
    add rbx, rax
    mov qword [rbx], 0x0000000000000000
    mov cr3, rax

.unmap_done:
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

    push qword 0x0000000000000000 | EFLAGS_RESERVED_BIT
    popfq

    ret

; ============================ i386 handover code ============================
BITS 32
handover_i386_begin:
    mov ebx, ecx

    ; Disable long mode
    mov ecx, EFER_NUMBER
    rdmsr
    and eax, ~LONG_MODE_BIT
    wrmsr

    mov ecx, ebx

    ; And finally enable paging again
    mov eax, cr0
    or eax, PAGING_BIT
    mov cr0, eax

    mov eax, [ecx + x86_handover_info.compat_code_addr]
    add eax, .higher_half - kernel_handover_x86_compat_code_begin
    add eax, [ecx + x86_handover_info.direct_map_base]
    jmp eax

.higher_half:
    mov esp, [ecx + x86_handover_info.stack]

    push dword 0x00000000
    push dword 0x00000000
    push dword [ecx + x86_handover_info.arg1]
    push dword [ecx + x86_handover_info.arg0]
    push dword 0x00000000
    push dword [ecx + x86_handover_info.entrypoint]

    mov al, [ecx + x86_handover_info.unmap_lower_half]
    test al, al
    jz .unmap_done

    mov edx, [ecx + x86_handover_info.direct_map_base]

    ; Transform bytes into 4MiB pages
    shr edx, 22

    mov al, [ecx + x86_handover_info.cr4]
    test al, CR4_PAE_BIT
    jz .not_pae

    ; Transform 4MiB pages into 512MiB pages
    ; (actually 1GiB, but the loop below unmaps in 4 byte strides)
    ; NOTE: this expects direct_map_base to be GiB-aligned
    shr edx, 7

.not_pae:
    mov eax, cr3
    add eax, [ecx + x86_handover_info.direct_map_base]

    mov ecx, edx

.unmap_one:
    mov dword [eax], 0x00000000
    add eax, 4

    dec ecx
    jnz .unmap_one

.reload_cr3:
    mov eax, cr3
    mov cr3, eax

.unmap_done:
    xor eax, eax
    xor ecx, ecx
    xor edx, edx
    xor ebx, ebx
    xor ebp, ebp
    xor esi, esi
    xor edi, edi

    push dword 0x00000000 | EFLAGS_RESERVED_BIT
    popfd

    ret
global kernel_handover_x86_compat_code_end
kernel_handover_x86_compat_code_end:
BITS 64

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
global gdt_struct_begin
gdt_struct_begin:
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
global gdt_struct_end
gdt_struct_end:
