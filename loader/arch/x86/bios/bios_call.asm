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

section .real_code

; The PXE API is reached through a real-mode far call (not an interrupt), with
; the parameter buffer far pointer and the opcode pushed on the stack. The
; entry point is discovered at runtime and stored in bios_pxe_entry.
; -----------------------------------------------------------------------------
; u16 bios_pxe_call(u16 opcode, u16 param_segment, u16 param_offset)
; esp + 12 [param_offset]
; esp + 8  [param_segment]
; esp + 4  [opcode]
; esp + 0  [ret]
global bios_pxe_call
bios_pxe_call:
BITS 32
    ; stash arguments where real mode can reach them
    mov ax, [esp + 4]
    mov [pxe_opcode], ax
    mov ax, [esp + 8]
    mov [pxe_param_segment], ax
    mov ax, [esp + 12]
    mov [pxe_param_offset], ax

    ; save non-scratch
    push ebx
    push esi
    push edi
    push ebp
    mov [pxe_saved_esp], esp

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
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov sp, [pxe_saved_esp]

    sti

    ; push the far pointer to the parameter buffer and the opcode, then call;
    ; the entry returns the exit code in AX and the caller cleans up the stack
    push word [pxe_param_segment]
    push word [pxe_param_offset]
    push word [pxe_opcode]
    call far [g_bios_pxe_entry]
    add sp, 6

    cli

    mov [pxe_ret], ax

    ; switch back to protected mode
    lgdt [ss:gdt_ptr]

    mov eax, cr0
    or  eax, PROTECTED_MODE_BIT
    mov cr0, eax

    jmp PROTECTED_MODE_CODE_SELECTOR:.protected_mode_code

.protected_mode_code:
BITS 32
    mov ax, PROTECTED_MODE_DATA_SELECTOR
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov esp, [pxe_saved_esp]
    pop ebp
    pop edi
    pop esi
    pop ebx

    movzx eax, word [pxe_ret]
    ret

section .real_data
align 4

in_regs_ptr:  dd 0
out_regs_ptr: dd 0
initial_esp:  dd 0

global g_bios_pxe_entry
g_bios_pxe_entry:  dd 0
pxe_saved_esp:     dd 0
pxe_opcode:        dw 0
pxe_param_segment: dw 0
pxe_param_offset:  dw 0
pxe_ret:           dw 0
