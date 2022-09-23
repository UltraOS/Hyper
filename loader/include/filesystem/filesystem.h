#pragma once

#include "common/types.h"
#include "common/string_view.h"
#include "common/range.h"

#include "disk_services.h"
#include "block_cache.h"

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

typedef
struct filesystem*
(*fs_detect_t)(
    const struct disk *d,
    struct range lba_range,
    struct block_cache *bc
);

#define FS_TYPE_CD (1 << 0)

struct filesystem_type {
    struct string_view name;
    u32 flags;
    fs_detect_t detect;
};

typedef struct filesystem_type *filesystem_type_entry;

#define DECLARE_FILESYSTEM(type) \
    static filesystem_type_entry CONCAT(type, hook) \
           CTOR_SECTION(filesystems) USED = &type

static inline u8 fs_block_shift(struct filesystem *fs)
{
    return fs->block_shift;
}

static inline u8 file_block_shift(struct file *f)
{
    return fs_block_shift(f->fs);
}

void fs_check_read(struct file *f, u64 offset, u32 size);
void fs_detect_all(struct disk *d, struct block_cache *bc);

struct filesystem *fs_try_detect(const struct disk *d, struct range lba_range,
                                 struct block_cache *bc);
