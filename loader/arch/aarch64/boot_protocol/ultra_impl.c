#include "common/minmax.h"
#include "handover.h"
#include "boot_protocol/ultra_impl.h"

#define AARCH64_HIGHER_HALF_BASE 0xFFFFFFFF80000000
#define AARCH64_48BIT_DIRECT_MAP_BASE 0xFFFF000000000000
#define AARCH64_52BIT_DIRECT_MAP_BASE 0xFFF0000000000000

u32 ultra_get_flags_for_binary_options(struct binary_options *bo,
                                       enum elf_arch arch)
{
    UNUSED(bo);
    UNUSED(arch);
    return 0;
}

u64 ultra_higher_half_base(u32 flags)
{
    UNUSED(flags);
    return 0xFFFFFFFF80000000;
}

u64 ultra_higher_half_size(u32 flags)
{
    return (0xFFFFFFFFFFFFFFFF - ultra_higher_half_base(flags)) + 1;
}

u64 ultra_direct_map_base(u32 flags)
{
    if (flags & HO_AARCH64_52_BIT_IA)
        return AARCH64_52BIT_DIRECT_MAP_BASE;

    return AARCH64_48BIT_DIRECT_MAP_BASE;
}

u64 ultra_max_binary_address(u32 flags)
{
    UNUSED(flags);

    // No known limitations
    return 0xFFFFFFFFFFFFFFFF;
}

bool ultra_should_map_high_memory(u32 flags)
{
    UNUSED(flags);
    return true;
}

u64 ultra_adjust_direct_map_min_size(u64 direct_map_min_size, u32 flags)
{
    UNUSED(flags);
    return MAX(direct_map_min_size, 4ull * GB);
}

u64 ultra_adjust_direct_map_min_size_for_lower_half(u64 direct_map_min_size,
                                                    u32 flags)
{
    UNUSED(flags);
    return direct_map_min_size;
}

bool ultra_configure_pt_type(struct handover_info *hi, u8 pt_levels,
                             enum pt_constraint constraint,
                             enum pt_type *out_type)
{
    enum pt_type type = PT_TYPE_AARCH64_4K_GRANULE_48_BIT;

    if ((pt_levels == 5 || constraint == PT_CONSTRAINT_AT_LEAST) &&
        handover_is_flag_supported(HO_AARCH64_52_BIT_IA))
    {
        hi->flags |= HO_AARCH64_52_BIT_IA;
        type = PT_TYPE_AARCH64_4K_GRANULE_52_BIT;
    }

    if (pt_levels == 5 && type != PT_TYPE_AARCH64_4K_GRANULE_52_BIT &&
        constraint != PT_CONSTRAINT_MAX)
        return false;

    *out_type = type;
    return true;
}
