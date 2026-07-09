#include "aarch64_handover.h"

#define CURRENT_EL_EL_MASK #0b1100
#define CURRENT_EL_1 #0b0100

.text

.global current_el
current_el:
    mrs x0, currentel
    and x0, x0, CURRENT_EL_EL_MASK
    lsr x0, x0, 2
    ret

.global read_id_aa64mmfr0_el1
read_id_aa64mmfr0_el1:
    mrs x0, id_aa64mmfr0_el1
    ret

.global read_id_aa64mmfr1_el1
read_id_aa64mmfr1_el1:
    mrs x0, id_aa64mmfr1_el1
    ret

#define SCTLR_M (1 << 0)
#define SPSEL_ELX 0b1

#define HCR_E2H (1 << 34)
#define HCR_TGE (1 << 27)

/*
 * void kernel_handover_aarch64(struct handover_info_aarch64 *hia)  // x0
 *
 * Switches to the kernel's translation tables and jumps to its entrypoint.
 *
 * The loader inherits the firmware's state with the MMU and caches on, so the
 * page tables and kernel image were written Write-Back cacheable. We hand off
 * with caches still enabled and RAM mapped Write-Back, so those attributes keep
 * matching and no cache maintenance is needed for RAM here (the kernel image's
 * instruction-cache coherency is handled separately, at load time).
 *
 * This function's own code is cleaned to the PoC beforehand (see
 * handover_prepare_for()): the fetches in the MMU-off window below are done
 * with mismatched attributes and are not guaranteed to observe cacheable
 * writes (the firmware loading this image) that haven't reached the PoC.
 */
.global kernel_handover_aarch64
kernel_handover_aarch64:
    // Mask all interrupts for the duration of the switch
    msr daifset, #0b1111

    /*
     * Read everything we need out of the struct while the MMU and caches are
     * still on. Once the MMU is disabled below, data accesses are Device/
     * non-cacheable and would miss any of these values still dirty in the
     * data cache.
     */
    ldr x1, [x0, handover_info_aarch64_ttbr0]
    ldr x2, [x0, handover_info_aarch64_ttbr1]
    ldr x3, [x0, handover_info_aarch64_mair]
    ldr x4, [x0, handover_info_aarch64_tcr]
    ldr x5, [x0, handover_info_aarch64_sctlr]
    ldrb w6, [x0, handover_info_aarch64_enable_vhe]
    ldr x7, [x0, handover_info_aarch64_stack]
    ldr x8, [x0, handover_info_aarch64_entrypoint]
    ldr x9, [x0, handover_info_aarch64_direct_map_base]
    ldr x10, [x0, handover_info_aarch64_arg0]
    ldr x11, [x0, handover_info_aarch64_arg1]
    ldrb w12, [x0, handover_info_aarch64_unmap_lower_half]

    // Complete all prior memory accesses before touching the MMU
    dsb sy
    isb

    /*
     * Disable the MMU while we reprogram the translation registers, then (at
     * EL2) switch to VHE. The current regime's MMU is controlled by SCTLR_EL2
     * at EL2 with E2H still 0, and by SCTLR_EL1 at EL1, so pick the right one.
     * Once E2H is set the _EL1 register names below redirect to their _EL2
     * counterparts, and setting E2H with the MMU already off avoids
     * reinterpreting a live SCTLR_EL2/TCR_EL2 in the EL1 (VHE) format.
     */
    cbz w6, .disable_el1

    mrs x13, sctlr_el2
    bic x13, x13, #SCTLR_M
    msr sctlr_el2, x13
    isb

    mrs x13, hcr_el2
    orr x13, x13, #HCR_E2H
    orr x13, x13, #HCR_TGE
    msr hcr_el2, x13
    isb
    b .mmu_disabled

.disable_el1:
    mrs x13, sctlr_el1
    bic x13, x13, #SCTLR_M
    msr sctlr_el1, x13
    isb

.mmu_disabled:
    msr mair_el1, x3
    msr tcr_el1, x4
    msr ttbr0_el1, x1
    msr ttbr1_el1, x2

    // Ensure the writes are observed by the table walker, then flush the TLB
    dsb ish
    cbz w6, .flush_el1

    /*
     * E2H is permitted to be cached in a TLB, so a change of its value
     * requires a TLBI ALLE2, which also drops any stale EL2-regime entries
     * created before the switch to VHE.
     */
    tlbi alle2
    b .flush_done

.flush_el1:
    tlbi vmalle1

.flush_done:
    dsb ish
    isb

    /*
     * Re-enable the MMU with data + instruction caches on. x5 only holds the
     * bits we force on; OR them onto the live register so we don't clobber its
     * RES1 / implementation-defined bits.
     */
    mrs x14, sctlr_el1
    orr x14, x14, x5
    msr sctlr_el1, x14
    isb

    // Set up the kernel stack (SP_ELx)
    msr spsel, #SPSEL_ELX
    mov sp, x7

    /*
     * Jump to the higher-half copy of the code below so we can drop the lower
     * half from under our own feet if requested
     */
    adr x13, .higher_half
    add x13, x13, x9
    br x13

.higher_half:
    cbz w12, .unmap_done

    msr ttbr0_el1, xzr
    dsb ish
    tlbi vmalle1
    dsb ish
    isb

.unmap_done:
    mov x30, x8
    mov x0, x10
    mov x1, x11

    mov x2, xzr
    mov x3, xzr
    mov x4, xzr
    mov x5, xzr
    mov x6, xzr
    mov x7, xzr
    mov x8, xzr
    mov x9, xzr
    mov x10, xzr
    mov x11, xzr
    mov x12, xzr
    mov x13, xzr
    mov x14, xzr
    mov x15, xzr
    mov x16, xzr
    mov x17, xzr
    mov x18, xzr
    mov x19, xzr
    mov x20, xzr
    mov x21, xzr
    mov x22, xzr
    mov x23, xzr
    mov x24, xzr
    mov x25, xzr
    mov x26, xzr
    mov x27, xzr
    mov x28, xzr
    mov x29, xzr

    msr nzcv, xzr
    ret

.global kernel_handover_aarch64_end
kernel_handover_aarch64_end:
