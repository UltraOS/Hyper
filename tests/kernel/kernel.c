#include "common/log.h"
#include "common/types.h"
#include "test_ctl.h"
#include "fb_tty.h"
#include "ultra_protocol.h"

static const char *me_type_to_str(u64 type)
{
    switch (type) {
    case ULTRA_MEMORY_TYPE_INVALID:
        return "invalid";
    case ULTRA_MEMORY_TYPE_FREE:
        return "free";
    case ULTRA_MEMORY_TYPE_RESERVED:
        return "reserved";
    case ULTRA_MEMORY_TYPE_RECLAIMABLE:
        return "reclaim";
    case ULTRA_MEMORY_TYPE_NVS:
        return "nvs";
    case ULTRA_MEMORY_TYPE_LOADER_RECLAIMABLE:
        return "loader-reclaim";
    case ULTRA_MEMORY_TYPE_MODULE:
        return "module";
    case ULTRA_MEMORY_TYPE_KERNEL_STACK:
        return "kernel-stack";
    case ULTRA_MEMORY_TYPE_KERNEL_BINARY:
        return "kernel-binary";
    default:
        return "<bug>";
    }
}

static void dump_memory_map(struct ultra_memory_map_attribute *mm)
{
    size_t entries = ULTRA_MEMORY_MAP_ENTRY_COUNT(mm->header);

    print("================ MEMORY MAP DUMP ================\n");
    for (size_t i = 0; i < entries; ++i) {
        struct ultra_memory_map_entry *me = &mm->entries[i];

        print("MM[%zu] 0x%016llX -> 0x%016llX (%s)\n",
              i, me->physical_address, me->physical_address + me->size,
              me_type_to_str(me->type));
    }
    print("==================================================\n");
}

static void validate_memory_map(struct ultra_memory_map_attribute *mm)
{
    size_t i, entries = ULTRA_MEMORY_MAP_ENTRY_COUNT(mm->header);
    uint64_t max_addr = 0;
    struct ultra_memory_map_entry *kmme = NULL;
    struct ultra_memory_map_entry *smme = NULL;

    // NOTE: 4 <-> 128 is an arbitrary range
    if (entries < 4 || entries > 128)
        test_fail("invalid number of MM entries %zu\n", entries);

    dump_memory_map(mm);

    for (i = 0; i < entries; ++i) {
        struct ultra_memory_map_entry *this, *next;
        uint64_t this_end;

        this = &mm->entries[i];
        next = (i + 1) < entries ? &mm->entries[i + 1] : NULL;
        this_end = this->physical_address + this->size;

        // NOTE: 64 GiB is an arbitrary number
        if (!this->size || (this->size > (64ull * 1024 * 1024 * 1024))) {
            test_fail("invalid entry size 0x%016llX - %llu\n",
                      this->physical_address, this->size);
        }

        if (max_addr && this->physical_address < max_addr)
            test_fail("unsorted memory map\n");
        max_addr = this->physical_address;

        if (next && this_end > next->physical_address) {
            uint64_t next_end = next->physical_address + next->size;

            test_fail("overlapping memory map entries 0x%016llX->0x%016llX => 0x%016llX->0x%016llX\n",
                      this->physical_address, this_end, next->physical_address, next_end);
        }

        switch (this->type) {
        case ULTRA_MEMORY_TYPE_FREE:
        case ULTRA_MEMORY_TYPE_RESERVED:
        case ULTRA_MEMORY_TYPE_RECLAIMABLE:
        case ULTRA_MEMORY_TYPE_NVS:
        case ULTRA_MEMORY_TYPE_LOADER_RECLAIMABLE:
        case ULTRA_MEMORY_TYPE_MODULE:
            break;
        case ULTRA_MEMORY_TYPE_KERNEL_STACK:
            if (smme)
                test_fail_on_non_unique("kernel stack memory ranges");

            smme = this;
            break;
        case ULTRA_MEMORY_TYPE_KERNEL_BINARY:
            if (kmme)
                test_fail_on_non_unique("kernel binary memory ranges");

            kmme = this;
            break;
        default:
            test_fail("invalid memory map entry type 0x%016llX\n", this->type);
        }
    }

    if (!smme)
        test_fail("no kernel stack memory range\n");
    if (!kmme)
        test_fail("no kernel binary memory range\n");
}

