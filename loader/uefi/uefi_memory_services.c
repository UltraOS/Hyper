#define MSG_FMT(msg) "UEFI-MEMORY: " msg

#include "common/align.h"
#include "common/log.h"
#include "uefi_globals.h"
#include "uefi_helpers.h"
#include "memory_services.h"
#include "services_impl.h"
#include "uefi_structures.h"

#define UEFI_MS_DEBUG 1

static bool has_efi_memops = false;
static void *memory_map_buf = NULL;
static size_t buf_byte_capacity = 0;
static size_t buf_entry_count = 0;
static size_t map_key = 0;
static size_t map_efi_desc_size = 0;

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
    if ((u32)type >= VALID_LOADER_MEMORY_TYPE_BASE)
        return type;

    switch (type) {
    case EfiReservedMemoryType:
        return MEMORY_TYPE_RESERVED;
    case EfiLoaderCode:
    case EfiLoaderData:
        return MEMORY_TYPE_LOADER_RECLAIMABLE;
    case EfiBootServicesCode:
    case EfiBootServicesData:
        return MEMORY_TYPE_FREE;
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

u64 ms_allocate_pages_at(u64 address, size_t count, u32 type)
{
    SERVICE_FUNCTION();

    EFI_STATUS ret;

    ret = g_st->BootServices->AllocatePages(AllocateAddress, native_memory_type_to_efi(type), count, &address);
    if (unlikely_efi_error(ret)) {
        struct string_view err_msg = uefi_status_to_string(ret);
        print_warn("AllocatePages(AllocateAddress, %zu, 0x%016llX) failed: %pSV\n", count, address, &err_msg);
        return 0;
    }

    return address;
}

u64 ms_allocate_pages(size_t count, u64 upper_limit, u32 type)
{
    SERVICE_FUNCTION();

    EFI_STATUS ret;
    u64 address = upper_limit;

    ret = g_st->BootServices->AllocatePages(AllocateMaxAddress, native_memory_type_to_efi(type), count, &address);
    if (unlikely_efi_error(ret)) {
        struct string_view err_msg = uefi_status_to_string(ret);
        print_warn("AllocatePages(AllocateMaxAddress, %zu, 0x%016llX) failed: %pSV\n", count, address, &err_msg);
        return 0;
    }

    return address;
}

void ms_free_pages(u64 address, size_t count)
{
    SERVICE_FUNCTION();

    EFI_STATUS ret = g_st->BootServices->FreePages(address, count);
    if (unlikely_efi_error(ret)) {
        struct string_view err_msg = uefi_status_to_string(ret);
        panic("FreePages(0x%016llX, %zu) failed: %pSV\n", address, count, &err_msg);
    }
}

static void internal_buf_ensure_capacity(size_t bytes)
{
    size_t rounded_up_bytes = PAGE_ROUND_UP(bytes);
    size_t page_count = rounded_up_bytes / PAGE_SIZE;
    EFI_PHYSICAL_ADDRESS addr;
    EFI_STATUS ret;

    if (rounded_up_bytes <= buf_byte_capacity)
        return;
    if (memory_map_buf)
        ms_free_pages((u64)memory_map_buf, buf_byte_capacity / PAGE_SIZE);

    ret = g_st->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, page_count, &addr);
    if (unlikely_efi_error(ret)) {
        struct string_view err_msg = uefi_status_to_string(ret);
        panic("failed to allocate internal memory buffer (%zu pages): %pSV\n", page_count, &err_msg);
    }

    memory_map_buf = (void*)addr;
    buf_byte_capacity = rounded_up_bytes;
}

static EFI_MEMORY_DESCRIPTOR *efi_md_at(size_t i)
{
    BUG_ON(i >= buf_entry_count);
    return memory_map_buf + i * map_efi_desc_size;
}

static struct memory_map_entry *mm_entry_at(size_t i)
{
    BUG_ON(i >= buf_entry_count);
    return memory_map_buf + i * sizeof(struct memory_map_entry);
}

static void efi_memory_map_fixup(void)
{
    size_t i, j = 0;

    // Convert UEFI memory map to native format, we do this in-place
    for (i = 0; i < buf_entry_count; ++i) {
        EFI_MEMORY_DESCRIPTOR *md = efi_md_at(i);

        struct memory_map_entry me = {
            .physical_address = md->PhysicalStart,
            .size_in_bytes = md->NumberOfPages << PAGE_SHIFT,
            .type = efi_memory_type_to_native(md->Type)
        };
        mme_align_if_needed(&me);

        if (mme_is_valid(&me))
            memcpy(mm_entry_at(j++), &me, sizeof(me));
    }

    buf_entry_count = mm_fixup(memory_map_buf, j,
                               buf_byte_capacity / sizeof(struct memory_map_entry),
                               FIXUP_UNSORTED | FIXUP_OVERLAP_RESOLVE);
}

