#define MSG_FMT(msg) "FAT: " msg

#include "common/log.h"
#include "common/constants.h"
#include "common/helpers.h"
#include "common/minmax.h"
#include "common/ctype.h"
#include "fat.h"
#include "structures.h"
#include "allocator.h"
#include "disk_services.h"

#define BPB_OFFSET 0x0B
#define EBPB_OLD_SIGNATURE 0x28
#define EBPB_SIGNATURE     0x29

#define FAT16_MIN_CLUSTER_COUNT 4085
#define FAT32_MIN_CLUSTER_COUNT 65525

// aka __builtin_ffs(sizeof(u32)) - 1
#define FAT32_FAT_INDEX_SHIFT 2

#define FAT32_CLUSTER_MASK 0x0FFFFFFF

// This capacity is picked so that the entire FAT is cached for both FAT12/16 at all times.
#define FAT_VIEW_BYTES (PAGE_SIZE * 32u)
BUILD_BUG_ON(FAT_VIEW_BYTES < ((FAT32_MIN_CLUSTER_COUNT - 1) * 2));

#define FAT_VIEW_CAPACITY_FAT32 (FAT_VIEW_BYTES / sizeof(u32))
#define FAT_VIEW_OFF_INVALID 0xFFFFFFFF

struct contiguous_file_range32 {
    u32 file_offset_cluster;
    u32 global_cluster;
};

struct contiguous_file_range16 {
    u16 file_offset_cluster;
    u16 global_cluster;
};

#define RANGES_PER_PAGE_FAT32       (PAGE_SIZE / sizeof(struct contiguous_file_range32))
#define RANGES_PER_PAGE_FAT12_OR_16 (PAGE_SIZE / sizeof(struct contiguous_file_range16))

#define IN_PLACE_RANGE_CAPACITY_BYTES (PAGE_SIZE - (8 * sizeof(void*)))

#define IN_PLACE_RANGE_CAPACITY_FAT32       (IN_PLACE_RANGE_CAPACITY_BYTES / \
                                             sizeof(struct contiguous_file_range32))

#define IN_PLACE_RANGE_CAPACITY_FAT12_OR_16 (IN_PLACE_RANGE_CAPACITY_BYTES / \
                                             sizeof(struct contiguous_file_range16))

struct fat_file {
    struct file f;

    union {
        u32 first_cluster;
        u32 first_sector_off; // FAT12/16 root directory
    };

    u32 range_count;

    /*
     * A contiguous_file_range array sorted in ascending order by file_offset_cluster.
     * Each range at i spans (range[i].file_offset_cluster -> range[i + 1].file_offset_cluster - 1) clusters
     * For last i the end is the last cluster of the file (inclusive).
     */
    void *ranges_extra;

    _Alignas(struct contiguous_file_range32)
    u8 in_place_ranges[IN_PLACE_RANGE_CAPACITY_BYTES];
};
BUILD_BUG_ON(sizeof(struct fat_file) > PAGE_SIZE);

enum fat_type {
    FAT_TYPE_12,
    FAT_TYPE_16,
    FAT_TYPE_32
};

struct fat_filesystem;

struct fat_ops {
    u32 eoc_val;
    u32 bad_val;
    u32 bits_per_cluster; // 12, 16 or 32
    u32 in_place_range_cap;
    u32 ranges_per_page;
    u32 range_stride;
    u32 (*get_fat_entry)(struct fat_filesystem*, u32);
    bool (*ensure_fat_entry_cached)(struct fat_filesystem*, u32);
    void (*file_insert_range)(void*, u32, struct contiguous_file_range32);
    size_t (*range_get_offset)(void*);
    size_t (*range_get_global_cluster)(void*);
};

struct fat_filesystem {
    struct filesystem f;
    struct fat_ops *fops;

    struct range fat_lba_range;
    struct range data_lba_range;
    u32 data_part_off;

    u16 fat_type;
    u16 root_dir_entries;

    union {
        // FAT32
        u32 root_dir_cluster;

        // FAT12/16 (offset from partition start)
        u32 root_dir_sector_off;
    };

    size_t fat_view_offset;
    void *fat_view;
};

static inline u8 cluster_shift(struct fat_filesystem *fs)
{
    return fs->f.block_shift;
}


// FAT12/16 root directory
#define DIR_FIXED_CAP_ROOT (1 << 1)
#define DIR_EOF            (1 << 0)

struct fat_dir_iter_ctx {
    union {
        u32 current_cluster;
        u32 first_sector_off;
    };
    u32 current_offset;
    u8 flags;
};
#define FAT_DIR_ITER_CTX(ctx) (struct fat_dir_iter_ctx*)((ctx)->opaque)

