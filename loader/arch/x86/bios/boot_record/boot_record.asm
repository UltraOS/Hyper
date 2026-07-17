BITS 16

%ifdef HYPER_ISO_BOOT_RECORD
BYTES_PER_SECTOR:       equ 2048
SECTORS_PER_BATCH:      equ 16
%else
BYTES_PER_SECTOR:       equ 512
SECTORS_PER_BATCH:      equ 64
%endif

ISO9660_VOLUME_DESCRIPTOR0_LBA: equ 16

; The plain MBR variant is the one built with no variant define at all, give
; it an explicit name so it can be tested for directly.
%ifndef HYPER_ISO_MBR
%ifndef HYPER_ISO_BOOT_RECORD
%ifndef HYPER_PXE_BOOT_RECORD
%define HYPER_PLAIN_MBR
%endif
%endif
%endif

MBR_LOAD_BASE:          equ 0x7C00
MBR_SIZE_IN_BYTES:      equ 512

; Synthetic, non-enumerable drive number handed to stage2 on a PXE boot so it
; can tell it was network-booted (must match BIOS_PXE_BOOT_DRIVE in the loader).
PXE_BOOT_DRIVE:         equ 0xFF
STAGE2_LOAD_BASE:       equ (MBR_LOAD_BASE + MBR_SIZE_IN_BYTES)
STAGE2_BASE_SECTOR:     equ 1

; The LBA stage2 starts at lives as a patchable qword at this fixed offset,
; right below the NT disk signature (0x1B8) and clear of the BPB area, which
; some firmware treats as its own (validating it, or even writing into the
; in-memory copy of this sector). Keep in sync with STAGE1_STAGE2_LBA_OFF in
; the installer.
STAGE2_LBA_PATCH_OFF:   equ 0x1B0
BYTES_PER_BATCH:        equ (SECTORS_PER_BATCH * BYTES_PER_SECTOR)
STAGE2_BYTES_TO_LOAD:   equ 131072
STAGE2_SECTORS_TO_LOAD: equ (STAGE2_BYTES_TO_LOAD / BYTES_PER_SECTOR) ; must be a multiple of SECTORS_PER_BATCH

ORG MBR_LOAD_BASE

start:
%ifndef HYPER_ISO_BOOT_RECORD
    jmp short skip_bpb
    nop

    ; A fake BPB filled with sane dummy values. LBA 0 is a (protective) MBR
    ; and never a real FAT volume, but some firmware treats whatever sector
    ; it boots as one:
    ; - IBM-style El Torito/USB emulation "fixes up" the BPB by writing the
    ;   fields marked (W) below into the in-memory copy of this sector, so
    ;   everything through the end of the EBPB must be data, never code
    ;   (our entry point used to sit at 0x24, exactly the drive number byte).
    ; - Quirky BIOSes (ThinkPads) refuse to recognize the disk as bootable
    ;   unless the fields marked (T) below hold plausible values.
    bpb:
        .oem_id:              db "HYPER   "
        .bytes_per_sector:    dw 512          ; (T)
        .sectors_per_cluster: db 0
        .reserved_sectors:    dw 0
        .fat_count:           db 0
        .root_dir_entries:    dw 0
        .sector_count:        dw 0
        .media_type:          db 0            ; (W)
        .sectors_per_fat:     dw 0
        .sectors_per_track:   dw 18           ; (T)
        .head_count:          dw 2            ; (T)
        .hidden_sectors:      dd 0            ; (W)
        .large_sector_count:  dd 0
        .drive_number:        db 0            ; (W)
        .reserved:            db 0
        .signature:           db 0
        .volume_id:           dd 0
        .volume_label:        db "HYPER      "
        .fs_type:     times 8 db 0

    ; guards the field layout above: the EBPB ends right before 0x3E
    times 0x3E - ($ - $$) db 0

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

; When booting over PXE the network stack has already loaded the entire image
; (this boot record + stage2) at MBR_LOAD_BASE, so stage2 is sitting right after
; us at STAGE2_LOAD_BASE and there is nothing to read from disk.
%ifndef HYPER_PXE_BOOT_RECORD
; A plain MBR reads stage2 from the installer-patched LBA (the MBR gap by
; default, or a GPT partition). The ISO variants set the DAP themselves above,
; so leave theirs alone.
%ifdef HYPER_PLAIN_MBR
    mov eax, [stage2_lba]
    mov [DAP.sector_begin_low], eax
    mov eax, [stage2_lba + 4]
    mov [DAP.sector_begin_high], eax
%endif
    mov cx, STAGE2_SECTORS_TO_LOAD

    ; The load cursor (LBA and destination segment) lives in the DAP itself
    ; and is advanced in place: no extended register survives an INT 13h call
    ; here. Some BIOSes destroy the upper halves of 32-bit registers inside
    ; their INT 13h handler (observed on a Samsung NF110 netbook, whose USB-HDD
    ; emulation truncated the destination previously kept in ebx to 16 bits,
    ; silently making later batches overwrite earlier ones).
    load_stage2:
        call read_disk

        add [DAP.sector_begin_low], dword SECTORS_PER_BATCH
%ifdef HYPER_PLAIN_MBR
        ; the start LBA is a full qword here, ripple the carry into the
        ; high half
        adc [DAP.sector_begin_high], dword 0
%endif
        add [DAP.read_into_segment], word BYTES_PER_BATCH >> 4
        sub cx, SECTORS_PER_BATCH
        jnz load_stage2
%endif

%ifdef HYPER_PXE_BOOT_RECORD
    ; Booted over the network: hand stage2 a synthetic, non-disk drive number so
    ; it can tell this was a PXE boot rather than a disk boot.
    mov dl, PXE_BOOT_DRIVE
%endif

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

%ifndef HYPER_ISO_BOOT_RECORD
; Windows 95/98/ME stamp a "disk timestamp" into bytes 0xDA-0xDF of the boot
; drive's MBR, on disk. Keep code and data out of that range so the stamp
; lands in padding instead of corrupting us.
times 0xDA - ($ - $$) db 0
times 6 db 0
%endif

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
    .read_into_offset:  dw 0x0000
    .read_into_segment: dw STAGE2_LOAD_BASE >> 4
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
; The LBA stage2 begins at. It defaults to the sector right after us (the MBR
; gap), but the installer overwrites it when stage2 lives elsewhere, e.g.
; inside a GPT partition where there is no usable gap before the first
; partition.
times STAGE2_LBA_PATCH_OFF - ($ - $$) db 0
stage2_lba: dq STAGE2_BASE_SECTOR

; padding before partition list (0x1B8 is the NT disk signature, keep zero)
times 446 - ($ - $$) db 0

; 4 empty partitions by default
times 16 * 4 db 0

boot_signature: dw 0xAA55
%endif
