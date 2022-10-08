#include "common/bug.h"
#include "common/align.h"
#include "common/string.h"
#include "common/log.h"

#include "elf.h"
#include "structures.h"
#include "allocator.h"

#define ELF_ERROR_N(err, reason_str, arg_0, arg_1, arg_2, cnt) \
    do {                                                       \
        (err)->reason = reason_str;                            \
        (err)->args[0] = arg_0;                                \
        (err)->args[1] = arg_1;                                \
        (err)->args[2] = arg_2;                                \
        (err)->arg_count = cnt;                                \
        return false;                                          \
    } while (0)

#define ELF_ERROR_3(err, reason_str, arg_0, arg_1, arg_2) \
    ELF_ERROR_N(err, reason_str, arg_0, arg_1, arg_2, 3)

#define ELF_ERROR_2(err, reason_str, arg_0, arg_1) \
    ELF_ERROR_N(err, reason_str, arg_0, arg_1, 0, 2)

#define ELF_ERROR_1(err, reason_str, arg_0) \
    ELF_ERROR_N(err, reason_str, arg_0, 0, 0, 1)

#define ELF_ERROR(err, reason_str) \
    ELF_ERROR_N(err, reason_str, 0, 0, 0, 0)

#define ARCH_STRUCT_VIEW(arch, data, type, action) \
    switch (arch) {                                \
    case ELF_ARCH_I386: {                          \
        const struct Elf32_##type *view = data;    \
        action                                     \
        break;                                     \
    }                                              \
    case ELF_ARCH_AMD64: {                         \
        const struct Elf64_##type *view = data;    \
        action                                     \
        break;                                     \
    }                                              \
    default:                                       \
        BUG();                                     \
}

struct elf_load_ph {
    Elf64_Addr phys_addr, virt_addr;
    Elf64_Xword memsz, filesz;
    Elf64_Off fileoff;
};

struct elf_ph_info {
    Elf64_Half count;
    Elf64_Half entsize;
    Elf64_Off off;
};

struct elf_load_ctx {
    const struct elf_load_spec *spec;
    bool alloc_anywhere;
    bool use_va;
    struct elf_ph_info ph_info;
    struct elf_binary_info *bi;
    struct elf_error *err;
};

static void elf_get_header_info(const void *data, enum elf_arch arch,
                                struct elf_ph_info *info, u64 *entrypoint)
{
    ARCH_STRUCT_VIEW(arch, data, Ehdr,
        info->count = view->e_phnum;
        info->entsize = view->e_phentsize;
        info->off = view->e_phoff;
        *entrypoint = view->e_entry;
    )
}

static void elf_get_load_ph(const void *data, enum elf_arch arch,
                            struct elf_load_ph *out)
{
    ARCH_STRUCT_VIEW(arch, data, Phdr,
        out->phys_addr = view->p_paddr;
        out->virt_addr = view->p_vaddr;
        out->filesz = view->p_filesz;
        out->memsz = view->p_memsz;
        out->fileoff = view->p_offset;
    )
}

static bool elf_is_valid_ph_size(u32 size, enum elf_arch arch)
{
    switch (arch) {
    case ELF_ARCH_I386:
        return sizeof(struct Elf32_Phdr) <= size;
    case ELF_ARCH_AMD64:
        return sizeof(struct Elf64_Phdr) <= size;
    default:
        BUG();
    }
}

static Elf64_Word elf_get_ph_type(void *data, enum elf_arch arch)
{
    ARCH_STRUCT_VIEW(arch, data, Phdr,
        return view->p_type;
    )
}

static bool is_valid_file_size(u32 size)
{
    return size > sizeof(struct Elf64_Ehdr);
}

static u64 data_alloc(u64 address, size_t pages, u32 type, bool alloc_anywhere)
{
    struct allocation_spec as = {
        .pages = pages,
        .flags = ALLOCATE_CRITICAL,
        .type = type
    };

    if (!alloc_anywhere) {
        as.addr = address;
        as.flags |= ALLOCATE_PRECISE;
    }

    return allocate_pages_ex(&as);
}

