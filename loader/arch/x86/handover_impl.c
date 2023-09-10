#include "common/align.h"
#include "common/string_view.h"

#include "handover.h"

struct cpuid_res {
    u32 a;
    u32 b;
    u32 c;
    u32 d;
};

void cpuid(u32 function, struct cpuid_res *id)
{
    asm volatile("cpuid"
        : "=a"(id->a), "=b"(id->b), "=c"(id->c), "=d"(id->d)
        : "a"(function), "c"(0));
}


static u64 get_i686_higher_half_length(u64 direct_map_base)
{
    BUG_ON(!direct_map_base || !IS_ALIGNED(direct_map_base, GB));
    return (4ull * GB) - direct_map_base;
}

u64 handover_get_minimum_map_length(u64 direct_map_base, u32 flags)
{
    if (flags & HO_X86_LME)
        return (4ull * GB);

    // At least the entire higher half
    return get_i686_higher_half_length(direct_map_base);
}

u64 handover_get_max_pt_address(u64 direct_map_base, u32 flags)
{
    if (flags & HO_X86_LME)
        // Handover code relies on this being a 32 bit value
        return (4ull * GB);

    // Must be accessible from the higher half
    return get_i686_higher_half_length(direct_map_base);
}

#define CR4_PSE  (1 << 4)
#define CR4_PAE  (1 << 5)
#define CR4_LA57 (1 << 12)

u32 handover_flags_to_cr4(u32 flags)
{
    u32 cr4 = 0;

    if (flags & HO_X86_PSE)
        cr4 |= CR4_PSE;
    if (flags & HO_X86_PAE)
        cr4 |= CR4_PAE;
    if (flags & HO_X86_LA57)
        cr4 |= CR4_LA57;

    return cr4;
}

bool handover_flags_map[32] = {};
struct string_view handover_flags_to_string[32] = {
    [HO_X86_LME_BIT]  = SV("Long Mode"),
    [HO_X86_PSE_BIT]  = SV("Page Size Extension"),
    [HO_X86_PAE_BIT]  = SV("Physical Address Extension"),
    [HO_X86_LA57_BIT] = SV("5-Level Paging"),
};

#define HIGHEST_FUNCTION_PARAMETER_AND_MANUFACTURER_ID_NUMBER 0x00000000
#define PROCESSOR_INFO_AND_FEATURE_BITS_FUNCTION_NUMBER       0x00000001
#define EXTENDED_FEATURES_FUNCTION_NUMBER                     0x00000007
#define HIGHEST_IMPLEMENTED_EXTENDED_FUNCTION_NUMBER          0x80000000
#define EXTENDED_PROCESSOR_INFO_FUNCTION_NUMBER               0x80000001

#define CPUID_LONG_MODE (1 << 29)
#define CPUID_PSE       (1 << 3)
#define CPUID_PAE       (1 << 6)
#define CPUID_LA57      (1 << 16)

void initialize_flags_map(void)
{
    struct cpuid_res id;
    u32 highest_number;

    cpuid(HIGHEST_FUNCTION_PARAMETER_AND_MANUFACTURER_ID_NUMBER, &id);
    highest_number = id.a;

    if (highest_number >= PROCESSOR_INFO_AND_FEATURE_BITS_FUNCTION_NUMBER) {
        cpuid(PROCESSOR_INFO_AND_FEATURE_BITS_FUNCTION_NUMBER, &id);
        handover_flags_map[HO_X86_PSE_BIT] = id.d & CPUID_PSE;
        handover_flags_map[HO_X86_PAE_BIT] = id.d & CPUID_PAE;
    }

    if (highest_number >= EXTENDED_FEATURES_FUNCTION_NUMBER) {
        cpuid(EXTENDED_FEATURES_FUNCTION_NUMBER, &id);
        handover_flags_map[HO_X86_LA57_BIT] = id.c & CPUID_LA57;
    }

    cpuid(HIGHEST_IMPLEMENTED_EXTENDED_FUNCTION_NUMBER, &id);
    highest_number = id.a;

    // Guard against bogus function numbers if it's not supported
    if ((highest_number <= HIGHEST_IMPLEMENTED_EXTENDED_FUNCTION_NUMBER) ||
       ((highest_number - HIGHEST_IMPLEMENTED_EXTENDED_FUNCTION_NUMBER) > 0xFF))
        return;

    cpuid(EXTENDED_PROCESSOR_INFO_FUNCTION_NUMBER, &id);
    handover_flags_map[HO_X86_LME_BIT] = id.d & CPUID_LONG_MODE;
}
