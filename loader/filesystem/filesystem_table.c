#include "common/dynamic_buffer.h"
#include "services_impl.h"
#include "filesystem/filesystem_table.h"

static struct fs_entry origin_fs;
static struct dynamic_buffer entry_buf;

static struct fs_entry *pxe_entry;

void fst_init(void)
{
    dynamic_buffer_init(&entry_buf, sizeof(struct fs_entry), true);
}

static void fst_fini(void)
{
    size_t i;
    struct fs_entry *fse = entry_buf.buf;

    for (i = 0; i < entry_buf.size; ++i) {
        struct filesystem *fs = fse[i].fs;

        if (fs->release)
            fs->release(fs);
    }

    dynamic_buffer_release(&entry_buf);
}
DECLARE_CLEANUP_HANDLER(fst_fini);

void fst_add_pxe_fs_entry(struct filesystem *fs, ip_addr *ip)
{
    struct fs_entry *fse = dynamic_buffer_slot_alloc(&entry_buf);
    if (unlikely(!fse))
        return;

    // At this moment, we only expect one single PXE server per boot
    BUG_ON(pxe_entry);
    pxe_entry = fse;

    *fse = (struct fs_entry) {
        .loc = {
            .entry_type = FSE_TYPE_PXE,
            .ip = *ip,
        },
        .fs = fs,
    };
}

void fst_add_raw_fs_entry(const struct disk *d, struct filesystem *fs)
{
    struct fs_entry *fse = dynamic_buffer_slot_alloc(&entry_buf);
    if (unlikely(!fse))
        return;

    *fse = (struct fs_entry) {
        .disk_handle = d->handle,
        .loc = {
            .disk_id = d->id,
            .disk_kind = d->kind,
            .partition_index = 0,
            .entry_type = FSE_TYPE_RAW,
        },
        .fs = fs
    };
}

void fst_add_mbr_fs_entry(const struct disk *d, u32 partition_index, struct filesystem *fs)
{
    struct fs_entry *fse = dynamic_buffer_slot_alloc(&entry_buf);
    if (unlikely(!fse))
        return;

    *fse = (struct fs_entry) {
        .disk_handle = d->handle,
        .loc = {
            .disk_id = d->id,
            .disk_kind = d->kind,
            .partition_index = partition_index,
            .entry_type = FSE_TYPE_MBR,
        },
        .fs = fs
    };
}

void fst_add_gpt_fs_entry(const struct disk *d, u32 partition_index,
                          const struct guid *disk_guid, const struct guid *partition_guid,
                          struct filesystem *fs)
{
    struct fs_entry *fse = dynamic_buffer_slot_alloc(&entry_buf);
    if (unlikely(!fse))
        return;

    *fse = (struct fs_entry) {
        .disk_handle = d->handle,
        .loc = {
            .disk_id = d->id,
            .disk_kind = d->kind,
            .partition_index = partition_index,
            .entry_type = FSE_TYPE_GPT,
            .disk_guid = *disk_guid,
            .partition_guid = *partition_guid,
        },
        .fs = fs
    };
}

const struct fs_entry *fst_fs_by_full_path(const struct full_path *path)
{
    bool by_disk_index = false, by_disk_guid = false;
    bool by_partition_index = false, raw_partition = false;
    u32 disk_index = 0, partition_index = 0;
    u8 disk_kind = 0;
    size_t i;

    if (path->disk_id_type == DISK_IDENTIFIER_INVALID ||
        path->partition_id_type == PARTITION_IDENTIFIER_INVALID)
        return NULL;

    if (path->disk_id_type == DISK_IDENTIFIER_PXE)
        return pxe_entry;

    if (path->disk_id_type == DISK_IDENTIFIER_ORIGIN) {
        const struct fs_entry *origin = fst_get_origin();

        if (path->partition_id_type == PARTITION_IDENTIFIER_ORIGIN ||
            path->partition_id_type == PARTITION_IDENTIFIER_RAW)
            return origin;

        disk_index = origin->loc.disk_id;
        disk_kind = origin->loc.disk_kind;
        by_disk_index = true;
    } else if (path->disk_id_type == DISK_IDENTIFIER_HD ||
               path->disk_id_type == DISK_IDENTIFIER_CD) {
        disk_index = path->disk_index;
        disk_kind = path->disk_id_type == DISK_IDENTIFIER_CD ? DISK_KIND_CD
                                                             : DISK_KIND_HD;
        by_disk_index = true;
    } else if (path->disk_id_type == DISK_IDENTIFIER_UUID) {
        by_disk_guid = true;
    }
    // DISK_IDENTIFIER_ANY leaves both flags clear: match a partition on any disk.

    if (path->partition_id_type == PARTITION_IDENTIFIER_INDEX) {
        partition_index = path->partition_index;
        by_partition_index = true;
    } else if (path->partition_id_type == PARTITION_IDENTIFIER_RAW) {
        raw_partition = true;
    }

    for (i = 0; i < entry_buf.size; ++i) {
        struct fs_entry *entry = dynamic_buffer_get_slot(&entry_buf, i);

        if (entry->loc.entry_type == FSE_TYPE_PXE)
            continue;

        if (by_disk_index) {
            if (disk_index != entry->loc.disk_id ||
                disk_kind != entry->loc.disk_kind)
                continue;
        } else if (by_disk_guid &&
                   guid_compare(&path->disk_guid, &entry->loc.disk_guid)) {
            continue;
        }

        if (raw_partition)
            return (entry->loc.entry_type == FSE_TYPE_RAW) ? entry : NULL;

        if (by_partition_index) {
            if (partition_index != entry->loc.partition_index)
                continue;
        } else if (guid_compare(&path->partition_guid,
                                &entry->loc.partition_guid)) {
            continue;
        }

        return entry;
    }

    return NULL;
}

void fst_set_origin(struct fs_entry *entry)
{
    origin_fs = *entry;
}

const struct fs_entry *fst_get_origin()
{
    return &origin_fs;
}

struct fs_entry *fst_list(size_t *count)
{
    *count = entry_buf.size;
    return entry_buf.buf;
}
