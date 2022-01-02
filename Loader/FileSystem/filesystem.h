#pragma once

#include "services.h"
#include "common/types.h"
#include "common/string_view.h"
#include "common/range.h"

struct guid {
    u32 data1;
    u16 data2;
    u16 data3;
    u8  data4[8];
};

static inline int guid_compare(const struct guid *lhs, const struct guid *rhs)
{
    return memcmp(lhs, rhs, sizeof(*rhs));
}

static struct disk_services *filesystem_set_backend(struct disk_services*);
static struct disk_services *filesystem_backend();

enum disk_identifier {
    DISK_IDENTIFIER_INVALID,
    DISK_IDENTIFIER_INDEX,
    DISK_IDENTIFIER_UUID,
    DISK_IDENTIFIER_ORIGIN
};

enum partition_identifier {
    PARTITION_IDENTIFIER_INVALID,
    PARTITION_IDENTIFIER_RAW,
    PARTITION_IDENTIFIER_MBR_INDEX,
    PARTITION_IDENTIFIER_GPT_INDEX,
    PARTITION_IDENTIFIER_GPT_UUID,
    PARTITION_IDENTIFIER_ORIGIN
};

struct full_path {
    enum disk_identifier disk_id_type;

    union {
        struct guid disk_guid;
        u32 disk_index;
    };

    enum partition_identifier partition_id_type;

    union {
        struct guid partition_guid;
        u32 partition_index;
    };

    struct string_view path_within_partition;
};

bool parse_guid(struct string_view *guid, struct guid*);
bool parse_path(struct string_view path, struct full_path *out_path);

struct filesystem;

struct file {
    struct filesystem *fs;
    size_t size;

    bool (*read)(struct file*, void *buffer, u32 offset, u32 bytes);
};

struct filesystem {
    struct disk d;
    struct range lba_range;
    struct file *(*open)(struct filesystem *fs, struct string_view path);
    void (*close)(struct filesystem *fs, struct file*);
};

bool split_prefix_and_path(struct string_view str, struct string_view *prefix, struct string_view *path);
bool next_path_node(struct string_view* path, struct string_view* node);
