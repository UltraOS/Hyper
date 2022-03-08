#include "fat.h"
#include "common/log.h"
#include "common/constants.h"
#include "common/helpers.h"
#include "common/minmax.h"
#include "common/ctype.h"
#include "allocator.h"

#undef MSG_FMT
#define MSG_FMT(msg) "FAT: " msg

#define BPB_OFFSET 0x0B
#define EBPB_OLD_SIGNATURE 0x28
#define EBPB_SIGNATURE     0x29

#define FAT16_MIN_CLUSTER_COUNT 4085
#define FAT32_MIN_CLUSTER_COUNT 65525

#define FAT32_CLUSTER_MASK 0x0FFFFFFF

// This capacity is picked so that the entire FAT is cached for both FAT12/16 at all times.
#define FAT_VIEW_BYTES (PAGE_SIZE * 32)
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

static void *find_range(void *ranges, size_t count, size_t range_stride,
                        size_t (*range_get_offset)(void*), size_t offset)
{
    size_t left = 0;
    size_t right = count - 1;
    void *out_range;

    while (left <= right) {
        size_t middle = left + ((right - left) / 2);
        void *mid_range = ranges + (middle * range_stride);
        size_t file_offset = range_get_offset(mid_range);

        if (file_offset < offset) {
            left = middle + 1;
        } else if (offset < file_offset) {
            right = middle - 1;
        } else {
            return mid_range;
        }
    }
    out_range = ranges + (right * range_stride);

    /*
     * right should always point to lower bound - 1,
     * aka range that this offset is a part of.
     */
    BUG_ON(range_get_offset(out_range) > offset);
    return out_range;
}

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

static u32 ft_to_size[] = {
    [FAT_TYPE_12] = 12,
    [FAT_TYPE_16] = 16,
    [FAT_TYPE_32] = 32,
};

static u32 ft_to_in_place_range_capacity[] = {
    [FAT_TYPE_12] = IN_PLACE_RANGE_CAPACITY_FAT12_OR_16,
    [FAT_TYPE_16] = IN_PLACE_RANGE_CAPACITY_FAT12_OR_16,
    [FAT_TYPE_32] = IN_PLACE_RANGE_CAPACITY_FAT32,
};

static u32 ft_to_ranges_per_page[] = {
    [FAT_TYPE_12] = RANGES_PER_PAGE_FAT12_OR_16,
    [FAT_TYPE_16] = RANGES_PER_PAGE_FAT12_OR_16,
    [FAT_TYPE_32] = RANGES_PER_PAGE_FAT32,
};

static u32 ft_to_range_stride[] = {
    [FAT_TYPE_12] = sizeof(struct contiguous_file_range16),
    [FAT_TYPE_16] = sizeof(struct contiguous_file_range16),
    [FAT_TYPE_32] = sizeof(struct contiguous_file_range32)
};

struct fat_filesystem {
    struct filesystem f;

    struct range fat_lba_range;
    struct range data_lba_range;

    u16 fat_type;
    u16 root_dir_entries;

    union {
        // FAT32
        u32 root_dir_cluster;

        // FAT12/16 (offset from partition start)
        u32 root_dir_sector_off;
    };

    u32 bytes_per_cluster;
    u32 fat_clusters;

    size_t fat_view_offset;
    void *fat_view;

    struct fat_file *root_directory;
};


// FAT12/16 root directory
#define DIR_FIXED_CAP_ROOT (1 << 1)
#define DIR_EOF            (1 << 0)

struct fat_directory {
    struct fat_filesystem *parent;
    union {
        u32 current_cluster;
        u32 first_sector_off;
    };
    u32 current_offset;
    u8 flags;
};

struct fat_directory_record {
    char name[255];
    u8 name_length;

    bool is_directory;
    u32 first_cluster;
    u32 size;
};

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

static u32 ft_to_eoc_value[] = {
    [FAT_TYPE_12] = FAT12_EOC_VALUE,
    [FAT_TYPE_16] = FAT16_EOC_VALUE,
    [FAT_TYPE_32] = FAT32_EOC_VALUE
};

#define FAT12_BAD_VALUE 0x00000FF7
#define FAT16_BAD_VALUE 0x0000FFF7
#define FAT32_BAD_VALUE 0x0FFFFFF7

