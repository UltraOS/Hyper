#define MSG_FMT(msg) "UEFI-RELOC: " msg

#include <common/log.h>
#include <common/align.h>
#include <common/panic.h>

#include "relocator.h"

#include "uefi_globals.h"
#include "uefi_helpers.h"

static EFI_PHYSICAL_ADDRESS last_allocation;
static EFI_PHYSICAL_ADDRESS last_ceiling;
static size_t last_bytes_rem;

void relocated_cb_write_u32(void *user, EFI_PHYSICAL_ADDRESS new_address)
{
    BUG_ON(new_address > 0xFFFFFFFF);
    *(u32*)user = (u32)new_address;
}

void relocated_cb_write_u64(void *user, EFI_PHYSICAL_ADDRESS new_address)
{
    *(u64*)user = new_address;
}

void relocate_entries(struct relocation_entry *entries)
{
    struct relocation_entry *entry;

    for (entry = entries; entry->begin; ++entry) {
        EFI_PHYSICAL_ADDRESS addr;
        size_t byte_len, pages, page_bytes;
        bool is_relocation;

        is_relocation = entry->end != NULL;
        addr = entry->max_address;

        if (is_relocation) {
            if ((u64)entry->end < entry->max_address) {
                addr = (EFI_PHYSICAL_ADDRESS)entry->begin;
                goto run_cb;
            }

            byte_len = entry->end - entry->begin;
            print_info("relocating an entry at 0x%016llX below 0x%016llX (%zu bytes)\n",
                       (u64)entry->begin, entry->max_address, byte_len);
        } else {
            byte_len = entry->size;
            print_info("allocating %zu bytes below 0x%016llX\n",
                       byte_len, entry->max_address);
        }

        byte_len = ALIGN_UP(byte_len, 8);
        page_bytes = PAGE_ROUND_UP(byte_len);
        pages = page_bytes >> PAGE_SHIFT;

        if (byte_len > last_bytes_rem || last_ceiling > entry->max_address) {
            EFI_STATUS ret;

            ret = g_st->BootServices->AllocatePages(AllocateMaxAddress, EfiLoaderData, pages, &addr);
            if (unlikely_efi_error(ret)) {
                panic("failed to allocate %zu pages below 0x%016llX\n",
                      pages, entry->max_address);
            }

            print_info("allocated %zu pages at 0x%016llX\n", pages, addr);

            last_allocation = addr + byte_len;
            last_ceiling = entry->max_address;
            last_bytes_rem = page_bytes - byte_len;
        } else {
            addr = last_allocation;
            last_bytes_rem -= byte_len;
            last_allocation += byte_len;
        }

        if (is_relocation)
            memcpy((void*)addr, entry->begin, byte_len);

    run_cb:
        if (entry->cb)
            entry->cb(entry->user, addr);
    }
}
