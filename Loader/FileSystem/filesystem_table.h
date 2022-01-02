#pragma once

#include "filesystem.h"

struct fs_entry {
    void *disk_handle;
    u32 disk_index;
    u32 partition_index;
    struct guid disk_guid;
    struct guid partition_guid;
    struct filesystem *fs;
};

void add_raw_fs_entry(void *disk_handle, u32 disk_index, struct filesystem*);
void add_mbr_fs_entry(void *disk_handle, u32 disk_index, u32 partition_index, struct filesystem*);
void add_gpt_fs_entry(void *disk_handle, u32 disk_index, u32 partition_index,
                      const struct guid *disk_guid, const struct guid *partition_guid, struct filesystem*);

const struct fs_entry *fs_by_full_path(const struct full_path *path);

void set_origin_fs(struct fs_entry*);
const struct fs_entry *get_origin_fs();
