#define MSG_FMT(msg) "AARCH64: " msg

#include "common/align.h"
#include "common/string_view.h"
#include "common/log.h"

#include "handover.h"
#include "aarch64_handover.h"

static u8 g_current_el;
static u64 g_ips_bits;
u32 g_aarch64_access_flag_mask;

bool handover_flags_map[32] = { 0 };
struct string_view handover_flags_to_string[32] = {
    [HO_AARCH64_52_BIT_IA_BIT] = SV("52-bit input address"),
};

static u64 get_feature_bits(u64 features, u64 first_bit, u64 last_bit)
{
    return (features & BIT_MASK(first_bit, last_bit + 1)) >> first_bit;
}

#define MMFR0_PARange_START 0
#define MMFR0_PARange_END 3
#define MMFR0_PARange_32BITS 0b0000
#define MMFR0_PARange_36BITS 0b0001
#define MMFR0_PARange_40BITS 0b0010
#define MMFR0_PARange_42BITS 0b0011
#define MMFR0_PARange_44BITS 0b0100
#define MMFR0_PARange_48BITS 0b0101
#define MMFR0_PARange_52BITS 0b0110

#define MMFR0_TGran4_START 28
#define MMFR0_TGran4_END 31
#define MMFR0_TGran4_SUPPORTED 0b0000
#define MMFR0_TGran4_SUPPORTED_52_BIT 0b0001
#define MMFT0_TGran4_UNSUPPORTED 0b1111

#define MMFR1_HFDBS_START 0
#define MMFR1_HFDBS_END 4

#define MMFR1_VH_START 8
#define MMFR1_VH_END 11
#define MMFR1_VH_PRESENT 0b0001

NORETURN
static void invalid_mmfr0(const char *which, u64 val)
{
    panic("Invalid ID_AA64MMFR0_EL1.%s value %llu\n", which, val);
}

void initialize_flags_map(void)
{
    u64 mmfr0, mmfr1, tgran4, parange;
    u8 parange_bits;
    bool has_vhe, has_hafdbs;

    g_current_el = current_el();
    print_info("running at EL%u\n", current_el());
    OOPS_ON(!g_current_el || g_current_el > 2);

    mmfr0 = read_id_aa64mmfr0_el1();

    tgran4 = get_feature_bits(mmfr0, MMFR0_TGran4_START, MMFR0_TGran4_END);
    switch (tgran4) {
    case MMFR0_TGran4_SUPPORTED_52_BIT:
        print_info("52-bit IA w/ 4K granule is supported\n");
        handover_flags_map[HO_AARCH64_52_BIT_IA_BIT] = true;
    case MMFR0_TGran4_SUPPORTED:
        break;
    case MMFT0_TGran4_UNSUPPORTED:
        panic("CPU doesn't support 4K translation granule\n");
    default:
        invalid_mmfr0("TGran4", tgran4);
    }

    parange = get_feature_bits(mmfr0, MMFR0_PARange_START, MMFR0_PARange_END);
    switch (parange) {
    case MMFR0_PARange_32BITS:
        parange_bits = 32;
        break;
    case MMFR0_PARange_36BITS:
        parange_bits = 36;
        break;
    case MMFR0_PARange_40BITS:
        parange_bits = 40;
        break;
    case MMFR0_PARange_42BITS:
        parange_bits = 42;
        break;
    case MMFR0_PARange_44BITS:
        parange_bits = 44;
        break;
    case MMFR0_PARange_48BITS:
        parange_bits = 48;
        break;
    case MMFR0_PARange_52BITS:
        parange_bits = 52;
        break;
    default:
        invalid_mmfr0("PARange", parange);
    }
    print_info("%d-bit physical address space\n", parange_bits);

    // Should be impossible
    if (unlikely(handover_flags_map[HO_AARCH64_52_BIT_IA_BIT] &&
                 parange_bits < 52)) {
        print_warn("52-bit IA is supported but PARange is less than 52 bits, "
                   "disabling...\n");
        handover_flags_map[HO_AARCH64_52_BIT_IA_BIT] = false;
    }

    g_ips_bits = parange << 32;

    mmfr1 = read_id_aa64mmfr1_el1();
    /*
     * We cannot provide proper higher half mappings in EL2 if FEAT_VHE is not
     * supported since TTBR1_EL2 is not accessible.
     *
     * There are multiple ways to solve this:
     * - Just drop down to EL1 and load TTBR1_EL1. Sure, this works. However,
     *   this forces the loader to take responsibility for having set up every
     *   system register correctly and doing full hardware feature detect prior
     *   to dropping down to EL1 as the actual kernel won't be able to do it on
     *   its own since it has no access to EL2 registers after handoff. No, we
     *   are not doing this.
     * - Just split the TTBR0_EL2 address space in half and consider its upper
     *   half "the upper half". This requires the kernel to be linked
     *   specifically for that scenario, which is not acceptable. So not an
     *   option either.
     * - Just don't configure any registers and rely on the hardware to having
     *   set them up correctly beforehand. Yeah, no.
     */
    has_vhe = get_feature_bits(
        mmfr1,
        MMFR1_VH_START,
        MMFR1_VH_END
    ) == MMFR1_VH_PRESENT;

    if (!has_vhe && g_current_el == 2)
        panic("EL2 boot is not supported without FEAT_VHE support\n");

    has_hafdbs = get_feature_bits(mmfr1, MMFR1_HFDBS_START, MMFR1_HFDBS_END);
    print_info("Hardware Access flag management: %s\n", has_hafdbs ? "yes" : "no");
    if (!has_hafdbs)
        g_aarch64_access_flag_mask = PAGE_AARCH64_ACCESS_FLAG;
}

