#include "FileSystemTable.h"
#include "Allocator.h"

static constexpr size_t entries_per_page = sizeof(FileSystemEntry) / page_size;

static FileSystemEntry* g_buffer = nullptr;
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

void add_raw_entry(void* disk_handle, FileSystem* fs)
{
    if (!ensure_has_capacity())
        return;

    auto& entry = g_buffer[g_size++];

    entry.disk_handle = disk_handle;
    entry.filesystem = fs;
}

void add_mbr_entry(void* disk_handle, u32 partition_id, FileSystem* fs)
{
    if (!ensure_has_capacity())
        return;

    auto& entry = g_buffer[g_size++];

    entry.disk_handle = disk_handle;
    entry.partition_id = partition_id;
    entry.filesystem = fs;
}

void add_gpt_entry(void* disk_handle, u32 partition_id, const GUID& disk_guid, const GUID& partition_guid, FileSystem* fs)
{
    if (!ensure_has_capacity())
        return;

    auto& entry = g_buffer[g_size++];

    entry.disk_handle = disk_handle;
    entry.partition_id = partition_id;
    entry.disk_guid = disk_guid;
    entry.partition_guid = partition_guid;
    entry.filesystem = fs;
}

FileSystem* get_raw_entry(void* disk_handle)
{
    for (size_t i = 0; i < g_size; ++i) {
        auto& entry = g_buffer[i];

        if (entry.disk_handle != disk_handle)
            continue;

        return entry.filesystem;
    }

    return nullptr;
}

FileSystem* get_mbr_entry(void* disk_handle, u32 partition_id)
{
    for (size_t i = 0; i < g_size; ++i) {
        auto& entry = g_buffer[i];

        if (entry.disk_handle != disk_handle || entry.partition_id != partition_id)
            continue;

        return entry.filesystem;
    }

    return nullptr;
}

FileSystem* get_gpt_entry(void* disk_handle, u32 partition_id)
{
    return get_mbr_entry(disk_handle, partition_id);
}

FileSystem* get_gpt_entry(const GUID& disk_guid, u32 partition_id)
{
    for (size_t i = 0; i < g_size; ++i) {
        auto& entry = g_buffer[i];

        if (entry.disk_guid != disk_guid || entry.partition_id != partition_id)
            continue;

        return entry.filesystem;
    }

    return nullptr;
}

FileSystem* get_gpt_entry(const GUID& disk_guid, const GUID& partition_guid)
{
    for (size_t i = 0; i < g_size; ++i) {
        auto& entry = g_buffer[i];

        if (entry.disk_guid != disk_guid || entry.partition_guid != partition_guid)
            continue;

        return entry.filesystem;
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

}