struct fat_dir_rec_data {
    union {
        u32 first_cluster;
        u32 first_sector_off;
    };
};
#define FAT_DIR_REC_DATA(rec) (struct fat_dir_rec_data*)((rec)->opaque)

static u8 generate_short_name_checksum(const char *name)
{
    u8 sum = 0;
    const u8 *byte_ptr = (const u8*)name;
    size_t i;

    for (i = FAT_FULL_SHORT_NAME_LENGTH; i != 0; i--) {
        sum = (sum >> 1) + ((sum & 1) << 7);
        sum += *byte_ptr++;
    }

    return sum;
}

enum fat_entry {
    FAT_ENTRY_FREE,
    FAT_ENTRY_RESERVED,
    FAT_ENTRY_BAD,
    FAT_ENTRY_END_OF_CHAIN,
    FAT_ENTRY_LINK,
};

#define RESERVED_CLUSTER_COUNT 2

#define FREE_CLUSTER_VALUE     0x00000000
#define RESERVED_CLUSTER_VALUE 0x00000001

#define FAT12_EOC_VALUE 0x00000FF8
#define FAT16_EOC_VALUE 0x0000FFF8
#define FAT32_EOC_VALUE 0x0FFFFFF8

#define FAT12_BAD_VALUE 0x00000FF7
#define FAT16_BAD_VALUE 0x0000FFF7
#define FAT32_BAD_VALUE 0x0FFFFFF7

static enum fat_entry entry_type_of_fat_value(u32 value, struct fat_ops *fops)
{
    value &= FAT32_CLUSTER_MASK;

    if (value == FREE_CLUSTER_VALUE)
        return FAT_ENTRY_FREE;
    if (value == RESERVED_CLUSTER_VALUE)
        return FAT_ENTRY_RESERVED;

    if (unlikely(value == fops->bad_val))
        return FAT_ENTRY_BAD;
    if (value >= fops->eoc_val)
        return FAT_ENTRY_END_OF_CHAIN;

    return FAT_ENTRY_LINK;
}

static u32 pure_cluster_value(u32 value)
{
    BUG_ON(value < RESERVED_CLUSTER_COUNT);
    return value - RESERVED_CLUSTER_COUNT;
}

static bool ensure_fat_view(struct fat_filesystem *fs)
{
    return fs->fat_view || (fs->fat_view = allocate_pages(FAT_VIEW_BYTES / PAGE_SIZE));
}

static bool ensure_fat_entry_cached_fat32(struct fat_filesystem *fs, u32 index)
{
    struct disk *d = &fs->f.d;
    u32 first_block, blocks_to_read;
    index &= ~(FAT_VIEW_CAPACITY_FAT32 - 1);

    if (!ensure_fat_view(fs))
        return false;

    // already have it cached
    if (fs->fat_view_offset == index)
        return true;

    fs->fat_view_offset = index;
    first_block = fs->fat_lba_range.begin + (index >> (d->block_shift - FAT32_FAT_INDEX_SHIFT));
    blocks_to_read = MIN(range_length(&fs->fat_lba_range), FAT_VIEW_BYTES >> d->block_shift);
    return ds_read_blocks(d->handle, fs->fat_view, first_block, blocks_to_read);
}

static bool ensure_fat_cached_fat12_or_16(struct fat_filesystem *fs, u32 index)
{
    struct disk *d = &fs->f.d;
    UNUSED(index); // we cache the entire fat anyway

    if (!ensure_fat_view(fs))
        return false;

    // already have it cached
    if (fs->fat_view_offset != FAT_VIEW_OFF_INVALID)
        return true;

    fs->fat_view_offset = 0;
    return ds_read_blocks(d->handle, fs->fat_view, fs->fat_lba_range.begin,
                          range_length(&fs->fat_lba_range));
}

static u32 get_fat_entry_fat12(struct fat_filesystem *fs, u32 index)
{
    void *view_offset = fs->fat_view + (index + (index / 2));
    u32 out_val = *(u16*)view_offset;

    if (index & 1)
        out_val >>= 4;
    else
        out_val &= 0x0FFF;

    return out_val;
}

static u32 get_fat_entry_fat16(struct fat_filesystem *fs, u32 index)
{
    return ((u16*)fs->fat_view)[index];
}

static u32 get_fat_entry_fat32(struct fat_filesystem *fs, u32 index)
{
    return ((u32*)fs->fat_view)[index - fs->fat_view_offset] & FAT32_CLUSTER_MASK;
}

