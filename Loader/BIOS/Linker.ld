SECTIONS
{
    . = 0x7E00;

    .entry : {
        *(.entry*)
    }

    /* make sure all data needed by real mode is within the first 64K */
    .real : {
        *(.real_code*)
        *(.real_data*)
    }

    .text : {
        *(.text .text.*)
    }

    .rodata : {
        *(.rodata .rodata.*)
    }

    .data : {
        *(.data .data.*)
    }

    .bss : {
        section_bss_begin = .;
        *(COMMON)
        *(.bss .bss.*)
        section_bss_end = .;
    }
}