static bool elf_do_load(struct elf_load_ctx *ctx)
{
    struct elf_binary_info *bi = ctx->bi;
    struct elf_ph_info *ph_info = &ctx->ph_info;
    struct elf_error *err = ctx->err;
    const struct elf_load_spec *spec = ctx->spec;
    void *ph_addr, *data = spec->data;
    u64 reference_base, reference_ceiling;
    size_t i, pages;

    ph_addr = data + ph_info->off;
    bi->virtual_base = -1ull;
    bi->physical_base = -1ull;
    bi->kernel_range_is_direct_map = !ctx->alloc_anywhere;

    for (i = 0; i < ph_info->count; ++i, ph_addr += ph_info->entsize) {
        struct elf_load_ph hdr;
        u64 hdr_end;

        if (elf_get_ph_type(ph_addr, bi->arch) != PT_LOAD)
            continue;

        elf_get_load_ph(ph_addr, bi->arch, &hdr);

        if (hdr.virt_addr < HIGHER_HALF_BASE && ctx->alloc_anywhere)
            ELF_ERROR_1(err, "invalid load address", hdr.virt_addr);

        if (hdr.virt_addr < bi->virtual_base)
            bi->virtual_base = hdr.virt_addr;

        hdr_end = hdr.virt_addr + hdr.memsz;
        if (hdr_end > bi->virtual_ceiling)
            bi->virtual_ceiling = hdr_end;

        // Relocate entrypoint to be within the physical address base if needed
        if (!ctx->use_va &&
           (bi->entrypoint_address >= hdr.virt_addr &&
            bi->entrypoint_address < hdr_end))
        {
            bi->entrypoint_address -= hdr.virt_addr;
            bi->entrypoint_address += hdr.phys_addr;
        }

        if (hdr.phys_addr >= HIGHER_HALF_BASE) {
            if (!ctx->use_va)
                ELF_ERROR_1(err, "invalid load address", hdr.phys_addr);

            hdr.phys_addr -= HIGHER_HALF_BASE;

            if ((hdr.phys_addr < (1 * MB)) && !ctx->alloc_anywhere)
                ELF_ERROR_1(err, "invalid load address", hdr.phys_addr);
        }

        if (hdr.phys_addr < bi->physical_base)
            bi->physical_base = hdr.phys_addr;

        hdr_end = hdr.phys_addr + hdr.memsz;
        if (hdr_end > bi->physical_ceiling)
            bi->physical_ceiling = hdr_end;
    }

    reference_base = ctx->use_va ? bi->virtual_base : bi->physical_base;
    reference_ceiling = ctx->use_va ? bi->virtual_ceiling :
                                      bi->physical_ceiling;

    if ((bi->entrypoint_address >= reference_ceiling) ||
        (bi->entrypoint_address < reference_base))
        ELF_ERROR_1(err, "invalid entrypoint address", bi->entrypoint_address);

    bi->virtual_base = PAGE_ROUND_DOWN(bi->virtual_base);
    bi->virtual_ceiling = PAGE_ROUND_UP(bi->virtual_ceiling);
    bi->physical_base = PAGE_ROUND_DOWN(bi->physical_base);
    bi->physical_ceiling = PAGE_ROUND_UP(bi->physical_ceiling);

    pages = (bi->virtual_ceiling - bi->virtual_base) / PAGE_SIZE;
    bi->physical_base = data_alloc(bi->physical_base, pages, spec->memory_type,
                                   ctx->alloc_anywhere);
    if (ctx->alloc_anywhere) {
        bi->physical_ceiling = bi->physical_base;
        bi->physical_ceiling += pages * PAGE_SIZE;
    }

    ph_addr = data + ph_info->off;
    for (i = 0; i < ph_info->count; ++i, ph_addr += ph_info->entsize) {
        struct elf_load_ph hdr;
        u64 addr, load_base;
        u64 ph_file_end;
        void *ph_file_data;
        u32 bytes_to_zero;

        if (elf_get_ph_type(ph_addr, bi->arch) != PT_LOAD)
            continue;

        elf_get_load_ph(ph_addr, bi->arch, &hdr);
        addr = ctx->use_va ? hdr.virt_addr : hdr.phys_addr;

        if ((addr + hdr.memsz) < addr) {
            ELF_ERROR_2(err, "invalid load address/size combination",
                        addr, hdr.memsz);
        }

        ph_file_end = hdr.fileoff + hdr.filesz;

        if ((ph_file_end < hdr.fileoff) || (hdr.memsz < hdr.filesz)
            || (spec->size < ph_file_end))
        {
            ELF_ERROR_3(err, "invalid program header", hdr.fileoff,
                        hdr.filesz, hdr.memsz);
        }

        if (addr >= HIGHER_HALF_BASE)
            addr -= HIGHER_HALF_BASE;

        if (!ctx->alloc_anywhere) {
            load_base = addr;
        } else {
            load_base = bi->physical_base;
            load_base += hdr.virt_addr - bi->virtual_base;
        }

        ph_file_data = data + hdr.fileoff;

        if (hdr.filesz) {
            memcpy((void*)((ptr_t)load_base), ph_file_data, hdr.filesz);
            load_base += hdr.filesz;
        }

        bytes_to_zero = hdr.memsz - hdr.filesz;
        if (bytes_to_zero)
            memzero((void*)((ptr_t)load_base), bytes_to_zero);
    }

    return true;
}