static u32 fat_entry_at(struct fat_filesystem *fs, u32 index)
{
    struct fat_ops *fops = fs->fops;

    bool cached = fops->ensure_fat_entry_cached(fs, index);

    // OOM, disk read error, corrupted fs etc
    if (unlikely(!cached))
        return fops->bad_val;

    return fops->get_fat_entry(fs, index);
}

static void file_insert_range_fat32(void *ranges, u32 idx, struct contiguous_file_range32 range)
{
    ((struct contiguous_file_range32*)ranges)[idx] = range;
}

static void file_insert_range_fat12_or_16(void *ranges, u32 idx, struct contiguous_file_range32 range)
{
    ((struct contiguous_file_range16*)ranges)[idx] = (struct contiguous_file_range16) {
        .global_cluster = range.global_cluster,
        .file_offset_cluster = range.file_offset_cluster
    };
}

static bool file_emplace_range(struct fat_file *file, struct contiguous_file_range32 range,
                               struct fat_ops *fops)
{
    u32 offset_into_extra;
    size_t extra_range_pages, extra_range_capacity;

    if (file->range_count < fops->in_place_range_cap) {
        fops->file_insert_range(file->in_place_ranges, file->range_count++, range);
        return true;
    }

    offset_into_extra = file->range_count - fops->in_place_range_cap;
    extra_range_pages = CEILING_DIVIDE(offset_into_extra, fops->ranges_per_page);
    extra_range_capacity = extra_range_pages * fops->ranges_per_page;

    if (extra_range_capacity == offset_into_extra) {
        struct contiguous_file_range *new_extra = allocate_pages(extra_range_pages + 1);
        if (!new_extra)
            return false;

        memcpy(new_extra, file->ranges_extra, extra_range_pages * PAGE_SIZE);
        free_pages(file->ranges_extra, extra_range_pages);
        file->ranges_extra = new_extra;
    }

    fops->file_insert_range(file->ranges_extra, file->range_count++, range);
    return true;
}

static bool file_compute_contiguous_ranges(struct fat_file *file)
{
    struct contiguous_file_range32 range = {
        .file_offset_cluster = 0,
        .global_cluster = file->first_cluster
    };
    u32 current_file_offset = 1;
    u32 current_cluster = file->first_cluster;
    struct fat_filesystem *fs = container_of(file->f.fs, struct fat_filesystem, f);

    for (;;) {
        u32 next_cluster = fat_entry_at(fs, current_cluster);

        switch (entry_type_of_fat_value(next_cluster, fs->fops)) {
        case FAT_ENTRY_END_OF_CHAIN: {
            if (unlikely((current_file_offset << cluster_shift(fs)) < file->f.size)) {
                print_warn("EOC before end of file");
                return false;
            }

            if (!file_emplace_range(file, range, fs->fops))
                return false;

            return true;
        }
        case FAT_ENTRY_LINK:
            if (next_cluster == current_cluster + 1)
                break;

            if (!file_emplace_range(file, range, fs->fops))
                return false;

            range = (struct contiguous_file_range32) { current_file_offset + 1, next_cluster };
            break;
        default:
            print_warn("Unexpected cluster %u in chain after %u\n", next_cluster, current_cluster);
            return false;
        }

        current_cluster = next_cluster;
        current_file_offset++;
    }
}

static size_t range32_get_offset(void *range)
{
    return ((struct contiguous_file_range32*)range)->file_offset_cluster;
}

static size_t range16_get_offset(void *range)
{
    return ((struct contiguous_file_range16*)range)->file_offset_cluster;
}

static size_t range32_get_global_cluster(void *range)
{
    return ((struct contiguous_file_range32*)range)->global_cluster;
}

static size_t range16_get_global_cluster(void *range)
{
    return ((struct contiguous_file_range16*)range)->global_cluster;
}

static bool fat_read(struct fat_filesystem *fs, u32 cluster, u32 offset, u32 bytes, void* buffer)
{
    u64 offset_to_read;

    offset_to_read = fs->data_lba_range.begin;
    offset_to_read <<= fs->f.d.block_shift;
    offset_to_read += cluster << cluster_shift(fs);
    offset_to_read += offset;

    return ds_read(fs->f.d.handle, buffer, offset_to_read, bytes);
}

