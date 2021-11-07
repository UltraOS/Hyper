#pragma once

#include "FileSystem.h"

struct FileSystemEntry {
    void* disk_handle;
    u32 partition_id;
    GUID disk_guid;
    GUID partition_guid;
    FileSystem* filesystem;
};

namespace fs_table {

void add_raw_entry(void* disk_handle, FileSystem*);
void add_mbr_entry(void* disk_handle, u32 partition_id, FileSystem*);
void add_gpt_entry(void* disk_handle, u32 partition_id, const GUID& disk_guid, const GUID& partition_guid, FileSystem*);

FileSystem* get_raw_entry(void* disk_handle);
FileSystem* get_mbr_entry(void* disk_handle, u32 partition_id);
FileSystem* get_gpt_entry(void* disk_handle, u32 partition_id);
FileSystem* get_gpt_entry(const GUID& disk_guid, u32 partition_id);
FileSystem* get_gpt_entry(const GUID& disk_guid, const GUID& partition_guid);

class all {
public:
    static FileSystemEntry* begin();
    static FileSystemEntry* end();
};

}