static void fill_internal_memory_map_buffer(void)
{
    UINT32 descriptor_version;
    UINTN bytes_inout;
    EFI_STATUS ret;

    for (;;) {
        bytes_inout = buf_byte_capacity;
        ret = g_st->BootServices->GetMemoryMap(&bytes_inout, memory_map_buf, &map_key,
                                               &map_efi_desc_size, &descriptor_version);
        if (ret == EFI_SUCCESS)
            break;

        if (unlikely(ret != EFI_BUFFER_TOO_SMALL)) {
            struct string_view err_msg = uefi_status_to_string(ret);
            panic("unexpected GetMemoryMap() error: %pSV\n", &err_msg);
        }

        if (unlikely(map_efi_desc_size < sizeof(EFI_MEMORY_DESCRIPTOR))) {
            panic("EFI_MEMORY_DESCRIPTOR size is too small, expected at least %zu got %zu\n",
                  sizeof(EFI_MEMORY_DESCRIPTOR), map_efi_desc_size);
        }

        internal_buf_ensure_capacity(bytes_inout);
    }

    buf_entry_count = bytes_inout / map_efi_desc_size;
    efi_memory_map_fixup();
}

size_t services_release_resources(void *buf, size_t capacity, size_t elem_size,
                                  mme_convert_t entry_convert)
{
    SERVICE_FUNCTION();
    EFI_STATUS ret;
    size_t i;

    /*
     * Only log errors after first call to GetMemoryMap,
     * as WriteString() is allowed to allocate.
     */
    logger_set_level(LOG_LEVEL_ERR);
    fill_internal_memory_map_buffer();

    if (capacity < buf_entry_count)
        return buf_entry_count;

    /*
     * The buffer is finally large enough, we can now destroy loader
     * reclaimable memory if the protocol doesn't support it and
     * transform it into MEMORY_TYPE_FREE safely as services are now
     * disabled.
     */
    buf_entry_count = mm_fixup(memory_map_buf, buf_entry_count,
                               buf_byte_capacity / sizeof(struct memory_map_entry),
                               FIXUP_NO_PRESERVE_LOADER_RECLAIM);

    for (i = 0; i < buf_entry_count; ++i) {
        struct memory_map_entry *me = mm_entry_at(i);

        if (entry_convert) {
            entry_convert(me, buf);
        } else {
            memcpy(buf, me, sizeof(struct memory_map_entry));
        }

        buf += elem_size;
    }

    ret = g_st->BootServices->ExitBootServices(g_img, map_key);
    BUG_ON(EFI_ERROR(ret));
    services_offline = true;

    return buf_entry_count;
}

u64 ms_get_highest_map_address(void)
{
    SERVICE_FUNCTION();

    if (!buf_entry_count)
        fill_internal_memory_map_buffer();

    return mme_end(mm_entry_at(buf_entry_count - 1));
}

void uefi_memory_services_init(void)
{
    has_efi_memops = g_st->BootServices->Hdr.Revision >= EFI_1_10_SYSTEM_TABLE_REVISION;
}

static bool can_use_efi_memops(void)
{
    return has_efi_memops && !services_offline;
}

static bool efi_copy_mem(void *dest, void *src, size_t count)
{
    if (!can_use_efi_memops())
        return false;

    g_st->BootServices->CopyMem(dest, src, count);
    return true;
}

#ifdef memset
#undef memset
#endif

void *memset(void *dest, int val, size_t count)
{
    if (can_use_efi_memops())
        g_st->BootServices->SetMem(dest, count, val);
    else
        memset_generic(dest, val, count);

    return dest;
}

#ifdef memcpy
#undef memcpy
#endif

void *memcpy(void *dest, void *src, size_t count)
{
    if (!efi_copy_mem(dest, src, count))
        memcpy_generic(dest, src, count);

    return dest;
}

#ifdef memmove
#undef memmove
#endif

void *memmove(void *dest, void *src, size_t count)
{
    if (!efi_copy_mem(dest, src, count))
        memmove_generic(dest, src, count);

    return dest;
}
