#include "ELF.h"
#include "Structures.h"
#include "Allocator.h"

#define LOAD_ERROR(reason)                \
    do {                                  \
      load_result.success = false;        \
      load_result.error_message = reason; \
      return load_result;                 \
    } while (0)

namespace elf {

template <typename T>
static constexpr T higher_half_load_address_for_type()
{
    static_assert(sizeof(T) == 4 || sizeof(T) == 8);

    if constexpr (sizeof(T) == 4)
        return 0xC0000000;
    else
        return 0xFFFFFFFF80000000;
}

template <typename HeaderT, typename ProgramHeaderT, typename AddrT>
static LoadResult do_load(Span<u8> file, UseVirtualAddress use_va, AllocateAnywhere alloc_anywhere, unsigned char machine_type)
{
    LoadResult load_result {};
    auto& info = load_result.info;

    info.virtual_base = numeric_limits<u64>::max();

    auto* header = reinterpret_cast<HeaderT*>(file.data());
    info.entrypoint_address = header->e_entry;

    if (header->e_machine != machine_type)
        LOAD_ERROR("unexpected machine type");
    if (header->e_type != ET_EXEC)
        LOAD_ERROR("not an executable");
    if (!header->e_phnum || header->e_phnum == PN_XNUM)
        LOAD_ERROR("invalid number of program headers");

    auto ph_begin = header->e_phoff;
    auto ph_end = ph_begin + (header->e_phentsize * header->e_phnum);

    if (ph_end < ph_begin || header->e_phentsize < sizeof(ProgramHeaderT) || file.size() < ph_end)
        LOAD_ERROR("invalid program header offset/size");

    Address ph_address = reinterpret_cast<ProgramHeaderT*>(file.data() + ph_begin);
    bool va = use_va == UseVirtualAddress::YES;
    info.physical_valid = !va;
    bool must_be_higher_half = alloc_anywhere == AllocateAnywhere::YES;

    static constexpr auto higher_half_address = higher_half_load_address_for_type<AddrT>();

    for (size_t i = 0; i < header->e_phnum; ++i, ph_address += header->e_phentsize) {
        auto* program_header = ph_address.as_pointer<ProgramHeaderT>();

        if (program_header->p_type != PT_LOAD)
            continue;

        if (program_header->p_vaddr < higher_half_address && must_be_higher_half)
            LOAD_ERROR("invalid load address");

        if (program_header->p_vaddr < info.virtual_base)
            info.virtual_base = program_header->p_vaddr;

        auto end = program_header->p_vaddr + program_header->p_memsz;
        if (end > info.virtual_ceiling)
            info.virtual_ceiling = end;

        // Relocate entrypoint to be within the physical address base if needed
        if (!va && (info.entrypoint_address >= program_header->p_vaddr && info.entrypoint_address < end)) {
            info.entrypoint_address -= program_header->p_vaddr;
            info.entrypoint_address += program_header->p_paddr;
        }

        if (program_header->p_paddr < info.physical_base)
            info.physical_base = program_header->p_paddr;

        end = program_header->p_paddr + program_header->p_memsz;
        if (end > info.physical_ceiling)
            info.physical_ceiling = end;
    }

    auto& reference_base = va ? info.virtual_base : info.physical_base;
    auto& reference_ceiling = va ? info.virtual_ceiling : info.physical_ceiling;

    if ((info.entrypoint_address >= reference_ceiling) || (info.entrypoint_address < reference_base))
        LOAD_ERROR("invalid entrypoint");

    info.virtual_base = page_round_down(info.virtual_base);
    info.virtual_ceiling = page_round_up(info.virtual_ceiling);
    info.physical_base = page_round_down(info.physical_base);
    info.physical_ceiling = page_round_up(info.physical_ceiling);

    if (alloc_anywhere == AllocateAnywhere::YES) {
        auto pages = (info.virtual_ceiling - info.virtual_base) / page_size;
        info.physical_base = allocator::allocate_critical_pages(pages);
        info.physical_ceiling = info.physical_base + (pages * page_size);
        info.physical_valid = true;
    }

    for (size_t i = 0; i < header->e_phnum; ++i, ph_address += header->e_phentsize) {
        auto* program_header = ph_address.as_pointer<ProgramHeaderT>();

        if (program_header->p_type != PT_LOAD)
            continue;

        auto& address = va ? program_header->p_vaddr : program_header->p_paddr;

        if ((address + program_header->p_memsz) < address)
            LOAD_ERROR("invalid load address");

        auto ph_file_end = program_header->p_offset + program_header->p_filesz;

        if ((ph_file_end < program_header->p_offset) ||
            (program_header->p_memsz < program_header->p_filesz) ||
            (file.size() < ph_file_end))
            LOAD_ERROR("invalid program header");

        if (address >= higher_half_address) {
            if (!va)
                LOAD_ERROR("invalid load address");

            address -= higher_half_address;

            if ((address < (1 * MB)) && alloc_anywhere == AllocateAnywhere::NO)
                LOAD_ERROR("invalid load address");
        }

        Address64 load_base {};

        if (alloc_anywhere == AllocateAnywhere::NO) {
            auto begin = page_round_down(address);
            auto end = page_round_up(begin + program_header->p_memsz);

            if (end > (4ull * GB))
                LOAD_ERROR("invalid load address");

            auto pages = (end - begin) / page_size;
            load_base = allocator::allocate_critical_pages_with_type_at(begin, pages, MEMORY_TYPE_KERNEL_BINARY);
            load_base += address - begin;
        }  else {
            load_base = info.physical_base + (program_header->p_vaddr - info.virtual_base);
        }

        Address ph_file_data = file.data() + program_header->p_offset;

        if (program_header->p_filesz) {
            copy_memory(ph_file_data.as_pointer<void>(), load_base.as_pointer<void>(), program_header->p_filesz);
            load_base += program_header->p_filesz;
        }

        auto to_zero = program_header->p_memsz - program_header->p_filesz;
        if (to_zero)
            zero_memory(load_base.as_pointer<void>(), to_zero);
    }

    load_result.success = true;
    return load_result;
}

LoadResult load(Span<u8> file, UseVirtualAddress use_va, AllocateAnywhere alloc_anywhere)
{
    LoadResult load_result;

    if (alloc_anywhere == AllocateAnywhere::YES && use_va == UseVirtualAddress::NO)
        LOAD_ERROR("invalid load options");

    if (file.size() < sizeof(Elf32_Ehdr))
        LOAD_ERROR("file is too small");

    auto* header = reinterpret_cast<Elf32_Ehdr*>(file.data());

    static unsigned char elf_magic[] = { ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3 };
    if (!compare_memory(header, elf_magic, sizeof(elf_magic)))
        LOAD_ERROR("invalid magic");
    if (header->e_ident[EI_DATA] != ELFDATA2LSB)
        LOAD_ERROR("not a little-endian file");

    if (header->e_ident[EI_CLASS] == ELFCLASS64) {
        load_result = do_load<Elf64_Ehdr, Elf64_Phdr, Address64>(file, use_va, alloc_anywhere, EM_AMD64);
        load_result.info.bitness = 64;
        return load_result;
    }

    if (header->e_ident[EI_CLASS] == ELFCLASS32) {
        if (use_va == UseVirtualAddress::YES)
            LOAD_ERROR("invalid load options");

        load_result = do_load<Elf32_Ehdr, Elf32_Phdr, Address32>(file, use_va, alloc_anywhere, EM_386);
        load_result.info.bitness = 32;
        return load_result;
    }

    LOAD_ERROR("invalid class");
}

u32 get_bitness(Span<u8> file)
{
    if (file.size() < sizeof(Elf32_Ehdr))
        return 0;

    auto* hdr = reinterpret_cast<Elf32_Ehdr*>(file.data());

    switch (hdr->e_ident[EI_CLASS]) {
    case ELFCLASS32:
        return 32;
    case ELFCLASS64:
        return 64;
    default:
        return 0;
    }
}

}
