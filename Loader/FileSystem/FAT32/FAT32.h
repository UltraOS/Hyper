#pragma once

#include "FileSystem/FileSystem.h"
#include "Common/StaticArray.h"
#include "Structures.h"

class FAT32 final : public FileSystem {
public:
    FAT32(const Disk&, LBARange, void* first_block_buffer);

    static bool detect(const Disk&, LBARange, void* first_block_buffer);

    ::File* open(StringView path) override;
    void close(::File*) override;

    [[nodiscard]] u32 bytes_per_cluster() const { return m_bytes_per_cluster; }

    u32 fat_entry_at(u32 index);

    class File final : public ::File {
    public:
        File(FAT32& parent, u32 first_cluster, u32 size)
            : ::File(parent, size)
            , m_first_cluster(first_cluster)
        {
        }

        bool read(void* buffer, u32 offset, u32 bytes) override;

        [[nodiscard]] u32 first_cluster() const { return m_first_cluster; }

        ~File();

    private:
        FAT32& fs_as_fat32() { return static_cast<FAT32&>(m_parent); }
        bool compute_contiguous_ranges();

        u32 cluster_from_offset(u32);

        struct ContiguousFileRange {
            u32 file_offset_cluster;
            u32 global_cluster;

            friend bool operator<(u32 l, const ContiguousFileRange& r)
            {
                return l < r.file_offset_cluster;
            }

            friend bool operator<(const ContiguousFileRange& l, u32 r)
            {
                return l.file_offset_cluster < r;
            }
        };
        bool emplace_range(ContiguousFileRange);

        static constexpr size_t approximate_class_size = sizeof(void*) * 8;
        static constexpr size_t in_place_range_capacity = (page_size - approximate_class_size) / sizeof(ContiguousFileRange);
        static constexpr size_t ranges_per_page = page_size / sizeof(ContiguousFileRange);

        u32 m_first_cluster { 0 };
        u32 m_range_count { 0 };
        ContiguousFileRange* m_contiguous_ranges_extra { nullptr };

        // Sorted in ascending order by file_offset_cluster.
        // Each range at i spans ([i].file_offset_cluster -> [i + 1].file_offset_cluster - 1) clusters
        // For last i the end is the last cluster of the file (inclusive).
        StaticArray<ContiguousFileRange, in_place_range_capacity> m_contiguous_ranges;
    };
    static_assert(sizeof(FAT32::File) < page_size);

    struct Entry {
        char name[255];
        u8 name_length;

        bool is_directory;
        u32 first_cluster;
        u32 size;

        StringView name_view() { return { name, name_length }; }
    };

    class Directory {
    public:
        Directory(FAT32& parent, u32 first_cluster)
            : m_parent(parent)
            , m_current_cluster(first_cluster)
        {
        }

        bool next_entry(Entry& out_entry);

    private:
        bool fetch_next(void* entry);

    private:
        FAT32& m_parent;
        u32 m_current_cluster { 0 };
        u32 m_current_offset { 0 };
        bool m_end { false };
    };

private:
    bool read(u32 cluster, u32 offset, u32 bytes, void* buffer);

    bool ensure_fat_entry(u32);
    bool ensure_root_directory();

private:
    EBPB m_ebpb;
    LBARange m_fat_range;
    LBARange m_data_range;

    u32 m_bytes_per_cluster { 0 };
    u32 m_fat_clusters { 0 };

    static constexpr size_t fat_view_pages = page_size * 16;
    static constexpr size_t fat_view_capacity = fat_view_pages / sizeof(u32);
    size_t m_fat_view_offset { 0 };
    u32* m_fat_view { nullptr };

    File* m_root_directory { nullptr };
};
