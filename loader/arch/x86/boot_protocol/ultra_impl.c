#define MSG_FMT(msg) "ULTRA-PROT-X86: " msg

#include "common/minmax.h"
#include "common/log.h"
#include "arch/constants.h"
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

          struct page_mapper_ctx {
    struct page_mapping_spec *spec;
    u64 direct_map_min_size;
    u64 direct_map_base;
    bool map_lower;
};

static bool do_map_high_memory(void *opaque, const struct memory_map_entry *me)
{
    struct page_mapper_ctx *ctx = opaque;
    struct page_mapping_spec *spec = ctx->spec;
    u64 aligned_begin, aligned_end;
    size_t page_count;

    aligned_end = me->physical_address + me->size_in_bytes;
    aligned_end = HUGE_PAGE_ROUND_UP(spec->pt, aligned_end);

    if (aligned_end <= ctx->direct_map_min_size)
        return true;

    aligned_begin = HUGE_PAGE_ROUND_DOWN(spec->pt, me->physical_address);
    aligned_begin = MAX(ctx->direct_map_min_size, aligned_begin);
    page_count = (aligned_end - aligned_begin) >> huge_page_shift(spec->pt);

    print_info("mapping high memory: 0x%016llX -> 0x%016llX (%zu pages)\n",
               aligned_begin, aligned_end, page_count);

    spec->virtual_base = aligned_begin;
    spec->physical_base = aligned_begin;
    spec->count = page_count;

    if (ctx->map_lower)
        map_pages(spec);

    spec->virtual_base += ctx->direct_map_base;
    map_pages(spec);

    return true;
}

/*
 * Always map the first 2/4MiB of physical memory with small pages.
 * We do this to avoid accidentally crossing any MTRR boundaries
 * with different cache types in the lower MiB.
 *
 * Intel® 64 and IA-32 Architectures Software Developer’s Manual:
 *
 * The Pentium 4, Intel Xeon, and P6 family processors provide special support
 * for the physical memory range from 0 to 4 MBytes, which is potentially mapped
 * by both the fixed and variable MTRRs. This support is invoked when a
 * Pentium 4, Intel Xeon, or P6 family processor detects a large page
 * overlapping the first 1 MByte of this memory range with a memory type that
 * conflicts with the fixed MTRRs. Here, the processor maps the memory range as
 * multiple 4-KByte pages within the TLB. This operation ensures correct
 * behavior at the cost of performance. To avoid this performance penalty,
 * operating-system software should reserve the large page option for regions
 * of memory at addresses greater than or equal to 4 MBytes.
 */
static void map_lower_huge_page(struct page_mapping_spec *spec, bool null_guard)
{
    size_t old_count = spec->count;
    size_t size_to_map = huge_page_size(spec->pt);

    spec->type = PAGE_TYPE_NORMAL;
    spec->physical_base = 0x0000000000000000;

    if (null_guard) {
        spec->physical_base += PAGE_SIZE;
        spec->virtual_base += PAGE_SIZE;
        size_to_map -= PAGE_SIZE;
    }
    spec->count = size_to_map >> PAGE_SHIFT;

    map_pages(spec);

    spec->type = PAGE_TYPE_HUGE;
    spec->physical_base += size_to_map;
    spec->virtual_base += size_to_map;
    spec->count = old_count - 1;
}

u64 ultra_build_arch_pt(struct kernel_info *ki, enum pt_type type,
                        bool higher_half_exclusive, bool null_guard)
{
    struct handover_info *hi = &ki->hi;
    struct elf_binary_info *bi = &ki->bin_info;
    enum elf_arch arch = bi->arch;
    u64 hh_base;

    struct page_table pt;
    struct page_mapping_spec spec = {
        .pt = &pt,
        .type = PAGE_TYPE_HUGE,
        .critical = true,
    };
    struct page_mapper_ctx ctx = {
        .spec = &spec,
        .direct_map_base = hi->direct_map_base,
        .map_lower = !higher_half_exclusive,
    };
    u8 hp_shift;

    hh_base = ultra_higher_half_base(hi->flags);
    page_table_init(
        &pt, type,
        handover_get_max_pt_address(ctx.direct_map_base, hi->flags)
    );
    hp_shift = huge_page_shift(&pt);
    ctx.direct_map_min_size =
            handover_get_minimum_map_length(ctx.direct_map_base, hi->flags);

    if (arch == ELF_ARCH_AMD64) {
        ctx.direct_map_min_size = MAX(ctx.direct_map_min_size, 4ull * GB);
    } else {
        u64 direct_map_size = (4ull * GB) - I686_DIRECT_MAP_BASE;

        BUG_ON(ctx.direct_map_min_size > direct_map_size);
        ctx.direct_map_min_size = direct_map_size;
    }

    // Direct map higher half
    spec.virtual_base = ctx.direct_map_base;
    spec.count = ctx.direct_map_min_size >> hp_shift;

    map_lower_huge_page(&spec, false);
    map_pages(&spec);

    if (ctx.map_lower) {
        spec.virtual_base = 0x0000000000000000;

        if (arch == ELF_ARCH_AMD64)
            spec.count = ctx.direct_map_min_size >> hp_shift;
        else
            spec.count = I686_DIRECT_MAP_BASE >> hp_shift;

        map_lower_huge_page(&spec, null_guard);
        map_pages(&spec);
    } else {
        u64 root_cov, off;
        root_cov = pt_level_entry_virtual_coverage(&pt, pt.levels - 1);

        // Steal the direct mapping from higher half, we're gonna unmap it later
        for (off = 0; off < ctx.direct_map_min_size; off += root_cov) {
            map_copy_root_entry(&pt, ctx.direct_map_base + off,
                                     0x0000000000000000  + off);
        }
    }

    if (arch == ELF_ARCH_AMD64)
        mm_foreach_entry(do_map_high_memory, &ctx);

    /*
     * If kernel had allocate-anywhere set to on, map virtual base to physical
     * base, otherwise simply direct map fist N gigabytes of physical.
     */
    if (ki->bin_opts.allocate_anywhere) {
        spec.physical_base = bi->physical_base;
        spec.virtual_base = bi->virtual_base;

        spec.count = PAGE_ROUND_UP(bi->physical_ceiling - bi->physical_base);
        spec.count >>= PAGE_SHIFT;

        spec.type = PAGE_TYPE_NORMAL;
        map_pages(&spec);
    } else if (hh_base != ctx.direct_map_base) {
        spec.virtual_base = hh_base;
        spec.count = ultra_higher_half_size(hi->flags) >> huge_page_shift(&pt);

        map_lower_huge_page(&spec, false);
        map_pages(&spec);
    }

    return (ptr_t)pt.root;
}
