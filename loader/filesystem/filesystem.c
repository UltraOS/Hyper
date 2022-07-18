#define MSG_FMT(msg) "FS: " msg

#include "common/types.h"
#include "common/string_view.h"

#include "filesystem/filesystem.h"
#include "filesystem/filesystem_table.h"
#include "filesystem/mbr.h"
#include "filesystem/gpt.h"

void fs_check_read(struct file *f, u64 offset, u32 size)
{
    u64 final_offset = offset + size;

    if (unlikely(size == 0))
        goto die;

    if (unlikely(final_offset < offset))
        goto die;

    if (unlikely(final_offset > f->size))
        goto die;

    return;

die:
    panic("BUG: invalid read at offset %llu with size %u!\n", offset, size);
}

enum fs_detect_type {
    FS_DETECT_CD,
    FS_DETECT_HDD,
};

static struct filesystem*
fs_do_detect(const struct disk *d, struct range lba_range,
             struct block_cache *bc, enum fs_detect_type dt)
{
    filesystem_type_entry *fse;
    struct filesystem *fs;

    for (fse = filesystems_begin; fse < filesystems_end; ++fse) {
        struct filesystem_type *fst = *fse;
        bool is_cd_fs = fst->flags & FS_TYPE_CD;

        if (is_cd_fs != (dt == FS_DETECT_CD))
            continue;

        if ((fs = fst->detect(d, lba_range, bc)))
            return fs;
    }

    return NULL;
}

struct filesystem *fs_try_detect(const struct disk *d, struct range lba_range,
                                 struct block_cache *bc)
{
    return fs_do_detect(d, lba_range, bc, FS_DETECT_HDD);
}

static bool detect_entire(const struct disk *d, struct block_cache *bc,
                          enum fs_detect_type dt)
{
    struct filesystem *fs;
    struct range lba_range = { 0, d->sectors };

    fs = fs_do_detect(d, lba_range, bc, dt);
    if (!fs)
        return false;

    fst_add_raw_fs_entry(d, fs);
    return true;
}

static bool detect_cd(const struct disk *d, struct block_cache *bc)
{
    return detect_entire(d, bc, FS_DETECT_CD);
}

static void detect_raw(const struct disk *d, struct block_cache *bc)
{
    detect_entire(d, bc, FS_DETECT_HDD);
}

void fs_detect_all(struct disk *d, struct block_cache *bc)
{
    if (detect_cd(d, bc))
        return;

    if (!block_cache_refill(bc, 0))
        return;

    if (gpt_initialize(d, bc))
        return;

    if (mbr_initialize(d, bc))
        return;

    detect_raw(d, bc);
}
