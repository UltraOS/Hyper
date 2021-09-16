BITS 16

MBR_LOAD_BASE:          equ 0x7C00
MBR_SIZE_IN_BYTES:      equ 512
STAGE2_LOAD_BASE:       equ MBR_LOAD_BASE + MBR_SIZE_IN_BYTES
STAGE2_BASE_SECTOR:     equ 1
STAGE2_SECTORS_TO_LOAD: equ 31 ; currently assuming first partition starts at LBA 32

ORG MBR_LOAD_BASE

start:
    cli

    ; in case BIOS sets 0x07C0:0000
    jmp 0:segment_0

    segment_0:
        xor ax, ax
        mov ds, ax
        mov es, ax
        mov ss, ax
        mov sp, MBR_LOAD_BASE
        sti

    load_lba_partition:
        clc ; carry gets set in case of an error, clear just in case

        ; NOTE: dl contains the drive number here, don't overwrite it
        ; Actual function: https://en.wikipedia.org/wiki/INT_13H#INT_13h_AH=42h:_Extended_Read_Sectors_From_Drive
        mov ax, DAP
        mov si, ax
        mov ah, 0x42
        int 0x13

        mov si, disk_error
        jc panic

    STAGE2_MAGIC_LOWER: equ 'Hype'
    STAGE2_MAGIC_UPPER: equ 'rST2'

    verify_stage2_magic:
        mov eax, [STAGE2_LOAD_BASE]
        cmp eax, STAGE2_MAGIC_LOWER
        jne on_invalid_magic

        mov eax, [STAGE2_LOAD_BASE + 4]
        cmp eax, STAGE2_MAGIC_UPPER
        je switch_to_protected_mode

    on_invalid_magic:
        mov si, invalid_magic_error
        jmp panic

    switch_to_protected_mode:
        cli

        lgdt [gdt_ptr]

        PROTECTED_MODE_BIT: equ (1 << 0)

        mov eax, cr0
        or  eax, PROTECTED_MODE_BIT
        mov cr0, eax

        jmp gdt_struct.code:protected_mode

BITS 32

    protected_mode:
        mov ax, gdt_struct.data
        mov ds, ax
        mov es, ax
        mov fs, ax
        mov gs, ax
        mov ss, ax

    off_to_stage2:
        ; jump to base + 16 to skip stage 2 signature
        jmp STAGE2_LOAD_BASE + 16

BITS 16

; void write_string(ds:si string)
; clobbers ax, bx, si
write_string:
    ; Uses this function: https://stanislavs.org/helppc/int_10-e.html
    cld
    lodsb
    or al, al
    jz .done

    xor bx, bx
    mov ah, 0x0E
    int 0x10

    jmp write_string
.done:
    ret

; [[noreturn]] void panic(ds:si message)
panic:
    call write_string

    mov si, reboot_msg
    call write_string

    ; https://stanislavs.org/helppc/int_16-0.html
    xor ax, ax
    int 0x16

    ; actually reboot
    jmp word 0xFFFF:0x0000

DAP_SIZE: equ 16
DAP:
    .size:              db DAP_SIZE
    .unused:            db 0x0
    .sector_count:      dw STAGE2_SECTORS_TO_LOAD
    .read_into_offset:  dw STAGE2_LOAD_BASE
    .read_into_segment: dw 0x0
    .sector_begin_low:  dd STAGE2_BASE_SECTOR
    .sector_begin_high: dd 0x0

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

    .code: equ $ - gdt_struct
    ; 32 bit code segment descriptor
    dw 0xFFFF ; limit
    dw 0x0000 ; base
    db 0x00   ; base
    db READWRITE | EXECUTABLE | CODE_OR_DATA | PRESENT
    db MODE_32BIT | PAGE_GRANULARITY | 0x0F ; 4 bits of flags + 4 bits of limit
    db 0x00   ; base

    .data: equ $ - gdt_struct
    ; 32 bit data segment descriptor
    dw 0xFFFF ; limit
    dw 0x0000 ; base
    db 0x00   ; base
    db READWRITE | CODE_OR_DATA | PRESENT
    db MODE_32BIT | PAGE_GRANULARITY | 0x0F ; 4 bits of flags + 4 bits of limit
    db 0x00   ; base
gdt_struct_end:

CR: equ 0x0D
LF: equ 0x0A

no_lba_support_error: db "This BIOS doesn't support LBA disk access!", CR, LF, 0
disk_error:           db "Error reading the disk!", CR, LF, 0
invalid_magic_error:  db "Invalid stage 2 loader magic!", CR, LF, 0
reboot_msg:           db "Press any key to reboot...", CR, LF, 0

; padding before partition list
times 446 - ($ - $$) db 0

; 4 empty partitions by default
times 16 * 4 db 0

boot_signature: dw 0xAA55
