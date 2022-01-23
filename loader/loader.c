#include "services.h"
#include "common/log.h"
#include "common/panic.h"
#include "common/constants.h"
#include "common/helpers.h"
#include "common/string_view.h"
#include "allocator.h"
#include "filesystem/filesystem.h"
#include "filesystem/filesystem_table.h"
#include "protocols/ultra.h"
#include "config.h"

void detect_all_filesystems(struct disk_services*, const struct disk *d, u32 disk_index);
struct file *find_config_file(struct fs_entry **fe);
void pick_loadable_entry(struct config *cfg, struct loadable_entry *le);

enum load_protocol {
    LOAD_PROTOCOL_ULTRA,
    // Maybe multiboot/stivale in the future?
};
enum load_protocol deduce_protocol(struct config *cfg, struct loadable_entry*);

void loader_entry(struct services *svc)
{
    size_t disk_count, disk_index;
    struct disk *disks;
    struct fs_entry *fe;
    struct file *cfg_file;
    char *cfg_data;
    struct string_view cfg_view;
    struct config cfg = { 0 };
    struct loadable_entry le;
    enum load_protocol prot;

    logger_set_backend(svc->vs);
    allocator_set_backend(svc->ms);
    allocator_set_default_alloc_type(MEMORY_TYPE_LOADER_RECLAIMABLE);
    filesystem_set_backend(svc->ds);

    disks = svc->ds->list_disks(&disk_count);
    for (disk_index = 0; disk_index < disk_count; ++disk_index)
        detect_all_filesystems(svc->ds, &disks[disk_index], disk_index);

    cfg_file = find_config_file(&fe);
    if (!cfg_file)
        oops("Couldn't find hyper.cfg anywhere on disk!");

    set_origin_fs(fe);

    cfg_data = allocate_bytes(cfg_file->size);
    if (!cfg_data)
        oops("not enough memory to read config file");

    if (!cfg_file->read(cfg_file, cfg_data, 0, cfg_file->size))
        oops("failed to read config file");
    cfg_view = (struct string_view) {
        .text = cfg_data,
        .size = cfg_file->size
    };

    if (!cfg_parse(cfg_view, &cfg)) {
        cfg_pretty_print_error(&cfg.last_error, cfg_view);
        for (;;);
    }

    pick_loadable_entry(&cfg, &le);
    prot = deduce_protocol(&cfg, &le);

    BUG_ON(prot != LOAD_PROTOCOL_ULTRA);
    ultra_protocol_load(&cfg, &le, svc);
}

struct PACKED mbr_partition_entry {
    u8 status;
    u8 chs_begin[3];
    u8 type;
    u8 chs_end[3];
    u32 first_block;
    u32 block_count;
};
BUILD_BUG_ON(sizeof(struct mbr_partition_entry) != 16);

enum {
    MBR_EMPTY_PARTITION = 0x00,
    MBR_EBR_PARTITION   = 0x05
};

#define OFFSET_TO_MBR_PARTITION_LIST 0x01BE

void initialize_from_mbr(struct disk_services *srvc, const struct disk *d, u32 disk_id,
                         void *mbr_buffer, size_t base_index, size_t sector_offset)
{
    struct mbr_partition_entry *partition = mbr_buffer + OFFSET_TO_MBR_PARTITION_LIST;
    bool is_ebr = base_index != 0;
    size_t i, max_partitions = is_ebr ? 2 : 4;

    for (i = 0; i < max_partitions; ++i, ++partition) {
        size_t real_partition_offset = sector_offset + partition->first_block;
        struct range lba_range = { real_partition_offset, partition->block_count };
        void *partition_page;
        struct filesystem *fs = NULL;

        if (partition->type == MBR_EMPTY_PARTITION)
            continue;

        if (partition->type == MBR_EBR_PARTITION) {
            if (is_ebr && i == 0) {
                print_warn("EBR with chain at index 0");
                break;
            }

            partition_page = allocate_pages(1);
            if (!partition_page)
                break;

            if (srvc->read_blocks(d->handle, partition_page, real_partition_offset, PAGE_SIZE / d->bytes_per_sector)) {
                initialize_from_mbr(srvc, d, disk_id, partition_page, base_index + (is_ebr ? 1 : 4),
                                    real_partition_offset);
            }
            free_pages(partition_page, 1);
            continue;
        }

        if (i == 1 && is_ebr) {
            print_warn("EBR with a non-EBR entry at index 1 (0x%X)", partition->type);
            break;
        }

        partition_page = allocate_pages(1);
        if (!partition_page)
            break;

        if (srvc->read_blocks(d->handle, partition_page, lba_range.begin, PAGE_SIZE / d->bytes_per_sector))
            fs = fs_try_detect(d, lba_range, partition_page);

        if (fs)
            add_mbr_fs_entry(d->handle, disk_id, base_index + i, fs);

        free_pages(partition_page, 1);
    }
}

