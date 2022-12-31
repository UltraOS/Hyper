section .text

EFER_NUMBER:             equ 0xC0000080
LONG_MODE_BIT:           equ (1 << 8)
PAGING_BIT:              equ (1 << 31)
LONG_MODE_CODE_SELECTOR: equ 0x28
EFLAGS_RESERVED_BIT:     equ (1 << 1)

struc x86_handover_info
    .arg0:             resq 1
    .arg1:             resq 1

    .entrypoint:       resq 1
    .stack:            resq 1
    .direct_map_base:  resq 1
    .cr3:              resd 1
    .is_long_mode:     resb 1
    .unmap_lower_half: resb 1
    .is_pae:           resb 1
endstruc

; NORETURN
; void kernel_handover_x86(struct x86_handover_info *info)
global kernel_handover_x86
kernel_handover_x86:
    mov ebx, [esp + 4]

    mov eax, [ebx + x86_handover_info.cr3]
    mov cr3, eax

    mov al, [ebx + x86_handover_info.is_long_mode]
    test al, al
    jz do_kernel_handover_i386

; ============================ amd64 handover code ============================
do_kernel_handover_amd64:
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
    mov rax, [rbx + x86_handover_info.direct_map_base]
    add rax, .higher_half
    jmp rax

.higher_half:
    mov rdi, [rbx + x86_handover_info.arg0]
    mov rsi, [rbx + x86_handover_info.arg1]

    mov rsp, [rbx + x86_handover_info.stack]
    mov r8,  [rbx + x86_handover_info.entrypoint]

    mov al, [rbx + x86_handover_info.unmap_lower_half]
    test al, al
    jz .unmap_done

    ; unmap lower half
    mov rax, cr3
    mov rcx, [rbx + x86_handover_info.direct_map_base]
    add rcx, rax
    mov qword [rcx], 0x0000000000000000
    mov cr3, rax

.unmap_done:
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

    push qword 0x0000000000000000 | EFLAGS_RESERVED_BIT
    popfq

    ret

BITS 32
; ============================ i386 handover code ============================
do_kernel_handover_i386:
    mov eax, cr0
    or  eax, PAGING_BIT
    mov cr0, eax

    mov eax, [ebx + x86_handover_info.direct_map_base]
    add ebx, eax
    add eax, .higher_half
    jmp eax

.higher_half:
    mov dl, [ebx + x86_handover_info.unmap_lower_half]
    test dl, dl
    jz .unmap_done

    ; Transform bytes into 4MiB pages
    shr eax, 22

    mov dl, [ebx + x86_handover_info.is_pae]
    test dl, dl
    jz .not_pae

    ; Transform 4MiB pages into 512MiB pages
    ; (actually 1GiB, but the loop below unmaps in 4 byte strides)
    ; NOTE: this expects direct_map_base to be GiB-aligned
    shr eax, 7

.not_pae:
    mov ecx, eax
    mov eax, cr3
    add eax, [ebx + x86_handover_info.direct_map_base]

.unmap_one:
    mov dword [eax], 0x00000000
    add eax, 4

    dec ecx
    jnz .unmap_one

.reload_cr3:
    mov eax, cr3
    mov cr3, eax

.unmap_done:
    mov esp, [ebx + x86_handover_info.stack]

    ; SysV ABI alignment
    push dword 0x00000000
    push dword 0x00000000

    push dword [ebx + x86_handover_info.arg1]
    push dword [ebx + x86_handover_info.arg0]

    push dword 0x00000000 ; fake ret address
    push dword [ebx + x86_handover_info.entrypoint]

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
