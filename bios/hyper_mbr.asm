BITS 16

BYTES_PER_SECTOR:       equ 512
MBR_LOAD_BASE:          equ 0x7C00
MBR_SIZE_IN_BYTES:      equ BYTES_PER_SECTOR
STAGE2_LOAD_BASE:       equ MBR_LOAD_BASE + MBR_SIZE_IN_BYTES
STAGE2_BASE_SECTOR:     equ 1
SECTORS_PER_BATCH:      equ 64
BYTES_PER_BATCH:        equ SECTORS_PER_BATCH * BYTES_PER_SECTOR
STAGE2_SECTORS_TO_LOAD: equ 128 ; must be a multiple of SECTORS_PER_BATCH

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
        mov fs, ax
        mov gs, ax
        mov sp, MBR_LOAD_BASE
        sti

    clc ; carry gets set in case of an error, clear just in case
    mov cx, STAGE2_SECTORS_TO_LOAD
    mov ebx, STAGE2_LOAD_BASE

    load_stage2:
        mov al, bl
        and al, ~0xF0
        mov [DAP.read_into_offset], al

        mov eax, ebx
        shr eax, 4
        mov [DAP.read_into_segment], ax

        ; NOTE: dl contains the drive number here, don't overwrite it
        ; Actual function: https://en.wikipedia.org/wiki/INT_13H#INT_13h_AH=42h:_Extended_Read_Sectors_From_Drive
        mov ax, DAP
        mov si, ax
        mov ah, 0x42
        int 0x13

        mov si, disk_error
        jc panic

        add [DAP.sector_begin_low], dword SECTORS_PER_BATCH
        add ebx, BYTES_PER_BATCH
        sub cx, SECTORS_PER_BATCH
        jnz load_stage2

    STAGE2_MAGIC_LOWER: equ 'Hype'
    STAGE2_MAGIC_UPPER: equ 'rST2'

    verify_stage2_magic:
        mov eax, [STAGE2_LOAD_BASE]
        cmp eax, STAGE2_MAGIC_LOWER
        jne on_invalid_magic

        mov eax, [STAGE2_LOAD_BASE + 4]
        cmp eax, STAGE2_MAGIC_UPPER
        je off_to_stage2

    on_invalid_magic:
        mov si, invalid_magic_error
        jmp panic

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
    .unused:            db 0x00
    .sector_count:      dw SECTORS_PER_BATCH
    .read_into_offset:  dw 0x0000
    .read_into_segment: dw 0x0000
    .sector_begin_low:  dd STAGE2_BASE_SECTOR
    .sector_begin_high: dd 0x00000000

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
