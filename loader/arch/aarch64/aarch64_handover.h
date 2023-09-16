#pragma once

#ifdef __ASSEMBLER__
#define handover_info_aarch64_arg0 0
#define handover_info_aarch64_arg1 8
#define handover_info_aarch64_entrypoint 16
#define handover_info_aarch64_stack 24
#define handover_info_aarch64_direct_map_base 32

#define handover_info_aarch64_ttbr0 40
#define handover_info_aarch64_ttbr1 48
#define handover_info_aarch64_mair 56
#define handover_info_aarch64_tcr 64
#define handover_info_aarch64_sctlr 72

#define handover_info_aarch64_unmap_lower_half 80

#else
#include "common/types.h"
#include "common/attributes.h"

// Make sure the macros above are aligned with these fields if changing them
struct handover_info_aarch64 {
    u64 arg0, arg1;
    u64 entrypoint;
    u64 stack;
    u64 direct_map_base;

    // Same for all ELs
    u64 ttbr0, ttbr1;
    u64 mair, tcr;
    u64 sctlr;

    bool unmap_lower_half;
};

NORETURN
void kernel_handover_aarch64(struct handover_info_aarch64 *hia);

u32 current_el(void);
u64 read_id_aa64mmfr0_el1(void);
u64 read_id_aa64mmfr1_el1(void);

u64 read_hcr_el2(void);
void write_hcr_el2(u64);
#endif