#define GPT_SIGNATURE "EFI PART"
#define OFFSET_TO_GPT_SIGNATURE 512

#define MBR_SIGNATURE 0xAA55
#define OFFSET_TO_MBR_SIGNATURE 510

void detect_all_filesystems(struct disk_services *srvc, const struct disk *d, u32 disk_id)
{
    void *table_page;

    // Currently unsupported
    if (d->bytes_per_sector != 512)
        return;

    table_page = allocate_pages(1);
    if (!table_page)
        return;
    if (!srvc->read_blocks(d->handle, table_page, 0, PAGE_SIZE / 512))
        return;

    if (!memcmp(table_page + OFFSET_TO_GPT_SIGNATURE, GPT_SIGNATURE, sizeof(GPT_SIGNATURE) - 1)) {
        print_warn("GPT-partitioned drive %p skipped\n", d->handle);
        return;
    }

    if (*(u16*)(table_page + OFFSET_TO_MBR_SIGNATURE) != MBR_SIGNATURE) {
        print_warn("unpartitioned drive %p skipped\n", d->handle);
        return;
    }

    initialize_from_mbr(srvc, d, disk_id, table_page, 0, 0);
    free_pages(table_page, 1);
}

static struct string_view search_paths[] = {
    SV_CONSTEXPR("/hyper.cfg"),
    SV_CONSTEXPR("/boot/hyper.cfg"),
    SV_CONSTEXPR("/boot/hyper/hyper.cfg"),
};

struct file *find_config_file(struct fs_entry **out_entry)
{
    struct fs_entry *entries;
    size_t i, j, entry_count;
    entries = list_fs_entries(&entry_count);

    for (i = 0; i < entry_count; ++i) {
        for (j = 0; j < ARRAY_SIZE(search_paths); ++j) {
            struct filesystem *fs = entries[i].fs;
            struct file *f = fs->open(fs, search_paths[j]);

            if (!f)
                continue;

            *out_entry = &entries[i];
            return f;
        }
    }

    return NULL;
}

#define DEFAULT_ENTRY_KEY SV("default-entry")

void pick_loadable_entry(struct config *cfg, struct loadable_entry *le)
{
    struct string_view loadable_entry_name;

    if (!cfg_get_global_string(cfg, DEFAULT_ENTRY_KEY, &loadable_entry_name)) {
        if (!cfg_first_loadable_entry(cfg, le))
            oops("configuration file must contain at least one loadable entry");
        return;
    }

    if (!cfg_get_loadable_entry(cfg, loadable_entry_name, le))
        oops("no loadable entry called %pSV", &loadable_entry_name);
}

#define PROTOCOL_KEY SV("protocol")

enum load_protocol deduce_protocol(struct config *cfg, struct loadable_entry *entry)
{
    struct string_view protocol_name;

    if (!cfg_get_string(cfg, entry, PROTOCOL_KEY, &protocol_name))
        return LOAD_PROTOCOL_ULTRA;
    if (!sv_equals_caseless(protocol_name, SV("ultra")))
        oops("unsupported load protocol: %pSV", &protocol_name);

    return LOAD_PROTOCOL_ULTRA;
}
