#pragma once

#include "structures.h"

struct elf_load_ph {
    Elf64_Addr phys_addr, virt_addr;
    Elf64_Xword memsz, filesz;
    Elf64_Off fileoff;
};

struct elf_ph_info {
    Elf64_Half count;
    Elf64_Half entsize;
    Elf64_Off off;
};

struct elf_load_ctx {
    struct elf_load_spec *spec;
    bool alloc_anywhere;
    bool use_va;
    struct elf_ph_info ph_info;
    struct elf_binary_info *bi;
    struct elf_error *err;
};

bool elf_is_supported_load_ctx(struct elf_load_ctx *ctx);
