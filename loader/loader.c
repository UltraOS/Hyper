#include "services.h"
#include "common/log.h"
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
        fs_detect_all(svc->ds, &disks[disk_index], disk_index);

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
