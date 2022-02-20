#include "structures.h"

#include "common/log.h"
#include "common/format.h"
#include "services.h"
#include "uefi_video_services.h"
#include "uefi_disk_services.h"
#include "uefi_memory_serivces.h"
#include "uefi_find_rsdp.h"
#include "uefi_helpers.h"

EFI_SYSTEM_TABLE *g_st = NULL;
EFI_HANDLE g_img = NULL;

bool uefi_exit_all_services(struct services *sv, size_t map_key)
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

    loader_entry(&s);
    return EFI_SUCCESS;
}
