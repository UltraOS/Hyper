#pragma once

#include "common/types.h"

// These are consistent with the ACPI specification
#define MEMORY_TYPE_INVALID            0x00000000
#define MEMORY_TYPE_FREE               0x00000001
#define MEMORY_TYPE_RESERVED           0x00000002
#define MEMORY_TYPE_ACPI_RECLAIMABLE   0x00000003
#define MEMORY_TYPE_NVS                0x00000004
#define MEMORY_TYPE_UNUSABLE           0x00000005
#define MEMORY_TYPE_DISABLED           0x00000006
#define MEMORY_TYPE_PERSISTENT         0x00000007

/*
 * All memory allocated by the loader is marked with this by default,
 * the real underlying type is of course MEMORY_TYPE_FREE.
 */
#define MEMORY_TYPE_LOADER_RECLAIMABLE 0xFFFF0001

struct memory_map_entry {
    u64 physical_address;
    u64 size_in_bytes;
    u64 type;
};

/*
 * Converts memory_map_entry to the native protocol memory map entry format.
 * entry -> current entry to be converted.
 * buf -> pointer to the caller buffer where the entry should be written.
 *        buf is guaranteed to have enough capacity for the entry.
 */
typedef void (*entry_convert_func) (struct memory_map_entry *entry, void *buf);

/*
 * Allocates count pages starting at address with type.
 * address -> page aligned address of the first byte of the range to allocate.
 * count -> number of 4096-byte pages to allocate.
 * type -> the type of range to allocate, must be one of the valid protocol values.
 * Returns the same value as 'address' if allocation succeeded, nullptr otherwise.
 */
u64 ms_allocate_pages_at(u64 address, size_t count, u32 type);

/*
 * Allocates count pages with type anywhere in available memory.
 * count -> number of 4096-byte pages to allocate.
 * upper_limit -> 1 + maximum allowed address within the allocated range.
 * type -> the type of range to allocate, must be one of the valid protocol values.
 * Returns the address of the first byte of the allocated range if allocation succeeded, nullptr otherwise.
 */
u64 ms_allocate_pages(size_t count, u64 upper_limit, u32 type);

/*
 * Frees count pages starting at address.
 */
void ms_free_pages(u64 address, size_t count);

/*
 * Copies protocol-formatted memory map entries into buffer.
 * buf -> pointer to the first byte of the buffer that receives memory map entries
 *        (allowed to be nullptr if capacity is passed as 0).
 * capacity -> number of elem_size elements that fit in the buffer.
 * elem_size -> size in bytes of the native memory map entry that will be written to 'buf'
 * out_key -> set by the service provider and is a unique id of the current state of the map
 *            that changes with every allocate/free call. Only set if capacity
 *            was enough to receive the entire map.
 * entry_convert -> a callback to use to convert each memory map entry to the native protocol format.
 *                  Can be NULL, in which case the memory_map_entry struct is copied verbatim
 *                  elem_size must be equal to sizeof(struct memory_map_entry) for this case.
 * Returns the number of entries that would've been copied if buffer had enough capacity.
 */
size_t ms_copy_map(void *buf, size_t capacity, size_t elem_size,
                   size_t *out_key, entry_convert_func entry_convert);

/*
 * Returns the address of the last byte of the last entry in the memory map + 1
 */
u64 ms_get_highest_map_address();
