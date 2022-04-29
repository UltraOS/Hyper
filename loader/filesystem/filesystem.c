#define MSG_FMT(msg) "FS: " msg

#include "filesystem.h"
#include "filesystem_table.h"
#include "common/string_view.h"
#include "common/types.h"
#include "common/ctype.h"
#include "common/conversions.h"
#include "common/log.h"
#include "common/helpers.h"
#include "filesystem/block_cache.h"
#include "filesystem/fat/fat.h"
#include "filesystem/iso9660/iso9660.h"


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

static bool path_consume_numeric_sequence(struct string_view *str, u32 *out)
{
    struct string_view prefix_str = { str->text, 0 };

    for (;;) {
        char c;

        if (sv_empty(*str))
            break;

        c = tolower(str->text[0]);

        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')) {
            sv_extend_by(&prefix_str, 1);
            sv_offset_by(str, 1);
            continue;
        }

        break;
    }

    return !sv_empty(prefix_str) && str_to_u32_with_base(prefix_str, out, 16);
}

// 4 dashes + 32 characters, e.g E0E0D5FB-48FA-4428-B73D-43D3F7E49A8A
#define CHARS_PER_GUID (32 + 4)
#define CHARS_PER_HEX_BYTE 2

static bool consume_guid_part(struct string_view *str, void *out, u8 width, bool has_dash)
{
    u16 str_len = CHARS_PER_HEX_BYTE * width;
    bool res;

    switch (width) {
    case 1:
        res = str_to_u8_with_base((struct string_view) { str->text, str_len }, out, 16);
        break;
    case 2:
        res = str_to_u16_with_base((struct string_view) { str->text, str_len }, out, 16);
        break;
    case 4:
        res = str_to_u32_with_base((struct string_view) { str->text, str_len }, out, 16);
        break;
    default:
        BUG();
    }

    sv_offset_by(str, str_len + has_dash);
    return res;
}

static bool consume_guid(struct string_view *str, struct guid *guid)
{
    size_t i;

    if (str->size < CHARS_PER_GUID)
        return false;

    if (!consume_guid_part(str, &guid->data1, sizeof(u32), true))
        return false;

    if (!consume_guid_part(str, &guid->data2, sizeof(u16), true))
        return false;

    if (!consume_guid_part(str, &guid->data3, sizeof(u16), true))
        return false;

    for (i = 0; i < 8; ++i) {
        if (!consume_guid_part(str, &guid->data4[i], sizeof(u8), i == 1))
            return false;
    }

    return true;
}

static bool path_skip_dash(struct string_view *path)
{
    if (sv_empty(*path))
        return false;

    sv_offset_by(path, 1);
    return true;
}

#define DISKUUID_STR SV("DISKUUID")
#define DISK_STR     SV("DISK")

static bool path_consume_disk_identifier(struct string_view *path, struct full_path *out_path)
{
    if (sv_starts_with(*path, DISKUUID_STR)) {
        sv_offset_by(path, DISKUUID_STR.size);

        if (!consume_guid(path, &out_path->disk_guid))
            return false;

        out_path->disk_id_type = DISK_IDENTIFIER_UUID;
        return path_skip_dash(path);
    }

    if (sv_starts_with(*path, DISK_STR)) {
        sv_offset_by(path, DISK_STR.size);

        if (!path_consume_numeric_sequence(path, &out_path->disk_index))
            return false;

        out_path->disk_id_type = DISK_IDENTIFIER_INDEX;
        return path_skip_dash(path);
    }

    return false;
}

#define PARTUUID_STR SV("PARTUUID-")
#define PART_STR     SV("PART")

static bool path_consume_partition_identifier(struct string_view *path, struct full_path *out_path)
{
    if (sv_starts_with(*path, PARTUUID_STR)) {
        sv_offset_by(path, PARTUUID_STR.size);

        out_path->partition_id_type = PARTITION_IDENTIFIER_UUID;
        return consume_guid(path, &out_path->partition_guid);
    }

    if (sv_starts_with(*path, PART_STR)) {
        sv_offset_by(path, PART_STR.size);

        out_path->partition_id_type = PARTITION_IDENTIFIER_INDEX;
        return path_consume_numeric_sequence(path, &out_path->partition_index);
    }

    if (sv_starts_with(*path, SV("::/"))) {
        // GPT disks cannot be treated as unpartitioned media
        if (out_path->disk_id_type != DISK_IDENTIFIER_INDEX)
            return false;

        out_path->partition_id_type = PARTITION_IDENTIFIER_RAW;
        return true;
    }

    return false;
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

    if (!path_consume_disk_identifier(&path, out_path))
        return false;

    if (!path_consume_partition_identifier(&path, out_path))
        return false;

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

static struct filesystem *fs_try_detect(const struct disk *d, struct range lba_range,
                                        struct block_cache *bc)
{
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

static void initialize_from_mbr(const struct disk *d, struct block_cache *bc,
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

            initialize_from_mbr(d, bc, base_index + (is_ebr ? 1 : 4),
                                real_partition_offset);
            continue;
        }

        if (i == 1 && is_ebr) {
            print_warn("EBR with a non-EBR entry at index 1 (0x%X)", p->type);
            break;
        }

        fs = fs_try_detect(d, lba_range, bc);
        if (fs)
            add_mbr_fs_entry(d, base_index + i, fs);
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
    size_t part_idx;
};

static void process_gpt_partition(struct gpt_part_ctx *ctx)
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

    add_gpt_fs_entry(ctx->d, ctx->part_idx, ctx->disk_guid,
                     &ctx->pe.UniquePartitionGUID, fs);
}

static void initialize_from_gpt(struct disk *d, struct block_cache *bc)
{
    struct gpt_header hdr;
    struct gpt_part_ctx part_ctx;
    u64 current_off;

    if (!block_cache_read(bc, &hdr, 1 << d->block_shift, sizeof(struct gpt_header)))
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

static bool check_cd(const struct disk *d, struct block_cache *bc)
{
    struct filesystem *fs;

    fs = try_create_iso9660(d, bc);
    if (!fs)
        return false;

    add_raw_fs_entry(d, fs);
    return true;
}

static void detect_raw(const struct disk *d, struct block_cache *bc)
{
    struct filesystem *fs;
    struct range lba_range = { 0, d->sectors };

    fs = fs_try_detect(d, lba_range, bc);
    if (!fs)
        return;

    add_raw_fs_entry(d, fs);
}

void fs_detect_all(struct disk *d, struct block_cache *bc)
{
    _Alignas(u64) u8 signature[8];

    if (check_cd(d, bc))
        return;

    if (!block_cache_refill(bc, 0))
        return;

    if (!block_cache_read(bc, signature, disk_block_size(d), sizeof(u64)))
        return;

    if (*(u64*)signature == GPT_SIGNATURE) {
        initialize_from_gpt(d, bc);
        return;
    }

    if (!block_cache_read(bc, signature, OFFSET_TO_MBR_SIGNATURE, sizeof(u16)))
        return;

    if (*(u16*)signature == MBR_SIGNATURE) {
        initialize_from_mbr(d, bc, 0, 0);
        return;
    }

    detect_raw(d, bc);
}
