#pragma once

#include "Common/Types.h"

static constexpr auto EI_MAG0 = 0;
static constexpr auto EI_MAG1 = 1;
static constexpr auto EI_MAG2 = 2;
static constexpr auto EI_MAG3 = 3;
static constexpr auto EI_CLASS = 4;
static constexpr auto EI_DATA = 5;
static constexpr auto EI_VERSION = 6;
static constexpr auto EI_OSABI = 7;
static constexpr auto EI_ABIVERSION = 8;
static constexpr auto EI_PAD = 9;
static constexpr auto EI_NIDENT = 16;

static constexpr unsigned char ELFMAG0 = 0x7f;
static constexpr unsigned char ELFMAG1 = 'E';
static constexpr unsigned char ELFMAG2 = 'L';
static constexpr unsigned char ELFMAG3 = 'F';

static constexpr unsigned char ELFCLASS32 = 1;
static constexpr unsigned char ELFCLASS64 = 2;

static constexpr unsigned char ELFDATA2LSB = 1;
static constexpr unsigned char ELFDATA2MSB = 2;

static constexpr u16 EM_386 = 3;
static constexpr u16 EM_AMD64 = 62;

static constexpr u16 ET_NONE = 0;
static constexpr u16 ET_REL = 1;
static constexpr u16 ET_EXEC = 2;
static constexpr u16 ET_DYN = 3;
static constexpr u16 ET_CORE = 4;
static constexpr u16 ET_LOPROC = 0xFF00;
static constexpr u16 ET_HIPROC = 0xFFFF;

using Elf32_Addr = u32;
using Elf32_Half = u16;
using Elf32_Off = u32;
using Elf32_Sword = i32;
using Elf32_Word = u32;

using Elf64_Addr = u64;
using Elf64_Half = u16;
using Elf64_Off = u64;
using Elf64_Sword = i32;
using Elf64_Word = u32;
using Elf64_Xword = u64;
using Elf64_Sxword = i64;

struct Elf32_Ehdr {
    unsigned char e_ident[EI_NIDENT];
    Elf32_Half e_type;
    Elf32_Half e_machine;
    Elf32_Word e_version;
    Elf32_Addr e_entry;
    Elf32_Off e_phoff;
    Elf32_Off e_shoff;
    Elf32_Word e_flags;
    Elf32_Half e_ehsize;
    Elf32_Half e_phentsize;
    Elf32_Half e_phnum;
    Elf32_Half e_shentsize;
    Elf32_Half e_shnum;
    Elf32_Half e_shstrndx;
};

struct Elf64_Ehdr {
    unsigned char e_ident[EI_NIDENT];
    Elf64_Half e_type;
    Elf64_Half e_machine;
    Elf64_Word e_version;
    Elf64_Addr e_entry;
    Elf64_Off e_phoff;
    Elf64_Off e_shoff;
    Elf64_Word e_flags;
    Elf64_Half e_ehsize;
    Elf64_Half e_phentsize;
    Elf64_Half e_phnum;
    Elf64_Half e_shentsize;
    Elf64_Half e_shnum;
    Elf64_Half e_shstrndx;
};

static constexpr u32 PT_NULL = 0;
static constexpr u32 PT_LOAD = 1;
static constexpr u32 PT_DYNAMIC = 2;
static constexpr u32 PT_INTERP = 3;
static constexpr u32 PT_NOTE = 4;
static constexpr u32 PT_SHLIB = 5;
static constexpr u32 PT_PHDR = 6;
static constexpr u32 PT_TLS = 7;
static constexpr u32 PT_LOOS = 0x60000000;
static constexpr u32 PT_SUNW_UNWIND = 0x6464e550;
static constexpr u32 PT_SUNW_EH_FRAME = 0x6474e550;
static constexpr u32 PT_LOSUNW = 0x6ffffffa;
static constexpr u32 PT_SUNWBSS = 0x6ffffffa;
static constexpr u32 PT_SUNWSTACK = 0x6ffffffb;
static constexpr u32 PT_SUNWDTRACE = 0x6ffffffc;
static constexpr u32 PT_SUNWCAP = 0x6ffffffd;
static constexpr u32 PT_HISUNW = 0x6fffffff;
static constexpr u32 PT_HIOS = 0x6fffffff;
static constexpr u32 PT_LOPROC = 0x70000000;
static constexpr u32 PT_HIPROC = 0x7fffffff;

static constexpr u16 PN_XNUM = 0xFFFF;

struct Elf32_Phdr {
    Elf32_Word p_type;
    Elf32_Off p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
};

struct Elf64_Phdr {
    Elf64_Word p_type;
    Elf64_Word p_flags;
    Elf64_Off p_offset;
    Elf64_Addr p_vaddr;
    Elf64_Addr p_paddr;
    Elf64_Xword p_filesz;
    Elf64_Xword p_memsz;
    Elf64_Xword p_align;
};
