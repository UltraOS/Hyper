#include "FAT32.h"
#include "Common/Logger.h"
#include "Allocator.h"

static constexpr size_t ebpb_offset = 0x0B;

bool FAT32::detect(const Disk& disk, LBARange lba_range, void* first_block_buffer)
{
    EBPB ebpb;
    copy_memory(reinterpret_cast<char*>(first_block_buffer) + ebpb_offset, &ebpb, EBPB::size);

    if (ebpb.bytes_per_sector != disk.bytes_per_sector)
        return false;

    static constexpr u8 ebpb_signature = 0x29;
    static constexpr StringView fat32_filesystem = "FAT32   ";

    if (ebpb_signature != ebpb.signature)
        return false;
    if (fat32_filesystem != StringView::from_char_array(ebpb.filesystem_type))
        return false;
    if (!ebpb.fat_count)
        return false;
    if (!ebpb.sectors_per_cluster)
        return false;
    if (!ebpb.sectors_per_fat)
        return false;

    logger::info("detected FAT32: ", ebpb.fat_count, " fats, ",
                 ebpb.sectors_per_cluster, " sectors/cluster, ",
                 ebpb.sectors_per_fat, " sectors/fat");

    auto data_range = lba_range;
    data_range.advance_begin_by(ebpb.reserved_sectors);
    data_range.advance_begin_by(ebpb.sectors_per_fat * ebpb.fat_count);
    auto cluster_count = data_range.length() / ebpb.sectors_per_cluster;

    static constexpr auto min_cluster_count_for_fat32 = 65525;
    if (cluster_count < min_cluster_count_for_fat32)
        return false;

    return true;
}

enum class FATEntryType {
    FREE,
    RESERVED,
    BAD,
    END_OF_CHAIN,
    LINK,
};
static constexpr u32 free_cluster = 0x00000000;
static constexpr u32 bad_cluster = 0x0FFFFFF7;
static constexpr u32 end_of_chain_min_cluster = 0x0FFFFFF8;
static constexpr u32 reserved_cluster_count = 2;

static FATEntryType entry_type_of_fat_value(u32 value)
{
    if (value == 0)
        return FATEntryType::FREE;
    if (value == 1)
        return FATEntryType::RESERVED;
    if (value >= end_of_chain_min_cluster)
        return FATEntryType::END_OF_CHAIN;
    if (value == bad_cluster)
        return FATEntryType::BAD;

    return FATEntryType::LINK;
}

static u32 pure_cluster_value(u32 value)
{
    ASSERT(value >= reserved_cluster_count);
    return value - reserved_cluster_count;
}


FAT32::FAT32(const Disk& disk, LBARange lba_range, void* first_block_buffer)
    : FileSystem(disk, lba_range)
{
    copy_memory(reinterpret_cast<char*>(first_block_buffer) + ebpb_offset, &m_ebpb, EBPB::size);

    m_fat_range = lba_range;
    m_fat_range.advance_begin_by(m_ebpb.reserved_sectors);
    m_fat_range.set_length(m_ebpb.sectors_per_fat);

    m_data_range = lba_range;
    m_data_range.advance_begin_by(m_ebpb.reserved_sectors);
    m_data_range.advance_begin_by(m_ebpb.sectors_per_fat * m_ebpb.fat_count);

    m_bytes_per_cluster = m_ebpb.sectors_per_cluster * m_ebpb.bytes_per_sector;
    m_fat_clusters = (m_fat_range.length() * disk.bytes_per_sector) / sizeof(u32);
}

bool FAT32::ensure_root_directory() {
    if (m_root_directory)
        return true;

    auto root_cluster = m_ebpb.root_dir_cluster;
    m_root_directory = allocator::allocate_new<FAT32::File>(*this, root_cluster, 0);
    return m_root_directory != nullptr;
}

u8 generate_short_name_checksum(StringView name)
{
    u8 sum = 0;
    const u8* byte_ptr = reinterpret_cast<const u8*>(name.data());

    for (size_t i = short_name_length + short_extension_length; i != 0; i--) {
        sum = (sum >> 1) + ((sum & 1) << 7);
        sum += *byte_ptr++;
    }

    return sum;
}

