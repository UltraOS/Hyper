#pragma once

#include "common/types.h"
#include "common/bug.h"

struct PACKED fat_ebpb {
    // BPB
    u16 bytes_per_sector;
    u8 sectors_per_cluster;
    u16 reserved_sectors;
    u8 fat_count;
    u16 max_root_dir_entries;
    u16 unused_1; // total logical sectors for FAT12/16
    u8 media_descriptor;
    u16 unused_2; // logical sectors per file allocation table for FAT12/16
    u16 sectors_per_track;
    u16 heads;
    u32 hidden_sectors;
    u32 total_logical_sectors;

    // EBPB
    u32 sectors_per_fat;
    u16 ext_flags;
    u16 version;
    u32 root_dir_cluster;
    u16 fs_information_sector;
    u16 backup_boot_sectors;
    u8 reserved[12];
    u8 drive_number;
    u8 unused_3;
    u8 signature;
    u32 volume_id;
    char volume_label[11];
    char filesystem_type[8];
};

BUILD_BUG_ON(sizeof(struct fat_ebpb) != 79);

#define FAT_SHORT_NAME_LENGTH 8
#define FAT_SHORT_EXTENSION_LENGTH 3
#define FAT_FULL_SHORT_NAME_LENGTH (FAT_SHORT_NAME_LENGTH + FAT_SHORT_EXTENSION_LENGTH)

#define END_OF_DIRECTORY_MARK 0x00
#define DELETED_FILE_MARK 0xE5

#define LONG_NAME_ATTRIBUTE    0x0F
#define DEVICE_ATTRIBUTE       (1 << 6)
#define ARCHIVE_ATTRIBUTE      (1 << 5)
#define SUBDIR_ATTRIBUTE       (1 << 4)
#define VOLUME_LABEL_ATTRIBUTE (1 << 3)
#define SYSTEM_ATTRIBUTE       (1 << 2)
#define HIDDEN_ATTRIBUTE       (1 << 1)
#define READ_ONLY_ATTRIBUTE    (1 << 0)

#define LOWERCASE_NAME_BIT (1 << 3)
#define LOWERCASE_EXTENSION_BIT (1 << 4)

struct PACKED fat_directory_entry {
    char filename[FAT_SHORT_NAME_LENGTH];
    char extension[FAT_SHORT_EXTENSION_LENGTH];

    u8 attributes;
    u8 case_info;
    u8 created_ms;
    u16 created_time;
    u16 created_date;
    u16 last_accessed_date;
    u16 cluster_high;
    u16 last_modified_time;
    u16 last_modified_date;
    u16 cluster_low;
    u32 size;
};

BUILD_BUG_ON(sizeof(struct fat_directory_entry) != 32);

#define BYTES_PER_UCS2_CHAR 2

#define NAME_1_CHARS 5
#define NAME_2_CHARS 6
#define NAME_3_CHARS 2
#define CHARS_PER_LONG_ENTRY (NAME_1_CHARS + NAME_2_CHARS + NAME_3_CHARS)

#define LAST_LOGICAL_ENTRY_BIT (1 << 6)
#define SEQUENCE_NUM_BIT_MASK 0b11111

struct PACKED long_name_fat_directory_entry {
    u8 sequence_number;
    u8 name_1[NAME_1_CHARS * BYTES_PER_UCS2_CHAR];
    u8 attributes;
    u8 type;
    u8 checksum;
    u8 name_2[NAME_2_CHARS * BYTES_PER_UCS2_CHAR];
    u16 first_cluster;
    u8 name_3[NAME_3_CHARS * BYTES_PER_UCS2_CHAR];
};

BUILD_BUG_ON(sizeof(struct long_name_fat_directory_entry) != 32);
