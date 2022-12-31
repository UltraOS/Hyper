SECTIONS
{
    /*
     * The alignment-related stuff is also used in the vanilla mingw32
     * link script, so it's probably a good idea to keep it.
     */
    . = SIZEOF_HEADERS;
    . = ALIGN(__section_alignment__);

    .text __image_base__ + (__section_alignment__ < 0x1000 ? . : __section_alignment__) : {
        *(.text .text.*)
    }

    .data ALIGN(__section_alignment__) : {
        *(.data)
    }

    .rdata ALIGN(__section_alignment__) : {
        *(.rdata)
        *(SORT(.rdata$*))
    }

    .boot_protocols : {
        boot_protocols_begin = .;
        *(.boot_protocols)
        boot_protocols_end = .;
    }

    .filesystems : {
        filesystems_begin = .;
        *(.filesystems)
        filesystems_end = .;
    }

    .cleanup_handlers : {
        cleanup_handlers_begin = .;
        *(.cleanup_handlers)
        cleanup_handlers_end = .;
    }

    .bss ALIGN(__section_alignment__) : {
        *(.bss)
        *(COMMON)
    }

    .reloc ALIGN(__section_alignment__) : {
        *(.reloc)
    }

    /DISCARD/ : {
        *(.pdata*)
        *(.xdata*)
        *(.edata*)
        *(.idata*)
        *(.eh_frame*)
        *(.debug*)
        *(.tls*)
        *(.rsrc)
    }
}