bool FAT32::Directory::fetch_next(void* entry)
{
    if (m_end)
        return false;

    if (m_current_offset == m_parent.bytes_per_cluster()) {
        auto next_cluster = m_parent.fat_entry_at(m_current_cluster);

        if (entry_type_of_fat_value(next_cluster) != FATEntryType::LINK) {
            m_end = true;
            return false;
        }

        m_current_cluster = next_cluster;
        m_current_offset = 0;
    }

    bool ok = m_parent.read(pure_cluster_value(m_current_cluster), m_current_offset, sizeof(DirectoryEntry), entry);
    m_end = !ok;
    m_current_offset += sizeof(DirectoryEntry);

    return ok;
}

bool FAT32::Directory::next_entry(Entry& out_entry)
{
    if (m_end)
        return false;

    DirectoryEntry normal_entry {};

    auto process_normal_entry = [](DirectoryEntry& in, Entry& out, bool is_small) {
        if (in.is_lowercase_name())
            to_lower(in.filename);
        if (in.is_lowercase_extension())
            to_lower(in.extension);

        auto name = StringView::from_char_array(in.filename);
        auto ext = StringView::from_char_array(in.extension);

        if (!is_small) {
            auto name_length = name.find(" ").value_or(name.size());
            copy_memory(name.data(), out.name, name_length);

            auto ext_length = ext.find(" ").value_or(ext.size());

            if (ext_length) {
                out.name[name_length++] = '.';
                copy_memory(ext.data(), out.name + name_length, ext_length);
            }

            out.name_length = name_length + ext_length;
        }

        out.size = in.size;
        out.first_cluster = (static_cast<u32>(in.cluster_high) << 16) | in.cluster_low;
        out.is_directory = in.is_directory();
    };

    auto process_ucs2 = [](const u8* ucs2, size_t count, char*& out) -> size_t {
        for (size_t i = 0; i < (count * bytes_per_ucs2_char); i += bytes_per_ucs2_char) {
            u16 ucs2_char = ucs2[i] | (static_cast<u16>(ucs2[i + 1]) << 8);

            char ascii;

            if (ucs2_char == 0) {
                return (i / bytes_per_ucs2_char);
            } else if (ucs2_char > 127) {
                ascii = '?';
            } else {
                ascii = static_cast<char>(ucs2_char & 0xFF);
            }

            *(out++) = ascii;
        }

        return count;
    };

    for (;;) {
        if (!fetch_next(&normal_entry))
            return false;

        if (normal_entry.is_deleted())
            continue;

        if (normal_entry.is_end_of_directory()) {
            m_end = true;
            return false;
        }

        if (normal_entry.is_device())
            continue;

        auto is_long = normal_entry.is_long_name();
        if (!is_long && normal_entry.is_volume_label())
            continue;

        if (!is_long) {
            process_normal_entry(normal_entry, out_entry, false);
            return true;
        }

        static constexpr size_t max_sequence_number = 20;

        auto long_entry = LongNameDirectoryEntry::from_normal(normal_entry);
        auto initial_sequence_number = long_entry.extract_sequence_number();
        auto sequence_number = initial_sequence_number;
        ASSERT(long_entry.is_last_logical());

        auto* cur_name_buffer_ptr = out_entry.name + sizeof(Entry::name);

        // Since you can have at max 20 chained long entries, the theoretical limit is 20 * 13 characters,
        // however, the actual allowed limit is 255, which would limit the 20th entry contribution to only 8 characters.
        static constexpr size_t max_characters_for_last_entry = 8;

        cur_name_buffer_ptr -= max_characters_for_last_entry;
        size_t chars_written = 0;

        u32 checksum_array[max_sequence_number] {};

        for (;;) {
            auto* name_ptr = cur_name_buffer_ptr;

            auto chars = process_ucs2(long_entry.name_1, long_entry.name_1_characters, name_ptr);
            chars_written += chars;

            if (chars == long_entry.name_1_characters) {
                chars = process_ucs2(long_entry.name_2, long_entry.name_2_characters, name_ptr);
                chars_written += chars;
            }

            if (chars == long_entry.name_2_characters) {
                chars = process_ucs2(long_entry.name_3, long_entry.name_3_characters, name_ptr);
                chars_written += chars;
            }

            checksum_array[sequence_number - 1] = long_entry.checksum;

            if (sequence_number == 1) {
                if (fetch_next(&normal_entry))
                    return false;

                break;
            }

            if (!fetch_next(&long_entry))
                return false;

            --sequence_number;
            cur_name_buffer_ptr -= LongNameDirectoryEntry::characters_per_entry;
        }

        ASSERT(chars_written <= sizeof(Entry::name));

        if (cur_name_buffer_ptr != out_entry.name)
            move_memory(cur_name_buffer_ptr, out_entry.name, chars_written);

        out_entry.name_length = chars_written;
        process_normal_entry(normal_entry, out_entry, true);

        auto expected_checksum = generate_short_name_checksum(StringView(normal_entry.filename,
                                                              short_name_length + short_extension_length));

        for (size_t i = 0; i < initial_sequence_number; ++i) {
            if (checksum_array[i] == expected_checksum)
                continue;

            logger::warning("Invalid FAT32 file checksum");
            return false;
        }

        return true;
    }
}

