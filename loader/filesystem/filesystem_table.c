#include "filesystem_table.h"
#include "allocator.h"
#include "common/constants.h"

#define ENTRIES_PER_PAGE (PAGE_SIZE / sizeof(struct fs_entry))
#define RAW_PARTITION_IDX 0xFFFFFFFF

static struct fs_entry *entry_buffer;
static struct fs_entry origin_fs;
static size_t entry_buffer_capacity = 0;
static size_t entry_buffer_size = 0;

static bool ensure_has_capacity()
{
    size_t new_capacity;
    void *new_buffer;

    if (entry_buffer_size < entry_buffer_capacity)
        return true;

    new_capacity = entry_buffer_capacity + ENTRIES_PER_PAGE;
    new_buffer = allocate_bytes(new_capacity * sizeof(struct fs_entry));
    if (!new_buffer)
        return false;

    if (entry_buffer_size)
        memcpy(new_buffer, entry_buffer, entry_buffer_size * sizeof(struct fs_entry));
    if (entry_buffer)
        free_bytes(entry_buffer, entry_buffer_capacity * sizeof(struct fs_entry));

    entry_buffer = new_buffer;
    entry_buffer_capacity = new_capacity;

    return true;
}

void add_raw_fs_entry(void *disk_handle, u32 disk_index, struct filesystem *fs)
{
    if (!ensure_has_capacity())
        return;

    entry_buffer[entry_buffer_size++] = (struct fs_entry) {
        .disk_index = disk_index,
        .disk_handle = disk_handle,
        .partition_index = RAW_PARTITION_IDX,
        .fs = fs
    };
}

void add_mbr_fs_entry(void* disk_handle, u32 disk_index, u32 partition_index, struct filesystem *fs)
{
    if (!ensure_has_capacity())
        return;

    entry_buffer[entry_buffer_size++] = (struct fs_entry) {
        .disk_handle = disk_handle,
        .disk_index = disk_index,
        .partition_index = partition_index,
        .fs = fs
    };
}

void add_gpt_fs_entry(void* disk_handle, u32 disk_index, u32 partition_index,
                      const struct guid *disk_guid, const struct guid *partition_guid,
                      struct filesystem *fs)
{
    if (!ensure_has_capacity())
        return;

    entry_buffer[entry_buffer_size++] = (struct fs_entry) {
        .disk_handle = disk_handle,
        .disk_index = disk_index,
        .partition_index = partition_index,
        .disk_guid = *disk_guid,
        .partition_guid = *partition_guid,
        .fs = fs
    };
}

const struct fs_entry *fs_by_full_path(const struct full_path *path)
{
    bool by_disk_index = false, by_partition_index = false, raw_partition = false;
    u32 disk_index = 0, partition_index = 0;

    if (path->disk_id_type == DISK_IDENTIFIER_INVALID ||
        path->partition_id_type == PARTITION_IDENTIFIER_INVALID)
        return NULL;

    if (path->disk_id_type == DISK_IDENTIFIER_ORIGIN) {
        if (path->partition_id_type == PARTITION_IDENTIFIER_ORIGIN ||
            path->partition_id_type == PARTITION_IDENTIFIER_RAW)
            return get_origin_fs();

        disk_index = get_origin_fs()->disk_index;
        by_disk_index = true;
    } else if (path->disk_id_type == DISK_IDENTIFIER_INDEX) {
        disk_index = path->disk_index;
        by_disk_index = true;
    }

    if (path->partition_id_type == PARTITION_IDENTIFIER_MBR_INDEX ||
        path->partition_id_type == PARTITION_IDENTIFIER_GPT_INDEX) {
        partition_index = path->partition_index;
        by_partition_index = true;
    } else if (path->partition_id_type == PARTITION_IDENTIFIER_RAW) {
        raw_partition = true;
    }

    for (size_t i = 0; i < entry_buffer_size; ++i) {
        struct fs_entry *entry = &entry_buffer[i];

        if (by_disk_index) {
            if (disk_index != entry->disk_index)
                continue;
        } else if (guid_compare(&path->disk_guid, &entry->disk_guid)) {
            continue;
        }

        if (raw_partition)
            return (entry->partition_index == RAW_PARTITION_IDX) ? entry : NULL;

        if (by_partition_index) {
            if (partition_index != entry->partition_index)
                continue;
        } else if (guid_compare(&path->partition_guid, &entry->partition_guid)) {
            continue;
        }

        return entry;
    }

    return NULL;
}

void set_origin_fs(struct fs_entry *entry)
{
    origin_fs = *entry;
}

const struct fs_entry *get_origin_fs()
{
    return &origin_fs;
}

struct fs_entry *list_fs_entries(size_t *count)
{
    *count = entry_buffer_size;
    return entry_buffer;
}
