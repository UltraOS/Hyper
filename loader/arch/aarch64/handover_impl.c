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

#define CTR_IMINLINE_MASK 0xF
#define CTR_DMINLINE_SHIFT 16
#define CTR_DMINLINE_MASK 0xF
#define CTR_IDC (1ull << 28)
#define CTR_DIC (1ull << 29)

#define CACHE_OP_RANGE(op, start, end, line)                  \
    do {                                                      \
        u64 addr_ = ALIGN_DOWN(start, line);                  \
                                                              \
        for (; addr_ < (end); addr_ += (line))                \
            asm volatile(op ", %0" :: "r"(addr_) : "memory"); \
    } while (0)

/*
 * Make the freshly-loaded kernel image coherent with the instruction stream:
 * clean it out of the data cache to the Point of Unification, then invalidate
 * the instruction cache over the same range. The loader still runs under the
 * firmware's identity-mapped, cacheable translation, so we operate on the
 * physical (identity) addresses.
 */
void handover_prepare_for(struct handover_info *hi)
{
    u64 ctr, dline, iline, end;

    asm volatile("mrs %0, ctr_el0" : "=r"(ctr));

    // Both are log2 of the line size in words, hence the 4ull
    dline = 4ull << ((ctr >> CTR_DMINLINE_SHIFT) & CTR_DMINLINE_MASK);
    iline = 4ull << (ctr & CTR_IMINLINE_MASK);

    /*
     * CTR_EL0.{IDC,DIC} waive the respective PoU maintenance steps for
     * instruction-to-data coherence. Note that they say nothing about the
     * PoC clean of the trampoline below, which is needed regardless.
     */
    end = hi->kernel_binary_base + hi->kernel_binary_size;
    if (!(ctr & CTR_IDC))
        CACHE_OP_RANGE("dc cvau", hi->kernel_binary_base, end, dline);
    asm volatile("dsb ish" ::: "memory");
    if (!(ctr & CTR_DIC))
        CACHE_OP_RANGE("ic ivau", hi->kernel_binary_base, end, iline);

    /*
     * The handover trampoline executes with the MMU off, where instruction
     * fetches are made with mismatched attributes (Outer Shareable, Write-
     * Through) against the Write-Back writes that loaded this image, and so
     * are only guaranteed to observe them once they reach the PoC. The
     * firmware only cleans loaded images to the PoU, so clean the trampoline
     * code to the PoC ourselves.
     */
    CACHE_OP_RANGE("dc cvac", (ptr_t)kernel_handover_aarch64,
                   (ptr_t)kernel_handover_aarch64_end, dline);

    asm volatile("dsb sy; isb" ::: "memory");
}

// TCR IRGN/ORGN encoding for Normal Write-Back Read-Allocate Write-Allocate
#define NORMAL_WRITE_BACK 0b01
#define INNER_SHAREABLE 0b11

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

    ret |= NORMAL_WRITE_BACK << TCR_IRGN0_SHIFT;
    ret |= NORMAL_WRITE_BACK << TCR_ORGN0_SHIFT;
    ret |= INNER_SHAREABLE << TCR_SH0_SHIFT;
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

    ret |= NORMAL_WRITE_BACK << TCR_IRGN1_SHIFT;
    ret |= NORMAL_WRITE_BACK << TCR_ORGN1_SHIFT;
    ret |= INNER_SHAREABLE << TCR_SH1_SHIFT;

    ret |= TCR_TG0_4K_GRANULE;
    ret |= tsz << TCR_T0SZ_SHIFT;

    return ret;
}

#define SCTLR_M  (1 << 0)
#define SCTLR_C  (1 << 2)
#define SCTLR_SA (1 << 3)
#define SCTLR_I  (1 << 12)

/*
 * Guaranteed MAIR layout (see the AARCH64 handoff state in the protocol spec):
 *   AttrIndx 0 - Normal, Inner+Outer Write-Back non-transient RA/WA (0xFF)
 *   AttrIndx 1 - Device-nGnRnE                                      (0x00)
 *   AttrIndx 2 - Normal, Inner+Outer Non-cacheable                  (0x44)
 * All RAM is mapped with AttrIndx 0; the kernel maps device memory
 * (MMIO/framebuffer) with AttrIndx 1 or 2.
 */
#define MAIR_NORMAL_WB     0xFFull
#define MAIR_DEVICE_nGnRnE 0x00ull
#define MAIR_NORMAL_NC     0x44ull

#define MAIR_ATTR(idx, val) ((val) << ((idx) * 8))

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

        /*
         * Request the trampoline to enable VHE (HCR_EL2.{E2H, TGE}) if running
         * at EL2, so that TTBR1_EL2 and the EL1-format control registers become
         * usable. The flip is deferred to the trampoline (with the MMU already
         * disabled) because setting E2H reinterprets the live SCTLR_EL2/TCR_EL2
         * in VHE format. VHE support is verified during initialization.
         */
        .enable_vhe = g_current_el == 2,
    };

    hia.ttbr0 = pt_get_root_pte_at(&hi->pt, 0x0000000000000000);
    hia.ttbr1 = pt_get_root_pte_at(&hi->pt, hi->direct_map_base);

    hia.mair = MAIR_ATTR(0, MAIR_NORMAL_WB) |
               MAIR_ATTR(1, MAIR_DEVICE_nGnRnE) |
               MAIR_ATTR(2, MAIR_NORMAL_NC);
    hia.tcr = build_tcr(hi);

    /*
     * Bits the trampoline forces on in SCTLR (OR'd onto the live register so
     * reserved bits are preserved): MMU, data & instruction caches, stack
     * alignment checking.
     */
    hia.sctlr = SCTLR_M | SCTLR_C | SCTLR_I | SCTLR_SA;

    kernel_handover_aarch64(&hia);
}