File* FAT32::open(StringView path)
{
    if (!ensure_root_directory())
        return nullptr;

    u32 first_cluster = m_root_directory->first_cluster();
    u32 size = 0;
    bool is_directory = true;
    bool node_found = false;

    for (auto node : IterablePath(path)) {
        if (node == ".")
            continue;

        if (!is_directory)
            return nullptr;

        logger::info("looking at ", node);

        Directory directory(*this, first_cluster);
        Entry dir_entry {};

        while (directory.next_entry(dir_entry)) {
            logger::info("found ", dir_entry.name_view());

            if (dir_entry.name_view() != node)
                continue;

            first_cluster = dir_entry.first_cluster;
            size = dir_entry.size;
            node_found = true;
            is_directory = dir_entry.is_directory;
            break;
        }

        if (!node_found)
            break;
    }

    if (!node_found || is_directory)
        return nullptr;

    return allocator::allocate_new<FAT32::File>(*this, first_cluster, size);
}

void FAT32::close(::File* file)
{
    if (file == m_root_directory)
        return;

    allocator::free(*file);
}

bool FAT32::ensure_fat_entry(u32 index)
{
    auto& d = disk();
    ASSERT(index < m_fat_clusters);

    bool was_null = m_fat_view == nullptr;
    if (was_null && !(m_fat_view = allocator::allocate_new_array<u32>(fat_view_capacity)))
        return false;

    // already have it cached
    if (!was_null && (m_fat_view_offset <= index && ((m_fat_view_offset + fat_view_capacity) > index)))
        return true;

    auto* srvc = disk_services();
    if (!srvc)
        return false;

    auto first_block = m_fat_range.begin() + ((index * sizeof(u32)) / d.bytes_per_sector);
    auto sectors_to_read = min(m_fat_range.length(), fat_view_pages / d.bytes_per_sector);
    return srvc->read_blocks(d.handle, m_fat_view, first_block, sectors_to_read);
}

u32 FAT32::fat_entry_at(u32 index)
{
    if (!ensure_fat_entry(index))
        return bad_cluster;

    return m_fat_view[index - m_fat_view_offset];
}

bool FAT32::File::emplace_range(ContiguousFileRange range)
{
    if (m_range_count < in_place_range_capacity) {
        m_contiguous_ranges[m_range_count++] = range;
        return true;
    }

    static constexpr size_t ranges_per_page = page_size / sizeof(ContiguousFileRange);
    auto offset_into_extra = m_range_count - in_place_range_capacity;
    size_t extra_range_capacity = ceiling_divide(offset_into_extra, ranges_per_page);

    if (extra_range_capacity == offset_into_extra) {
        auto* new_extra = allocator::allocate_new_array<ContiguousFileRange>(extra_range_capacity + page_size);
        if (!new_extra)
            return false;

        copy_memory(m_contiguous_ranges_extra, new_extra, extra_range_capacity);
        allocator::free_array(m_contiguous_ranges_extra, extra_range_capacity);

        m_contiguous_ranges_extra = new_extra;
    }

    m_contiguous_ranges_extra[offset_into_extra] = range;
    m_range_count++;
    return true;
}

