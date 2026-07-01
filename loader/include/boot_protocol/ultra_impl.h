#pragma once

#include "common/types.h"
#include "filesystem/path.h"
#include "filesystem/filesystem_table.h"
#include "handover.h"
#include "elf.h"
#include "virtual_memory.h"

struct binary_options {
    struct full_path path;
    struct filesystem *fs;
    /*
     * A copy (not a pointer) of the resolved entry's location: the fs table is
     * freed by services_cleanup() before the kernel info attribute is written,
     * so a pointer into it would dangle.
     */
    struct fs_location loc;
    bool allocate_anywhere;
};

u32 ultra_get_flags_for_binary_options(struct binary_options *bo,
                                       enum elf_arch arch);

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

/*
 * 1 + the highest physical address the direct map (higher half) and identity
 * map (lower half) respectively are able to cover. Unbounded on 64-bit, but the
 * i686 direct map only reaches 1 GiB and its identity map 3 GiB.
 */
u64 ultra_direct_map_max_size(u32 flags);
u64 ultra_identity_map_max_size(u32 flags);

bool ultra_configure_pt_type(struct handover_info *hi, u8 pt_levels,
                             enum pt_constraint constraint,
                             enum pt_type *out_type);
