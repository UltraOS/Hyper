#pragma once

#include "filesystem.h"
#include "guid.h"

#define MAX_PATH_SIZE 255

enum disk_identifier {
    DISK_IDENTIFIER_INVALID,
    DISK_IDENTIFIER_INDEX,
    DISK_IDENTIFIER_UUID,
    DISK_IDENTIFIER_ORIGIN
};

enum partition_identifier {
    PARTITION_IDENTIFIER_INVALID,
    PARTITION_IDENTIFIER_RAW,
    PARTITION_IDENTIFIER_INDEX,
    PARTITION_IDENTIFIER_UUID,
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


bool path_parse(struct string_view path, struct full_path *out_path);
struct file *path_open(struct filesystem *fs, struct string_view path);