static const char *platform_to_string(uint32_t type)
{
    switch (type) {
    case ULTRA_PLATFORM_BIOS:
        return "BIOS";
    case ULTRA_PLATFORM_UEFI:
        return "UEFI";
    default:
        test_fail("invalid loader platform type %u\n", type);
    }
}

static void attribute_array_verify(struct ultra_boot_context *bctx)
{
    struct ultra_platform_info_attribute *pi = NULL;
    struct ultra_kernel_info_attribute *ki = NULL;
    struct ultra_command_line_attribute *cl = NULL;
    struct ultra_framebuffer_attribute *fb = NULL;
    struct ultra_module_info_attribute *modules_begin = NULL;
    size_t module_count = 0;
    bool modules_eof = false;
    bool seen_mm = false;

    void *cursor = bctx->attributes;
    for (size_t i = 0; i < bctx->attribute_count; ++i) {
        struct ultra_attribute_header *hdr = cursor;

        if (modules_begin) {
            if (hdr->type != ULTRA_ATTRIBUTE_MODULE_INFO) {
                modules_eof = true;
            } else {
                if (modules_eof)
                    test_fail("sparse module attributes, expected contiguous stream\n");
            }
        }

        switch (hdr->type) {
        case ULTRA_ATTRIBUTE_PLATFORM_INFO:
            if (i != 0)
                test_fail("expected platform info as the first attribute, got %zu\n", i + 1);

            if (pi)
                test_fail_on_non_unique("platform info attributes");

            pi = cursor;
            break;

        case ULTRA_ATTRIBUTE_KERNEL_INFO:
            if (i != 1)
                test_fail("expected kernel info as the second attribute, got %zu\n", i + 1);

            if (ki)
                test_fail_on_non_unique("kernel info attributes");

            ki = cursor;
            break;

        case ULTRA_ATTRIBUTE_MEMORY_MAP:
            if (seen_mm)
                test_fail_on_non_unique("memory map attributes");

            validate_memory_map(cursor);
            seen_mm = true;
            break;

        case ULTRA_ATTRIBUTE_COMMAND_LINE:
            if (cl)
                test_fail("encountered multiple command line attributes(?)\n");

            cl = cursor;
            break;

        case ULTRA_ATTRIBUTE_FRAMEBUFFER_INFO:
            if (fb)
                test_fail_on_non_unique("framebuffer attributes");

            fb = cursor;
            break;

        case ULTRA_ATTRIBUTE_MODULE_INFO:
            if (!modules_begin)
                modules_begin = cursor;
            module_count++;

            break;

        default:
            test_fail("invalid attribute type %u\n", hdr->type);
        }

        cursor += hdr->size;
    }

    if (!pi)
        test_fail_on_no_mandatory("platform info attribute");

    print("Loader info: %s (version %d.%d) on %s\n", pi->loader_name, pi->loader_major,
          pi->loader_minor, platform_to_string(pi->platform_type));

    if (!ki)
        test_fail_on_no_mandatory("kernel info attribute");
    if (!seen_mm)
        test_fail_on_no_mandatory("memory map attribute");
}

int main(struct ultra_boot_context *bctx, uint32_t magic)
{
    print("============== BEGINNING OF KERNEL LOG =============\n");

    if (magic != ULTRA_MAGIC)
        test_fail("invalid magic %u\n", magic);

    test_ctl_init(bctx);

    if (bctx->protocol_major < 1)
        test_fail("invalid protocol version %d.%d\n",
                  bctx->protocol_major, bctx->protocol_minor);

    // At least a platform_info, kernel_info, memory_map
    // NOTE: 256 is an arbitrary number
    if (bctx->attribute_count < 3 || bctx->attribute_count > 256)
        test_fail("invalid attribute count %u\n", bctx->attribute_count);

    fb_tty_init(bctx);
    attribute_array_verify(bctx);

    test_pass();
}

