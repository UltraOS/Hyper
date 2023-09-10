#define MSG_FMT(msg) "UEFI-RSDP: " msg

#include "common/string.h"
#include "common/log.h"
#include "services.h"
#include "uefi/globals.h"

#define EFI_ACPI_20_TABLE_GUID \
    { 0x8868E871, 0xE4F1, 0x11D3, { 0xBC, 0x22, 0x00, 0x80, 0xC7, 0x3C, 0x88, 0x81 } }

#define EFI_ACPI_10_TABLE_GUID \
    { 0xEB9D2D30, 0x2D88, 0x11D3, { 0x9A, 0x16, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D } }

ptr_t services_find_rsdp(void)
{
    EFI_GUID table10_guid = EFI_ACPI_10_TABLE_GUID;
    EFI_GUID table20_guid = EFI_ACPI_20_TABLE_GUID;

    VOID *table10_ptr = NULL, *table20_ptr = NULL;
    UINTN i;

    for (i = 0; i < g_st->NumberOfTableEntries; ++i) {
        if (memcmp(&table10_guid, &g_st->ConfigurationTable[i].VendorGuid, sizeof(EFI_GUID)) == 0) {
            table10_ptr = g_st->ConfigurationTable[i].VendorTable;
            continue;
        }

        if (memcmp(&table20_guid, &g_st->ConfigurationTable[i].VendorGuid, sizeof(EFI_GUID)) == 0) {
            table20_ptr = g_st->ConfigurationTable[i].VendorTable;
            break;
        }
    }

    print_info("table 1.0 at %p, table 2.0 at %p\n", table10_ptr, table20_ptr);
    return table20_ptr ? (ptr_t)table20_ptr : (ptr_t)table10_ptr;
}