static bool fixed_root_dir_fetch_next_entry(struct fat_filesystem *fs, struct fat_dir_iter_ctx *ctx,
                                            void *entry)
{
    struct disk *d = &fs->f.d;
    u64 offset_to_read;

    if ((ctx->current_offset / sizeof(struct fat_directory_entry)) == fs->root_dir_entries) {
       ctx->flags |= DIR_EOF;
       return false;
    }

    offset_to_read = fs->f.lba_range.begin + ctx->first_sector_off;
    offset_to_read <<= d->block_shift;
    offset_to_read += ctx->current_offset;
    ctx->current_offset += sizeof(struct fat_directory_entry);

    return ds_read(d->handle, entry, offset_to_read, sizeof(struct fat_directory_entry));
}

static bool dir_fetch_next_entry(struct fat_filesystem *fs, struct fat_dir_iter_ctx *ctx,
                                 void* entry)
{
    if (ctx->flags & DIR_EOF)
        return false;

    if (ctx->flags & DIR_FIXED_CAP_ROOT)
        return fixed_root_dir_fetch_next_entry(fs, ctx, entry);

    if ((ctx->current_offset >> cluster_shift(fs)) == 1) {
        u32 next_cluster = fat_entry_at(fs, ctx->current_cluster);

        if (entry_type_of_fat_value(next_cluster, fs->fops) != FAT_ENTRY_LINK) {
            ctx->flags |= DIR_EOF;
            return false;
        }

        ctx->current_cluster = next_cluster;
        ctx->current_offset = 0;
    }

    bool ok = fat_read(fs, pure_cluster_value(ctx->current_cluster),
                       ctx->current_offset, sizeof(struct fat_directory_entry), entry);
    ctx->flags |= !ok ? DIR_EOF : 0;
    ctx->current_offset += sizeof(struct fat_directory_entry);

    return ok;
}

static void process_normal_entry(struct fat_directory_entry *entry, struct dir_rec *out, bool is_small)
{
    struct fat_dir_rec_data *fd = FAT_DIR_REC_DATA(out);
    struct string_view name_view = { .text = entry->filename, FAT_SHORT_NAME_LENGTH };
    struct string_view extension_view = { .text = entry->extension, FAT_SHORT_EXTENSION_LENGTH };

    if (entry->case_info & LOWERCASE_NAME_BIT)
        str_tolower(entry->filename, FAT_SHORT_NAME_LENGTH);
    if (entry->case_info & LOWERCASE_EXTENSION_BIT)
        str_tolower(entry->extension, FAT_SHORT_EXTENSION_LENGTH);

    if (!is_small) {
        ssize_t name_len = sv_find(name_view, SV(" "), 0);
        ssize_t extension_len = sv_find(extension_view, SV(" "), 0);

        if (name_len < 0)
            name_len = FAT_SHORT_NAME_LENGTH;
        if (extension_len < 0)
            extension_len = FAT_SHORT_EXTENSION_LENGTH;

        memcpy(out->name, name_view.text, name_len);

        if (extension_len) {
            out->name[name_len++] = '.';
            memcpy(out->name + name_len, extension_view.text, extension_len);
        }

        out->name_len = name_len + extension_len;
    }

    out->size = entry->size;
    fd->first_cluster = ((u32)entry->cluster_high << 16) | entry->cluster_low;
    out->flags = (entry->attributes & SUBDIR_ATTRIBUTE) ? DIR_REC_SUBDIR : 0;
}

static size_t ucs2_to_ascii(const u8 *ucs2, size_t count, char **out)
{
    size_t i;

    for (i = 0; i < (count * BYTES_PER_UCS2_CHAR); i += BYTES_PER_UCS2_CHAR) {
        u16 ucs2_char = ucs2[i] | ((u16)ucs2[i + 1] << 8);

        char ascii;

        if (ucs2_char == 0) {
            return (i / BYTES_PER_UCS2_CHAR);
        } else if (ucs2_char > 127) {
            ascii = '?';
        } else {
            ascii = (char)(ucs2_char & 0xFF);
        }

        *(*out)++ = ascii;
    }

    return count;
}

#define MAX_SEQUENCE_NUMBER 20
#define MAX_NAME_LENGTH 255
BUILD_BUG_ON(MAX_NAME_LENGTH > DIR_REC_MAX_NAME_LEN);

/*
 * Since you can have at max 20 chained long entries, the theoretical limit is 20 * 13 characters,
 * however, the actual allowed limit is 255, which would limit the 20th entry contribution to only 8 characters.
 */
#define CHARS_FOR_LAST_LONG_ENTRY 8

static bool fat_next_dir_rec(struct filesystem *base_fs, struct dir_iter_ctx *ctx,
                             struct dir_rec *out_rec)
{
    struct fat_filesystem *fs = container_of(base_fs, struct fat_filesystem, f);
    struct fat_dir_iter_ctx *fctx = FAT_DIR_ITER_CTX(ctx);
    struct fat_directory_entry normal_entry;

