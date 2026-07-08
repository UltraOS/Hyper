#define MSG_FMT(msg) "UEFI-MEMORY: " msg

#include "common/attributes.h"
#include "common/align.h"
#include "common/log.h"
#include "uefi/globals.h"
#include "uefi/helpers.h"
#include "memory_services.h"
#include "services_impl.h"
#include "uefi/structures.h"

#define UEFI_MS_DEBUG 1

static void *memory_map_buf = NULL;
static size_t buf_byte_capacity = 0;
static size_t buf_entry_count = 0;
static size_t map_key = 0;
static size_t map_efi_desc_size = 0;
static u32 map_efi_desc_version = 0;
static size_t map_raw_bytes = 0;

/*
 * Buffer holding a verbatim copy of the raw EFI memory map, reserved by
 * services_setup_uefi_handoff() and filled right before ExitBootServices()
 * (see capture_raw_efi_memory_map()).
 */
static void *uefi_map_capture_buf = NULL;
static size_t uefi_map_capture_capacity = 0;
static size_t uefi_map_captured_bytes = 0;

/*
 * Storage for protocol-specific allocations that use custom memory types.
 *
 * While UEFI does have a native way to allocate memory using custom types,
 * older implementations using pre-2011 EDK2 all have the same bug, which causes
 * GetMemoryMap to crash if a custom memory type is used.
 *
 * Fix for the bug in EDK2:
 * https://github.com/tianocore/edk2/commit/10fe0d814add860a1040e648b1f5782c0de350e6
 *
 * The code with protocol_allocations & account_allocation is workaround for
 * that.
 */
static struct memory_map_entry *protocol_allocations = NULL;
static size_t protocol_allocations_count = 0;
static size_t protocol_allocations_capacity = 0;

#define PROTOCOL_ALLOCATIONS_BUFFER_INCREMENT 64

static u32 efi_md_to_native_type(EFI_MEMORY_DESCRIPTOR *md)
{
    u32 out_type = MEMORY_TYPE_FREE;

    switch (md->Type) {
    case EfiLoaderCode:
    case EfiLoaderData:
        out_type = MEMORY_TYPE_LOADER_RECLAIMABLE;
        FALLTHROUGH;
    case EfiBootServicesCode:
    case EfiBootServicesData:
    case EfiConventionalMemory:
        if (md->Attribute & EFI_MEMORY_SP)
            return MEMORY_TYPE_SOFT_RESERVED;
        if (md->Attribute & EFI_MEMORY_WB)
            return out_type;
        return MEMORY_TYPE_RESERVED;
    case EfiUnusableMemory:
        return MEMORY_TYPE_UNUSABLE;
    case EfiACPIReclaimMemory:
        return MEMORY_TYPE_ACPI_RECLAIMABLE;
    case EfiACPIMemoryNVS:
        return MEMORY_TYPE_ACPI_NVS;
    case EfiPersistentMemory:
        return MEMORY_TYPE_PERSISTENT;
    case EfiUnacceptedMemoryType:
        return MEMORY_TYPE_UNACCEPTED;
    case EfiReservedMemoryType:
    case EfiRuntimeServicesCode:
    case EfiRuntimeServicesData:
    case EfiMemoryMappedIO:
    case EfiMemoryMappedIOPortSpace:
    case EfiPalCode:
    default:
        return MEMORY_TYPE_RESERVED;
    }
}

static inline void account_allocation(u64 address, size_t count, u32 type)
{
    struct memory_map_entry *entry;

    if (type < MEMORY_TYPE_PROTO_SPECIFIC_BASE)
        return;

    if (protocol_allocations_count == protocol_allocations_capacity) {
        void *new_buf;

        OOPS_ON(!uefi_pool_alloc(
            EfiLoaderData, sizeof(struct memory_map_entry),
            protocol_allocations_capacity + PROTOCOL_ALLOCATIONS_BUFFER_INCREMENT,
            &new_buf
        ));

        if (protocol_allocations != NULL) {
            memcpy(
                new_buf, protocol_allocations,
                protocol_allocations_capacity * sizeof(struct memory_map_entry)
            );
            g_st->BootServices->FreePool(protocol_allocations);
        }

        protocol_allocations_capacity += PROTOCOL_ALLOCATIONS_BUFFER_INCREMENT;
        protocol_allocations = new_buf;
    }

    entry = &protocol_allocations[protocol_allocations_count++];

    entry->physical_address = address;
    entry->size_in_bytes = count << PAGE_SHIFT;
    entry->type = type;
}

u64 ms_allocate_pages_at(u64 address, size_t count, u32 type)
{
    SERVICE_FUNCTION();

    EFI_STATUS ret;

    ret = g_st->BootServices->AllocatePages(AllocateAddress, EfiLoaderData, count, &address);
    if (unlikely_efi_error(ret)) {
        struct string_view err_msg = uefi_status_to_string(ret);
        print_warn("AllocatePages(AllocateAddress, %zu, 0x%016llX) failed: %pSV\n", count, address, &err_msg);
        return 0;
    }

    account_allocation(address, count, type);
    return address;
}

u64 ms_allocate_pages(size_t count, u64 upper_limit, u32 type)
{
    SERVICE_FUNCTION();

    EFI_STATUS ret;
    u64 address = upper_limit;

    ret = g_st->BootServices->AllocatePages(AllocateMaxAddress, EfiLoaderData, count, &address);
    if (unlikely_efi_error(ret)) {
        struct string_view err_msg = uefi_status_to_string(ret);
        print_warn("AllocatePages(AllocateMaxAddress, %zu, 0x%016llX) failed: %pSV\n", count, address, &err_msg);
        return 0;
    }

    account_allocation(address, count, type);
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

static void page_buf_ensure_capacity(void **buf, size_t *byte_capacity,
                                     size_t bytes)
{
    size_t rounded_up_bytes = PAGE_ROUND_UP(bytes);
    size_t page_count = rounded_up_bytes / PAGE_SIZE;
    EFI_PHYSICAL_ADDRESS addr;
    EFI_STATUS ret;

    if (rounded_up_bytes <= *byte_capacity)
        return;
    if (*buf)
        ms_free_pages((u64)*buf, *byte_capacity / PAGE_SIZE);

    ret = g_st->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, page_count, &addr);
    if (unlikely_efi_error(ret)) {
        struct string_view err_msg = uefi_status_to_string(ret);
        panic("failed to allocate internal memory buffer (%zu pages): %pSV\n", page_count, &err_msg);
    }

    *buf = (void*)addr;
    *byte_capacity = rounded_up_bytes;
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
    u8 fixup_flags = FIXUP_UNSORTED | FIXUP_OVERLAP_RESOLVE;

    // Convert UEFI memory map to native format, we do this in-place
    for (i = 0; i < buf_entry_count; ++i) {
        EFI_MEMORY_DESCRIPTOR *md = efi_md_at(i);

        struct memory_map_entry me = {
            .physical_address = md->PhysicalStart,
            .size_in_bytes = md->NumberOfPages << PAGE_SHIFT,
            .type = efi_md_to_native_type(md)
        };
        mme_align_if_needed(&me);

        if (mme_is_valid(&me))
            memcpy(mm_entry_at(j++), &me, sizeof(me));
    }
    buf_entry_count = j;

    /*
     * Pretend the custom type allocations were there in the first place, even
     * though this causes collisions with EfiLoaderData ranges. This is fine as
     * mm_fixup is able to take care of those by letting the higher priority
     * (larger type value) allocations win in the range collision resolution.
     */
    if (protocol_allocations_count) {
        fixup_flags |= FIXUP_OVERLAP_INTENTIONAL;
        buf_entry_count += protocol_allocations_count;
        memcpy(mm_entry_at(j), protocol_allocations,
               protocol_allocations_count * sizeof(struct memory_map_entry));
    }

    buf_entry_count = mm_fixup(
        memory_map_buf, buf_entry_count,
        buf_byte_capacity / sizeof(struct memory_map_entry),
        fixup_flags
    );
}

