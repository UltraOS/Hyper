#define MSG_FMT(msg) "BIOS-TBL: " msg

#include "common/constants.h"
#include "common/string.h"
#include "common/log.h"
#include "services.h"
#include "bios_call.h"

#define RSDP_SIGNATURE "RSD PTR "
#define RSDP_SIGNATURE_LEN 8
#define RSDP_ALIGNMENT 16

/*
 * ACPI 6.4 (5.2.5.1 Finding the RSDP on IA-PC Systems)
 * ----------------------------------------------------------------------------------------------------
 * OSPM finds the Root System Description Pointer (RSDP) structure by searching physical memory ranges
 * on 16-byte boundaries for a valid Root System Description Pointer structure signature and checksum
 * match as follows:
 * - The first 1 KB of the Extended BIOS Data Area (EBDA). For EISA or MCA systems, the EBDA can
 *   be found in the two-byte location 40:0Eh on the BIOS data area.
 * - The BIOS read-only memory space between 0E0000h and 0FFFFFh.
 * ----------------------------------------------------------------------------------------------------
*/

// contains (ebda_base >> 4), aka segment value
#define BDA_EBDA_POINTER_OFFSET 0x0E

#define EBDA_SEARCH_BASE      0x00400
#define BIOS_AREA_SEARCH_BASE 0xE0000
#define BIOS_AREA_SEARCH_END  0xFFFFF

#define EBDA_SEARCH_SIZE (1 * KB)

static u32 find_signature_in_range(
    const char *signature, size_t length, u32 align, u32 addr, u32 end
)
{
    // Don't attempt to search too low
    if (addr <= EBDA_SEARCH_BASE)
        return 0;

    for (; addr < end; addr += align) {
        if (memcmp((void*)addr, signature, length) != 0)
            continue;

        return addr;
    }

    return 0;
}

ptr_t services_find_rsdp(void)
{
    u32 address, ebda_address;

    ebda_address = bios_read_bda(BDA_EBDA_POINTER_OFFSET, 2);
    ebda_address <<= 4;

    address = find_signature_in_range(
        RSDP_SIGNATURE, RSDP_SIGNATURE_LEN, RSDP_ALIGNMENT,
        ebda_address, ebda_address + EBDA_SEARCH_SIZE
    );
    if (address == 0) {
        address = find_signature_in_range(
            RSDP_SIGNATURE, RSDP_SIGNATURE_LEN, RSDP_ALIGNMENT,
            BIOS_AREA_SEARCH_BASE, BIOS_AREA_SEARCH_END
        );
    }

    if (address)
        print_info("found RSDP at 0x%08X\n", address);

    return address;
}

ptr_t services_find_dtb(void)
{
    return 0;
}

/*
 * On non-UEFI systems, the 32-bit SMBIOS Entry Point structure, can be located
 * by application software by searching for the anchor-string on paragraph
 * (16-byte) boundaries within the physical memory address range 000F0000h to
 * 000FFFFFh.
 */
#define SMBIOS_RANGE_BEGIN 0x000F0000
#define SMBIOS_RANGE_END 0x000FFFFF
#define SMBIOS_ALIGNMENT 16

#define SMBIOS_2_ANCHOR_STRING "_SM_"
#define SMBIOS_2_ANCHOR_STRING_LENGTH 4

#define SMBIOS_3_ANCHOR_STRING "_SM3_"
#define SMBIOS_3_ANCHOR_STRING_LENGTH 5

ptr_t services_find_smbios(void)
{
    u8 bitness = 64;
    u32 address;

    address = find_signature_in_range(
        SMBIOS_3_ANCHOR_STRING, SMBIOS_3_ANCHOR_STRING_LENGTH,
        SMBIOS_ALIGNMENT, SMBIOS_RANGE_BEGIN, SMBIOS_RANGE_END
    );
    if (address == 0) {
        address = find_signature_in_range(
            SMBIOS_2_ANCHOR_STRING, SMBIOS_2_ANCHOR_STRING_LENGTH,
            SMBIOS_ALIGNMENT, SMBIOS_RANGE_BEGIN, SMBIOS_RANGE_END
        );
        bitness = 32;
    }

    if (address) {
        print_info("found (%d-bit) SMBIOS entry at 0x%08X\n",
                   bitness, address);
    }

    return address;
}
