#pragma once

#include "Common/Types.h"
#include "Common/Utilities.h"

struct PACKED EBPB {
    // BPB
    u16 bytes_per_sector;
    u8 sectors_per_cluster;
    u16 reserved_sectors;
    u8 fat_count;
    u16 max_root_dir_entries;
    u16 unused_1; // total logical sectors for FAT12/16
    u8 media_descriptor;
    u16 unused_2; // logical sectors per file allocation table for FAT12/16
    u16 sectors_per_track;
    u16 heads;
    u32 hidden_sectors;
    u32 total_logical_sectors;

    // EBPB
    u32 sectors_per_fat;
    u16 ext_flags;
    u16 version;
    u32 root_dir_cluster;
    u16 fs_information_sector;
    u16 backup_boot_sectors;
    u8 reserved[12];
    u8 drive_number;
    u8 unused_3;
    u8 signature;
    u32 volume_id;
    char volume_label[11];
    char filesystem_type[8];

    static constexpr size_t size = 79;
};

static_assert(sizeof(EBPB) == EBPB::size, "Incorrect size of EBPB");

static constexpr size_t short_name_length = 8;
static constexpr size_t short_extension_length = 3;

struct PACKED DirectoryEntry {
    static constexpr size_t full_short_name_length = short_name_length + short_extension_length;

    char filename[short_name_length];
    char extension[short_extension_length];

    enum class Attributes : u8 {
        READ_ONLY = SET_BIT(0),
        HIDDEN = SET_BIT(1),
        SYSTEM = SET_BIT(2),
        VOLUME_LABEL = SET_BIT(3),
        SUBDIRECTORY = SET_BIT(4),
        ARCHIVE = SET_BIT(5),
        DEVICE = SET_BIT(6)
    } attributes;

    friend Attributes operator&(Attributes l, Attributes r)
    {
        return static_cast<Attributes>(static_cast<u8>(l) & static_cast<u8>(r));
    }

    enum class CaseInfo : u8 {
        LOWERCASE_NAME = SET_BIT(3),
        LOWERCASE_EXTENSION = SET_BIT(4)
    } case_info;

    friend CaseInfo operator&(CaseInfo l, CaseInfo r)
    {
        return static_cast<CaseInfo>(static_cast<u8>(l) & static_cast<u8>(r));
    }

    friend CaseInfo operator|(CaseInfo l, CaseInfo r)
    {
        return static_cast<CaseInfo>(static_cast<u8>(l) | static_cast<u8>(r));
    }

    friend CaseInfo& operator|=(CaseInfo& l, CaseInfo r)
    {
        auto temp = l | r;
        l = temp;

        return l;
    }

    u8 created_ms;
    u16 created_time;
    u16 created_date;
    u16 last_accessed_date;
    u16 cluster_high;
    u16 last_modified_time;
    u16 last_modified_date;
    u16 cluster_low;
    u32 size;

    bool is_read_only() const { return (attributes & Attributes::READ_ONLY) == Attributes::READ_ONLY; }
    bool is_hidden() const { return (attributes & Attributes::HIDDEN) == Attributes::HIDDEN; }
    bool is_system() const { return (attributes & Attributes::SYSTEM) == Attributes::SYSTEM; }
    bool is_volume_label() const { return (attributes & Attributes::VOLUME_LABEL) == Attributes::VOLUME_LABEL; }
    bool is_directory() const { return (attributes & Attributes::SUBDIRECTORY) == Attributes::SUBDIRECTORY; };
    bool is_archive() const { return (attributes & Attributes::ARCHIVE) == Attributes::ARCHIVE; }
    bool is_device() const { return (attributes & Attributes::DEVICE) == Attributes::DEVICE; }
    bool is_long_name() const { return (attributes & static_cast<Attributes>(0x0F)) == static_cast<Attributes>(0x0F); }

    bool is_lowercase_name() const { return (case_info & CaseInfo::LOWERCASE_NAME) == CaseInfo::LOWERCASE_NAME; }
    bool is_lowercase_extension() const { return (case_info & CaseInfo::LOWERCASE_EXTENSION) == CaseInfo::LOWERCASE_EXTENSION; }

    static constexpr u8 deleted_mark = 0xE5;
    bool is_deleted() const { return static_cast<u8>(filename[0]) == deleted_mark; }

    static constexpr u8 end_of_directory_mark = 0x00;
    bool is_end_of_directory() const { return static_cast<u8>(filename[0]) == end_of_directory_mark; }

    static constexpr size_t size_in_bytes = 32;
};

static constexpr size_t bytes_per_ucs2_char = 2;

struct PACKED LongNameDirectoryEntry {
    static constexpr size_t name_1_characters = 5;
    static constexpr size_t name_2_characters = 6;
    static constexpr size_t name_3_characters = 2;
    static constexpr size_t characters_per_entry = name_1_characters + name_2_characters + name_3_characters;

    u8 sequence_number;
    u8 name_1[name_1_characters * bytes_per_ucs2_char];
    u8 attributes;
    u8 type;
    u8 checksum;
    u8 name_2[name_2_characters * bytes_per_ucs2_char];
    u16 first_cluster;
    u8 name_3[name_3_characters * bytes_per_ucs2_char];

    static LongNameDirectoryEntry from_normal(DirectoryEntry& directory_entry)
    {
        return bit_cast<LongNameDirectoryEntry>(directory_entry);
    }

    static LongNameDirectoryEntry from_storage(void* storage)
    {
        LongNameDirectoryEntry entry;
        copy_memory(storage, &entry, sizeof(LongNameDirectoryEntry));

        return entry;
    }

    static constexpr u8 sequence_bits_mask = 0b11111;
    u8 extract_sequence_number() { return sequence_number & sequence_bits_mask; }

    static constexpr u8 last_logical_entry_bit = SET_BIT(6);
    bool is_last_logical() { return sequence_number & last_logical_entry_bit; }

    void make_last_logical_with_sequence_number(u8 number)
    {
        sequence_number |= last_logical_entry_bit;
        sequence_number |= number;
    }

    static constexpr size_t size_in_bytes = 32;
};

static_assert(sizeof(DirectoryEntry) == DirectoryEntry::size_in_bytes, "Incorrect directory entry size");
static_assert(sizeof(LongNameDirectoryEntry) == LongNameDirectoryEntry::size_in_bytes, "Incorrect long name directory entry size");
