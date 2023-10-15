section .text

BDA_BASE: equ 0x0400

; u32 bios_read_bda(u16 offset, u8 width)
global bios_read_bda
bios_read_bda:
BITS 32
    mov eax, BDA_BASE
    add ax, word [esp + 4]

    movzx edx, byte [esp + 4]

.read_as_byte:
    cmp edx, 1
    jne .read_as_word

    movzx eax, byte [eax]
    ret

.read_as_word:
    cmp edx, 2
    jne .read_as_dword

    movzx eax, word [eax]
    ret

.read_as_dword:
    mov eax, dword [eax]
    ret


extern gdt_ptr

PROTECTED_MODE_CODE_SELECTOR: equ 0x08
PROTECTED_MODE_DATA_SELECTOR: equ 0x10

REAL_MODE_CODE_SELECTOR: equ 0x18
REAL_MODE_DATA_SELECTOR: equ 0x20

PROTECTED_MODE_BIT: equ 1

SIZEOF_REGISTER_STATE: equ 40

section .real_code

; NORETURN void bios_jmp_to_reset_vector(void)
global bios_jmp_to_reset_vector
bios_jmp_to_reset_vector:
BITS 32
    jmp REAL_MODE_CODE_SELECTOR:.real_mode_transition

.real_mode_transition:
BITS 16
    mov ax, REAL_MODE_DATA_SELECTOR
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov eax, cr0
    and eax, ~PROTECTED_MODE_BIT
    mov cr0, eax

    jmp 0x0:.real_mode_code

.real_mode_code:
    jmp 0xFFFF:0x0000


; NOTE: this function assumes all pointers are located within
;       the first 64K of memory.
; -----------------------------------------------------------------------------
; void bios_call(u32 number, const struct real_mode_regs *in,
;                                  struct real_mode_regs *out)
; esp + 12 [out]
; esp + 8  [in]
; esp + 4  [number]
; esp + 0  [ret]
global bios_call
bios_call:
BITS 32
    ; save arguments so that we don't have to access the stack
    mov al, [esp + 4]
    mov [.call_number], al

    mov eax, [esp + 8]
    mov [in_regs_ptr], eax

    ; out_regs_ptr will point to the end of the structure for convenience
    ; when we push the state state after call
    mov eax, [esp + 12]
    add eax, SIZEOF_REGISTER_STATE
    mov [out_regs_ptr], eax

    ; save non-scratch
    push ebx
    push esi
    push edi
    push ebp
    mov [initial_esp], esp

    jmp REAL_MODE_CODE_SELECTOR:.real_mode_transition

.real_mode_transition:
BITS 16

    mov ax, REAL_MODE_DATA_SELECTOR
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov eax, cr0
    and eax, ~PROTECTED_MODE_BIT
    mov cr0, eax

    jmp 0x0:.real_mode_code

.real_mode_code:
    xor ax, ax
    mov ss, ax

    ; pop requested register state pre-call
    mov sp, [ss:in_regs_ptr]
    pop eax
    pop ebx
    pop ecx
    pop edx
    pop esi
    pop edi
    pop ebp
    pop gs
    pop fs
    pop es
    pop ds
    popfd

    mov sp, [ss:initial_esp]

    sti

    db 0xCD ; int followed by number
.call_number:
    db 0

    cli

    ; push final state post-call
    mov sp, [ss:out_regs_ptr]
    pushfd
    push ds
    push es
    push fs
    push gs
    push ebp
    push edi
    push esi
    push edx
    push ecx
    push ebx
    push eax

    ; switch back to protected mode
    lgdt [ss:gdt_ptr]

    mov eax, cr0
    or  eax, PROTECTED_MODE_BIT
    mov cr0, eax

    jmp PROTECTED_MODE_CODE_SELECTOR:.protected_mode_code

.protected_mode_code:
BITS 32

    ; restore data segments
    mov ax, PROTECTED_MODE_DATA_SELECTOR
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; restore non-scratch
    mov esp, [initial_esp]
    pop ebp
    pop edi
    pop esi
    pop ebx

    ret

section .real_data
align 4

in_regs_ptr:  dd 0
out_regs_ptr: dd 0
initial_esp:  dd 0
