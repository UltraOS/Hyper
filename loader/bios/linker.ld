EBDA_BEGIN             = 0x00080000;
STAGE2_LOAD_BASE       = 0x00007E00;
REAL_MODE_SEGMENT0_END = 0x00010000;

SECTIONS
{
    . = STAGE2_LOAD_BASE;

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

    .data : {
        *(.data .data.*)
    }

    ASSERT((. - STAGE2_LOAD_BASE) < 128K, "Looks like stage2 is now bigger than 256 sectors, please correct the STAGE2_BYTES_TO_LOAD constant in the MBR")

    .bss : {
        section_bss_begin = .;
        *(COMMON)
        *(.bss .bss.*)
        section_bss_end = .;

        ASSERT(. < EBDA_BEGIN, "Loader executable is too large");
    }

    /*
     * Must be a separate section because of "section type mismatch for ..."
     */
    .rel_sections : {
        *(.rel.plt)
        *(.rel.dyn)
    }

    .gotplt_sections : {
        *(.got)
        *(.got.plt)
        *(.plt)
        *(.iplt)
    }

    ASSERT(SIZEOF(.gotplt_sections) == 0 && SIZEOF(.rel_sections) == 0,
           "Relocation sections are not empty, recheck build flags")

    .symtab : { *(.symtab) }
    .strtab : { *(.strtab) }
    .shstrtab : { *(.shstrtab) }
    .symtab_shndx : { *(.symtab_shndx) }
    ASSERT(SIZEOF(.symtab_shndx) == 0, "Too many sections(?)")

    /DISCARD/ : {
        *(.comment*)
        *(.eh_frame)
        *(.debug*)
        *(.note.*)
    }
}
