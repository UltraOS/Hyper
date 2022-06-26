#include "common/attributes.h"
#include "common/range.h"
#include "common/log.h"

#include "filesystem.h"
#include "filesystem_table.h"
#include "mbr.h"

struct PACKED mbr_partition_entry {
        u8 status;
        u8 chs_begin[3];
        u8 type;
        u8 chs_end[3];
        u32 first_block;
        u32 block_count;
};
BUILD_BUG_ON(sizeof(struct mbr_partition_entry) != 16);

enum {
    MBR_EMPTY_PARTITION = 0x00,
    MBR_EBR_PARTITION   = 0x05
};

#define OFFSET_TO_MBR_PARTITION_LIST 0x01BE

static void mbr_do_initialize(const struct disk *d, struct block_cache *bc,
                              size_t base_index, u64 sector_offset)
{
    struct mbr_partition_entry partitions[4];
    u64 part_abs_byte_off = (sector_offset << d->block_shift) + OFFSET_TO_MBR_PARTITION_LIST;
    bool is_ebr = base_index != 0;
    size_t i, max_partitions = is_ebr ? 2 : 4;

    if (!block_cache_read(bc, partitions, part_abs_byte_off, sizeof(partitions)))
        return;

    for (i = 0; i < max_partitions; ++i) {
        struct mbr_partition_entry *p = &partitions[i];
        u64 real_partition_offset = sector_offset + p->first_block;
        struct range lba_range = {
            real_partition_offset,
            real_partition_offset + p->block_count
        };
        struct filesystem *fs = NULL;

        if (p->type == MBR_EMPTY_PARTITION)
            continue;

        if (p->type == MBR_EBR_PARTITION) {
            if (is_ebr && i == 0) {
                print_warn("EBR with chain at index 0");
                break;
            }

            mbr_do_initialize(d, bc, base_index + (is_ebr ? 1 : 4),
                              real_partition_offset);
            continue;
        }

        if (i == 1 && is_ebr) {
            print_warn("EBR with a non-EBR entry at index 1 (0x%X)", p->type);
            break;
        }

        fs = fs_try_detect(d, lba_range, bc);
        if (fs)
            fst_add_mbr_fs_entry(d, base_index + i, fs);
    }
}

#define MBR_SIGNATURE 0xAA55
#define MBR_OFFSET_TO_SIGNATURE 510

bool mbr_initialize(const struct disk *d, struct block_cache *bc)
{
    u16 signature;

    if (!block_cache_read(bc, &signature, MBR_OFFSET_TO_SIGNATURE,
                          sizeof(signature)))
        return false;

    if (signature != MBR_SIGNATURE)
        return false;

    mbr_do_initialize(d, bc, 0, 0);
    return true;
}
