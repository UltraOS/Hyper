#include "elf/machine.h"
#include "elf/context.h"

bool elf_machine_to_arch(Elf32_Half machine,
                         enum elf_arch *out_arch,
                         u8 *out_expected_ptr_width)
{
    if (machine != EM_AARCH64)
        return false;

    *out_expected_ptr_width = 8;
    *out_arch = ELF_ARCH_AARCH64;
    return true;
}

bool elf_is_supported_load_ctx(struct elf_load_ctx *ctx)
{
    return !(ctx->alloc_anywhere && !ctx->use_va);
}
