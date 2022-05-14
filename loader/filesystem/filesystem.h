#pragma once

#include "disk_services.h"
#include "common/types.h"
#include "common/string_view.h"
#include "common/range.h"
#include "block_cache.h"

#define MAX_PATH_SIZE 255

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

bool parse_path(struct string_view path, struct full_path *out_path);

struct filesystem;

struct file {
    struct filesystem *fs;
    u64 size;
};

struct dir_iter_ctx {
    /*
     * This is not very good, but it allows us to avoid allocations
     * when iterating directories.
     */
    _Alignas(u64)
    char opaque[4 * sizeof(u64)];
};

#define DIR_REC_MAX_NAME_LEN 255
struct dir_rec {
    char name[DIR_REC_MAX_NAME_LEN];
    u8 name_len;

#define DIR_REC_SUBDIR (1 << 0)
    u8 flags;

    u64 size;

    _Alignas(u64)
    char opaque[2 * sizeof(u64)];
};

static inline bool dir_rec_is_subdir(struct dir_rec *rec)
{
    return rec->flags & DIR_REC_SUBDIR;
}

#define BLOCK_RANGE_OFF_EMPTY_HOLE 0xFFFFFFFFFFFFFFFF

struct block_range {
    u64 part_byte_off;
    size_t blocks;
};

static inline bool is_block_range_hole(struct block_range *br)
{
    return br->part_byte_off == BLOCK_RANGE_OFF_EMPTY_HOLE;
}

static inline void block_range_make_hole(struct block_range *br)
{
    br->part_byte_off = BLOCK_RANGE_OFF_EMPTY_HOLE;
}

struct filesystem {
    struct disk d;
    struct range lba_range;
    u8 block_shift;

    // ctx is initialized from the root directory if 'rec' is NULL.
    void (*iter_ctx_init)(struct filesystem *fs, struct dir_iter_ctx *ctx, struct dir_rec *rec);
    bool (*next_dir_rec)(struct filesystem *fs, struct dir_iter_ctx *ctx, struct dir_rec *out_rec);

    struct file *(*open_file)(struct filesystem *fs, struct dir_rec *rec);
    void (*close_file)(struct file*);
    bool (*read_file)(struct file*, void *buffer, u64 offset, u32 bytes);
};

static inline u8 fs_block_shift(struct filesystem *fs)
{
    return fs->block_shift;
}

static inline u8 file_block_shift(struct file *f)
{
    return fs_block_shift(f->fs);
}

void check_read(struct file *f, u64 offset, u32 size);

typedef bool (*file_get_range_t)(struct file*, u64 file_block_off,
                                 size_t want_blocks, struct block_range *out);
bool bulk_read_file(struct file* f, void *buffer, u64 offset, u32 bytes,
                    file_get_range_t get_range);

void fs_detect_all(struct disk *d, struct block_cache *bc);
struct file *fs_open(struct filesystem *fs, struct string_view path);
