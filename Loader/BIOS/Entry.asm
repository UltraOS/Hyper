BITS 32

section .entry
    stage2_magic: db "HyperST2"

    ; pad to 16
    dq 0

    extern bios_entry
    call bios_entry
