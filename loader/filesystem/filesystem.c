#define MSG_FMT(msg) "FS: " msg

#include "common/types.h"
#include "common/string_view.h"

#include "filesystem.h"
#include "filesystem_table.h"
#include "mbr.h"
#include "gpt.h"
#include "fat/fat.h"
#include "iso9660/iso9660.h"

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
    panic("BUG: invalid read at offset %llu with size %u", offset, size);
}

struct filesystem *fs_try_detect(const struct disk *d, struct range lba_range,
                                 struct block_cache *bc)
{
    return try_create_fat(d, lba_range, bc);
}

static bool check_cd(const struct disk *d, struct block_cache *bc)
{
    struct filesystem *fs;

    fs = try_create_iso9660(d, bc);
    if (!fs)
        return false;

    fst_add_raw_fs_entry(d, fs);
    return true;
}

static void detect_raw(const struct disk *d, struct block_cache *bc)
{
    struct filesystem *fs;
    struct range lba_range = { 0, d->sectors };

    fs = fs_try_detect(d, lba_range, bc);
    if (!fs)
        return;

    fst_add_raw_fs_entry(d, fs);
}

void fs_detect_all(struct disk *d, struct block_cache *bc)
{
    if (check_cd(d, bc))
        return;

    if (!block_cache_refill(bc, 0))
        return;

    if (gpt_initialize(d, bc))
        return;

    if (mbr_initialize(d, bc))
        return;

    detect_raw(d, bc);
}
