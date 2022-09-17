#include "elf.h"
#include "structures.h"
#include "allocator.h"
#include "common/align.h"
#include "common/string.h"

#define LOAD_ERROR(reason)     \
    do {                       \
      res->error_msg = reason; \
      return false;            \
    } while (0)

static Elf64_Half get_machine_type(const u8 *data, u8 bitness)
{
    if (bitness == 32)
        return ((struct Elf32_Ehdr*)data)->e_machine;

    return ((struct Elf64_Ehdr*)data)->e_machine;
}

static Elf64_Half get_binary_type(const u8 *data, u8 bitness)
{
    if (bitness == 32)
        return ((struct Elf32_Ehdr*)data)->e_type;

    return ((struct Elf64_Ehdr*)data)->e_type;
}

static Elf64_Half get_program_header_count(const u8 *data, u8 bitness)
{
    if (bitness == 32)
        return ((struct Elf32_Ehdr*)data)->e_phnum;

    return ((struct Elf64_Ehdr*)data)->e_phnum;
}

static Elf64_Addr get_entrypoint(const u8 *data, u8 bitness)
{
    if (bitness == 32)
        return ((struct Elf32_Ehdr*)data)->e_entry;

    return ((struct Elf64_Ehdr*)data)->e_entry;
}

static Elf64_Off get_ph_offset(const u8 *data, u8 bitness)
{
    if (bitness == 32)
        return ((struct Elf32_Ehdr*)data)->e_phoff;

    return ((struct Elf64_Ehdr*)data)->e_phoff;
}

static Elf64_Half get_ph_entry_size(const u8 *data, u8 bitness)
{
    if (bitness == 32)
        return ((struct Elf32_Ehdr*)data)->e_phentsize;

    return ((struct Elf64_Ehdr*)data)->e_phentsize;
}

static bool is_valid_ph_size(u32 size, u8 bitness)
{
    if (bitness == 32)
        return sizeof(struct Elf32_Ehdr) < size;

    return sizeof(struct Elf64_Ehdr) < size;
}

static bool is_valid_file_size(u32 size)
{
    return size > sizeof(struct Elf64_Ehdr);
}

static Elf64_Word get_ph_type(void *data, u8 bitness)
{
    if (bitness == 32)
        return ((struct Elf32_Phdr*)data)->p_type;

    return ((struct Elf64_Phdr*)data)->p_type;
}

struct load_ph {
    Elf64_Addr phys_addr, virt_addr;
    Elf64_Xword memsz, filesz;
    Elf64_Off fileoff;
};
static void get_load_ph(void *data, struct load_ph *out, u8 bitness)
{
    if (bitness == 32) {
        struct Elf32_Phdr *hdr = data;
        *out = (struct load_ph) {
            .phys_addr = hdr->p_paddr,
            .virt_addr = hdr->p_vaddr,
            .filesz = hdr->p_filesz,
            .memsz = hdr->p_memsz,
            .fileoff = hdr->p_offset
        };
    } else {
        struct Elf64_Phdr *hdr = data;
        *out = (struct load_ph) {
            .phys_addr = hdr->p_paddr,
            .virt_addr = hdr->p_vaddr,
            .filesz = hdr->p_filesz,
            .memsz = hdr->p_memsz,
            .fileoff = hdr->p_offset
        };
    }
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

static bool do_load(u8 *data, size_t size, bool use_va, bool alloc_anywhere,
                    Elf64_Half machine_type, u8 bitness, u32 binary_alloc_type,
                    struct load_result *res)
{
    struct binary_info *info = &res->info;
    Elf64_Half ph_count = get_program_header_count(data, bitness);
    Elf64_Half ph_size = get_ph_entry_size(data, bitness);
    Elf64_Off ph_begin = get_ph_offset(data, bitness);
    Elf64_Off ph_end = ph_begin + (get_ph_entry_size(data, bitness) * ph_count);
    void *ph_addr = data + ph_begin;
    u64 reference_base, reference_ceiling;
    bool must_be_higher_half = alloc_anywhere;
    size_t i, pages;

    info->virtual_base = -1ull;
    info->physical_base = -1ull;
    info->entrypoint_address = get_entrypoint(data, bitness);
    info->kernel_range_is_direct_map = !alloc_anywhere;

    if (get_machine_type(data, bitness) != machine_type)
        LOAD_ERROR("unexpected machine type");
    if (get_binary_type(data, bitness) != ET_EXEC)
        LOAD_ERROR("not an executable");
    if (!ph_count || ph_count == PN_XNUM)
        LOAD_ERROR("invalid number of program headers");
    if (ph_end < ph_begin || !is_valid_ph_size(size, bitness) || size < ph_end)
        LOAD_ERROR("invalid program header offset/size");

    for (i = 0; i < ph_count; ++i, ph_addr += ph_size) {
        struct load_ph hdr;
        u64 hdr_end;

        if (get_ph_type(ph_addr, bitness) != PT_LOAD)
            continue;

        get_load_ph(ph_addr, &hdr, bitness);

        if (hdr.virt_addr < HIGHER_HALF_BASE && must_be_higher_half)
            LOAD_ERROR("invalid load address");

        if (hdr.virt_addr < info->virtual_base)
            info->virtual_base = hdr.virt_addr;

        hdr_end = hdr.virt_addr + hdr.memsz;
        if (hdr_end > info->virtual_ceiling)
            info->virtual_ceiling = hdr_end;

        // Relocate entrypoint to be within the physical address base if needed
        if (!use_va && (info->entrypoint_address >= hdr.virt_addr && info->entrypoint_address < hdr_end)) {
            info->entrypoint_address -= hdr.virt_addr;
            info->entrypoint_address += hdr.phys_addr;
        }

        if (hdr.phys_addr >= HIGHER_HALF_BASE) {
            if (!use_va)
                LOAD_ERROR("invalid load address");

            hdr.phys_addr -= HIGHER_HALF_BASE;

            if ((hdr.phys_addr < (1 * MB)) && !alloc_anywhere)
                LOAD_ERROR("invalid load address");
        }

        if (hdr.phys_addr < info->physical_base)
            info->physical_base = hdr.phys_addr;

        hdr_end = hdr.phys_addr + hdr.memsz;
        if (hdr_end > info->physical_ceiling)
            info->physical_ceiling = hdr_end;
    }

    reference_base = use_va ? info->virtual_base : info->physical_base;
    reference_ceiling = use_va ? info->virtual_ceiling : info->physical_ceiling;

    if ((info->entrypoint_address >= reference_ceiling) || (info->entrypoint_address < reference_base))
        LOAD_ERROR("invalid entrypoint");

    info->virtual_base = PAGE_ROUND_DOWN(info->virtual_base);
    info->virtual_ceiling = PAGE_ROUND_UP(info->virtual_ceiling);
    info->physical_base = PAGE_ROUND_DOWN(info->physical_base);
    info->physical_ceiling = PAGE_ROUND_UP(info->physical_ceiling);

    pages = (info->virtual_ceiling - info->virtual_base) / PAGE_SIZE;
    info->physical_base = data_alloc(info->physical_base, pages,
                                     binary_alloc_type, alloc_anywhere);

    if (alloc_anywhere)
        info->physical_ceiling = info->physical_base + (pages * PAGE_SIZE);

    ph_addr = data + ph_begin;
    for (i = 0; i < ph_count; ++i, ph_addr += ph_size) {
        struct load_ph hdr;
        u64 addr, load_base;
        u64 ph_file_end;
        void *ph_file_data;
        u32 bytes_to_zero;

        if (get_ph_type(ph_addr, bitness) != PT_LOAD)
            continue;

        get_load_ph(ph_addr, &hdr, bitness);
        addr = use_va ? hdr.virt_addr : hdr.phys_addr;

        if ((addr + hdr.memsz) < addr)
            LOAD_ERROR("invalid load address");

        ph_file_end = hdr.fileoff + hdr.filesz;

        if ((ph_file_end < hdr.fileoff) ||
            (hdr.memsz < hdr.filesz) ||
            (size < ph_file_end))
            LOAD_ERROR("invalid program header");

        if (addr >= HIGHER_HALF_BASE)
            addr -= HIGHER_HALF_BASE;

        if (!alloc_anywhere) {
            load_base = addr;
        }  else {
            load_base = info->physical_base + (hdr.virt_addr - info->virtual_base);
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

u8 elf_bitness(void *data, size_t size)
{
    struct Elf32_Ehdr *hdr = data;

    if (!is_valid_file_size(size))
        return 0;

    switch (hdr->e_ident[EI_CLASS]) {
        case ELFCLASS32:
            return 32;
        case ELFCLASS64:
            return 64;
        default:
            return 0;
    }
}

bool elf_load(void *data, size_t size, bool use_va, bool alloc_anywhere, u32 binary_alloc_type, struct load_result *res)
{
    struct Elf32_Ehdr *hdr = data;
    res->info.bitness = elf_bitness(data, size);
    Elf64_Half machine_type = res->info.bitness == 64 ? EM_AMD64 : EM_386;

    if (!res->info.bitness)
        LOAD_ERROR("invalid elf class");
    if (res->info.bitness == 32 && (alloc_anywhere || use_va))
        LOAD_ERROR("invalid load options");
    if (alloc_anywhere && !use_va)
        LOAD_ERROR("invalid load options");

    static unsigned char elf_magic[] = { ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3 };
    if (memcmp(data, elf_magic, sizeof(elf_magic)) != 0)
        LOAD_ERROR("invalid magic");
    if (hdr->e_ident[EI_DATA] != ELFDATA2LSB)
        LOAD_ERROR("not a little-endian file");

    return do_load(data, size, use_va, alloc_anywhere, machine_type, res->info.bitness, binary_alloc_type, res);
}
