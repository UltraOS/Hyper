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

.global read_hcr_el2
read_hcr_el2:
    mrs x0, hcr_el2
    ret

.global write_hcr_el2
write_hcr_el2:
    msr hcr_el2, x0
    ret

#define SCTLR_M 0b1
#define SPSEL_ELX 0b1

/*
 * TODO: Figure out the barrier spam mess here.
 * I don't know whether this order is correct or whether these are all
 * really necessary, but better safe than sorry. For now just spam
 * instruction and data barriers everywhere.
 */

.global kernel_handover_aarch64
kernel_handover_aarch64:
    // Configure PSTATE (mask all interrupts, clear flags)
    msr daifset, #0b1111
    msr nzcv, xzr

    ldr x1, [x0, handover_info_aarch64_ttbr0]
    ldr x2, [x0, handover_info_aarch64_ttbr1]
    ldr x3, [x0, handover_info_aarch64_mair]
    ldr x4, [x0, handover_info_aarch64_tcr]
    ldr x5, [x0, handover_info_aarch64_sctlr]

    isb
    dsb nsh

    // Disable MMU while we mess with translation registers
    mrs x6, sctlr_el1
    and x6, x6, ~SCTLR_M
    msr sctlr_el1, x6

    isb
    dsb nsh

    msr mair_el1, x3
    msr TCR_el1, x4
    msr ttbr0_el1, x1
    msr ttbr1_el1, x2

    isb
    tlbi vmalle1
    dsb nsh

    // Finally enable the MMU again with the provided configuration
    msr sctlr_el1, x5

    isb
    dsb nsh

    ldr x3, [x0, handover_info_aarch64_stack]
    msr spsel, #SPSEL_ELX
    mov sp, x3

    ldr x30, [x0, handover_info_aarch64_entrypoint]

    ldr x3, [x0, handover_info_aarch64_direct_map_base]
    add x0, x0, x3
    adr x4, .higher_half
    add x3, x3, x4
    br x3

.higher_half:
    ldrb w1, [x0, handover_info_aarch64_unmap_lower_half]
    cmp x1, #0
    beq .unmap_done

    msr ttbr0_el1, xzr
    tlbi vmalle1
    dsb nsh

.unmap_done:
    ldr x1, [x0, handover_info_aarch64_arg1]
    ldr x0, [x0, handover_info_aarch64_arg0]

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

    ret
