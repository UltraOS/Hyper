#pragma once

#include "common/types.h"
#include "filesystem/path.h"
#include "handover.h"
#include "elf.h"
#include "virtual_memory.h"

struct binary_options {
    struct full_path path;
    bool allocate_anywhere;
};

struct kernel_info {
    struct binary_options bin_opts;
    struct elf_binary_info bin_info;
    struct file *binary;

    bool is_higher_half;
    struct handover_info hi;
};

enum pt_constraint {
    PT_CONSTRAINT_AT_LEAST,
    PT_CONSTRAINT_EXACTLY,
    PT_CONSTRAINT_MAX,
};

u64 ultra_higher_half_base(u32 flags);
u64 ultra_higher_half_size(u32 flags);
u64 ultra_direct_map_base(u32 flags);
u64 ultra_max_binary_address(u32 flags);

bool ultra_configure_pt_type(struct handover_info *hi, u8 pt_levels,
                             enum pt_constraint constraint,
                             enum pt_type *out_type);

u64 ultra_build_arch_pt(struct kernel_info *ki, enum pt_type type,
                        bool higher_half_exclusive, bool null_guard);