    if (fctx->flags & DIR_EOF)
        return false;

    for (;;) {
        bool is_long;
        struct long_name_fat_directory_entry *long_entry;
        u8 checksum, initial_sequence_number, sequence_number;
        char *name_ptr;
        size_t i, chars_written = 0;
        u32 checksum_array[MAX_SEQUENCE_NUMBER] = { 0 };

        if (!dir_fetch_next_entry(fs, fctx, &normal_entry))
            return false;

        if ((u8)normal_entry.filename[0] == DELETED_FILE_MARK)
            continue;

        if ((u8)normal_entry.filename[0] == END_OF_DIRECTORY_MARK) {
            fctx->flags |= DIR_EOF;
            return false;
        }

        if (normal_entry.attributes & DEVICE_ATTRIBUTE)
            continue;

        is_long = (normal_entry.attributes & LONG_NAME_ATTRIBUTE) == LONG_NAME_ATTRIBUTE;
        if (!is_long && (normal_entry.attributes & VOLUME_LABEL_ATTRIBUTE))
            continue;

        if (!is_long) {
            process_normal_entry(&normal_entry, out_rec, false);
            return true;
        }

        long_entry = (struct long_name_fat_directory_entry*)&normal_entry;

        initial_sequence_number = long_entry->sequence_number & SEQUENCE_NUM_BIT_MASK;
        sequence_number = initial_sequence_number;
        if (!(long_entry->sequence_number & LAST_LOGICAL_ENTRY_BIT))
            return false;

        name_ptr = out_rec->name + MAX_NAME_LENGTH;
        name_ptr -= CHARS_FOR_LAST_LONG_ENTRY;

        for (;;) {
            char *local_name_ptr = name_ptr;
            size_t name_chars;

            name_chars = ucs2_to_ascii(long_entry->name_1, NAME_1_CHARS, &local_name_ptr);
            chars_written += name_chars;

            if (name_chars == NAME_1_CHARS) {
                name_chars = ucs2_to_ascii(long_entry->name_2, NAME_2_CHARS, &local_name_ptr);
                chars_written += name_chars;
            }

            if (name_chars == NAME_2_CHARS) {
                name_chars = ucs2_to_ascii(long_entry->name_3, NAME_3_CHARS, &local_name_ptr);
                chars_written += name_chars;
            }

            checksum_array[sequence_number - 1] = long_entry->checksum;

            if (sequence_number == 1) {
                if (!dir_fetch_next_entry(fs, fctx, &normal_entry))
                    return false;

                break;
            }

            if (!dir_fetch_next_entry(fs, fctx, &normal_entry))
                return false;

            --sequence_number;
            name_ptr -= CHARS_PER_LONG_ENTRY;
        }

        BUG_ON(chars_written >= MAX_NAME_LENGTH);

        if (name_ptr != out_rec->name)
            memmove(out_rec->name, name_ptr, chars_written);

        out_rec->name_len = chars_written;
        process_normal_entry(&normal_entry, out_rec, true);

        checksum = generate_short_name_checksum(normal_entry.filename);

        for (i = 0; i < initial_sequence_number; ++i) {
            if (checksum_array[i] == checksum)
                continue;

            print_warn("invalid file checksum\n");
            return false;
        }

        return true;
    }
}

static size_t find_range_idx(void *ranges, size_t count, size_t offset,
                             struct fat_ops *fops)
{
    size_t left = 0;
    size_t right = count - 1;
    void *out_range;

    while (left <= right) {
        size_t middle = left + ((right - left) / 2);
        void *mid_range = ranges + (middle * fops->range_stride);
        size_t file_offset = fops->range_get_offset(mid_range);

        if (file_offset < offset) {
            left = middle + 1;
        } else if (offset < file_offset) {
            right = middle - 1;
        } else {
            return middle;
        }
    }
    out_range = ranges + (right * fops->range_stride);

    /*
     * right should always point to lower bound - 1,
     * aka range that this offset is a part of.
     */
    BUG_ON(fops->range_get_offset(out_range) > offset);
    return right;
}

static void *get_range(void *ranges, size_t idx, u32 stride)
{
    return ranges + (idx * stride);
}

static u64 cluster_as_part_off(u32 cluster,  struct fat_filesystem *fs)
{
    u64 off;

    off = pure_cluster_value(cluster);
    off <<= fs_block_shift(&fs->f);
    off += fs->data_part_off;

    return off;
}

