#include "common/log.h"
#include "common/types.h"
#include "common/align.h"
#include "common/range.h"
#include "common/string_ex.h"
#include "common/conversions.h"
#include "test_ctl.h"
#include "fb_tty.h"
#include "ultra_protocol.h"
#include "ultra_helpers.h"

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
    print("==================================================\n\n");
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

    print("memory map OK\n");
}

static void
memory_map_ensure_range_is_of_type(struct ultra_memory_map_attribute *mm,
                                   uint64_t addr, size_t bytes,
                                   uint32_t expected_type)
{
    size_t i, entries = ULTRA_MEMORY_MAP_ENTRY_COUNT(mm->header);
    struct ultra_memory_map_entry *me = mm->entries;

    for (i = 0; i < entries; ++i, ++me) {
        size_t len_after_cutoff;

        /*
         * We know for sure that the first entry that we find where range end
         * is greater than what we're looking for is the correct one since the
         * map is sorted in ascending order and doesn't contain overlapping
         * entries.
         */
        if ((me->physical_address + me->size) <= addr)
            continue;

        if (me->type != expected_type) {
            test_fail("memory range 0x%016llX->0x%016llX has an unexpected type '%s' (expected '%s')\n",
                      me->physical_address, me->physical_address + me->size,
                      me_type_to_str(me->type), me_type_to_str(expected_type));
        }

        len_after_cutoff = me->size - (addr - me->physical_address);
        if (len_after_cutoff < bytes) {
            test_fail("memory range 0x%016llX->0x%016llX is not long enough to fit 0x%016llX->0x%016llX\n",
                      me->physical_address, me->physical_address + me->size,
                      addr, addr + bytes);
        }

        return;
    }

    test_fail("couldn't find a memory range that fits 0x%016llX->0x%016llX\n",
              addr, addr + bytes);
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

static const char *module_type_to_string(uint32_t type)
{
    switch (type) {
    case ULTRA_MODULE_TYPE_FILE:
        return "file";
    case ULTRA_MODULE_TYPE_MEMORY:
        return "memory";
    default:
        test_fail("invalid module type %u\n", type);
    }
}

static void dump_modules(struct ultra_module_info_attribute *mi, size_t module_count)
{
    size_t i;

    print("\n=================== MODULE DUMP ==================\n");

    for (i = 0; i < module_count; ++i, ++mi) {
        print("MODULE[%zu] \"%s\" (%s) @ 0x%016llX %zu bytes\n",
              i, mi->name, module_type_to_string(mi->type), mi->address,
              mi->size);
    }

    print("==================================================\n\n");
}

static void validate_fill(void *data, size_t offset, size_t total_size,
                          uint8_t fill)
{
    size_t i;
    uint8_t *bd = data;

    for (i = offset; i < total_size; ++i) {
        if (bd[i] == fill)
            continue;

        test_fail("module is not properly 0x%02X-filled: found 0x%02X at offset %zu\n",
                   fill, bd[i], i);
    }
}

#define MAX_MODULES 64

static ssize_t find_containing_range(struct range *ranges, size_t count,
                                     u64 address)
{
    size_t i;

    for (i = 0; i < count; ++i) {
        struct range *r = &ranges[i];

        if (r->begin <= address && address < r->end)
            return i;
    }

    return -1;
}

static void validate_modules(struct ultra_module_info_attribute *mi,
                             size_t module_count,
                             struct ultra_memory_map_attribute *mm,
                             struct ultra_platform_info_attribute *pi)
{
    static struct range seen_ranges[MAX_MODULES];
    size_t i;

    if (module_count == 0)
        return;
    if (module_count > MAX_MODULES)
        test_fail("too many modules: %zu\n", module_count);

    dump_modules(mi, module_count);

    for (i = 0; i < module_count; ++i, ++mi) {
        size_t j, aligned_len = PAGE_ROUND_UP(mi->size);
        bool check_fill = false;
        uint8_t expect_fill;
        struct range *r = &seen_ranges[i];

        r->begin = mi->address;
        if (r->begin >= pi->higher_half_base)
            r->begin -= pi->higher_half_base;
        if (!r->begin)
            test_fail("module %zu address is NULL\n", i);
        if (!mi->size)
            test_fail("module %zu is empty\n", i);

        if (!IS_ALIGNED(r->begin, PAGE_SIZE)) {
            test_fail("module %zu address is not properly aligned - 0x%016llX\n",
                      i, r->begin);
        }

        memory_map_ensure_range_is_of_type(mm, r->begin, aligned_len,
                                           ULTRA_MEMORY_TYPE_MODULE);

        r->end = r->begin + mi->size;
        if (find_containing_range(seen_ranges, i, r->begin) != -1) {
            test_fail("module %zu has a non-unique address 0x%016llX\n",
                      i, r->begin);
        }

        if (mi->type == ULTRA_MODULE_TYPE_MEMORY) {
            check_fill = true;
            expect_fill = 0;
        } else {
            /*
             * Module names like 'cc-fill', the entire module memory is filled
             * with 0xCC if the loader read it in correctly. We verify that here.
             */
            if (strlen(mi->name) == 7 && strcmp(mi->name + 2, "-fill") == 0) {
                check_fill = str_to_u8_with_base((struct string_view) { mi->name, 2 },
                                                 &expect_fill, 16);
                if (!check_fill)
                    test_fail("invalid fill string: %s\n", mi->name);
            }
        }

        if (check_fill) {
            validate_fill(ADDR_TO_PTR(mi->address), 0, mi->size, expect_fill);
            print("module %zu - 0x%02X fill OK (%zu bytes)\n", i, expect_fill, mi->size);
        }

        if (aligned_len != mi->size) {
            size_t fill_len = aligned_len  - mi->size;
            validate_fill(ADDR_TO_PTR(mi->address), mi->size, aligned_len, 0);
            print("module %zu - padding zero fill OK (%zu bytes)\n", i, fill_len);
        }
    }

    print("modules OK\n");
}

static void validate_platform_info(struct ultra_platform_info_attribute *pi,
                                   struct ultra_kernel_info_attribute *ki)
{
    switch (pi->higher_half_base) {
    case AMD64_DIRECT_MAP_BASE:
        if (pi->page_table_depth != 4)
            goto invalid_pt_depth;
        if (sizeof(void*) != 8)
            goto invalid_hh_base;
        break;
    case AMD64_LA57_DIRECT_MAP_BASE:
        if (pi->page_table_depth != 5)
            goto invalid_pt_depth;
        if (sizeof(void*) != 8)
            goto invalid_hh_base;
        break;
    case I686_DIRECT_MAP_BASE:
        if (pi->page_table_depth != 2 &&
            pi->page_table_depth != 3)
            goto invalid_pt_depth;
        if (sizeof(void*) != 4)
            goto invalid_hh_base;
        break;
    case AARCH64_48BIT_DIRECT_MAP_BASE:
        if (pi->page_table_depth != 4)
            goto invalid_pt_depth;
        break;
    case AARCH64_52BIT_DIRECT_MAP_BASE:
        if (pi->page_table_depth != 5)
            goto invalid_pt_depth;
        break;
    default:
        goto invalid_hh_base;
    }

    if (ki->virtual_base < pi->higher_half_base &&
        ki->virtual_base != ki->physical_base) {
        test_fail("kernel virtual base 0x%016llX is below hh base 0x%016llX\n",
                  ki->virtual_base, pi->higher_half_base);
    }
    return;

invalid_pt_depth:
    test_fail("page_table_depth %d is invalid for higher_half_base 0x%016llX\n",
              pi->page_table_depth, pi->higher_half_base);
invalid_hh_base:
    test_fail("higher_half_base 0x%016llX is invalid\n",
              pi->higher_half_base);
}

static void attribute_array_verify(struct ultra_boot_context *bctx)
{
    struct ultra_platform_info_attribute *pi = NULL;
    struct ultra_kernel_info_attribute *ki = NULL;
    struct ultra_command_line_attribute *cl = NULL;
    struct ultra_framebuffer_attribute *fb = NULL;
    struct ultra_memory_map_attribute *mm = NULL;
    struct ultra_module_info_attribute *modules_begin = NULL;
    size_t i, module_count = 0;
    bool modules_eof = false;
    void *cursor = bctx->attributes;

    print("attribute array @ 0x%016llX\n", ((u64)(ptr_t)bctx));

    if (!IS_ALIGNED((ptr_t)bctx, 8))
        test_fail("boot context is misaligned\n");

    for (i = 0; i < bctx->attribute_count; ++i) {
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
            if (mm)
                test_fail_on_non_unique("memory map attributes");

            mm = cursor;
            validate_memory_map(mm);
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
    if (!ki)
        test_fail_on_no_mandatory("kernel info attribute");
    if (!mm)
        test_fail_on_no_mandatory("memory map attribute");

    print("attribute array OK\n");

    validate_platform_info(pi, ki);
    validate_modules(modules_begin, module_count, mm, pi);

    print("\nLoader info: %s (version %d.%d) on %s\n",
          pi->loader_name, pi->loader_major, pi->loader_minor,
          platform_to_string(pi->platform_type));
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

    /*
     * At least a platform_info, kernel_info, memory_map
     * NOTE: 256 is an arbitrary number
     */
    if (bctx->attribute_count < 3 || bctx->attribute_count > 256)
        test_fail("invalid attribute count %u\n", bctx->attribute_count);

    fb_tty_init(bctx);
    attribute_array_verify(bctx);

    test_pass();
}