bool elf_get_arch(void *hdr_data, size_t size,
                  enum elf_arch *arch, struct elf_error *err)
{
    struct Elf32_Ehdr *hdr = hdr_data;
    u8 ptr_width_expected = 0, ptr_width = 0;
    u8 ei_class;

    if (!is_valid_file_size(size))
        ELF_ERROR(err, "invalid file size");

    ei_class = hdr->e_ident[EI_CLASS];

    switch (ei_class) {
    case ELFCLASS32:
        ptr_width = 4;
        break;
    case ELFCLASS64:
        ptr_width = 8;
        break;
    default:
        ELF_ERROR_1(err, "invalid EI_CLASS", ei_class);
    }

    switch (hdr->e_machine) {
    case EM_386:
        ptr_width_expected = 4;
        *arch = ELF_ARCH_I386;
        break;
    case EM_AMD64:
        ptr_width_expected = 8;
        *arch = ELF_ARCH_AMD64;
        break;
    default:
        ELF_ERROR_1(err, "invalid machine type", hdr->e_machine);
    }

    if (!ptr_width || ptr_width != ptr_width_expected) {
        ELF_ERROR_2(err, "invalid EI_CLASS for machine type", ei_class,
                    hdr->e_machine);
    }

    return true;
}

static bool elf_check_header(struct Elf32_Ehdr *hdr, struct elf_error *err)
{
    static unsigned char elf_magic[] = { ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3 };

    if (memcmp(&hdr->e_ident, elf_magic, sizeof(elf_magic)) != 0)
        ELF_ERROR(err, "invalid magic");
    if (hdr->e_ident[EI_DATA] != ELFDATA2LSB)
        ELF_ERROR_1(err, "not a little-endian file", hdr->e_ident[EI_DATA]);
    if (hdr->e_type != ET_EXEC)
        ELF_ERROR_1(err, "not an executable type", hdr->e_type);

    return true;
}

static bool elf_check_ph_info(struct elf_load_ctx *ctx)
{
    struct elf_ph_info *info = &ctx->ph_info;
    struct elf_error *err = ctx->err;
    size_t size = ctx->spec->size;
    u64 end;

    if (!info->count || info->count == PN_XNUM)
        ELF_ERROR_1(err, "invalid number of program headers", info->count);

    end = info->off + (info->entsize * info->count);

    if (!elf_is_valid_ph_size(info->entsize, ctx->bi->arch))
        ELF_ERROR_1(err, "invalid program header entsize", info->entsize);
    if (end < info->off || size < end) {
        ELF_ERROR_2(err, "invalid program header offset/count combination",
                    info->off, info->count);
    }

    return true;
}

static bool elf_init_ctx(struct elf_load_ctx *ctx)
{
    const struct elf_load_spec *spec = ctx->spec;
    struct elf_binary_info *info = ctx->bi;
    bool options_are_invalid = false;

    if (!elf_check_header(spec->data, ctx->err))
        return false;

    ctx->use_va = spec->flags & ELF_USE_VIRTUAL_ADDRESSES;
    ctx->alloc_anywhere = spec->flags & ELF_ALLOCATE_ANYWHERE;

    if (!elf_get_arch(spec->data, spec->size, &info->arch, ctx->err))
        return false;

    switch (info->arch) {
    case ELF_ARCH_I386:
        options_are_invalid |= ctx->alloc_anywhere || ctx->use_va;
        break;
    case ELF_ARCH_AMD64:
        options_are_invalid = ctx->alloc_anywhere && !ctx->use_va;
        break;
    default:
        BUG();
    }

    elf_get_header_info(spec->data, info->arch, &ctx->ph_info,
                        &info->entrypoint_address);

    if (!elf_check_ph_info(ctx))
        return false;

    return !options_are_invalid;
}

bool elf_load(const struct elf_load_spec *spec, struct elf_binary_info *bi,
              struct elf_error *err)
{
    struct elf_load_ctx ctx = {
        .spec = spec,
        .bi = bi,
        .err = err,
    };

    if (!elf_init_ctx(&ctx))
        return false;

    return elf_do_load(&ctx);
}

void elf_pretty_print_error(const struct elf_error *err, const char *prefix)
{
    size_t i;
    const char *reason = err->reason ?: "no error";

    if (!prefix)
        prefix = "ELF error";

    print_err("%s: %s", prefix, reason);

    for (i = 0; i < err->arg_count; ++i)
        print_err(" 0x%016llX", err->args[i]);

    print_err("\n");
}
