#include "services.h"
#include "common/log.h"
#include "common/helpers.h"
#include "common/string_view.h"
#include "allocator.h"
#include "filesystem/filesystem.h"
#include "filesystem/filesystem_table.h"
#include "protocols/ultra.h"
#include "config.h"

void init_all_disks(void);
void init_config(struct config *out_cfg);

struct file *find_config_file(struct fs_entry **fe);
void pick_loadable_entry(struct config *cfg, struct loadable_entry *le);


enum load_protocol {
    LOAD_PROTOCOL_ULTRA,
    // Maybe multiboot/stivale in the future?
};
enum load_protocol get_load_protocol(struct config *cfg, struct loadable_entry*);

void loader_entry(void)
{
    struct config cfg = { 0 };
    struct loadable_entry le;
    enum load_protocol prot;

    allocator_set_default_alloc_type(MEMORY_TYPE_LOADER_RECLAIMABLE);
    fs_table_init();

    init_all_disks();
    init_config(&cfg);

    pick_loadable_entry(&cfg, &le);
    prot = get_load_protocol(&cfg, &le);

    BUG_ON(prot != LOAD_PROTOCOL_ULTRA);
    ultra_protocol_load(&cfg, &le);
}

// TODO: support chain-loading configs
void init_config(struct config *out_cfg)
{
    struct fs_entry *fe;
    struct file *cfg_file;
    char *cfg_data;
    struct string_view cfg_view;

    cfg_file = find_config_file(&fe);
    if (!cfg_file)
        oops("Couldn't find hyper.cfg anywhere on disk!\n");

    set_origin_fs(fe);
    cfg_data = allocate_critical_bytes(cfg_file->size);

    if (!cfg_file->read(cfg_file, cfg_data, 0, cfg_file->size))
        oops("failed to read config file\n");

    cfg_view = (struct string_view) {
        .text = cfg_data,
        .size = cfg_file->size
    };

    if (!cfg_parse(cfg_view, out_cfg)) {
        cfg_pretty_print_error(&out_cfg->last_error, cfg_view);
        loader_abort();
    }
}

void init_all_disks(void)
{
    size_t disk_index;
    u32 disk_count = ds_get_disk_count();
    struct block_cache bc;
    void *buf = allocate_pages(1);
    if (unlikely(!buf))
        return;

    for (disk_index = 0; disk_index < disk_count; ++disk_index) {
        struct disk d;
        ds_query_disk(disk_index, &d);

        block_cache_init(&bc, &ds_read_blocks, d.handle, d.block_shift,
                         buf, PAGE_SIZE >> d.block_shift);

        fs_detect_all(&d, &bc);
    }

    free_pages(buf, 1);
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
            oops("configuration file must contain at least one loadable entry\n");
        return;
    }

    if (!cfg_get_loadable_entry(cfg, loadable_entry_name, le))
        oops("no loadable entry \"%pSV\"\n", &loadable_entry_name);
}

#define PROTOCOL_KEY SV("protocol")

enum load_protocol get_load_protocol(struct config *cfg, struct loadable_entry *entry)
{
    struct string_view protocol_name;
    CFG_MANDATORY_GET(string, cfg, entry, PROTOCOL_KEY, &protocol_name);

    if (!sv_equals_caseless(protocol_name, SV("ultra")))
        oops("unsupported load protocol: %pSV\n", &protocol_name);

    return LOAD_PROTOCOL_ULTRA;
}
