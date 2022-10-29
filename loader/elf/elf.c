#include "common/bug.h"
#include "common/align.h"
#include "common/string.h"
#include "common/log.h"
#include "common/minmax.h"

#include "filesystem/filesystem.h"
#include "structures.h"
#include "allocator.h"
#include "elf.h"

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
    struct elf_load_spec *spec;
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

static u64 data_alloc(u64 address, size_t pages,
                      const struct elf_load_spec *spec,
                      bool alloc_anywhere)
{
    struct allocation_spec as = {
        .ceiling = spec->binary_ceiling,
        .pages = pages,
        .flags = ALLOCATE_CRITICAL,
        .type = spec->memory_type,
    };

    if (!alloc_anywhere) {
        as.addr = address;
        as.flags |= ALLOCATE_PRECISE;
    }

    return allocate_pages_ex(&as);
}

static bool elf_io_take_ref(struct elf_io *io, void **out, size_t off,
                            size_t bytes, struct elf_error *err)
{
    if (!block_cache_take_ref(&io->hdr_cache, out, off, bytes))
        ELF_ERROR(err, "disk read error");

    return true;
}

static void elf_io_unref(struct elf_io *io)
{
    block_cache_release_ref(&io->hdr_cache);
}

static bool elf_get_ph_if_load(struct elf_load_ctx *ctx, size_t offset,
                               struct elf_load_ph *out_ph, bool *skip,
                               struct elf_error *err)
{
    size_t entsize = ctx->ph_info.entsize;
    struct elf_io *io = &ctx->spec->io;
    enum elf_arch arch = ctx->bi->arch;
    void *ph_data;

    if (!elf_io_take_ref(io, &ph_data, offset, entsize, err))
        return false;

    *skip = elf_get_ph_type(ph_data, arch) != PT_LOAD;
    if (*skip)
        goto out;

    elf_get_load_ph(ph_data, arch, out_ph);

out:
    elf_io_unref(io);
    return true;
}

