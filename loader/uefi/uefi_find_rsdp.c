#define MSG_FMT(msg) "UEFI-RSDP: " msg

#include "common/log.h"
#include "services.h"
#include "uefi/helpers.h"

#define EFI_ACPI_20_TABLE_GUID \
    { 0x8868E871, 0xE4F1, 0x11D3, { 0xBC, 0x22, 0x00, 0x80, 0xC7, 0x3C, 0x88, 0x81 } }

#define EFI_ACPI_10_TABLE_GUID \
    { 0xEB9D2D30, 0x2D88, 0x11D3, { 0x9A, 0x16, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D } }

ptr_t services_find_rsdp(void)
{
    EFI_GUID table10_guid = EFI_ACPI_10_TABLE_GUID;
    EFI_GUID table20_guid = EFI_ACPI_20_TABLE_GUID;

    ptr_t table_addr;
    int table_version = 2;

    table_addr = (ptr_t)uefi_find_configuration(&table20_guid);
    if (table_addr)
        goto out;

    table_addr = (ptr_t)uefi_find_configuration(&table10_guid);
    table_version = 1;
    if (table_addr)
        goto out;

    print_warn("couldn't find RSDP, ACPI is unsupported by host(?)\n");
    return table_addr;

out:
    print_info("table v%d @0x%016llX\n", table_version, table_addr);
    return table_addr;
}

#define EFI_DTB_TABLE_GUID \
    { 0xB1B621D5, 0xF19C, 0x41A5, { 0x83, 0x0B, 0xD9, 0x15, 0x2C, 0x69, 0xAA, 0xE0 } }

ptr_t services_find_dtb(void)
{
    EFI_GUID dtb_guid = EFI_DTB_TABLE_GUID;
    ptr_t dtb_addr;

    dtb_addr = (ptr_t)uefi_find_configuration(&dtb_guid);
    if (dtb_addr)
        print_info("device tree blob @0x%016llX\n", dtb_addr);

    return dtb_addr;
}
