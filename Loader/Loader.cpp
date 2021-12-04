#include "Services.h"
#include "Common/Logger.h"
#include "Allocator.h"
#include "FileSystem/FileSystem.h"
#include "FileSystem/FileSystemTable.h"

void detect_all_filesystems(DiskServices&, const Disk&);
File* find_config_file(FileSystemEntry& out_entry);

void loader_entry(Services& services)
{
    logger::set_backend(&services.video_services());
    allocator::set_backend(&services.memory_services());
    FileSystem::set_backend(&services.disk_services());

    auto& disk_srvc = services.disk_services();

    for (const auto& disk : disk_srvc.list_disks())
        detect_all_filesystems(disk_srvc, disk);

    FileSystemEntry fs_entry {};
    File* config_file = find_config_file(fs_entry);

    if (!config_file)
        panic("couldn't find ultra.cfg anywhere on disk");

    auto* config_file_data = allocator::allocate_bytes(config_file->size());
    if (!config_file_data)
        panic("not enough memory to read config file");

    if (!config_file->read(config_file_data, 0, config_file->size()))
        panic("failed to read config file");

    for (;;);
}

void initialize_from_mbr(DiskServices& srvc, const Disk& disk, u32 disk_id, void* mbr_buffer, size_t base_index = 0, size_t sector_offset = 0)
{
    static constexpr u8 empty_partition_type = 0x00;
    static constexpr u8 ebr_partition_type = 0x05;

    struct PACKED MBRPartitionEntry {
        u8 status;
        u8 chs_begin[3];
        u8 type;
        u8 chs_end[3];
        u32 first_block;
        u32 block_count;
    };
    static_assert(sizeof(MBRPartitionEntry) == 16);

    static constexpr size_t offset_to_partitions = 0x01BE;
    Address mbr_address = mbr_buffer;
    mbr_address += offset_to_partitions;
    auto* partition = mbr_address.as_pointer<MBRPartitionEntry>();

    bool is_ebr = base_index != 0;
    size_t max_partitions = is_ebr ? 2 : 4;

    for (size_t i = 0; i < max_partitions; ++i, ++partition) {
        if (partition->type == empty_partition_type)
            continue;

        auto real_offset = sector_offset + partition->first_block;

        if (i == 1)
            warnln("index type 1 ({}), base {}", partition->type, base_index);

        if (partition->type == ebr_partition_type) {
            if (is_ebr && i == 0) {
                warnln("EBR with chain at index 0");
                break;
            }

            auto ebr_page = allocator::ScopedPageAllocation(1);
            if (ebr_page.failed())
                break;

            if (srvc.read_blocks(disk.handle, ebr_page.address(), real_offset, page_size / disk.bytes_per_sector))
                initialize_from_mbr(srvc, disk, disk_id, ebr_page.address(), base_index + (is_ebr ? 1 : 4), real_offset);

            continue;
        }

        if (i == 1 && is_ebr) {
            warnln("EBR with a non-EBR entry at index 1 ({})", partition->type);
            break;
        }

        allocator::ScopedPageAllocation first_partition_page(1);
        if (first_partition_page.failed())
            break;

        FileSystem* fs = nullptr;
        LBARange range { real_offset, partition->block_count };

        if (srvc.read_blocks(disk.handle, first_partition_page.address(), range.begin(), page_size / disk.bytes_per_sector))
            fs = FileSystem::try_detect(disk, range, first_partition_page.address());

        if (fs)
            fs_table::add_mbr_entry(disk.handle, disk_id, base_index + i, fs);
    }
}

void detect_all_filesystems(DiskServices& srvc, const Disk& disk, u32 disk_id)
{
    // Currently unsupported
    if (disk.bytes_per_sector != 512)
        return;

    allocator::ScopedPageAllocation first_page(1);
    if (first_page.failed())
        return;
    if (!srvc.read_blocks(disk.handle, first_page.address(), 0, page_size / 512))
        return;

    static constexpr StringView gpt_signature = "EFI PART";
    static constexpr size_t offset_to_gpt_signature = 512;

    auto* gpt_signature_data = first_page.as_pointer<const char>() + offset_to_gpt_signature;

    if (gpt_signature == gpt_signature_data) {
        warnln("GPT-partitioned drive {x} skipped", disk.handle);
        return;
    }

    static constexpr u16 mbr_signature = 0xAA55;
    static constexpr size_t offset_to_mbr_signature = 510;

    auto* mbr_signature_data = first_page.as_pointer<u16>() + (offset_to_mbr_signature / sizeof(u16));

    if (*mbr_signature_data != mbr_signature) {
        warnln("unpartitioned drive {x} skipped", disk.handle);
        return;
    }

    initialize_from_mbr(srvc, disk, disk_id, first_page.address());
}

File* find_config_file(FileSystemEntry& out_entry)
{
    StringView search_paths[] = {
        "/ultra.cfg",
        "/boot/ultra.cfg",
        "/boot/ultra/ultra.cfg",
        "/boot/Ultra/ultra.cfg",
        "/Boot/ultra.cfg",
        "/Boot/ultra/ultra.cfg",
        "/Boot/Ultra/ultra.cfg",
    };

    for (auto& entry : fs_table::all()) {
        for (auto path : search_paths) {
            auto* file = entry.filesystem->open(path);
            if (!file)
                continue;

            out_entry = entry;
            return file;
        }
    }

    return nullptr;
}
