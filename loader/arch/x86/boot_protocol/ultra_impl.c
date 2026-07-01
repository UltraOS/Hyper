#define MSG_FMT(msg) "ULTRA-PROT-X86: " msg

#include "virtual_memory.h"
#include "handover.h"
#include "memory_services.h"
#include "boot_protocol/ultra_impl.h"

#define AMD64_HIGHER_HALF_BASE     0xFFFFFFFF80000000
#define I686_HIGHER_HALF_BASE      0xC0000000

#define AMD64_DIRECT_MAP_BASE      0xFFFF800000000000
#define AMD64_LA57_DIRECT_MAP_BASE 0xFF00000000000000
#define I686_DIRECT_MAP_BASE       I686_HIGHER_HALF_BASE

u64 ultra_higher_half_base(u32 flags)
{
    if (flags & HO_X86_LME)
        return AMD64_HIGHER_HALF_BASE;

    return I686_HIGHER_HALF_BASE;
}

u64 ultra_higher_half_size(u32 flags)
{
    u64 hh = ultra_higher_half_base(flags);
    u64 max_addr = 4ull * GB;

    if (flags & HO_X86_LME)
        max_addr = 0xFFFFFFFFFFFFFFFF;

    return (max_addr - hh) + 1;
}

u64 ultra_direct_map_base(u32 flags)
{
    if (flags & HO_X86_LME) {
        if (flags & HO_X86_LA57)
            return AMD64_LA57_DIRECT_MAP_BASE;

        return AMD64_DIRECT_MAP_BASE;
    }

    return I686_DIRECT_MAP_BASE;
}

u64 ultra_max_binary_address(u32 flags)
{
    if (flags & HO_X86_LME) {
    #ifdef __i386__
        return (4ull * GB);
    #else
        // No known limitations
        return 0xFFFFFFFFFFFFFFFF;
    #endif
    }

    // Must be accessible from the higher half
    return (4ull * GB) - I686_DIRECT_MAP_BASE;
}

u64 ultra_direct_map_max_size(u32 flags)
{
    // The direct map spans the entire (huge) higher half on amd64
    if (flags & HO_X86_LME)
        return 0xFFFFFFFFFFFFFFFF;

    // The i686 direct map lives in the higher half and only reaches this far
    return (4ull * GB) - I686_DIRECT_MAP_BASE;
}

u64 ultra_identity_map_max_size(u32 flags)
{
    if (flags & HO_X86_LME)
        return 0xFFFFFFFFFFFFFFFF;

    // The i686 identity map covers the entire lower half
    return I686_DIRECT_MAP_BASE;
}

u32 ultra_get_flags_for_binary_options(struct binary_options *bo,
                                       enum elf_arch arch)
{
    if (arch == ELF_ARCH_I386) {
        if (bo->allocate_anywhere)
            oops("allocate-anywhere is only allowed for 64 bit kernels\n");

        return 0;
    }

    return HO_X86_LME;
}

bool ultra_configure_pt_type(struct handover_info *hi, u8 pt_levels,
                             enum pt_constraint constraint,
                             enum pt_type *out_type)
{
    enum pt_type type;

    if (handover_is_flag_supported(HO_X86_PSE))
        hi->flags |= HO_X86_PSE;

    if (hi->flags & HO_X86_LME) {
        hi->flags |= HO_X86_PAE;
        type = PT_TYPE_AMD64_4LVL;

        if ((pt_levels == 5 || constraint == PT_CONSTRAINT_AT_LEAST) &&
            handover_is_flag_supported(HO_X86_LA57))
        {
            hi->flags |= HO_X86_LA57;
            type = PT_TYPE_AMD64_5LVL;
        }

        if (pt_levels == 5 && type != PT_TYPE_AMD64_5LVL &&
            constraint != PT_CONSTRAINT_MAX)
            return false;
    } else {
        type = PT_TYPE_I386_NO_PAE;

        if ((pt_levels == 3 || constraint == PT_CONSTRAINT_AT_LEAST) &&
            handover_is_flag_supported(HO_X86_PAE))
        {
            hi->flags |= HO_X86_PAE;
            type = PT_TYPE_I386_PAE;
        }

        if (pt_levels == 3 && type != PT_TYPE_I386_PAE &&
            constraint != PT_CONSTRAINT_MAX)
            return false;
    }

    *out_type = type;
    return true;
}
