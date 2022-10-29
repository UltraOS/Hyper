#include "common/log.h"
#include "services.h"
#include "uefi_structures.h"
#include "uefi_memory_services.h"
#include "uefi_video_services.h"
#include "uefi_disk_services.h"

EFI_SYSTEM_TABLE *g_st = NULL;
EFI_HANDLE g_img = NULL;

enum service_provider services_get_provider(void)
{
    return SERVICE_PROVIDER_UEFI;
}

void loader_abort(void)
{
    UINTN idx;
    EFI_INPUT_KEY key;
    EFI_STATUS sts;

    // Empty any pending keystrokes
    for (;;) {
        sts = g_st->ConIn->ReadKeyStroke(g_st->ConIn, &key);
        if (sts != EFI_SUCCESS)
            break;
    }

    if (sts == EFI_UNSUPPORTED) {
        print_err("Loading aborted! Exiting in 10 seconds...\n");
        g_st->BootServices->Stall(10 * 1000 * 1000);
    } else {
        print_err("Loading aborted! Press any key to continue...\n");
        g_st->BootServices->WaitForEvent(1, &g_st->ConIn->WaitForKey, &idx);
    }

    g_st->BootServices->Exit(g_img, EFI_ABORTED, 0, NULL);
    __builtin_unreachable();
}

EFI_STATUS EFIAPI EfiMain(
    IN EFI_HANDLE ImageHandle,
    IN EFI_SYSTEM_TABLE *SystemTable
)
{
    g_img = ImageHandle;
    g_st = SystemTable;

    uefi_memory_services_init();
    uefi_video_services_init();
    uefi_disk_services_init();

    loader_entry();
}
