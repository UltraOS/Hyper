BITS 16

%ifdef HYPER_ISO_BOOT_RECORD
BYTES_PER_SECTOR:       equ 2048
SECTORS_PER_BATCH:      equ 16
%else
BYTES_PER_SECTOR:       equ 512
SECTORS_PER_BATCH:      equ 64
%endif

ISO9660_VOLUME_DESCRIPTOR0_LBA: equ 16

MBR_LOAD_BASE:          equ 0x7C00
MBR_SIZE_IN_BYTES:      equ 512
STAGE2_LOAD_BASE:       equ (MBR_LOAD_BASE + MBR_SIZE_IN_BYTES)
STAGE2_BASE_SECTOR:     equ 1
BYTES_PER_BATCH:        equ (SECTORS_PER_BATCH * BYTES_PER_SECTOR)
STAGE2_BYTES_TO_LOAD:   equ 131072
STAGE2_SECTORS_TO_LOAD: equ (STAGE2_BYTES_TO_LOAD / BYTES_PER_SECTOR) ; must be a multiple of SECTORS_PER_BATCH

ORG MBR_LOAD_BASE

start:
%ifndef HYPER_ISO_BOOT_RECORD
    jmp skip_bpb

    ; BPB + one extra nop after jmp
    ; NOTE: this could be used as a scratch area in the future if needed
    times 36 - ($ - $$) db 0x90 ; nops

skip_bpb:
%endif

    cli

    ; in case BIOS sets 0x07C0:0000
    jmp 0:segment_0

%ifdef HYPER_ISO_BOOT_RECORD
    times 8 - ($ - $$) db 0

    BOOT_INFO_INVALID_LOCATION: equ 0xFFFFFFFF

    boot_information:
        .primary_volume_descriptor: dd 0
        .boot_file_location:        dd BOOT_INFO_INVALID_LOCATION
        .boot_file_length:          dd 0
        .checksum:                  db 0
        .reserved:         times 40 dd 0
%endif

    segment_0:
        xor ax, ax
        mov ds, ax
        mov es, ax
        mov ss, ax
        mov fs, ax
        mov gs, ax
        mov sp, MBR_LOAD_BASE
        sti

; Boot record inside an ISO, simply reads the boot information table
%ifdef HYPER_ISO_BOOT_RECORD
    mov ecx, [boot_information.boot_file_location]
    cmp ecx, BOOT_INFO_INVALID_LOCATION
    jne .boot_info_ok

    mov si, boot_info_table_invalid
    jmp panic

.boot_info_ok:
    inc ecx
    mov [DAP.sector_begin_low], ecx

; MBR on a hybird ISO, parses el-torito boot record -> boot catalog -> retrieves stage2 base LBA
%elifdef HYPER_ISO_MBR
    BYTES_PER_VOLUME_DESCRIPTOR:      equ 2048
    VOLUME_DESCRIPTOR_TYPE_OFF:       equ 0x00
    BOOT_RECORD_BOOT_CATALOG_PTR_OFF: equ 0x47

    BOOT_RECORD_VOLUME_DESCRIPTOR_TYPE: equ 0x00
    VOLUME_DESCRIPTOR_SET_TERMINATOR:   equ 0xFF

    BYTES_PER_BOOT_CATALOG_ENTRY: equ 32
    BOOT_CATALOG_LOAD_RBA_OFF:    equ 8

    ; Read the first 16 volume descriptors
    call read_disk

    ; Attempt to find the boot record volume descriptor
    mov ax, SECTORS_PER_BATCH
    mov bx, STAGE2_LOAD_BASE

.try_next:
    cmp byte [bx], BOOT_RECORD_VOLUME_DESCRIPTOR_TYPE
    je .done

    cmp byte [bx], VOLUME_DESCRIPTOR_SET_TERMINATOR
    je .on_no_boot_record

    add bx, BYTES_PER_VOLUME_DESCRIPTOR
    dec ax
    jnz .try_next

.on_no_boot_record:
    mov si, no_boot_record_error
    jmp panic

.done:
    ; Absolute pointer to first sector of Boot Catalog
    mov ecx, [bx + BOOT_RECORD_BOOT_CATALOG_PTR_OFF]

    ; 2048 byte sectors -> 512 byte sectors
    shl ecx, 2

    ; We just need one single sector
    mov [DAP.sector_count], word 1
    mov [DAP.sector_begin_low], ecx
    call read_disk

    ; Load RBA
    mov ecx, [STAGE2_LOAD_BASE + BYTES_PER_BOOT_CATALOG_ENTRY + BOOT_CATALOG_LOAD_RBA_OFF]

    ; Skip ISO boot sector
    inc ecx

    ; 2048 byte sectors -> 512 byte sectors
    shl ecx, 2

    mov [DAP.sector_count], word SECTORS_PER_BATCH
    mov [DAP.sector_begin_low], ecx
%endif

    mov ebx, STAGE2_LOAD_BASE
    mov cx, STAGE2_SECTORS_TO_LOAD

    load_stage2:
        movzx ax, bl
        and ax, ~0xF0
        mov [DAP.read_into_offset], ax

        mov eax, ebx
        shr eax, 4
        mov [DAP.read_into_segment], ax

        call read_disk

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

; void read_disk()
; clobbers ax, si, CF
read_disk:
    clc ; carry gets set in case of an error, clear just in case

    ; NOTE: dl contains the drive number here, don't overwrite it
    ; Actual function: https://en.wikipedia.org/wiki/INT_13H#INT_13h_AH=42h:_Extended_Read_Sectors_From_Drive
    mov ax, DAP
    mov si, ax
    mov ah, 0x42
    int 0x13

    mov si, disk_error
    jc panic

    ret

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
    .read_into_offset:  dw STAGE2_LOAD_BASE
    .read_into_segment: dw 0x0000
%ifdef HYPER_ISO_MBR
    .sector_begin_low:  dd ISO9660_VOLUME_DESCRIPTOR0_LBA * 4
%else
    .sector_begin_low:  dd STAGE2_BASE_SECTOR
%endif
    .sector_begin_high: dd 0x00000000

CR: equ 0x0D
LF: equ 0x0A

disk_error:           db "Error reading the disk!", CR, LF, 0
invalid_magic_error:  db "Invalid stage 2 loader magic!", CR, LF, 0
reboot_msg:           db "Press any key to reboot...", CR, LF, 0

%ifdef HYPER_ISO_MBR
no_boot_record_error: db "Couldn't find a valid volume boot record!", CR, LF, 0
%endif

%ifdef HYPER_ISO_BOOT_RECORD
boot_info_table_invalid: db "Boot information table is invalid!", CR, LF
                         db "Please format the iso with -boot-info-table", CR, LF, 0

times BYTES_PER_SECTOR - ($ - $$) db 0
%else
; padding before partition list
times 446 - ($ - $$) db 0

; 4 empty partitions by default
times 16 * 4 db 0

boot_signature: dw 0xAA55
%endif