static bool fat_file_get_range(struct file *base_file, u64 file_block_off,
                               size_t want_blocks, struct block_range *out_range)
{
    struct filesystem *base_fs = base_file->fs;
    struct fat_filesystem *fs = container_of(base_fs, struct fat_filesystem, f);
    struct fat_file *f = container_of(base_file, struct fat_file, f);
    struct fat_ops *fops = fs->fops;
    u32 this_range_offset, range_count;
    size_t range_len, range_idx, range_idx_global = 0;
    void *this_range, *ranges = f->in_place_ranges;

    if (!file_compute_contiguous_ranges(f))
        return false;
    range_count = f->range_count;

    if (f->ranges_extra && fops->range_get_offset(f->ranges_extra) >= file_block_off) {
        range_idx_global = fops->in_place_range_cap;
        ranges = f->ranges_extra;
        range_count -= range_idx_global;
    }

    range_idx = find_range_idx(ranges, range_count, file_block_off, fops);
    this_range = get_range(ranges, range_idx, fops->range_stride);
    this_range_offset = file_block_off - fops->range_get_offset(this_range);
    range_idx_global += ++range_idx;

    if (range_idx_global == f->range_count) {
        range_len = -1;
    } else {
        void *next_range;

        if (range_idx_global == fops->in_place_range_cap) {
            ranges = f->ranges_extra;
            range_idx = 0;
        }

        next_range = get_range(ranges, range_idx, fops->range_stride);
        range_len = fops->range_get_offset(next_range) - this_range_offset;
    }

    this_range_offset += fops->range_get_global_cluster(this_range);
    out_range->part_byte_off = cluster_as_part_off(this_range_offset, fs);
    out_range->blocks = MIN(want_blocks, range_len);

    return true;
}

static bool fat_read_file(struct file *f, void *buf, u64 off, u32 bytes)
{
    return bulk_read_file(f, buf, off, bytes, fat_file_get_range);
}

static struct fat_file *fat_do_open_file(struct fat_filesystem *fs, u32 first_cluster, u32 size)
{
    struct fat_file *file = allocate_bytes(sizeof(struct fat_file));
    if (!file)
        return NULL;

    file->f = (struct file) {
        .fs = &fs->f,
        .size = size
    };

    file->ranges_extra = NULL;
    file->range_count = 0;
    file->first_cluster = first_cluster;
    return file;
}

static struct file *fat_open_file(struct filesystem *base_fs, struct dir_rec *rec)
{
    struct fat_filesystem *fs = container_of(base_fs, struct fat_filesystem, f);
    struct fat_dir_rec_data *fd = FAT_DIR_REC_DATA(rec);
    struct fat_file *file;

    BUG_ON(rec->flags & DIR_REC_SUBDIR);

    file = fat_do_open_file(fs, fd->first_cluster, rec->size);
    if (!file)
        return NULL;

    return &file->f;
}

static void fat_iter_ctx_init(struct filesystem *base_fs, struct dir_iter_ctx *ctx,
                              struct dir_rec *rec)
{
    struct fat_filesystem *fs = container_of(base_fs, struct fat_filesystem, f);
    struct fat_dir_iter_ctx *fctx = FAT_DIR_ITER_CTX(ctx);

    fctx->current_cluster = 0;
    fctx->current_offset = 0;
    fctx->flags = 0;

    if (rec) {
        struct fat_dir_rec_data *fd = FAT_DIR_REC_DATA(rec);
        fctx->current_cluster = fd->first_cluster;
    }

    /*
     * Tried to open '..' in a root subdir or rec is NULL.
     * Either way what we do here is target the ctx at the root directory.
     */
    if (fctx->current_cluster == 0) {
        fctx->current_cluster = fs->root_dir_cluster;

        if (fs->fat_type != FAT_TYPE_32)
            fctx->flags |= DIR_FIXED_CAP_ROOT;
    }
}

static void fat_file_free(struct fat_file *file, struct fat_ops *fops)
{
    if (file->ranges_extra) {
        size_t offset_into_extra = file->range_count - fops->in_place_range_cap;
        size_t extra_range_capacity = CEILING_DIVIDE(offset_into_extra, fops->ranges_per_page);
        free_pages(file->ranges_extra, extra_range_capacity);
    }

    free_bytes(file, sizeof(struct fat_file));
}

static void fat_file_close(struct file *f)
{
    struct fat_file *file = container_of(f, struct fat_file, f);
    struct fat_filesystem *fs = container_of(file->f.fs, struct fat_filesystem, f);

    fat_file_free(file, fs->fops);
}