bool FAT32::File::compute_contiguous_ranges()
{
    ContiguousFileRange range {};
    range.file_offset_cluster = 0;
    range.global_cluster = m_first_cluster;

    u32 current_file_offset = 1;
    u32 current_cluster = m_first_cluster;

    auto& fs = fs_as_fat32();

    for (;;) {
        auto next_cluster = fs.fat_entry_at(current_cluster);

        switch (entry_type_of_fat_value(next_cluster)) {
        case FATEntryType::END_OF_CHAIN: {
            if (current_file_offset * fs.bytes_per_cluster() < size()) {
                logger::warning("FAT32: EOC before end of file");
                return false;
            }

            if (!emplace_range(range))
                return false;

            logger::info("Computed contiguous ranges: countL: ", m_range_count);

            for (size_t i = 0; i < m_range_count; ++i)
                logger::info("range[", i, "] -> ", m_contiguous_ranges[i].global_cluster, " ", m_contiguous_ranges[i].file_offset_cluster);

            return true;
        }
        case FATEntryType::LINK:
            if (next_cluster == current_cluster + 1)
                break;

            if (!emplace_range(range))
                return false;

            range = { current_file_offset + 1, next_cluster };
            break;
        default:
            return false;
        }

        current_cluster = next_cluster;
        current_file_offset++;
    }
}

u32 FAT32::File::cluster_from_offset(u32 offset)
{
    ASSERT(m_range_count);
    ASSERT(offset < ceiling_divide(size(), fs_as_fat32().bytes_per_cluster()));

    auto* begin = m_contiguous_ranges.begin();
    auto* end = begin + m_range_count;

    if (m_contiguous_ranges_extra && m_contiguous_ranges_extra->file_offset_cluster >= offset) {
        begin = m_contiguous_ranges_extra;
        end = m_contiguous_ranges_extra + (m_range_count - in_place_range_capacity);
    }

    auto itr = lower_bound(begin, end, offset);

    if (itr == end)
        --itr;
    if (itr->file_offset_cluster > offset)
        --itr;

    auto global_cluster = itr->global_cluster + (offset - itr->file_offset_cluster);
    logger::info("cluster ", logger::hex, global_cluster, " at offset ", offset);
    ASSERT(entry_type_of_fat_value(global_cluster) == FATEntryType::LINK);

    return global_cluster;
}

bool FAT32::read(u32 cluster, u32 offset, u32 bytes, void* buffer)
{
    ASSERT(bytes != 0);

    auto* srvc = disk_services();
    if (!srvc)
        return false;

    auto& d = disk();

    auto sector_to_read = m_data_range.begin();
    sector_to_read += cluster * m_ebpb.sectors_per_cluster;

    logger::info("reading ", bytes, " at ", offset, " cluster ", cluster);

    return srvc->read(d.handle, buffer, sector_to_read * d.bytes_per_sector + offset, bytes);
}

bool FAT32::File::read(void* buffer, u32 offset, u32 size)
{
    auto& fs = fs_as_fat32();
    ASSERT(size != 0);

    if (!m_range_count && !compute_contiguous_ranges())
        return false;

    auto cluster_offset = offset / fs.bytes_per_cluster();
    auto offset_within_cluster = offset - (cluster_offset * fs.bytes_per_cluster());
    auto bytes_left_after_offset = this->size() - offset;

    size_t bytes_to_read = min(size, bytes_left_after_offset);
    u8* byte_buffer = reinterpret_cast<u8*>(buffer);

    for (;;) {
        auto current_cluster = cluster_from_offset(cluster_offset++);

        auto bytes_to_read_for_this_cluster = min<size_t>(bytes_to_read, fs.bytes_per_cluster() - offset_within_cluster);
        auto res = fs.read(pure_cluster_value(current_cluster), offset_within_cluster, bytes_to_read_for_this_cluster, byte_buffer);
        if (!res)
            return res;

        byte_buffer += bytes_to_read_for_this_cluster;
        bytes_to_read -= bytes_to_read_for_this_cluster;

        if (!bytes_to_read)
            break;

        offset_within_cluster = 0;
    }

    return true;
}

FAT32::File::~File()
{
    if (m_contiguous_ranges_extra) {
        auto offset_into_extra = m_range_count - in_place_range_capacity;
        size_t extra_range_capacity = ceiling_divide(offset_into_extra, ranges_per_page);
        allocator::free_pages(m_contiguous_ranges_extra, extra_range_capacity);
    }
}
