#include "FileSystem.h"
#include "FAT32/FAT32.h"
#include "Allocator.h"

static StringView extract_numeric_prefix(StringView& string, bool allow_hex, bool& ok, size_t max_size = 0)
{
    ok = false;
    auto local_string = string;
    StringView out_string(string.data(), static_cast<size_t>(0));

    auto consume_character = [&]() {
        out_string.extend_by(1);
        local_string.offset_by(1);
    };

    for (;;) {
        if (max_size && out_string.size() == max_size)
            break;

        if (local_string.empty())
            break;

        char c = local_string.front();

        if (c >= '0' && c <= '9') {
            consume_character();
            continue;
        }

        if (!allow_hex)
            break;

        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            consume_character();
            continue;
        }
    }

    if (out_string.empty())
        return {};

    ok = true;
    return out_string;
}

GUID parse_guid(StringView string, bool& ok)
{
    ok = false;

    if (string.size() != chars_per_guid)
        return {};

    GUID guid {};

    guid.data1 = from_hex_string<u32>(StringView(string, 4 * chars_per_hex_byte), ok, false);
    string.offset_by(4 * chars_per_hex_byte);
    if (!ok)
        return {};

    guid.data2 = from_hex_string<u16>(StringView(string, 2 * chars_per_hex_byte), ok, false);
    string.offset_by(2 * chars_per_hex_byte);
    if (!ok)
        return {};

    guid.data3 = from_hex_string<u16>(StringView(string, 2 * chars_per_hex_byte), ok, false);
    string.offset_by(2 * chars_per_hex_byte);
    if (!ok)
        return {};

    for (size_t i = 0; i < 8; ++i) {
        guid.data4[i] = from_hex_string<u8>(StringView(string, 1 * chars_per_hex_byte), ok, false);
        if (!ok)
            return {};

        string.offset_by(1 * chars_per_hex_byte);
    }

    return guid;
}

FullPath parse_path(StringView path, bool& did_parse)
{
    FullPath out_path {};
    did_parse = false;

    // path relative to config disk
    if (path.starts_with("/") || path.starts_with("::/")) {
        out_path.disk_id_type = FullPath::DiskIdentifier::ORIGIN;
        out_path.partition_id_type = FullPath::PartitionIdentifier::ORIGIN;

        path.offset_by(path.front() == ':' ? 2 : 0);
        out_path.path_within_partition = path;
        did_parse = true;
        return out_path;
    }

    if (path.starts_with("DISKUUID")) {
        bool ok = false;

        path.offset_by(7);
        auto prefix = extract_numeric_prefix(path, true, ok, chars_per_guid);
        if (!ok)
            return out_path;

        out_path.disk_id_type = FullPath::DiskIdentifier::UUID;
        out_path.disk_guid = parse_guid(prefix, ok);
        if (!ok)
            return out_path;
    } else if (path.starts_with("DISK")) {
        bool ok = false;
        path.offset_by(4);
        auto prefix = extract_numeric_prefix(path, false, ok);
        if (!ok)
            return out_path;

        auto as_number = from_dec_string<u64>(prefix, ok, false);
        if (!ok)
            return out_path;

        out_path.disk_id_type = FullPath::DiskIdentifier::INDEX;
        out_path.disk_index = as_number;
    } else { // invalid prefix
        return out_path;
    }

    if (path.starts_with("GPTUUID")) {
        bool ok = false;

        path.offset_by(7);
        auto prefix = extract_numeric_prefix(path, true, ok, chars_per_guid);
        if (!ok)
            return out_path;

        out_path.partition_id_type = FullPath::PartitionIdentifier::GPT_UUID;
        out_path.partition_guid = parse_guid(prefix, ok);
        if (!ok)
            return out_path;
    } else if (path.starts_with("MBR") || path.starts_with("GPT")) {
        bool ok = false;

        path.offset_by(3);
        auto prefix = extract_numeric_prefix(path, false, ok);
        if (!ok)
            return out_path;

        auto as_number = from_dec_string<u64>(prefix, ok, false);
        if (!ok)
            return out_path;

        out_path.partition_id_type = path[0] == 'M' ? FullPath::PartitionIdentifier::MBR_INDEX : FullPath::PartitionIdentifier::GPT_INDEX;
        out_path.partition_index = as_number;
    } else if (path.starts_with("::/")) {
        // GPT disks cannot be treated as a raw device
        if (out_path.disk_id_type != FullPath::DiskIdentifier::INDEX)
            return out_path;

        out_path.partition_id_type = FullPath::PartitionIdentifier::RAW;
    } else {
        return out_path;
    }

    if (!path.starts_with("::/"))
        return out_path;

    path.offset_by(2);
    out_path.path_within_partition = path;

    did_parse = true;
    return out_path;
}

DiskServices* FileSystem::set_backend(DiskServices* backend)
{
    auto* previous = s_backend;
    s_backend = backend;
    return previous;
}

FileSystem* FileSystem::try_detect(const Disk& disk, LBARange range, void* first_block_buffer)
{
    if (!s_backend)
        return nullptr;

    if (FAT32::detect(disk, range, first_block_buffer))
        return allocator::allocate_new<FAT32>(disk, range, first_block_buffer);

    return nullptr;
}