/*
 * Fetch the firmware memory map into memory_map_buf in its raw EFI descriptor
 * form, growing the buffer as needed. Leaves buf_entry_count as the raw
 * descriptor count and map_raw_bytes as its size in bytes. The map_key is only
 * valid as long as no allocation happens afterwards, so any use of it (i.e.
 * ExitBootServices) must follow immediately.
 */
static void acquire_efi_memory_map(void)
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

        page_buf_ensure_capacity(
            &memory_map_buf, &buf_byte_capacity,
            bytes_inout +
            protocol_allocations_count * sizeof(struct memory_map_entry)
        );
    }

    map_efi_desc_version = descriptor_version;
    map_raw_bytes = bytes_inout;
    buf_entry_count = bytes_inout / map_efi_desc_size;
}

/*
 * If a capture buffer was reserved, copy the raw EFI memory map that was just
 * acquired into it. This runs between acquiring the map and converting it in
 * place to the native format (which destroys the EFI descriptors), and involves
 * no allocation, so on the final acquisition the copy matches the map key used
 * for ExitBootServices().
 */
static void capture_raw_efi_memory_map(void)
{
    if (!uefi_map_capture_buf)
        return;

    BUG_ON(map_raw_bytes > uefi_map_capture_capacity);
    memcpy(uefi_map_capture_buf, memory_map_buf, map_raw_bytes);
    uefi_map_captured_bytes = map_raw_bytes;
}

static void fill_internal_memory_map_buffer(void)
{
    acquire_efi_memory_map();
    efi_memory_map_fixup();
}

bool services_setup_uefi_handoff(struct uefi_handoff_info *out)
{
    SERVICE_FUNCTION();

    fill_internal_memory_map_buffer();

    /*
     * Reserve the buffer for the raw map capture done right before
     * ExitBootServices(). The map can still grow between now and then as
     * allocations (including this one) perturb it, so keep a page of slack
     * over the current size to absorb that.
     */
    page_buf_ensure_capacity(&uefi_map_capture_buf,
                             &uefi_map_capture_capacity,
                             map_raw_bytes + PAGE_SIZE);

    out->system_table = (u64)(ptr_t)g_st;
    out->descriptor_size = map_efi_desc_size;
    out->descriptor_version = map_efi_desc_version;
    out->memory_map_capacity = uefi_map_capture_capacity;

    // A UEFI application runs at the firmware's native width, so ours is it.
    out->firmware_width = sizeof(void*) * 8;
    return true;
}

/*
 * Deliberately not a SERVICE_FUNCTION(): the capture is consumed after
 * ExitBootServices(), and this only reads loader-owned memory.
 */
const void *services_get_captured_uefi_map(size_t *out_size)
{
    *out_size = uefi_map_captured_bytes;
    return uefi_map_capture_buf;
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

    /*
     * Hand the kernel the exact map used for ExitBootServices() below: capture
     * the raw EFI descriptors before converting the buffer to the native format
     * in place. Probing calls (buf == NULL) never reach ExitBootServices(), so
     * skip the copy for those. No allocation happens between here and
     * ExitBootServices(), so the map key stays valid.
     */
    acquire_efi_memory_map();
    if (buf)
        capture_raw_efi_memory_map();
    efi_memory_map_fixup();

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

void mm_foreach_entry(mme_foreach_t func, void *user)
{
    SERVICE_FUNCTION();
    size_t i;

    if (!buf_entry_count)
        fill_internal_memory_map_buffer();

    for (i = 0; i < buf_entry_count; i++) {
        if (!func(user, mm_entry_at(i)))
            break;
    }
}