u64 handover_get_minimum_map_length(u64 direct_map_base, u32 flags)
{
    UNUSED(direct_map_base);
    UNUSED(flags);

    return 4ull * GB;
}

u64 handover_get_max_pt_address(u64 direct_map_base, u32 flags)
{
    UNUSED(direct_map_base);
    UNUSED(flags);

    // No known limitations
    return 0xFFFFFFFFFFFFFFFF;
}

void handover_prepare_for(struct handover_info *hi)
{
    UNUSED(hi);
}

#define NORMAL_NON_CACHEABLE 0b00
#define OUTER_SHAREABLE 0b10

#define TCR_DS (1ull << 59)
#define TCR_HA (1ull << 39)
#define TCR_TG1_4K_GRANULE (0b10 << 30)
#define TCR_TG0_4K_GRANULE (0b00 << 14)
#define TCR_SH1_SHIFT 28
#define TCR_ORGN1_SHIFT 26
#define TCR_IRGN1_SHIFT 24
#define TCR_SH0_SHIFT 12
#define TCR_ORGN0_SHIFT 10
#define TCR_IRGN0_SHIFT 8

#define TCR_T1SZ_SHIFT 16
#define TCR_T0SZ_SHIFT 0

static u64 build_tcr(struct handover_info *hi)
{
    u64 ret = 0;
    u32 tsz;

    if (g_aarch64_access_flag_mask != PAGE_AARCH64_ACCESS_FLAG)
        ret |= TCR_HA;

    ret |= g_ips_bits;

    ret |= NORMAL_NON_CACHEABLE << TCR_IRGN0_SHIFT;
    ret |= NORMAL_NON_CACHEABLE << TCR_ORGN0_SHIFT;
    ret |= OUTER_SHAREABLE << TCR_SH0_SHIFT;
    ret |= TCR_TG1_4K_GRANULE;

    if (hi->flags & HO_AARCH64_52_BIT_IA) {
        tsz = 64 - 52;

        /*
         * NOTE: We enable DS simply for the sake of having access to 52-bit
         *       input addresses, we don't actually support the custom PA
         *       format where the upper bits of the address are actually stored
         *       in the lower bits of a PTE, so we rely on those bits to always
         *       be equal to zero, this can obviously break in the future.
         * TODO: add an abstraction for this and implement it properly.
         */
        ret |= TCR_DS;
    } else {
        tsz = 64 - 48;
    }

    ret |= tsz << TCR_T1SZ_SHIFT;

    ret |= NORMAL_NON_CACHEABLE << TCR_IRGN1_SHIFT;
    ret |= NORMAL_NON_CACHEABLE << TCR_ORGN1_SHIFT;
    ret |= OUTER_SHAREABLE << TCR_SH1_SHIFT;

    ret |= TCR_TG0_4K_GRANULE;
    ret |= tsz << TCR_T0SZ_SHIFT;

    return ret;
}

#define HCR_E2H (1ull << 34)
#define HCR_TGE (1ull << 27)
#define SCTLR_SA (1 << 3)
#define SCTLR_M (1 << 0)

#define MAIR_NON_CACHEABLE 0b0100
#define MAIR_I_SHIFT 0
#define MAIR_O_SHIFT 4

NORETURN
void kernel_handover(struct handover_info *hi)
{
    struct handover_info_aarch64 hia = (struct handover_info_aarch64) {
        .arg0 = hi->arg0,
        .arg1 = hi->arg1,
        .direct_map_base = hi->direct_map_base,
        .entrypoint = hi->entrypoint,
        .stack = hi->stack,
        .unmap_lower_half = hi->flags & HO_HIGHER_HALF_ONLY,
    };

    hia.ttbr0 = pt_get_root_pte_at(&hi->pt, 0x0000000000000000);
    hia.ttbr1 = pt_get_root_pte_at(&hi->pt, hi->direct_map_base);

    /*
     * Enable E2H if running at EL2 to enable TTBR1_EL2
     * TGE is enabled for sanity reasons
     */
    if (g_current_el == 2) {
        // NOTE: VHE support is verified during initialization
        u64 hcr;

        hcr = read_hcr_el2();
        hcr |= HCR_E2H | HCR_TGE;
        write_hcr_el2(hcr);
    }

    // Just play it safe
    hia.mair = (MAIR_NON_CACHEABLE << MAIR_O_SHIFT) |
               (MAIR_NON_CACHEABLE << MAIR_I_SHIFT);
    hia.tcr = build_tcr(hi);

    // Cache disabled, stack alignment checking enabled, MMU enabled
    hia.sctlr = SCTLR_SA | SCTLR_M;

    kernel_handover_aarch64(&hia);
}
