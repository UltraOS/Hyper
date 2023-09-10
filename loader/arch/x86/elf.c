#include "elf/machine.h"
#include "elf/context.h"

bool elf_machine_to_arch(Elf32_Half machine, enum elf_arch *out_arch,
                        u8 *out_expected_ptr_width)
{
    bool out = true;

    switch (machine) {
    case EM_386:
        *out_expected_ptr_width = 4;
        *out_arch = ELF_ARCH_I386;
        break;
    case EM_AMD64:
        *out_expected_ptr_width = 8;
        *out_arch = ELF_ARCH_AMD64;
        break;
    default:
        out = false;
    }

    return out;
}

bool elf_is_supported_load_ctx(struct elf_load_ctx *ctx)
{
    struct elf_binary_info *info = ctx->bi;
    bool ret = false;

    switch (info->arch) {
    case ELF_ARCH_I386:
        ret = !ctx->alloc_anywhere;
        break;
    case ELF_ARCH_AMD64:
        ret = !(ctx->alloc_anywhere && !ctx->use_va);
        break;
    default:
        break;
    }

    return ret;
}
