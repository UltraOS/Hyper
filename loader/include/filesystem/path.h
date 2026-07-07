#pragma once

#include "filesystem.h"
#include "guid.h"

#define MAX_PATH_SIZE 255

enum disk_identifier {
    DISK_IDENTIFIER_INVALID,
    DISK_IDENTIFIER_HD,
    DISK_IDENTIFIER_CD,
    DISK_IDENTIFIER_UUID,
    DISK_IDENTIFIER_ORIGIN,
    DISK_IDENTIFIER_PXE
};

enum partition_identifier {
    PARTITION_IDENTIFIER_INVALID,
    PARTITION_IDENTIFIER_RAW,
    PARTITION_IDENTIFIER_INDEX,
    PARTITION_IDENTIFIER_UUID,
    PARTITION_IDENTIFIER_ORIGIN
};

/*
 * A parsed path selector: which disk/partition the user named and how. Only the
 * member matching the corresponding *_id_type is meaningful. The identifiers are
 * kept as plain fields (rather than a union) so the index and GUID can't alias
 * each other.
 */
struct full_path {
    enum disk_identifier disk_id_type;
    struct guid disk_guid;
    u32 disk_index;

    enum partition_identifier partition_id_type;
    struct guid partition_guid;
    u32 partition_index;

    struct string_view path_within_partition;
};


bool path_parse(struct string_view path, struct full_path *out_path);
struct file *path_open(struct filesystem *fs, struct string_view path);
