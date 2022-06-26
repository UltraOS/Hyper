#include "common/helpers.h"
#include "common/log.h"

#include "gpt.h"
#include "guid.h"
#include "filesystem.h"
#include "filesystem_table.h"

struct gpt_header {
    u64 Signature;
    u32 Revision;
    u32 HeaderSize;
    u32 HeaderCRC32;
    u32 Reserved;
    u64 MyLBA;
    u64 AlternateLBA;
    u64 FirstUsableLBA;
    u64 LastUsableLBA;
    struct guid DiskGUID;
    u64 PartitionEntryLBA;
    u32 NumberOfPartitionEntries;
    u32 SizeOfPartitionEntry;
    u32 PartitionEntryArrayCRC32;
    u32 Reserved1;
    // u8 Reserved1[512 - 92]; A bit useless, comment out for now
};
BUILD_BUG_ON(sizeof(struct gpt_header) != 96);

struct gpt_partition_entry {
    struct guid PartitionTypeGUID;
    struct guid UniquePartitionGUID;
    u64 StartingLBA;
    u64 EndingLBA;
    u64 Attributes;
    u16 PartitionName[36];
};
BUILD_BUG_ON(sizeof(struct gpt_partition_entry) != 128);

#define UNUSED_PARTITION_GUID \
    { 0x00000000, 0x0000, 0x0000, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } };
static struct guid unused_part_guid = UNUSED_PARTITION_GUID;

struct gpt_part_ctx {
    struct block_cache *bc;
    struct guid *disk_guid;
    struct gpt_partition_entry pe;
    const struct disk *d;
    size_t part_idx;
};

static void gpt_initialize_partition(struct gpt_part_ctx *ctx)
{
    struct filesystem *fs = NULL;
    struct range lba_range;

    if (guid_compare(&unused_part_guid, &ctx->pe.PartitionTypeGUID) == 0)
        return;

    lba_range = (struct range) {
        ctx->pe.StartingLBA,
        ctx->pe.EndingLBA
    };

    fs = fs_try_detect(ctx->d, lba_range, ctx->bc);
    if (!fs)
        return;

    fst_add_gpt_fs_entry(ctx->d, ctx->part_idx, ctx->disk_guid,
                         &ctx->pe.UniquePartitionGUID, fs);
}

static void gpt_do_initialize(const struct disk *d, struct block_cache *bc)
{
    struct gpt_header hdr;
    struct gpt_part_ctx part_ctx;
    u64 current_off;

    if (!block_cache_read(bc, &hdr, 1 << d->block_shift,
                          sizeof(struct gpt_header)))
        return;

    if (hdr.SizeOfPartitionEntry < sizeof(struct gpt_partition_entry)) {
        print_warn("invalid GPT partition entry size %u, skipped (disk %u)\n",
                   hdr.SizeOfPartitionEntry, d->id);
        return;
    }

    part_ctx.bc = bc;
    part_ctx.disk_guid = &hdr.DiskGUID;
    part_ctx.d = d;
    current_off = hdr.PartitionEntryLBA << d->block_shift;

    for (part_ctx.part_idx = 0;
         part_ctx.part_idx < hdr.NumberOfPartitionEntries;
         ++part_ctx.part_idx)
    {
        if (!block_cache_read(bc, &part_ctx.pe, current_off,
                              sizeof(struct gpt_partition_entry)))
            continue;

        gpt_initialize_partition(&part_ctx);
        current_off += hdr.SizeOfPartitionEntry;
    }
}

// "EFI PART"
#define GPT_SIGNATURE 0x5452415020494645

bool gpt_initialize(const struct disk *d, struct block_cache *bc)
{
    u64 signature;

    if (!block_cache_read(bc, &signature, disk_block_size(d),
                          sizeof(signature)))
        return false;

    if (signature != GPT_SIGNATURE)
        return false;

    gpt_do_initialize(d, bc);
    return true;
}