static bool elf_do_load(struct elf_load_ctx *ctx)
{
    struct elf_binary_info *bi = ctx->bi;
    struct elf_ph_info *ph_info = &ctx->ph_info;
    struct elf_error *err = ctx->err;
    struct elf_load_spec *spec = ctx->spec;
    struct elf_io *io = &spec->io;

    u64 reference_base, reference_ceiling;
    size_t i, cur_off, pages;

    cur_off = ph_info->off;
    bi->virtual_base = -1ull;
    bi->physical_base = -1ull;

    for (i = 0; i < ph_info->count; ++i, cur_off += ph_info->entsize) {
        struct elf_load_ph hdr;
        u64 hdr_end;
        bool skip;

        if (!elf_get_ph_if_load(ctx, cur_off, &hdr, &skip, err))
            return false;

        if (skip)
            continue;

        if (hdr.virt_addr < spec->higher_half_base && ctx->alloc_anywhere)
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

        if (hdr.phys_addr >= spec->higher_half_base) {
            if (!ctx->use_va)
                ELF_ERROR_1(err, "invalid load address", hdr.phys_addr);

            hdr.phys_addr -= spec->higher_half_base;

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
    if (spec->binary_ceiling && bi->physical_ceiling > spec->binary_ceiling) {
        ELF_ERROR_2(err, "load address is above max", bi->physical_ceiling,
                                                      spec->binary_ceiling);
    }

    bi->physical_base = data_alloc(bi->physical_base, pages, spec,
                                   ctx->alloc_anywhere);
    if (ctx->alloc_anywhere) {
        bi->physical_ceiling = bi->physical_base;
        bi->physical_ceiling += pages * PAGE_SIZE;
    }

    cur_off = ph_info->off;
    for (i = 0; i < ph_info->count; ++i, cur_off += ph_info->entsize) {
        struct elf_load_ph hdr;
        bool skip;
        u64 addr, load_base, ph_file_end;
        u32 bytes_to_zero;

        if (!elf_get_ph_if_load(ctx, cur_off, &hdr, &skip, err))
            return false;

        if (skip)
            continue;

        addr = ctx->use_va ? hdr.virt_addr : hdr.phys_addr;

        if ((addr + hdr.memsz) < addr) {
            ELF_ERROR_2(err, "invalid load address/size combination",
                        addr, hdr.memsz);
        }

        ph_file_end = hdr.fileoff + hdr.filesz;

        if ((ph_file_end < hdr.fileoff) || (hdr.memsz < hdr.filesz)
            || (io->binary->size < ph_file_end))
        {
            ELF_ERROR_3(err, "invalid program header", hdr.fileoff,
                        hdr.filesz, hdr.memsz);
        }

        if (addr >= spec->higher_half_base)
            addr -= spec->higher_half_base;

        if (!ctx->alloc_anywhere) {
            load_base = addr;
        } else {
            load_base = bi->physical_base;
            load_base += hdr.virt_addr - bi->virtual_base;
        }

        if (hdr.filesz) {
            struct file *f = spec->io.binary;
            bool ok;

            ok = f->fs->read_file(f, (void*)((ptr_t)load_base),
                                  hdr.fileoff, hdr.filesz);
            if (!ok)
                ELF_ERROR(err, "disk read error");

            load_base += hdr.filesz;
        }

        bytes_to_zero = hdr.memsz - hdr.filesz;
        if (bytes_to_zero)
            memzero((void*)((ptr_t)load_base), bytes_to_zero);
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

static bool elf_do_get_arch(void *hdr, size_t file_size, enum elf_arch *arch,
                            struct elf_error *err)
{
    struct Elf32_Ehdr *ehdr = hdr;
    u8 ptr_width_expected, ptr_width;
    u8 ei_class;

    if (!elf_check_header(ehdr, err))
        return false;

    if (!is_valid_file_size(file_size))
        ELF_ERROR(err, "invalid file size");

    ei_class = ehdr->e_ident[EI_CLASS];

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

    switch (ehdr->e_machine) {
    case EM_386:
        ptr_width_expected = 4;
        *arch = ELF_ARCH_I386;
        break;
    case EM_AMD64:
        ptr_width_expected = 8;
        *arch = ELF_ARCH_AMD64;
        break;
    default:
        ELF_ERROR_1(err, "invalid machine type", ehdr->e_machine);
    }

    if (ptr_width != ptr_width_expected) {
        ELF_ERROR_2(err, "invalid EI_CLASS for machine type", ei_class,
                    ehdr->e_machine);
    }

    return true;
}

bool elf_get_arch(struct elf_io *io, enum elf_arch *arch, struct elf_error *err)
{
    void *hdr;
    bool ret;

    if (!elf_io_take_ref(io, (void**)&hdr, 0,
                         sizeof(struct Elf32_Ehdr), err))
        return false;

    ret = elf_do_get_arch(hdr, io->binary->size, arch, err);
    elf_io_unref(io);

    return ret;
}

static bool elf_check_ph_info(struct elf_load_ctx *ctx)
{
    struct elf_ph_info *info = &ctx->ph_info;
    struct elf_error *err = ctx->err;
    u64 size = ctx->spec->io.binary->size;
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
    struct elf_load_spec *spec = ctx->spec;
    struct elf_binary_info *info = ctx->bi;
    struct elf_io *io = &spec->io;
    bool ret = false;
    struct Elf64_Ehdr *hdr;

    ctx->use_va = spec->flags & ELF_USE_VIRTUAL_ADDRESSES;
    ctx->alloc_anywhere = spec->flags & ELF_ALLOCATE_ANYWHERE;

    if (!elf_io_take_ref(io, (void**)&hdr, 0, sizeof(*hdr), ctx->err))
        return false;

    if (!elf_do_get_arch(hdr, io->binary->size, &info->arch, ctx->err))
        goto out;

    switch (info->arch) {
    case ELF_ARCH_I386:
        ret = !ctx->alloc_anywhere;
        break;
    case ELF_ARCH_AMD64:
        ret = !(ctx->alloc_anywhere && !ctx->use_va);
        break;
    default:
        BUG();
    }

    elf_get_header_info(hdr, info->arch, &ctx->ph_info,
                        &info->entrypoint_address);

    if (!elf_check_ph_info(ctx))
        ret = false;

out:
    elf_io_unref(io);
    return ret;
}

bool elf_load(struct elf_load_spec *spec, struct elf_binary_info *bi,
              struct elf_error *err)
{
    struct elf_load_ctx ctx = {
        .spec = spec,
        .bi = bi,
        .err = err,
    };
    struct block_cache *hdr_cache = &spec->io.hdr_cache;
    bool ret;

    if (!block_cache_get_buf(hdr_cache) &&
        !elf_init_io_cache(&spec->io, err))
        return false;

    ret = elf_init_ctx(&ctx);
    if (!ret)
        goto out;

    ret = elf_do_load(&ctx);

out:
    block_cache_release(hdr_cache);
    return ret;
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

static bool elf_read_blocks_from_fs(void *file, void *buf, u64 block,
                                    size_t count)
{
    struct file *f = file;
    size_t byte_off, shift;
    u64 size_rem;

    shift = fs_block_shift(f->fs);
    byte_off = block << shift;
    count = count << shift;

    BUG_ON(f->size <= byte_off);
    size_rem = f->size - byte_off;
    count = MIN(count, size_rem);

    return f->fs->read_file(f, buf, byte_off, count);
}

bool elf_init_io_cache(struct elf_io *io, struct elf_error *err)
{
    struct file *bin = io->binary;
    struct filesystem *fs = bin->fs;
    void *cache_page;
    u8 fs_shift;
    size_t cache_size;

    fs_shift = fs_block_shift(fs);
    cache_size = MAX((unsigned int)PAGE_SIZE, 1ul << fs_shift);

    cache_page = allocate_bytes(cache_size);
    if (!cache_page)
        ELF_ERROR(err, "out of memory");

    block_cache_init(&io->hdr_cache, elf_read_blocks_from_fs,
                     bin, fs_shift, cache_page, cache_size >> fs_shift);
    block_cache_enable_direct_io(&io->hdr_cache);

    return true;
}
