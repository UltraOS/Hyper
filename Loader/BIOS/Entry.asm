BITS 32

section .entry
    stage2_magic: db "HyperST2"

    ; pad to 16
    dq 0

    extern bios_entry
    jmp bios_entry

section .data

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
PAGE_GRANULARITY: equ (1 << 7)

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

    .data16: equ $ - gdt_struct
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
gdt_struct_end:
