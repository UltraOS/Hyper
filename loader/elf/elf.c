#include "elf.h"
#include "structures.h"
#include "allocator.h"
#include "common/constants.h"
#include "common/string.h"

#define LOAD_ERROR(reason)     \
    do {                       \
      res->success = false;    \
      res->error_msg = reason; \
      return;                  \
    } while (0)

#define HIGHER_HALF_BASE 0xFFFFFFFF80000000

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

static void do_load(u8 *data, size_t size, bool use_va, bool alloc_anywhere,
                    Elf64_Half machine_type, u8 bitness, struct load_result *res)
{
    struct binary_info *info = &res->info;
    Elf64_Half ph_count = get_program_header_count(data, bitness);
    Elf64_Half ph_size = get_ph_entry_size(data, bitness);
    Elf64_Off ph_begin = get_ph_offset(data, bitness);
    Elf64_Off ph_end = ph_begin + (get_ph_entry_size(data, bitness) * ph_count);
    void *ph_addr = data + ph_begin;
    u64 reference_base, reference_ceiling;
    bool must_be_higher_half = alloc_anywhere;
    size_t i;

    info->virtual_base = -1ull;
    info->entrypoint_address = get_entrypoint(data, bitness);
    info->physical_valid = !use_va;

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

    if (alloc_anywhere) {
        size_t pages = (info->virtual_ceiling - info->virtual_base) / PAGE_SIZE;
        info->physical_base = (u64)allocate_critical_pages(pages);
        info->physical_ceiling = info->physical_base + (pages * PAGE_SIZE);
        info->physical_valid = true;
    }

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

        if (addr >= HIGHER_HALF_BASE) {
            if (!use_va)
                LOAD_ERROR("invalid load address");

            addr -= HIGHER_HALF_BASE;

            if ((addr < (1 * MB)) && !alloc_anywhere)
                LOAD_ERROR("invalid load address");
        }

        if (!alloc_anywhere) {
            u64 begin = PAGE_ROUND_DOWN(addr);
            u64 end = PAGE_ROUND_UP(begin + hdr.memsz);
            size_t pages = (end - begin) / PAGE_SIZE;

            if (end > (4ull * GB))
                LOAD_ERROR("invalid load address");

            load_base = (u64)allocate_critical_pages_with_type_at(begin, pages, MEMORY_TYPE_KERNEL_BINARY);
            load_base += addr - begin;
        }  else {
            load_base = info->physical_base + (hdr.virt_addr - info->virtual_base);
        }

        ph_file_data = data + hdr.fileoff;

        if (hdr.filesz) {
            memcpy((void*)load_base, ph_file_data, hdr.filesz);
            load_base += hdr.filesz;
        }

        bytes_to_zero = hdr.memsz - hdr.filesz;
        if (bytes_to_zero)
            memzero(load_base, bytes_to_zero);
    }

    res->success = true;
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

void load(void *data, size_t size, bool use_va, bool alloc_anywhere, struct load_result *res)
{
    struct Elf32_Ehdr *hdr = data;
    u8 bitness = elf_bitness(data, size);
    Elf64_Half machine_type = bitness == 64 ? EM_386 : EM_AMD64;

    if (!bitness)
        LOAD_ERROR("invalid elf class");
    if (alloc_anywhere && use_va)
        LOAD_ERROR("invalid load options");

    static unsigned char elf_magic[] = { ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3 };
    if (!memcmp(data, elf_magic, sizeof(elf_magic)))
        LOAD_ERROR("invalid magic");
    if (hdr->e_ident[EI_DATA] != ELFDATA2LSB)
        LOAD_ERROR("not a little-endian file");

    do_load(data, size, use_va, alloc_anywhere, machine_type, bitness, res);
}
