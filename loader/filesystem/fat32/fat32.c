#include "fat32.h"
#include "common/log.h"
#include "common/constants.h"
#include "common/helpers.h"
#include "common/minmax.h"
#include "common/ctype.h"
#include "allocator.h"

#undef MSG_FMT
#define MSG_FMT(msg) "FAT32: " msg

#define EBPB_OFFSET 0x0B
#define EBPB_SIGNATURE 0x29

#define MIN_CLUSTER_COUNT_FAT32 65525

#define FAT_VIEW_PAGES (PAGE_SIZE * 16)
#define FAT_VIEW_CAPACITY (FAT_VIEW_PAGES / sizeof(u32))

struct contiguous_file_range {
    u32 file_offset_cluster;
    u32 global_cluster;
};

static struct contiguous_file_range *find_range(struct contiguous_file_range *ranges, size_t count, size_t offset)
{
    size_t left = 0;
    size_t right = count - 1;

    while (left <= right) {
        size_t middle = left + ((right - left) / 2);

        if (ranges[middle].file_offset_cluster < offset) {
            left = middle + 1;
        } else if (offset < ranges[middle].file_offset_cluster) {
            right = middle - 1;
        } else {
            return &ranges[middle];
        }
    }

    /*
     * right should always point to lower bound - 1,
     * aka range that this offset is a part of.
     */
    BUG_ON(ranges[right].file_offset_cluster > offset);
    return &ranges[right];
}

#define RANGES_PER_PAGE (PAGE_SIZE / sizeof(struct contiguous_file_range))
#define IN_PLACE_RANGE_CAPACITY ((PAGE_SIZE - 32 ) / sizeof(struct contiguous_file_range))

struct fat32_file {
    struct file f;

    u32 first_cluster;
    u32 range_count;

    /*
     * Sorted in ascending order by file_offset_cluster.
     * Each range at i spans (range[i].file_offset_cluster -> range[i + 1].file_offset_cluster - 1) clusters
     * For last i the end is the last cluster of the file (inclusive).
     */
    struct contiguous_file_range *ranges_extra;
    struct contiguous_file_range ranges[IN_PLACE_RANGE_CAPACITY];
};
BUILD_BUG_ON(sizeof(struct fat32_file) > PAGE_SIZE);

struct fat32_filesystem {
    struct filesystem f;

    struct fat_ebpb ebpb;
    struct range fat_lba_range;
    struct range data_lba_range;

    u32 bytes_per_cluster;
    u32 fat_clusters;

    size_t fat_view_offset;
    u32 *fat_view;

    struct fat32_file *root_directory;
};

struct fat32_directory {
    struct fat32_filesystem *parent;
    u32 current_cluster;
    u32 current_offset;
    bool end;
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
#define FREE_CLUSTER     0x00000000
#define BAD_CLUSTER      0x0FFFFFF7
#define EOC_MAIN_CLUSTER 0x0FFFFFF8

#define RESERVED_CLUSTER_COUNT 2

static enum fat_entry entry_type_of_fat_value(u32 value)
{
    if (value == 0)
        return FAT_ENTRY_FREE;
    if (value == 1)
        return FAT_ENTRY_RESERVED;
    if (value >= EOC_MAIN_CLUSTER)
        return FAT_ENTRY_END_OF_CHAIN;
    if (value == BAD_CLUSTER)
        return FAT_ENTRY_BAD;

