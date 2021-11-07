#include "FileSystem.h"
#include "FAT32/FAT32.h"
#include "Allocator.h"

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
