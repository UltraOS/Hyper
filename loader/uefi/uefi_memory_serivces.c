#include "uefi_memory_serivces.h"
#include "uefi_globals.h"
#include "uefi_helpers.h"
#include "common/constants.h"
#include "services.h"
#include "structures.h"
#include "common/log.h"

#undef MSG_FMT
#define MSG_FMT(msg) "UEFI-MEMORY: " msg


static void *internal_memory_map_buf = NULL;
static size_t internal_map_byte_capacity = 0;
static size_t internal_map_entries = 0;
static size_t internal_map_key = 0;
static size_t internal_descriptor_size = 0;

// Reserved for use by UEFI OS loaders that are provided by operating system vendors
#define VALID_LOADER_MEMORY_TYPE_BASE 0x80000000

static EFI_MEMORY_TYPE native_memory_type_to_efi(u32 type)
{
    if (likely(type >= VALID_LOADER_MEMORY_TYPE_BASE))
        return type;

    panic("invalid native -> efi memory type conversion: type 0x%08X\n", type);
}

static u32 efi_memory_type_to_native(EFI_MEMORY_TYPE type)
{
    if (type >= VALID_LOADER_MEMORY_TYPE_BASE)
        return type;

    switch (type) {
    case EfiReservedMemoryType:
        return MEMORY_TYPE_RESERVED;
    case EfiLoaderCode:
    case EfiLoaderData:
    case EfiBootServicesCode:
    case EfiBootServicesData:
        return MEMORY_TYPE_LOADER_RECLAIMABLE;
    case EfiRuntimeServicesCode:
    case EfiRuntimeServicesData:
        return MEMORY_TYPE_RESERVED;
    case EfiConventionalMemory:
        return MEMORY_TYPE_FREE;
    case EfiUnusableMemory:
        return MEMORY_TYPE_UNUSABLE;
    case EfiACPIReclaimMemory:
        return MEMORY_TYPE_ACPI_RECLAIMABLE;
    case EfiACPIMemoryNVS:
        return MEMORY_TYPE_NVS;
    case EfiMemoryMappedIO:
    case EfiMemoryMappedIOPortSpace:
    case EfiPalCode:
        return MEMORY_TYPE_RESERVED;
    case EfiPersistentMemory:
        return MEMORY_TYPE_PERSISTENT;
    case EfiUnacceptedMemoryType:
        return MEMORY_TYPE_DISABLED;
    default:
        break;
    }

    panic("don't know how to convert efi memory type 0x%08X into native\n", type);
}

static u64 uefi_allocate_pages_at(u64 address, size_t count, u32 type)
{
    EFI_STATUS ret;

    ret = g_st->BootServices->AllocatePages(AllocateAddress, native_memory_type_to_efi(type), count, &address);
    if (unlikely(EFI_ERROR(ret))) {
        struct string_view err_msg = uefi_status_to_string(ret);
        print_warn("AllocatePages(AllocateAddress, %zu, 0x%016llX) failed: %pSV\n", address, count, &err_msg);
        return 0;
    }

    return address;
}

static u64 uefi_allocate_pages(size_t count, u64 upper_limit, u32 type)
{
    EFI_STATUS ret;
    u64 address = upper_limit;

    ret = g_st->BootServices->AllocatePages(AllocateMaxAddress, native_memory_type_to_efi(type), count, &address);
    if (unlikely(EFI_ERROR(ret))) {
        struct string_view err_msg = uefi_status_to_string(ret);
        print_warn("AllocatePages(AllocateMaxAddress, %zu, 0x%016llX) failed: %pSV\n", address, count, &err_msg);
        return 0;
    }

    return address;
}

static void uefi_free_pages(u64 address, size_t count)
{
    EFI_STATUS ret = g_st->BootServices->FreePages(address, count);
    struct string_view err_msg;

    if (likely(!EFI_ERROR(ret)))
        return;

    err_msg = uefi_status_to_string(ret);
    panic("FreePages(0x%016llX, %zu) failed: %pSV\n", address, count, &err_msg);
}

static void internal_buf_ensure_capacity(size_t bytes)
{
    size_t rounded_up_bytes = PAGE_ROUND_UP(bytes);
    size_t page_count = rounded_up_bytes / PAGE_SIZE;
    EFI_PHYSICAL_ADDRESS addr;
    EFI_STATUS ret;

    if (rounded_up_bytes <= internal_map_byte_capacity)
        return;
    if (internal_memory_map_buf)
        uefi_free_pages((u64)internal_memory_map_buf, internal_map_byte_capacity / PAGE_SIZE);

    ret = g_st->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, page_count, &addr);
    if (unlikely(EFI_ERROR(ret))) {
        struct string_view err_msg = uefi_status_to_string(ret);
        panic("failed to allocate internal memory buffer (%zu pages): %pSV\n", page_count, &err_msg);
    }

    internal_memory_map_buf = (void*)addr;
    internal_map_byte_capacity = rounded_up_bytes;
}

static size_t fill_internal_memory_map_buffer()
{
    UINT32 descriptor_version;
    UINTN bytes_inout;
    EFI_STATUS ret;

    for (;;) {
        bytes_inout = internal_map_byte_capacity;
        ret = g_st->BootServices->GetMemoryMap(&bytes_inout, internal_memory_map_buf, &internal_map_key,
                                               &internal_descriptor_size, &descriptor_version);

        if (ret == EFI_SUCCESS)
            break;

        if (unlikely(ret != EFI_BUFFER_TOO_SMALL)) {
            struct string_view err_msg = uefi_status_to_string(ret);
            panic("unexpected GetMemoryMap() error: %pSV\n", &err_msg);
        }

        if (unlikely(internal_descriptor_size < sizeof(EFI_MEMORY_DESCRIPTOR))) {
            panic("EFI_MEMORY_DESCRIPTOR size is too small, expected at least %zu got %zu\n",
                  sizeof(EFI_MEMORY_DESCRIPTOR), internal_descriptor_size);
        }

        internal_buf_ensure_capacity(bytes_inout);
    }

    internal_map_entries = bytes_inout / internal_descriptor_size;
    return internal_map_entries;
}

static size_t uefi_copy_map(void *buf, size_t capacity, size_t elem_size,
                            size_t *out_key, entry_convert_func entry_convert)
{
    size_t entries = fill_internal_memory_map_buffer();
    void *buf_cursor = internal_memory_map_buf;
    if (capacity < entries)
        return entries;

    while (entries--) {
        EFI_MEMORY_DESCRIPTOR *desc = buf_cursor;

        struct memory_map_entry entry = {
            .physical_address = desc->PhysicalStart,
            .size_in_bytes = desc->NumberOfPages * PAGE_SIZE,
            .type = efi_memory_type_to_native(desc->Type)
        };

        if (entry_convert) {
            entry_convert(&entry, buf);
        } else {
            memcpy(buf, &entry, sizeof(struct memory_map_entry));
        }

        buf += elem_size;
        buf_cursor += internal_descriptor_size;
    }

    *out_key = internal_map_key;
    return internal_map_entries;
}

static struct memory_services uefi_memory_services = {
    .allocate_pages_at = uefi_allocate_pages_at,
    .allocate_pages = uefi_allocate_pages,
    .free_pages = uefi_free_pages,
    .copy_map = uefi_copy_map
};

struct memory_services *memory_services_init()
{
    return &uefi_memory_services;
}
