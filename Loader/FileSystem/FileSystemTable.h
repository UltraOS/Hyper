#pragma once

#include "FileSystem.h"

struct FileSystemEntry {
    void* disk_handle;
    u32 disk_index;
    u32 partition_index;
    GUID disk_guid;
    GUID partition_guid;
    FileSystem* filesystem;
};

namespace fs_table {

void add_raw_entry(void* disk_handle, u32 disk_index, FileSystem*);
void add_mbr_entry(void* disk_handle, u32 disk_index, u32 partition_index, FileSystem*);
void add_gpt_entry(void* disk_handle, u32 disk_index, u32 partition_index,
                   const GUID& disk_guid, const GUID& partition_guid, FileSystem*);

const FileSystemEntry* get_by_full_path(const FullPath&);

void set_origin(FileSystemEntry&);
const FileSystemEntry& get_origin();

class all {
public:
    static FileSystemEntry* begin();
    static FileSystemEntry* end();
};

}
