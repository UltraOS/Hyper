SECTIONS
{
    . = 0x7E000;

    .text ALIGN(4K) : AT (ADDR (.text) - 0x7E000)
    {
        *(.entry)
        *(.text .text.*)
    }

    .rodata ALIGN(4K) : AT (ADDR (.rodata) - 0x7E000)
    {
        *(.rodata .rodata.*)
    }

    .data ALIGN(4K) : AT (ADDR (.data) - 0x7E000)
    {
        *(.data .data.*)
    }

    .bss ALIGN(4K) : AT (ADDR (.bss) - 0x7E000)
    {
        section_bss_begin = .;
        *(COMMON)
        *(.bss .bss.*)
        section_bss_end = .;
    }
}
