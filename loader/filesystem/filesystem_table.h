#pragma once

#include "filesystem.h"
#include "guid.h"
#include "path.h"

enum fse_type {
    FSE_TYPE_RAW,
    FSE_TYPE_MBR,
    FSE_TYPE_GPT
};

struct fs_entry {
    void *disk_handle;
    u32 disk_id;
    u32 partition_index;
    u16 entry_type;
    struct guid disk_guid;
    struct guid partition_guid;
    struct filesystem *fs;
};

void fst_init(void);

void fst_add_raw_fs_entry(const struct disk *d, struct filesystem*);

void fst_add_mbr_fs_entry(const struct disk *d, u32 partition_index,
                          struct filesystem*);

void fst_add_gpt_fs_entry(const struct disk *d, u32 partition_index,
                          const struct guid *disk_guid,
                          const struct guid *partition_guid,
                          struct filesystem*);

const struct fs_entry *fst_fs_by_full_path(const struct full_path *path);

void fst_set_origin(struct fs_entry*);
const struct fs_entry *fst_get_origin(void);

struct fs_entry *fst_list(size_t *count);
