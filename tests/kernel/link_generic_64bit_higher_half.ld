ENTRY(main)

SECTIONS
{
    . = 0xFFFFFFFF80000000; /* MAX - 2GB */

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
