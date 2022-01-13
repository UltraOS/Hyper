#include "bios_find_rsdp.h"
#include "common/constants.h"
#include "common/string.h"
#include "common/log.h"

#define RSDP_SIGNATURE "RSD PTR "
#define RSDP_SIGNATURE_LEN 8
#define RSDP_ALIGNMENT 16

// contains (ebda_base >> 4), aka segment value
#define BDA_EBDA_POINTER 0x040E

#define EXPECTED_EBDA_BASE 0x80000

#define BIOS_AREA_SEARCH_BASE 0xE0000
#define BIOS_AREA_SEARCH_END  0xFFFFF

#define EBDA_SEARCH_SIZE (1 * KB)

static ptr_t find_signature_in_range(ptr_t addr, ptr_t end)
{
    for (; addr < end; addr += RSDP_ALIGNMENT) {
        if (memcmp((void*)addr, RSDP_SIGNATURE, RSDP_SIGNATURE_LEN))
            continue;

        print_info("found RSDP at 0x%08X\n", addr);
        return addr;
    }

    return 0;
}

ptr_t bios_find_rsdp()
{
    ptr_t ebda_address = *(volatile u16*)BDA_EBDA_POINTER;
    ebda_address <<= 4;

    if (ebda_address != EXPECTED_EBDA_BASE)
        print_warn("EBDA address 0x%08X doesn't match expected 0x%08X\n", ebda_address, EXPECTED_EBDA_BASE);

    return find_signature_in_range(ebda_address, ebda_address + EBDA_SEARCH_SIZE) ?:
           find_signature_in_range(BIOS_AREA_SEARCH_BASE, BIOS_AREA_SEARCH_END);
}
