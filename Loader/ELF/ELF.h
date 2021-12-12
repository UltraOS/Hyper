#pragma once

#include "FileSystem/FileSystem.h"
#include "Common/Types.h"

namespace elf {

enum class UseVirtualAddress {
    YES,
    NO
};

enum class AllocateAnywhere {
    YES,
    NO
};

struct BinaryInformation {
    Address64 entrypoint_address;

    Address64 virtual_base;
    Address64 virtual_ceiling;

    Address64 physical_base;
    Address64 physical_ceiling;

    u32 bitness;
    bool physical_valid;
};

struct LoadResult {
    BinaryInformation info;
    bool success;
    StringView error_message;
};

LoadResult load(Span<u8> file, UseVirtualAddress, AllocateAnywhere);

u32 get_bitness(Span<u8> file);

}
