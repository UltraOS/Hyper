#pragma once

#include <common/types.h>

struct x86_handover_info {
    u64 arg0, arg1;
    u64 entrypoint;
    u64 stack;
    u64 direct_map_base;
    u32 compat_code_addr;
    u32 cr3, cr4;
    bool is_long_mode;
    bool unmap_lower_half;
};

extern struct x86_handover_info *xhi_relocated;
extern u32 kernel_handover_x86_compat_code_relocated;