static u32 ft_to_bad_value[] = {
    [FAT_TYPE_12] = FAT12_BAD_VALUE,
    [FAT_TYPE_16] = FAT16_BAD_VALUE,
    [FAT_TYPE_32] = FAT32_BAD_VALUE
};

static enum fat_entry entry_type_of_fat_value(u32 value, enum fat_type ft)
{
    value &= FAT32_CLUSTER_MASK;

    if (value == FREE_CLUSTER_VALUE)
        return FAT_ENTRY_FREE;
    if (value == RESERVED_CLUSTER_VALUE)
        return FAT_ENTRY_RESERVED;

    if (unlikely(value == ft_to_bad_value[ft]))
        return FAT_ENTRY_BAD;
    if (value >= ft_to_eoc_value[ft])
        return FAT_ENTRY_END_OF_CHAIN;

    return FAT_ENTRY_LINK;
}

static u32 pure_cluster_value(u32 value)
{
    BUG_ON(value < RESERVED_CLUSTER_COUNT);
    return value - RESERVED_CLUSTER_COUNT;
}

static struct fat_file *fat_do_open_file(struct fat_filesystem *fs, u32 root_cluster, u32 size);

static bool ensure_root_directory(struct fat_filesystem *fs) {
    if (fs->root_directory)
        return true;

    fs->root_directory = fat_do_open_file(fs, fs->root_dir_cluster, 0);
    return fs->root_directory != NULL;
}

static bool ensure_fat_view(struct fat_filesystem *fs)
{
    return fs->fat_view || (fs->fat_view = allocate_pages(FAT_VIEW_BYTES / PAGE_SIZE));
}

static bool ensure_fat_entry_cached_fat32(struct fat_filesystem *fs, u32 index)
{
    struct disk_services *srvc = filesystem_backend();
    struct disk *d = &fs->f.d;
    u32 first_block, blocks_to_read;
    u32 fat_entries_per_block = d->bytes_per_sector / sizeof(u32); // TODO: cache this?
    index &= ~(FAT_VIEW_CAPACITY_FAT32 - 1);

    if (unlikely(!srvc))
        return false;
    if (!ensure_fat_view(fs))
        return false;

    BUG_ON(index >= fs->fat_clusters);

    // already have it cached
    if (fs->fat_view_offset == index)
        return true;

    fs->fat_view_offset = index;
    first_block = fs->fat_lba_range.begin + (index / fat_entries_per_block);
    blocks_to_read = MIN(range_length(&fs->fat_lba_range), FAT_VIEW_BYTES / (size_t)d->bytes_per_sector);
    return srvc->read_blocks(d->handle, fs->fat_view, first_block, blocks_to_read);
}

static bool ensure_fat_cached_fat12_or_16(struct fat_filesystem *fs, u32 index)
{
    struct disk_services *srvc = filesystem_backend();
    struct disk *d = &fs->f.d;
    UNUSED(index); // we cache the entire fat anyway

    if (!ensure_fat_view(fs))
        return false;
    if (unlikely(!srvc))
        return false;

    // already have it cached
    if (fs->fat_view_offset != FAT_VIEW_OFF_INVALID)
        return true;

    fs->fat_view_offset = 0;
    return srvc->read_blocks(d->handle, fs->fat_view, fs->fat_lba_range.begin,
                             range_length(&fs->fat_lba_range));
}

static u32 extract_cached_fat_entry_at_index_fat12(struct fat_filesystem *fs, u32 index)
{
    void *view_offset = fs->fat_view + (index + (index / 2));
    u32 out_val = *(u16*)view_offset;

    if (index & 1)
        out_val >>= 4;
    else
        out_val &= 0x0FFF;

    return out_val;
}

static u32 extract_cached_fat_entry_at_index_fat16(struct fat_filesystem *fs, u32 index)
{
    return ((u16*)fs->fat_view)[index];
}

static u32 extract_cached_fat_entry_at_index_fat32(struct fat_filesystem *fs, u32 index)
{
    return ((u32*)fs->fat_view)[index - fs->fat_view_offset] & FAT32_CLUSTER_MASK;
}