    return FAT_ENTRY_LINK;
}

static u32 pure_cluster_value(u32 value)
{
    BUG_ON(value < RESERVED_CLUSTER_COUNT);
    return value - RESERVED_CLUSTER_COUNT;
}

static struct fat32_file *fat32_do_open_file(struct fat32_filesystem *fs, u32 root_cluster, u32 size);

static bool ensure_root_directory(struct fat32_filesystem *fs) {
    if (fs->root_directory)
        return true;

    fs->root_directory = fat32_do_open_file(fs, fs->ebpb.root_dir_cluster, 0);
    return fs->root_directory != NULL;
}

static bool ensure_fat_entry(struct fat32_filesystem *fs, u32 index)
{
    struct disk_services *srvc = filesystem_backend();
    struct disk *d = &fs->f.d;
    u32 first_block, sectors_to_read;
    bool was_null = fs->fat_view == NULL;

    if (was_null && !(fs->fat_view = allocate_bytes(FAT_VIEW_PAGES)))
        return false;

    BUG_ON(index >= fs->fat_clusters);

    // already have it cached
    if (!was_null && (fs->fat_view_offset <= index && ((fs->fat_view_offset + FAT_VIEW_CAPACITY) > index)))
        return true;

    if (!srvc)
        return false;

    first_block = fs->fat_lba_range.begin + ((index * sizeof(u32)) / fs->f.d.bytes_per_sector);
    sectors_to_read = MIN(range_length(&fs->fat_lba_range), FAT_VIEW_PAGES / (size_t)d->bytes_per_sector);
    return srvc->read_blocks(d->handle, fs->fat_view, first_block, sectors_to_read);
}

static u32 fat_entry_at(struct fat32_filesystem *fs, u32 index)
{
    if (!ensure_fat_entry(fs, index))
        return BAD_CLUSTER;

    return fs->fat_view[index - fs->fat_view_offset];
}

static bool file_emplace_range(struct fat32_file *file, struct contiguous_file_range range)
{
    u32 offset_into_extra;
    size_t extra_range_capacity;

    if (file->range_count < IN_PLACE_RANGE_CAPACITY) {
        file->ranges[file->range_count++] = range;
        return true;
    }

    offset_into_extra = file->range_count - IN_PLACE_RANGE_CAPACITY;
    extra_range_capacity = CEILING_DIVIDE(offset_into_extra, RANGES_PER_PAGE);

    if (extra_range_capacity == offset_into_extra) {
        size_t new_capacity = extra_range_capacity * sizeof(struct contiguous_file_range) + PAGE_SIZE;
        struct contiguous_file_range *new_extra = allocate_bytes(new_capacity);
        if (!new_extra)
            return false;

        memcpy(new_extra, file->ranges_extra, extra_range_capacity);
        free_bytes(file->ranges_extra, extra_range_capacity);
        file->ranges_extra = new_extra;
    }

    file->ranges_extra[offset_into_extra] = range;
    file->range_count++;
    return true;
}

static bool file_compute_contiguous_ranges(struct fat32_file *file)
{
    struct contiguous_file_range range = {
        .file_offset_cluster = 0,
        .global_cluster = file->first_cluster
    };
    u32 current_file_offset = 1;
    u32 current_cluster = file->first_cluster;
    struct fat32_filesystem *fs = container_of(file->f.fs, struct fat32_filesystem, f);

    for (;;) {
        u32 next_cluster = fat_entry_at(fs, current_cluster);

        switch (entry_type_of_fat_value(next_cluster)) {
        case FAT_ENTRY_END_OF_CHAIN: {
            if (current_file_offset * fs->bytes_per_cluster < file->f.size) {
                print_warn("FAT32: EOC before end of file");
                return false;
            }

            if (!file_emplace_range(file, range))
                return false;

            return true;
        }
        case FAT_ENTRY_LINK:
            if (next_cluster == current_cluster + 1)
                break;

            if (!file_emplace_range(file, range))
                return false;

            range = (struct contiguous_file_range ) { current_file_offset + 1, next_cluster };
            break;
        default:
            return false;
        }

        current_cluster = next_cluster;
        current_file_offset++;
    }
}

static u32 file_cluster_from_offset(struct fat32_file *file, u32 offset)
{
    struct contiguous_file_range *ranges = file->ranges;
    struct contiguous_file_range *this_range;
    size_t range_count = file->range_count;
    u32 global_cluster;

    BUG_ON(file->range_count == 0);
    //BUG_ON(offset < CEILING_DIVIDE(file->f.size, fs_as_fat32().bytes_per_cluster()));

    if (file->ranges_extra && file->ranges_extra[0].file_offset_cluster >= offset) {
        ranges = file->ranges_extra;
        range_count = range_count - IN_PLACE_RANGE_CAPACITY;
    }

    this_range = find_range(ranges, range_count, offset);
    global_cluster = this_range->global_cluster + (offset - this_range->file_offset_cluster);
    BUG_ON(entry_type_of_fat_value(global_cluster) != FAT_ENTRY_LINK);

    return global_cluster;
}

static bool fat32_read(struct fat32_filesystem *fs, u32 cluster, u32 offset, u32 bytes, void* buffer)
{
    struct disk_services *srvc = filesystem_backend();
    u32 sector_to_read;
    if (!srvc)
        return false;

    sector_to_read = fs->data_lba_range.begin;
    sector_to_read += cluster * fs->ebpb.sectors_per_cluster;

    return srvc->read(fs->f.d.handle, buffer, sector_to_read * fs->f.d.bytes_per_sector + offset, bytes);
}

static bool directory_fetch_next_entry(struct fat32_directory *dir, void* entry)
{
    if (dir->end)
        return false;

    if (dir->current_offset == dir->parent->bytes_per_cluster) {
        u32 next_cluster = fat_entry_at(dir->parent, dir->current_cluster);

        if (entry_type_of_fat_value(next_cluster) != FAT_ENTRY_LINK) {
            dir->end = true;
            return false;
        }

        dir->current_cluster = next_cluster;
        dir->current_offset = 0;
    }

    bool ok = fat32_read(dir->parent, pure_cluster_value(dir->current_cluster),
                         dir->current_offset,
                         sizeof(struct fat_directory_entry), entry);
    dir->end = !ok;
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

static bool directory_next_entry(struct fat32_directory *dir, struct fat_directory_record *out)
{
    struct fat_directory_entry normal_entry;

    if (dir->end)
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
            dir->end = true;
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

            print_warn("Invalid FAT32 file checksum\n");
            return false;
        }

        return true;
    }
}

