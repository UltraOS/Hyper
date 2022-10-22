#pragma once

#include "common/types.h"
#include "filesystem/block_cache.h"

struct file;

#define ELF_ALLOCATE_ANYWHERE     (1 << 0)
#define ELF_USE_VIRTUAL_ADDRESSES (1 << 1)

struct elf_io {
    struct file *binary;
    struct block_cache hdr_cache;
};

struct elf_load_spec {
    struct elf_io io;

    u32 flags;

    u32 memory_type;
    u64 binary_ceiling;
};

enum elf_arch {
    ELF_ARCH_INVALID = 0,
    ELF_ARCH_I386    = 1,
    ELF_ARCH_AMD64   = 2,
};

struct elf_binary_info {
    u64 entrypoint_address;

    u64 virtual_base;
    u64 virtual_ceiling;

    u64 physical_base;
    u64 physical_ceiling;

    enum elf_arch arch;
    bool kernel_range_is_direct_map;
};

struct elf_error {
    const char *reason;
    u64 args[3];
    u8 arg_count;
};

// Called automatically by elf_load if needed
bool elf_init_io_cache(struct elf_io *io, struct elf_error *err);

bool elf_load(struct elf_load_spec *spec,
              struct elf_binary_info *out_info,
              struct elf_error *out_error);

bool elf_get_arch(struct elf_io *io, enum elf_arch *arch,
                  struct elf_error *err);

void elf_pretty_print_error(const struct elf_error *err, const char *prefix);