static u32 (*extract_cached_fat_entry_at_index[])(struct fat_filesystem*, u32) = {
    [FAT_TYPE_12] = extract_cached_fat_entry_at_index_fat12,
    [FAT_TYPE_16] = extract_cached_fat_entry_at_index_fat16,
    [FAT_TYPE_32] = extract_cached_fat_entry_at_index_fat32,
};

static bool (*ensure_fat_entry_cached[])(struct fat_filesystem*, u32) = {
    [FAT_TYPE_12] = ensure_fat_cached_fat12_or_16,
    [FAT_TYPE_16] = ensure_fat_cached_fat12_or_16,
    [FAT_TYPE_32] = ensure_fat_entry_cached_fat32,
};

static u32 fat_entry_at(struct fat_filesystem *fs, u32 index)
{
    bool cached = ensure_fat_entry_cached[fs->fat_type](fs, index);

    // OOM, disk read error, corrupted fs etc
    if (unlikely(!cached))
        return ft_to_bad_value[fs->fat_type];

    return extract_cached_fat_entry_at_index[fs->fat_type](fs, index);
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

static void (*file_insert_range[])(void*, u32, struct contiguous_file_range32) = {
    [FAT_TYPE_12] = file_insert_range_fat12_or_16,
    [FAT_TYPE_16] = file_insert_range_fat12_or_16,
    [FAT_TYPE_32] = file_insert_range_fat32
};

static bool file_emplace_range(struct fat_file *file, struct contiguous_file_range32 range,
                               enum fat_type ft)
{
    u32 offset_into_extra;
    size_t extra_range_pages, extra_range_capacity;

    if (file->range_count < ft_to_in_place_range_capacity[ft]) {
        file_insert_range[ft](file->in_place_ranges, file->range_count++, range);
        return true;
    }

    offset_into_extra = file->range_count - ft_to_in_place_range_capacity[ft];
    extra_range_pages = CEILING_DIVIDE(offset_into_extra, ft_to_ranges_per_page[ft]);
    extra_range_capacity = extra_range_pages * ft_to_ranges_per_page[ft];

    if (extra_range_capacity == offset_into_extra) {
        struct contiguous_file_range *new_extra = allocate_pages(extra_range_pages + 1);
        if (!new_extra)
            return false;

        memcpy(new_extra, file->ranges_extra, extra_range_pages * PAGE_SIZE);
        free_pages(file->ranges_extra, extra_range_pages);
        file->ranges_extra = new_extra;
    }

    file_insert_range[ft](file->ranges_extra, file->range_count++, range);
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

        switch (entry_type_of_fat_value(next_cluster, fs->fat_type)) {
        case FAT_ENTRY_END_OF_CHAIN: {
            if (unlikely(current_file_offset * fs->bytes_per_cluster < file->f.size)) {
                print_warn("EOC before end of file");
                return false;
            }

            if (!file_emplace_range(file, range, fs->fat_type))
                return false;

            return true;
        }
        case FAT_ENTRY_LINK:
            if (next_cluster == current_cluster + 1)
                break;

            if (!file_emplace_range(file, range, fs->fat_type))
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

static size_t (*ft_to_range_get_offset[])(void*) = {
    [FAT_TYPE_12] = range16_get_offset,
    [FAT_TYPE_16] = range16_get_offset,
    [FAT_TYPE_32] = range32_get_offset
};

static size_t range32_get_global_cluster(void *range)
{
    return ((struct contiguous_file_range32*)range)->global_cluster;
}

static size_t range16_get_global_cluster(void *range)
{
    return ((struct contiguous_file_range16*)range)->global_cluster;
}

static size_t (*ft_to_range_get_global_cluster[])(void*) = {
    [FAT_TYPE_12] = range16_get_global_cluster,
    [FAT_TYPE_16] = range16_get_global_cluster,
    [FAT_TYPE_32] = range32_get_global_cluster
};

static u32 file_cluster_from_offset(struct fat_file *file, u32 offset, enum fat_type ft)
{
    void *ranges = file->in_place_ranges;
    void *this_range;
    size_t range_count = file->range_count;
    u32 global_cluster;
    u32 range_stride = ft_to_range_stride[ft];
    size_t (*range_get_offset)(void*) = ft_to_range_get_offset[ft];
    size_t (*range_get_global_cluster)(void*) = ft_to_range_get_global_cluster[ft];

    BUG_ON(file->range_count == 0);

    if (file->ranges_extra && range_get_offset(file->ranges_extra) >= offset) {
        ranges = file->ranges_extra;
        range_count = range_count - ft_to_in_place_range_capacity[ft];
    }

    this_range = find_range(ranges, range_count, range_stride, range_get_offset, offset);
    global_cluster = range_get_global_cluster(this_range) + (offset - range_get_offset(this_range));
    BUG_ON(entry_type_of_fat_value(global_cluster, ft) != FAT_ENTRY_LINK);

    return global_cluster;
}

static bool fat_read(struct fat_filesystem *fs, u32 cluster, u32 offset, u32 bytes, void* buffer)
{
    struct disk_services *srvc = filesystem_backend();
    u64 offset_to_read;

    if (unlikely(!srvc))
        return false;

    // FIXME: This seems unnecessarily expensive
    offset_to_read = fs->data_lba_range.begin;
    offset_to_read *= fs->f.d.bytes_per_sector;
    offset_to_read += cluster * fs->bytes_per_cluster;
    offset_to_read += offset;

    return srvc->read(fs->f.d.handle, buffer, offset_to_read, bytes);
}

static bool fixed_root_directory_fetch_next_entry(struct fat_directory *dir, void *entry)
{
    struct fat_filesystem *fs = dir->parent;
    struct disk *d = &fs->f.d;
    struct disk_services *ds = filesystem_backend();
    u64 offset_to_read;

    if (unlikely(!ds))
        return false;

    if ((dir->current_offset / sizeof(struct fat_directory_entry)) == fs->root_dir_entries) {
       dir->flags |= DIR_EOF;
       return false;
    }

    // FIXME: This seems unnecessarily expensive
    offset_to_read = fs->f.lba_range.begin + dir->first_sector_off;
    offset_to_read *= d->bytes_per_sector;
    offset_to_read += dir->current_offset;
    dir->current_offset += sizeof(struct fat_directory_entry);

    return ds->read(d->handle, entry, offset_to_read, sizeof(struct fat_directory_entry));
}

static bool directory_fetch_next_entry(struct fat_directory *dir, void* entry)
{
    if (dir->flags & DIR_EOF)
        return false;

    if (dir->flags & DIR_FIXED_CAP_ROOT)
        return fixed_root_directory_fetch_next_entry(dir, entry);

    if (dir->current_offset == dir->parent->bytes_per_cluster) {
        u32 next_cluster = fat_entry_at(dir->parent, dir->current_cluster);

        if (entry_type_of_fat_value(next_cluster, dir->parent->fat_type) != FAT_ENTRY_LINK) {
            dir->flags |= DIR_EOF;
            return false;
        }

        dir->current_cluster = next_cluster;
        dir->current_offset = 0;
    }

    bool ok = fat_read(dir->parent, pure_cluster_value(dir->current_cluster),
                       dir->current_offset, sizeof(struct fat_directory_entry), entry);
    dir->flags |= !ok ? DIR_EOF : 0;
    dir->current_offset += sizeof(struct fat_directory_entry);

    return ok;
}

static void process_normal_entry(struct fat_directory_entry *entry, struct fat_directory_record *out, bool is_small)
{
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

        out->name_length = name_len + extension_len;
    }

    out->size = entry->size;
    out->first_cluster = ((u32)entry->cluster_high << 16) | entry->cluster_low;
    out->is_directory = entry->attributes & SUBDIR_ATTRIBUTE;
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

/*
 * Since you can have at max 20 chained long entries, the theoretical limit is 20 * 13 characters,
 * however, the actual allowed limit is 255, which would limit the 20th entry contribution to only 8 characters.
 */
#define CHARS_FOR_LAST_LONG_ENTRY 8

static bool directory_next_entry(struct fat_directory *dir, struct fat_directory_record *out)
{
    struct fat_directory_entry normal_entry;

    if (dir->flags & DIR_EOF)
        return false;

    for (;;) {
        bool is_long;
        struct long_name_fat_directory_entry *long_entry;
        u8 initial_sequence_number;
        u8 sequence_number;
        char *name_ptr;
        size_t chars_written = 0;
        u32 checksum_array[MAX_SEQUENCE_NUMBER] = { 0 };
        u8 checksum;
        size_t i;

        if (!directory_fetch_next_entry(dir, &normal_entry))
            return false;

        if ((u8)normal_entry.filename[0] == DELETED_FILE_MARK)
            continue;

        if ((u8)normal_entry.filename[0] == END_OF_DIRECTORY_MARK) {
            dir->flags |= DIR_EOF;
            return false;
        }

        if (normal_entry.attributes & DEVICE_ATTRIBUTE)
            continue;

        is_long = (normal_entry.attributes & LONG_NAME_ATTRIBUTE) == LONG_NAME_ATTRIBUTE;
        if (!is_long && (normal_entry.attributes & VOLUME_LABEL_ATTRIBUTE))
            continue;

        if (!is_long) {
            process_normal_entry(&normal_entry, out, false);
            return true;
        }

        long_entry = (struct long_name_fat_directory_entry*)&normal_entry;

        initial_sequence_number = long_entry->sequence_number & SEQUENCE_NUM_BIT_MASK;
        sequence_number = initial_sequence_number;
        if (!(long_entry->sequence_number & LAST_LOGICAL_ENTRY_BIT))
            return false;

        name_ptr = out->name + MAX_NAME_LENGTH;
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
                if (!directory_fetch_next_entry(dir, &normal_entry))
                    return false;

                break;
            }

            if (!directory_fetch_next_entry(dir, &normal_entry))
                return false;

            --sequence_number;
            name_ptr -= CHARS_PER_LONG_ENTRY;
        }

        BUG_ON(chars_written >= MAX_NAME_LENGTH);

        if (name_ptr != out->name)
            memmove(out->name, name_ptr, chars_written);

        out->name_length = chars_written;
        process_normal_entry(&normal_entry, out, true);

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

static bool fat_file_read(struct file *base_file, void* buffer, u32 offset, u32 size)
{
    struct fat_file *file = container_of(base_file, struct fat_file, f);
    struct fat_filesystem *fs = container_of(file->f.fs, struct fat_filesystem, f);
    u32 cluster_offset;
    u32 offset_within_cluster;
    u32 bytes_left_after_offset;
    size_t bytes_to_read;
    u8 *byte_buffer = (u8*)buffer;

    BUG_ON(size == 0);

    if (!file->range_count && !file_compute_contiguous_ranges(file))
        return false;

    cluster_offset = offset / fs->bytes_per_cluster;
    offset_within_cluster = offset - (cluster_offset * fs->bytes_per_cluster);
    bytes_left_after_offset = file->f.size - offset;
    bytes_to_read = MIN(size, bytes_left_after_offset);

    for (;;) {
        u32 current_cluster = file_cluster_from_offset(file, cluster_offset++, fs->fat_type);
        size_t bytes_to_read_for_this_cluster = MIN(bytes_to_read, fs->bytes_per_cluster - offset_within_cluster);

        if (!fat_read(fs, pure_cluster_value(current_cluster),
                      offset_within_cluster, bytes_to_read_for_this_cluster, byte_buffer))
            return false;

        byte_buffer += bytes_to_read_for_this_cluster;
        bytes_to_read -= bytes_to_read_for_this_cluster;

        if (!bytes_to_read)
            break;

        offset_within_cluster = 0;
    }

    return true;
}

static struct fat_file *fat_do_open_file(struct fat_filesystem *fs, u32 first_cluster, u32 size)
{
    struct fat_file *file = allocate_bytes(sizeof(struct fat_file));
    if (!file)
        return NULL;

    file->f = (struct file) {
        .fs = &fs->f,
        .read = fat_file_read,
        .size = size
    };

    file->ranges_extra = NULL;
    file->range_count = 0;
    file->first_cluster = first_cluster;
    return file;
}

static struct file *fat_open(struct filesystem *base_fs, struct string_view path)
{
    struct fat_filesystem *fs = container_of(base_fs, struct fat_filesystem, f);
    struct fat_directory dir;
    struct fat_file *file;
    u32 first_cluster, size = 0;
    bool is_directory = true, node_found = false;
    struct string_view node;

    if (!ensure_root_directory(fs))
        return NULL;

    first_cluster = fs->root_directory->first_cluster;
    dir = (struct fat_directory) {
        .parent = fs,
        .current_cluster = first_cluster,
        .flags = (fs->fat_type != FAT_TYPE_32) ? DIR_FIXED_CAP_ROOT : 0
    };

    while (next_path_node(&path, &node)) {
        struct fat_directory_record rec = { 0 };

        if (sv_equals(node, SV(".")))
            continue;
        if (!is_directory)
            return NULL;

        while (directory_next_entry(&dir, &rec)) {
            if (!sv_equals((struct string_view) { rec.name, rec.name_length }, node))
                continue;

            first_cluster = rec.first_cluster;
            size = rec.size;
            node_found = true;
            is_directory = rec.is_directory;
            break;
        }

        if (!node_found)
            break;

        dir.current_cluster = first_cluster;
        dir.current_offset = dir.flags = 0;
    }

    if (!node_found || is_directory)
        return NULL;

    file = fat_do_open_file(fs, first_cluster, size);
    if (!file)
        return NULL;

    return &file->f;
}

static void fat_file_free(struct fat_file *file, enum fat_type ft)
{
    if (file->ranges_extra) {
        size_t offset_into_extra = file->range_count - ft_to_in_place_range_capacity[ft];
        size_t extra_range_capacity = CEILING_DIVIDE(offset_into_extra, ft_to_ranges_per_page[ft]);
        free_pages(file->ranges_extra, extra_range_capacity);
    }

    free_bytes(file, sizeof(struct fat_file));
}

static void fat_close(struct filesystem *base_fs, struct file *f)
{
    (void)base_fs;
    struct fat_file *file = container_of(f, struct fat_file, f);
    struct fat_filesystem *fs = container_of(file->f.fs, struct fat_filesystem, f);

    if (file == fs->root_directory)
        return;

    fat_file_free(file, fs->fat_type == FAT_TYPE_32);
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
        print_warn("Unexpected file system type: %pSV\n", &actual_view);
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

    if (bpb20->bytes_per_sector != d->bytes_per_sector)
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
    if (!out_info->sectors_per_cluster)
        return false;
    if (!out_info->sectors_per_fat)
        return false;
    if (!out_info->reserved_sectors)
        return false;

    root_dir_bytes = out_info->max_root_dir_entries * sizeof(struct fat_directory_entry);
    out_info->root_dir_sectors = CEILING_DIVIDE(root_dir_bytes,d->bytes_per_sector);

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

struct filesystem *try_create_fat(const struct disk *d, struct range lba_range, void *first_page)
{
    void *bpb = first_page + BPB_OFFSET;
    struct fat_filesystem *fs;
    struct fat_info info = { 0 };

    if (!detect_fat(d, lba_range, bpb, &info))
        return NULL;

    print_info("detected fat%d with %d fats, %d sectors/cluster, %u sectors/fat\n",
               ft_to_size[info.type], info.fat_count, info.sectors_per_cluster,
               info.sectors_per_fat);

    fs = allocate_bytes(sizeof(struct fat_filesystem));
    if (!fs)
        return NULL;

    fs->f = (struct filesystem) {
        .d = *d,
        .lba_range = lba_range,
        .open = fat_open,
        .close = fat_close
    };

    fs->fat_type = info.type;
    fs->fat_view = NULL;
    fs->fat_view_offset = FAT_VIEW_OFF_INVALID;
    fs->root_directory = NULL;

    range_advance_begin(&lba_range, info.reserved_sectors);

    fs->fat_lba_range = lba_range;
    range_set_length(&fs->fat_lba_range, info.sectors_per_fat);

    range_advance_begin(&lba_range, info.sectors_per_fat * info.fat_count);

    if (info.type == FAT_TYPE_12 || info.type == FAT_TYPE_16) {
        fs->root_dir_sector_off = lba_range.begin - fs->f.lba_range.begin;
        fs->root_dir_entries = info.max_root_dir_entries;
        range_advance_begin(&lba_range, info.root_dir_sectors);
    } else if (info.type == FAT_TYPE_32) {
        fs->root_dir_cluster = info.root_dir_cluster;
    } else {
        BUG();
    }

    fs->data_lba_range = lba_range;
    fs->bytes_per_cluster = info.sectors_per_cluster * d->bytes_per_sector;
    fs->fat_clusters = (range_length(&fs->fat_lba_range) * d->bytes_per_sector) / sizeof(u32);

    return &fs->f;
}
