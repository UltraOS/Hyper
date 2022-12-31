ENTRY(main)

SECTIONS
{
    . = 0xC0100000;

    .text :
    {
        *(.text .text.*)
    }

    .rodata :
    {
        *(.rodata .rodata.*)
    }

    .data :
    {
        *(.data .data.*)
    }

    .bss :
    {
        *(COMMON)
        *(.bss .bss.*)
    }
}