struct fat_info {
    enum fat_type type;
    u32 fat_count;
    u32 sectors_per_cluster;
    u32 sectors_per_fat;
    u32 cluster_count;
    u32 reserved_sectors;

    // FAT32
    u32 root_dir_cluster;

    // FAT12/16
    u16 root_dir_sectors;
    u16 max_root_dir_entries;
};

static bool check_fs_type(struct string_view expected, const char *actual)
{
    int res = memcmp(expected.text, actual, expected.size);
    if (res != 0) {
        struct string_view actual_view = { actual, expected.size };
        print_warn("unexpected file system type: %pSV\n", &actual_view);
    }

    return res == 0;
}

static bool detect_fat(const struct disk *d, struct range lba_range, struct dos33_bpb *bpb33,
                       struct fat_info *out_info)
{
    struct dos20_bpb *bpb20 = &bpb33->d20_bpb;
    struct fat12_or_16_ebpb *ebpb16 = (struct fat12_or_16_ebpb*)bpb33;
    struct fat32_ebpb *ebpb32 = (struct fat32_ebpb*)bpb33;
    bool ebpb16_valid = false, ebpb32_valid = false;
    u32 root_dir_bytes, data_sectors;

    if (__builtin_popcount(bpb20->bytes_per_sector) != 1)
        return false;
    if ((bpb20->bytes_per_sector >> d->block_shift) != 1)
        return false;

    ebpb16_valid = ebpb16->signature == EBPB_OLD_SIGNATURE || ebpb16->signature == EBPB_SIGNATURE;
    if (ebpb16->signature < EBPB_OLD_SIGNATURE)
        ebpb32_valid = ebpb32->signature == EBPB_OLD_SIGNATURE ||
                       ebpb32->signature == EBPB_SIGNATURE;

    out_info->fat_count = bpb20->fat_count;
    out_info->sectors_per_cluster = bpb20->sectors_per_cluster;
    out_info->sectors_per_fat = bpb20->sectors_per_fat_fat12_or_16;
    out_info->reserved_sectors = bpb20->reserved_sectors;
    out_info->max_root_dir_entries = bpb20->max_root_dir_entries;

    if (out_info->sectors_per_fat == 0) {
        // Old EBPB format but sectors per fat not set
        if (!ebpb32_valid)
            return false;

        out_info->sectors_per_fat = ebpb32->sectors_per_fat;
    }

    if (!out_info->fat_count)
        return false;
    if (!out_info->sectors_per_cluster || (__builtin_popcount(out_info->sectors_per_cluster) != 1))
        return false;
    if (!out_info->sectors_per_fat)
        return false;
    if (!out_info->reserved_sectors)
        return false;

    root_dir_bytes = out_info->max_root_dir_entries * sizeof(struct fat_directory_entry);
    out_info->root_dir_sectors = CEILING_DIVIDE(root_dir_bytes, 1ul << d->block_shift);

    data_sectors = range_length(&lba_range);

    data_sectors -= out_info->reserved_sectors;
    data_sectors -= out_info->root_dir_sectors;
    data_sectors -= out_info->fat_count * out_info->sectors_per_fat;
    out_info->cluster_count = data_sectors / out_info->sectors_per_cluster;

    if (out_info->cluster_count < FAT16_MIN_CLUSTER_COUNT) {
        if (ebpb16_valid)
            check_fs_type(SV("FAT12   "), ebpb16->filesystem_type);

        out_info->type = FAT_TYPE_12;
        return out_info->max_root_dir_entries != 0;
    }

    if (out_info->cluster_count < FAT32_MIN_CLUSTER_COUNT) {
        if (ebpb16_valid)
            check_fs_type(SV("FAT16   "), ebpb16->filesystem_type);

        out_info->type = FAT_TYPE_16;
        return out_info->max_root_dir_entries != 0;
    }

    if (!ebpb32_valid)
        return false;

    check_fs_type(SV("FAT32   "), ebpb32->filesystem_type);

    out_info->type = FAT_TYPE_32;
    out_info->root_dir_cluster = ebpb32->root_dir_cluster;
    return out_info->root_dir_cluster >= RESERVED_CLUSTER_COUNT;
}

static struct fat_ops fat12_ops = {
    .eoc_val = FAT12_EOC_VALUE,
    .bad_val = FAT12_BAD_VALUE,
    .bits_per_cluster = 12,
    .in_place_range_cap = IN_PLACE_RANGE_CAPACITY_FAT12_OR_16,
    .ranges_per_page = RANGES_PER_PAGE_FAT12_OR_16,
    .range_stride = sizeof(struct contiguous_file_range16),
    .get_fat_entry = get_fat_entry_fat12,
    .ensure_fat_entry_cached = ensure_fat_cached_fat12_or_16,
    .file_insert_range = file_insert_range_fat12_or_16,
    .range_get_offset = range16_get_offset,
    .range_get_global_cluster = range16_get_global_cluster
};

