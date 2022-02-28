#include "structures.h"

#include "common/log.h"
#include "common/format.h"
#include "common/constants.h"
#include "services.h"
#include "uefi_video_services.h"
#include "uefi_disk_services.h"
#include "uefi_memory_serivces.h"
#include "uefi_find_rsdp.h"
#include "uefi_helpers.h"

extern char gdt_ptr[];
extern char gdt_struct[];
#define GDT_STRUCT_SIZE (4 * 8) // 4 descriptors each one is 8 bytes

EFI_SYSTEM_TABLE *g_st = NULL;
EFI_HANDLE g_img = NULL;

static void set_gdt_address(u64 value)
{
    // Skip the number of entries and set the address
    u64 *address = (u64*)(gdt_ptr + 2);
    *address = value;
}

static bool uefi_exit_all_services(struct services *sv, size_t map_key)
{
    EFI_STATUS ret = g_st->BootServices->ExitBootServices(g_img, map_key);
    if (unlikely_efi_error(ret)) {
        struct string_view err_msg = uefi_status_to_string(ret);
        print_warn("ExitBootServices() failed: %pSV\n", &err_msg);
        return false;
    }

    *sv = (struct services) {};
    return true;
}

EFI_STATUS EFIAPI EfiMain(
    IN EFI_HANDLE ImageHandle,
    IN EFI_SYSTEM_TABLE *SystemTable
)
{
    struct services s = {
        .provider = SERVICE_PROVIDER_UEFI,
        .get_rsdp = uefi_find_rsdp,
        .exit_all_services = uefi_exit_all_services,
    };

    g_img = ImageHandle;
    g_st = SystemTable;

    s.vs = video_services_init();
    s.ds = disk_services_init();
    s.ms = memory_services_init();

    if ((ptr_t)gdt_struct > (4ull * GB)) {
        EFI_PHYSICAL_ADDRESS paddr = 4ull * GB;
        EFI_STATUS ret;
        print_info("UEFI: GDT ended up too high in memory, relocating under 4GB\n");

        ret = g_st->BootServices->AllocatePages(AllocateMaxAddress, EfiLoaderData, 1, &paddr);
        BUG_ON(EFI_ERROR(ret));

        memcpy((void*)paddr, gdt_struct, GDT_STRUCT_SIZE);
        set_gdt_address(paddr);
    }

    loader_entry(&s);
    return EFI_SUCCESS;
}