static bool fat32_file_read(struct file *base_file, void* buffer, u32 offset, u32 size)
{
    struct fat32_file *file = container_of(base_file, struct fat32_file, f);
    struct fat32_filesystem *fs = container_of(file->f.fs, struct fat32_filesystem, f);
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
        u32 current_cluster = file_cluster_from_offset(file, cluster_offset++);
        size_t bytes_to_read_for_this_cluster = MIN(bytes_to_read, fs->bytes_per_cluster - offset_within_cluster);

        if (!fat32_read(fs, pure_cluster_value(current_cluster),
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

static struct fat32_file *fat32_do_open_file(struct fat32_filesystem *fs, u32 first_cluster, u32 size)
{
    struct fat32_file *file = allocate_bytes(sizeof(struct fat32_file));
    if (!file)
        return NULL;

    file->f = (struct file) {
        .fs = &fs->f,
        .read = fat32_file_read,
        .size = size
    };

    file->ranges_extra = NULL;
    file->range_count = 0;
    file->first_cluster = first_cluster;
    return file;
}

static struct file *fat32_open(struct filesystem *base_fs, struct string_view path)
{
    struct fat32_filesystem *fs = container_of(base_fs, struct fat32_filesystem, f);
    struct fat32_file *file;
    u32 first_cluster, size = 0;
    bool is_directory = true, node_found = false;
    struct string_view node;

    if (!ensure_root_directory(fs))
        return NULL;

    first_cluster = fs->root_directory->first_cluster;

    while (next_path_node(&path, &node)) {
        struct fat32_directory dir = {
            .parent = fs,
            .current_cluster = first_cluster
        };
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
    }

    if (!node_found || is_directory)
        return NULL;

    file = fat32_do_open_file(fs, first_cluster, size);
    if (!file)
        return NULL;

    return &file->f;
}

static void fat32_file_free(struct fat32_file *file)
{
    if (file->ranges_extra) {
        size_t offset_into_extra = file->range_count - IN_PLACE_RANGE_CAPACITY;
        size_t extra_range_capacity = CEILING_DIVIDE(offset_into_extra, RANGES_PER_PAGE);
        free_pages(file->ranges_extra, extra_range_capacity);
    }
    free_bytes(file, sizeof(struct fat32_file));
}

static void fat32_close(struct filesystem *base_fs, struct file *f)
{
    (void)base_fs;
    struct fat32_file *file = container_of(f, struct fat32_file, f);
    struct fat32_filesystem *fs = container_of(file->f.fs, struct fat32_filesystem, f);

    if (file == fs->root_directory)
        return;

    fat32_file_free(file);
}

static bool is_fat32_fs(const struct disk *d, struct range lba_range, struct fat_ebpb *ebpb)
{
    static const char fat32_signature[] = "FAT32   ";

    u32 cluster_count;

    if (ebpb->bytes_per_sector != d->bytes_per_sector)
        return NULL;
    if (ebpb->signature != EBPB_SIGNATURE)
        return NULL;
    if (memcmp(ebpb->filesystem_type, fat32_signature, sizeof(fat32_signature) - 1) != 0)
        return NULL;
    if (!ebpb->fat_count)
        return NULL;
    if (!ebpb->sectors_per_cluster)
        return NULL;
    if (!ebpb->sectors_per_fat)
        return NULL;

    range_advance_begin(&lba_range, ebpb->reserved_sectors);
    range_advance_begin(&lba_range, ebpb->sectors_per_fat * ebpb->fat_count);
    cluster_count = range_length(&lba_range) / ebpb->sectors_per_cluster;

    if (cluster_count < MIN_CLUSTER_COUNT_FAT32)
        return false;

    return true;
}

struct filesystem *try_create_fat32(const struct disk *d, struct range lba_range, void *first_page)
{
    struct fat_ebpb *ebpb = first_page + EBPB_OFFSET;
    struct fat32_filesystem *fs;

    if (!is_fat32_fs(d, lba_range, ebpb))
        return NULL;

    print_info("detected with %d fats, %d sectors/cluster, %u sectors/fat\n",
               ebpb->fat_count, ebpb->sectors_per_cluster, ebpb->sectors_per_fat);

    fs = allocate_bytes(sizeof(struct fat32_filesystem));
    if (!fs)
        return NULL;

    fs->f = (struct filesystem) {
        .d = *d,
        .lba_range = lba_range,
        .open = fat32_open,
        .close = fat32_close
    };

    memcpy(&fs->ebpb, ebpb, sizeof(struct fat_ebpb));

    fs->fat_view = NULL;
    fs->fat_view_offset = 0;
    fs->root_directory = NULL;

    fs->fat_lba_range = lba_range;
    range_advance_begin(&fs->fat_lba_range, ebpb->reserved_sectors);
    range_set_length(&fs->fat_lba_range, ebpb->sectors_per_fat);

    fs->data_lba_range = lba_range;
    range_advance_begin(&fs->data_lba_range, ebpb->reserved_sectors);
    range_advance_begin(&fs->data_lba_range, ebpb->sectors_per_fat * ebpb->fat_count);

    fs->bytes_per_cluster = ebpb->sectors_per_cluster * ebpb->bytes_per_sector;
    fs->fat_clusters = (range_length(&fs->fat_lba_range) * d->bytes_per_sector) / sizeof(u32);

    return &fs->f;
}
