ENTRY(main)

SECTIONS
{
    . = 0x0000000040000000;

    .text :
    {
        *(.text .text.*)
    }

    . = ALIGN(0x1000);

    .rodata :
    {
        *(.rodata .rodata.*)
    }

    . = ALIGN(0x1000);

    .data :
    {
        *(.data .data.*)
    }

    . = ALIGN(0x1000);

    .bss :
    {
        *(COMMON)
        *(.bss .bss.*)
    }
}
