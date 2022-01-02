#pragma once

#include "common/types.h"

struct binary_info {
    u64 entrypoint_address;

    u64 virtual_base;
    u64 virtual_ceiling;

    u64 physical_base;
    u64 physical_ceiling;

    u32 bitness;
    bool physical_valid;
};

struct load_result {
    struct binary_info info;
    bool success;
    const char *error_msg;
};

void elf_load(u8 *file_data, size_t size, bool use_va, bool allocate_anywhere, struct load_result *res);
u32 elf_bitness(void *file_data, size_t size);