static struct fat_ops fat16_ops = {
    .eoc_val = FAT16_EOC_VALUE,
    .bad_val = FAT16_BAD_VALUE,
    .bits_per_cluster = 16,
    .in_place_range_cap = IN_PLACE_RANGE_CAPACITY_FAT12_OR_16,
    .ranges_per_page = RANGES_PER_PAGE_FAT12_OR_16,
    .range_stride = sizeof(struct contiguous_file_range16),
    .get_fat_entry = get_fat_entry_fat16,
    .ensure_fat_entry_cached = ensure_fat_cached_fat12_or_16,
    .file_insert_range = file_insert_range_fat12_or_16,
    .range_get_offset = range16_get_offset,
    .range_get_global_cluster = range16_get_global_cluster
};

static struct fat_ops fat32_ops = {
    .eoc_val = FAT32_EOC_VALUE,
    .bad_val = FAT32_BAD_VALUE,
    .bits_per_cluster = 32,
    .in_place_range_cap = IN_PLACE_RANGE_CAPACITY_FAT32,
    .ranges_per_page = RANGES_PER_PAGE_FAT32,
    .range_stride = sizeof(struct contiguous_file_range32),
    .get_fat_entry = get_fat_entry_fat32,
    .ensure_fat_entry_cached = ensure_fat_entry_cached_fat32,
    .file_insert_range = file_insert_range_fat32,
    .range_get_offset = range32_get_offset,
    .range_get_global_cluster = range32_get_global_cluster
};

struct fat_ops *ft_to_fat_ops[] = {
    [FAT_TYPE_12] = &fat12_ops,
    [FAT_TYPE_16] = &fat16_ops,
    [FAT_TYPE_32] = &fat32_ops
};

struct filesystem *try_create_fat(const struct disk *d, struct range lba_range,
                                  struct block_cache *bc)
{
    void *bpb;
    u64 abs_bpb_off = (lba_range.begin << d->block_shift) + BPB_OFFSET;
    struct fat_filesystem *fs;
    struct fat_ops *fops;
    struct fat_info info = { 0 };
    bool ok;

    if (!block_cache_take_ref(bc, &bpb, abs_bpb_off, sizeof(struct fat32_ebpb)))
        return NULL;

    ok = detect_fat(d, lba_range, bpb, &info);
    block_cache_release_ref(bc);

    if (!ok)
        return NULL;

    fops = ft_to_fat_ops[info.type];
    print_info("detected fat%d with %d fats, %d sectors/cluster, %u sectors/fat\n",
               fops->bits_per_cluster, info.fat_count, info.sectors_per_cluster,
               info.sectors_per_fat);

    fs = allocate_bytes(sizeof(struct fat_filesystem));
    if (unlikely(!fs))
        return NULL;

    fs->f = (struct filesystem) {
        .d = *d,
        .lba_range = lba_range,
        .iter_ctx_init = fat_iter_ctx_init,
        .next_dir_rec = fat_next_dir_rec,
        .open_file = fat_open_file,
        .close_file = fat_file_close,
        .read_file = fat_read_file,
    };

    fs->fops = fops;
    fs->fat_type = info.type;
    fs->fat_view = NULL;
    fs->fat_view_offset = FAT_VIEW_OFF_INVALID;

    range_advance_begin(&lba_range, info.reserved_sectors);

    fs->fat_lba_range = lba_range;
    range_set_length(&fs->fat_lba_range, info.sectors_per_fat);

    range_advance_begin(&lba_range, info.sectors_per_fat * info.fat_count);

    switch (info.type) {
    case FAT_TYPE_12:
    case FAT_TYPE_16:
        fs->root_dir_sector_off = lba_range.begin - fs->f.lba_range.begin;
        fs->root_dir_entries = info.max_root_dir_entries;
        range_advance_begin(&lba_range, info.root_dir_sectors);
        break;
    case FAT_TYPE_32:
        fs->root_dir_cluster = info.root_dir_cluster;
        break;
    default:
        BUG();
    }

    fs->data_lba_range = lba_range;
    fs->data_part_off = (fs->data_lba_range.begin - fs->f.lba_range.begin) << d->block_shift;
    fs->f.block_shift = (__builtin_ffs(info.sectors_per_cluster) - 1) + d->block_shift;

    return &fs->f;
}
