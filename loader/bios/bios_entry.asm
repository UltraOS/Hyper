extern enable_a20
extern bios_entry

STAGE2_LOAD_BASE:   equ 0x00007E00
STAGE2_STACK_END:   equ STAGE2_LOAD_BASE
STAGE2_STACK_BEGIN: equ 0x00000500

section .entry

stage2_magic: db "HyperST2"

; pad to 16
dq 0

main:
BITS 16
    mov sp, STAGE2_LOAD_BASE
    cld

    call enable_a20
    mov [a20_enabled], al
    cli

    lgdt [gdt_ptr]

    PROTECTED_MODE_BIT: equ (1 << 0)
    mov eax, cr0
    or  eax, PROTECTED_MODE_BIT
    mov cr0, eax

    jmp gdt_struct.code32:.protected_mode

.protected_mode:
BITS 32

    mov ax, gdt_struct.data32
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

%ifdef STACK_DEBUG_SPRAY
    mov eax, 0xCAFEBABE
    mov ecx, (STAGE2_STACK_END - STAGE2_STACK_BEGIN) / 4
    mov edi, STAGE2_STACK_BEGIN
    rep stosd
%endif

    call bios_entry

align 16
section .real_data

global gdt_ptr
gdt_ptr:
    dw gdt_struct_end - gdt_struct - 1
    dd gdt_struct

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

    .code16: equ $ - gdt_struct
    ; 16 bit code segment descriptor
    dw 0xFFFF ; limit
    dw 0x0000 ; base
    db 0x00   ; base
    db READWRITE | EXECUTABLE | CODE_OR_DATA | PRESENT
    db 0x00   ; byte granularity
    db 0x00   ; base

    .data16: equ $ - gdt_struct
    ; 16 bit data segment descriptor
    dw 0xFFFF ; limit
    dw 0x0000 ; base
    db 0x00   ; base
    db READWRITE | CODE_OR_DATA | PRESENT
    db 0x00   ; byte granularity
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

global a20_enabled
a20_enabled: db 0
