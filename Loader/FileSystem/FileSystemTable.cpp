#include "FileSystemTable.h"
#include "Allocator.h"

static constexpr size_t entries_per_page = sizeof(FileSystemEntry) / page_size;
static constexpr u32 raw_partition_index = numeric_limits<u32>::max();

static FileSystemEntry* g_buffer = nullptr;
static FileSystemEntry g_origin;
static size_t g_capacity = 0;
static size_t g_size = 0;

namespace fs_table {

static bool ensure_has_capacity()
{
    if (g_size < g_capacity)
        return true;

    auto new_capacity = g_capacity + entries_per_page;
    auto* new_buffer = allocator::allocate_new_array<FileSystemEntry>(new_capacity);
    if (!new_buffer)
        return false;

    if (g_size)
        copy_memory(g_buffer, new_buffer, g_size * sizeof(FileSystemEntry));
    if (g_buffer)
        allocator::free_array(g_buffer, g_capacity);

    g_buffer = new_buffer;
    g_capacity = new_capacity;

    return true;
}

void add_raw_entry(void* disk_handle, u32 disk_index, FileSystem* fs)
{
    if (!ensure_has_capacity())
        return;

    auto& entry = g_buffer[g_size++];

    entry.disk_index = disk_index;
    entry.disk_handle = disk_handle;
    entry.partition_index = raw_partition_index;
    entry.filesystem = fs;
}

void add_mbr_entry(void* disk_handle, u32 disk_index, u32 partition_index, FileSystem* fs)
{
    if (!ensure_has_capacity())
        return;

    auto& entry = g_buffer[g_size++];

    entry.disk_handle = disk_handle;
    entry.disk_index = disk_index;
    entry.partition_index = partition_index;
    entry.filesystem = fs;
}

void add_gpt_entry(void* disk_handle, u32 disk_index, u32 partition_index,
                   const GUID& disk_guid, const GUID& partition_guid, FileSystem* fs)
{
    if (!ensure_has_capacity())
        return;

    auto& entry = g_buffer[g_size++];

    entry.disk_handle = disk_handle;
    entry.disk_index = disk_index;
    entry.partition_index = partition_index;
    entry.disk_guid = disk_guid;
    entry.partition_guid = partition_guid;
    entry.filesystem = fs;
}

const FileSystemEntry* get_by_full_path(const FullPath& path)
{
    if (path.disk_id_type == FullPath::DiskIdentifier::INVALID ||
        path.partition_id_type == FullPath::PartitionIdentifier::INVALID)
        return nullptr;

    bool by_disk_index = false;
    u32 disk_index = 0;

    bool by_partition_index = false;
    bool raw_partition = false;
    u32 partition_index = 0;

    if (path.disk_id_type == FullPath::DiskIdentifier::ORIGIN) {
        if (path.partition_id_type == FullPath::PartitionIdentifier::ORIGIN ||
            path.partition_id_type == FullPath::PartitionIdentifier::RAW)
            return &get_origin();

        disk_index = get_origin().disk_index;
        by_disk_index = true;
    } else if (path.disk_id_type == FullPath::DiskIdentifier::INDEX) {
        disk_index = path.disk_index;
        by_disk_index = true;
    }

    if (path.partition_id_type == FullPath::PartitionIdentifier::MBR_INDEX ||
        path.partition_id_type == FullPath::PartitionIdentifier::GPT_INDEX) {
        partition_index = path.partition_index;
        by_partition_index = true;
    } else if (path.partition_id_type == FullPath::PartitionIdentifier::RAW) {
        raw_partition = true;
    }

    for (size_t i = 0; i < g_size; ++i) {
        auto& entry = g_buffer[i];

        if (by_disk_index) {
            if (disk_index != entry.disk_index)
                continue;
        } else if (path.disk_guid != entry.disk_guid) {
            continue;
        }

        if (raw_partition)
            return (entry.partition_index == raw_partition_index) ? &entry : nullptr;

        if (by_partition_index) {
            if (partition_index != entry.partition_index)
                continue;
        } else if (path.partition_guid != entry.partition_guid) {
            continue;
        }

        return &entry;
    }

    return nullptr;
}

FileSystemEntry* all::begin()
{
    return g_buffer;
}

FileSystemEntry* all::end()
{
    return g_buffer + g_size;
}

void set_origin(FileSystemEntry& entry)
{
    g_origin = entry;
}

const FileSystemEntry& get_origin()
{
    return g_origin;
}

}
