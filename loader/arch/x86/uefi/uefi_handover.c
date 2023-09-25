#include "common/constants.h"
#include "common/rw_helpers.h"
#include "handover.h"
#include "services_impl.h"
#include "uefi/relocator.h"

extern char gdt_ptr[];

extern char gdt_struct_begin[];
extern char gdt_struct_end[];

extern char kernel_handover_x86_compat_code_begin[];
extern char kernel_handover_x86_compat_code_end[];

struct x86_handover_info {
    u64 arg0, arg1;
    u64 entrypoint;
    u64 stack;
    u64 direct_map_base;
    u32 compat_code_addr;
    u32 cr3, cr4;
    bool is_long_mode;
    bool unmap_lower_half;
} *xhi_relocated;
u32 kernel_handover_x86_compat_code_relocated;

/*
 * We drop down to protected mode to set the desired paging mode, so handover
 * code MUST be located below 4GiB. Make sure we never go above that.
 */
#define UEFI_HANDOVER_MAX_PHYS_ADDR (4ull * GB)

static struct relocation_entry relocations[] = {
    {
        .begin = gdt_struct_begin,
        .end = gdt_struct_end,
        .max_address = UEFI_HANDOVER_MAX_PHYS_ADDR,
        .user = gdt_ptr + 2,
        .cb = write_u64,
    },
    {
        .begin = kernel_handover_x86_compat_code_begin,
        .end = kernel_handover_x86_compat_code_end,
        .max_address = UEFI_HANDOVER_MAX_PHYS_ADDR,
        .user = &kernel_handover_x86_compat_code_relocated,
        .cb = write_u32_u64,
    },
    {
        .size = sizeof(*xhi_relocated),
        .max_address = UEFI_HANDOVER_MAX_PHYS_ADDR,
        .user = &xhi_relocated,
        .cb = write_u64,
    },
    {}
};

void handover_prepare_for(struct handover_info *hi)
{
    struct relocation_entry *re = relocations;

    /*
     * The higher half base for 32-bit kernels is definitely somewhere below
     * 4GiB, most likely around the 3GiB area. Make sure the handover code
     * lives in the physical memory range that fits direct-mapped higher
     * half area for those cases as well.
     */
    if (!(hi->flags & HO_X86_LME)) {
        u64 max_address = UEFI_HANDOVER_MAX_PHYS_ADDR - hi->direct_map_base;

        BUG_ON(!max_address || (max_address > UEFI_HANDOVER_MAX_PHYS_ADDR));

        while (re->max_address) {
            re->max_address = max_address;
            re++;
        }
    }

    relocate_entries(relocations);
}

NORETURN
void kernel_handover_x86(struct x86_handover_info *hi);

NORETURN
void kernel_handover(struct handover_info *hi)
{
    *xhi_relocated = (struct x86_handover_info) {
        .arg0 = hi->arg0,
        .arg1 = hi->arg1,
        .entrypoint = hi->entrypoint,
        .stack = hi->stack,
        .direct_map_base = hi->direct_map_base,
        .compat_code_addr = kernel_handover_x86_compat_code_relocated,
        .cr3 = hi->pt_root,
        .cr4 = handover_flags_to_cr4(hi->flags),
        .is_long_mode = hi->flags & HO_X86_LME,
        .unmap_lower_half = hi->flags & HO_HIGHER_HALF_ONLY
    };

    kernel_handover_x86(xhi_relocated);
}
