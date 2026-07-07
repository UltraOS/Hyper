#include "services.h"
#include "disk_services.h"
#include "common/log.h"
#include "common/string_view.h"
#include "allocator.h"
#include "filesystem/filesystem.h"
#include "filesystem/filesystem_table.h"
#include "config.h"
#include "boot_protocol.h"

void init_all_disks(void);
void init_config(struct config *out_cfg);

struct file *find_config_file(struct fs_entry **fe);
void pick_loadable_entry(struct config *cfg, struct loadable_entry *le);

void loader_entry(void)
{
    struct config cfg = { 0 };
    struct loadable_entry le;

    logger_init();

    fst_init();

    init_all_disks();
    init_config(&cfg);

    pick_loadable_entry(&cfg, &le);
    boot(&cfg, &le);
}

// TODO: support chain-loading configs
void init_config(struct config *out_cfg)
{
    struct fs_entry *fe;
    struct file *cfg_file;
    char *cfg_data;
    struct config_source cfg_src;

    cfg_file = find_config_file(&fe);
    if (!cfg_file)
        oops("Couldn't find hyper.cfg anywhere on disk!\n");

    fst_set_origin(fe);
    cfg_data = allocate_critical_bytes(cfg_file->size);

    if (!cfg_file->fs->read_file(cfg_file, cfg_data, 0, cfg_file->size))
        oops("failed to read config file\n");

    cfg_src = (struct config_source) {
        .text = cfg_data,
        .size = cfg_file->size
    };

    cfg_file->fs->close_file(cfg_file);

    if (!cfg_parse(cfg_src, out_cfg)) {
        cfg_pretty_print_error(out_cfg);
        loader_abort();
    }
}

void init_all_disks(void)
{
    size_t disk_index;
    u32 disk_count;
    struct block_cache bc;
    void *buf;

    buf = allocate_pages(1);
    if (unlikely(!buf))
        return;

    disk_count = ds_get_disk_count();

    for (disk_index = 0; disk_index < disk_count; ++disk_index) {
        struct disk d;
        ds_query_disk(disk_index, &d);

        block_cache_init(&bc, &ds_read_blocks, d.handle, d.block_shift,
                         buf, PAGE_SIZE >> d.block_shift);

        fs_detect_all(&d, &bc);
    }

    free_pages(buf, 1);

    fs_detect_pxe();

    /*
     * Now that all existing filesystems are detected, attempt to find our boot
     * partition, it will take priority when we scan for the bootloader config
     * file later on
     */
    fst_resolve_boot_entry();
}

static struct string_view search_paths[] = {
    SV_CONSTEXPR("/hyper.cfg"),
    SV_CONSTEXPR("/boot/hyper.cfg"),
    SV_CONSTEXPR("/boot/hyper/hyper.cfg"),
};

static struct file *config_from_entry(struct fs_entry *entry)
{
    size_t j;

    for (j = 0; j < ARRAY_SIZE(search_paths); ++j) {
        struct file *f = path_open(entry->fs, search_paths[j]);
        if (f)
            return f;
    }

    return NULL;
}

static void print_boot_device(const struct boot_device_info *bd,
                              const struct fs_entry *boot_entry)
{
    const char *kind;

    if (bd == NULL) {
        print_warn("Unable to determine boot device, will scan all disks\n");
        return;
    }

    if (bd->type == BOOT_DEVICE_TYPE_PXE) {
        print_info("Booted via network (PXE)\n");
        return;
    }

    kind = bd->disk_kind == DISK_KIND_CD ? "cd" : "hd";
    if (boot_entry == NULL || boot_entry->loc.entry_type == FSE_TYPE_RAW) {
        print_info("Boot device is %s%u\n", kind, bd->disk_id);
        return;
    }

    print_info("Boot device is %s%u, partition %u\n", kind, bd->disk_id,
               boot_entry->loc.partition_index);
}

struct file *find_config_file(struct fs_entry **out_entry)
{
    struct fs_entry *entries, *boot_entry;
    const struct boot_device_info *bdi;
    size_t i, entry_count;
    struct file *f;

    entries = fst_list(&entry_count);
    bdi = fst_boot_device_info();
    boot_entry = fst_boot_entry();

    print_boot_device(bdi, boot_entry);

    /*
     * If we were able to resolve the exact partition we have booted from,
     * it obviously takes the priority and we attempt to find a configuration
     * file on there first.
     */
    if (boot_entry) {
        f = config_from_entry(boot_entry);
        if (f) {
            *out_entry = boot_entry;
            return f;
        }
    }

    /*
     * Otherwise prefer any filesystem on the device we booted from, still ahead
     * of whichever one happened to be enumerated first.
     */
    if (bdi != NULL) {
        bool searched_boot_device = boot_entry != NULL;

        for (i = 0; i < entry_count; ++i) {
            // Already probed above
            if (&entries[i] == boot_entry)
                continue;
            if (!fst_entry_on_boot_device(&entries[i]))
                continue;

            searched_boot_device = true;

            f = config_from_entry(&entries[i]);
            if (f) {
                *out_entry = &entries[i];
                return f;
            }
        }

        if (searched_boot_device) {
            print_warn(
                "No hyper.cfg on the boot device, "
                "doing a full filesystem scan!\n"
            );
        }
    }

    // No luck so far, scan every filesystem we were able to detect
    for (i = 0; i < entry_count; ++i) {
        f = config_from_entry(&entries[i]);
        if (f) {
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
            oops("configuration file must contain at least one loadable entry\n");
        return;
    }

    if (!cfg_get_loadable_entry(cfg, loadable_entry_name, le))
        oops("no loadable entry \"%pSV\"\n", &loadable_entry_name);
}
