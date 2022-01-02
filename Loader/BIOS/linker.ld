EBDA_BEGIN = 0x00080000;
REAL_MODE_SEGMENT0_END = 0x10000;

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
        ASSERT(. < REAL_MODE_SEGMENT0_END, "Real mode section is too large");
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

        ASSERT(. < EBDA_BEGIN, "Loader executable is too large");
    }
}
