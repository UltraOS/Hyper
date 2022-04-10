#include "filesystem.h"
#include "filesystem_table.h"
#include "common/string_view.h"
#include "common/types.h"
#include "common/ctype.h"
#include "common/conversions.h"
#include "common/log.h"
#include "common/constants.h"
#include "common/helpers.h"
#include "common/minmax.h"
#include "allocator.h"
#include "filesystem/block_cache.h"
#include "filesystem/fat/fat.h"

#undef MSG_FMT
#define MSG_FMT(msg) "FS: " msg

static struct disk_services *backend;

struct disk_services *filesystem_set_backend(struct disk_services* sv)
{
    struct disk_services *prev = backend;
    backend = sv;
    return prev;
}

struct disk_services *filesystem_backend()
{
    return backend;
}

void check_read(struct file *f, u64 offset, u32 size)
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

bool split_prefix_and_path(struct string_view str, struct string_view *prefix, struct string_view *path)
{
    struct string_view delim = SV("::");
    ssize_t pref_idx = sv_find(str, delim, 0);

    if (pref_idx < 0) {
        sv_clear(prefix);
        *path = str;
    } else {
        *prefix = (struct string_view) { str.text, pref_idx };
        *path = (struct string_view) { str.text + pref_idx + 2, str.size - pref_idx - 2 };
    }

    return true;
}

bool next_path_node(struct string_view *path, struct string_view *node)
{
    struct string_view sep = SV("/");
    ssize_t path_end;
    *node = *path;

    while (node->size && node->text[0] == '/')
        sv_offset_by(node, 1);

    if (!node->size)
        return false;

    path_end = sv_find(*node, sep, 0);
    if (path_end != -1) {
        const char *end = &node->text[path_end];
        path->size -= end - path->text;
        path->text = end;
        node->size = path_end;
    } else {
        path->size = 0;
    }

    return true;
}

#define CHARS_PER_GUID 32
#define CHARS_PER_HEX_BYTE 2

static bool extract_numeric_prefix(struct string_view *str, struct string_view *prefix, bool allow_hex, size_t size)
{
    prefix->text = str->text;
    prefix->size = 0;

    for (;;) {
        char c;

        if (size && prefix->size == size)
            break;

        if (sv_empty(*str))
            break;

        c = tolower(str->text[0]);

        if ((c >= '0' && c <= '9') || (allow_hex && (c >= 'a' && c <= 'z'))) {
            sv_extend_by(prefix, 1);
            sv_offset_by(str, 1);
            continue;
        }

        break;
    }

    return !sv_empty(*str) && (size && str->size == size);
}

bool parse_guid(struct string_view *str, struct guid *guid)
{
    size_t i;

    if (str->size != CHARS_PER_GUID)
        return false;

    if (!str_to_u32((struct string_view) { str->text, 4 * CHARS_PER_HEX_BYTE }, &guid->data1))
        return false;
    sv_offset_by(str, 4 * CHARS_PER_HEX_BYTE);

    if (!str_to_u16((struct string_view) { str->text, 2 * CHARS_PER_HEX_BYTE }, &guid->data2))
        return false;
    sv_offset_by(str, 2 * CHARS_PER_HEX_BYTE);

    if (!str_to_u16((struct string_view) { str->text, 2 * CHARS_PER_HEX_BYTE }, &guid->data3))
        return false;
    sv_offset_by(str, 2 * CHARS_PER_HEX_BYTE);

    for (i = 0; i < 8; ++i) {
        if (!str_to_u8((struct string_view) { str->text, 1 * CHARS_PER_HEX_BYTE }, &guid->data4[i]))
            return false;

        sv_offset_by(str, 1 * CHARS_PER_HEX_BYTE);
    }

    return true;
}

bool parse_path(struct string_view path, struct full_path *out_path)
{
    // path relative to config disk
    if (sv_starts_with(path, SV("/")) || sv_starts_with(path, SV("::/"))) {
        out_path->disk_id_type = DISK_IDENTIFIER_ORIGIN;
        out_path->partition_id_type = PARTITION_IDENTIFIER_ORIGIN;

        sv_offset_by(&path, path.text[0] == ':' ? 2 : 0);
        out_path->path_within_partition = path;
        return true;
    }

    if (sv_starts_with(path, SV("DISKUUID"))) {
        struct string_view prefix;

        sv_offset_by(&path, 8);
        if (!extract_numeric_prefix(&path, &prefix, true, CHARS_PER_GUID))
            return false;

        out_path->disk_id_type = DISK_IDENTIFIER_UUID;
        if (!parse_guid(&prefix, &out_path->disk_guid))
            return false;
    } else if (sv_starts_with(path, SV("DISK"))) {
        struct string_view prefix;

        sv_offset_by(&path, 4);
        if (!extract_numeric_prefix(&path, &prefix, false, 0))
            return false;
        if (!str_to_u32(prefix, &out_path->disk_index))
            return false;

        out_path->disk_id_type = DISK_IDENTIFIER_INDEX;
    } else { // invalid prefix
        return false;
    }

    if (sv_starts_with(path, SV("GPTUUID"))) {
        struct string_view prefix;

        sv_offset_by(&path, 7);
        if (!extract_numeric_prefix(&path, &prefix, true, CHARS_PER_GUID))
            return false;

        out_path->partition_id_type = PARTITION_IDENTIFIER_GPT_UUID;
        if (!parse_guid(&prefix, &out_path->partition_guid))
            return false;
    } else if (sv_starts_with(path, SV("MBR")) || sv_starts_with(path, SV("GPT"))) {
        struct string_view prefix;

        sv_offset_by(&path, 3);
        if (!extract_numeric_prefix(&path, &prefix, false, 0))
            return false;
        if (!str_to_u32(prefix, &out_path->partition_index))
            return false;

        out_path->partition_id_type = path.text[0] == 'M' ? PARTITION_IDENTIFIER_MBR_INDEX :
                                                            PARTITION_IDENTIFIER_GPT_INDEX;
    } else if (sv_starts_with(path, SV("::/"))) {
        // GPT disks cannot be treated as a raw device
        if (out_path->disk_id_type != DISK_IDENTIFIER_INDEX)
            return false;

        out_path->partition_id_type = PARTITION_IDENTIFIER_RAW;
    } else {
        return false;
    }

    if (!sv_starts_with(path, SV("::/")))
        return false;

    sv_offset_by(&path, 2);
    if (path.size >= MAX_PATH_SIZE) {
        oops("path \"%pSV\" is too big (%zu vs max %u)\n",
             &path, path.size, MAX_PATH_SIZE);
    }

    out_path->path_within_partition = path;

    return true;
}

struct filesystem *fs_try_detect(const struct disk *d, struct range lba_range,
                                 struct block_cache *bc)
{
    if (!backend)
        return NULL;

    return try_create_fat(d, lba_range, bc);
}

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

void initialize_from_mbr(struct disk_services *sv, const struct disk *d, u32 disk_id,
                         struct block_cache *bc, size_t base_index, u64 sector_offset)
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

            initialize_from_mbr(sv, d, disk_id, bc, base_index + (is_ebr ? 1 : 4),
                                real_partition_offset);
            continue;
        }

        if (i == 1 && is_ebr) {
            print_warn("EBR with a non-EBR entry at index 1 (0x%X)", p->type);
            break;
        }

        fs = fs_try_detect(d, lba_range, bc);
        if (fs)
            add_mbr_fs_entry(d->handle, disk_id, base_index + i, fs);
    }
}


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
    struct disk *d;
    u32 disk_id;
    size_t part_idx;
};

void process_gpt_partition(struct gpt_part_ctx *ctx)
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

    add_gpt_fs_entry(ctx->d->handle, ctx->disk_id, ctx->part_idx,
                     ctx->disk_guid, &ctx->pe.UniquePartitionGUID, fs);
}

void initialize_from_gpt(struct disk *d, u32 disk_id, struct block_cache *bc)
{
    struct gpt_header hdr;
    struct gpt_part_ctx part_ctx;
    u64 current_off;

    if (!block_cache_read(bc, &hdr, 1 << d->block_shift, sizeof(struct gpt_header)))
        return;

    if (hdr.SizeOfPartitionEntry < sizeof(struct gpt_partition_entry)) {
        print_warn("invalid GPT partition entry size %u, skipped (disk %u)\n",
                   hdr.SizeOfPartitionEntry, disk_id);
        return;
    }

    part_ctx.bc = bc;
    part_ctx.disk_guid = &hdr.DiskGUID;
    part_ctx.d = d;
    part_ctx.disk_id = disk_id;
    current_off = hdr.PartitionEntryLBA << d->block_shift;

    for (part_ctx.part_idx = 0; part_ctx.part_idx < hdr.NumberOfPartitionEntries; ++part_ctx.part_idx) {
        if (!block_cache_read(bc, &part_ctx.pe, current_off, sizeof(struct gpt_partition_entry)))
            return;

        process_gpt_partition(&part_ctx);
        current_off += hdr.SizeOfPartitionEntry;
    }
}

// "EFI PART"
#define GPT_SIGNATURE 0x5452415020494645

#define MBR_SIGNATURE 0xAA55
#define OFFSET_TO_MBR_SIGNATURE 510

void fs_detect_all(struct disk_services *sv, struct disk *d, u32 disk_id,
                   struct block_cache *bc)
{
    u8 signature[8];

    if (!block_cache_read(bc, signature, disk_block_size(d), sizeof(u64)))
        return;

    if (*(u64*)signature == GPT_SIGNATURE) {
        initialize_from_gpt(d, disk_id, bc);
        return;
    }

    if (!block_cache_read(bc, signature, OFFSET_TO_MBR_SIGNATURE, sizeof(u16)))
        return;

    if (*(u16*)signature == MBR_SIGNATURE) {
        initialize_from_mbr(sv, d, disk_id, bc, 0, 0);
        return;
    }

    print_warn("unpartitioned drive %p skipped\n", d->handle);
}
