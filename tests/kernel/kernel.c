#include "common/log.h"
#include "common/types.h"
#include "common/align.h"
#include "common/range.h"
#include "common/string_ex.h"
#include "common/string_view.h"
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

static void validate_apm_info(struct ultra_apm_attribute *apm)
{
    struct ultra_apm_info *info = &apm->info;

    if (info->version < 0x0100)
        test_fail("bogus APM version 0x%04X\n", info->version);
    if (!(info->flags & 2))
        test_fail("bogus APM flags: %d\n", info->flags);
    if (info->pm_code_segment_length == 0)
        test_fail("bogus pm code segment length %d\n", info->pm_code_segment_length);
    if (info->rm_code_segment_length == 0)
        test_fail("bogus rm code segment length %d\n", info->rm_code_segment_length);
    if (info->data_segment_length == 0)
        test_fail("bogus data segment length %d\n", info->data_segment_length);

    print("APM info OK\n");
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

static const char *partition_type_to_string(uint64_t type)
{
    switch (type) {
    case ULTRA_PARTITION_TYPE_RAW:
        return "raw";
    case ULTRA_PARTITION_TYPE_MBR:
        return "mbr";
    case ULTRA_PARTITION_TYPE_GPT:
        return "gpt";
    case ULTRA_PARTITION_TYPE_PXE_V4:
        return "pxe-v4";
    case ULTRA_PARTITION_TYPE_PXE_V6:
        return "pxe-v6";
    default:
        return "<invalid>";
    }
}

/*
 * Parse a canonical GUID string ("XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX") into
 * an ultra_guid, matching the on-disk (and loader) byte layout so it can be
 * compared verbatim against what the loader reports.
 */
static bool parse_guid(struct string_view s, struct ultra_guid *g)
{
    static const u8 data4_off[8] = { 19, 21, 24, 26, 28, 30, 32, 34 };
    size_t i;

    if (s.size != 36)
        return false;
    if (s.text[8] != '-' || s.text[13] != '-' ||
        s.text[18] != '-' || s.text[23] != '-')
        return false;

    if (!str_to_u32_with_base((struct string_view) { s.text, 8 },
                              &g->data1, 16))
        return false;
    if (!str_to_u16_with_base((struct string_view) { s.text + 9, 4 },
                              &g->data2, 16))
        return false;
    if (!str_to_u16_with_base((struct string_view) { s.text + 14, 4 },
                              &g->data3, 16))
        return false;

    for (i = 0; i < 8; ++i) {
        if (!str_to_u8_with_base(
                (struct string_view) { s.text + data4_off[i], 2 },
                &g->data4[i], 16))
            return false;
    }

    return true;
}

static bool cmdline_next_token(struct string_view *rest, struct string_view *tok)
{
    ssize_t space;

    while (rest->size && rest->text[0] == ' ')
        sv_offset_by(rest, 1);

    if (sv_empty(*rest))
        return false;

    space = sv_find(*rest, SV(" "), 0);
    if (space < 0) {
        *tok = *rest;
        sv_clear(rest);
    } else {
        *tok = (struct string_view) { rest->text, (size_t)space };
        sv_offset_by(rest, space + 1);
    }

    return true;
}

static bool cmdline_split_kv(struct string_view tok, struct string_view *key,
                             struct string_view *val)
{
    ssize_t eq = sv_find(tok, SV("="), 0);

    if (eq < 0)
        return false;

    *key = (struct string_view) { tok.text, (size_t)eq };
    *val = (struct string_view) { tok.text + eq + 1, tok.size - eq - 1 };
    return true;
}

/*
 * The test harness passes the expected origin of the kernel binary through the
 * command line as "key=value" tokens (see test_loader.py). Validate whatever
 * the loader reported in the kernel info attribute against them; unknown tokens
 * are ignored so ordinary command lines still work.
 */
static void validate_ki_expectations(struct ultra_kernel_info_attribute *ki,
                                     struct ultra_command_line_attribute *cl)
{
    struct string_view rest, tok, key, val;
    bool checked = false;

    if (!cl)
        return;

    rest = (struct string_view) { cl->text, strlen(cl->text) };

    while (cmdline_next_token(&rest, &tok)) {
        if (!cmdline_split_kv(tok, &key, &val))
            continue;

        if (sv_equals(key, SV("part-type"))) {
            const char *got = partition_type_to_string(ki->partition_type);

            if (!sv_equals(val, SV(got)))
                test_fail("partition type mismatch: got '%s', expected '%pSV'\n",
                          got, &val);
            checked = true;
        } else if (sv_equals(key, SV("disk-index"))) {
            u32 want;

            if (!str_to_u32_with_base(val, &want, 16))
                test_fail("bad disk-index value '%pSV'\n", &val);
            if (ki->disk_index != want)
                test_fail("disk index mismatch: got %u, expected %u\n",
                          ki->disk_index, want);
            checked = true;
        } else if (sv_equals(key, SV("part-index"))) {
            u32 want;

            if (!str_to_u32_with_base(val, &want, 16))
                test_fail("bad part-index value '%pSV'\n", &val);
            if (ki->partition_index != want)
                test_fail("partition index mismatch: got %u, expected %u\n",
                          ki->partition_index, want);
            checked = true;
        } else if (sv_equals(key, SV("disk-guid"))) {
            struct ultra_guid want;

            if (!parse_guid(val, &want))
                test_fail("bad disk-guid value '%pSV'\n", &val);
            if (memcmp(&ki->disk_guid, &want, sizeof(want)))
                test_fail("disk GUID mismatch (expected '%pSV')\n", &val);
            checked = true;
        } else if (sv_equals(key, SV("part-guid"))) {
            struct ultra_guid want;

            if (!parse_guid(val, &want))
                test_fail("bad part-guid value '%pSV'\n", &val);
            if (memcmp(&ki->partition_guid, &want, sizeof(want)))
                test_fail("partition GUID mismatch (expected '%pSV')\n", &val);
            checked = true;
        } else if (sv_equals(key, SV("cmdline-check"))) {
            /*
             * Stress the loader's dynamically sized command line: the value is
             * the expected total length, and everything past the first space is
             * a deterministic 'A'..'Z' filler. Validate both so truncation and
             * corruption anywhere in a huge command line are caught.
             */
            struct string_view full = { cl->text, strlen(cl->text) };
            ssize_t sp = sv_find(full, SV(" "), 0);
            const char *filler;
            u32 want;
            size_t j;

            if (!str_to_u32_with_base(val, &want, 10))
                test_fail("bad cmdline-check value '%pSV'\n", &val);
            if (full.size != want)
                test_fail("command line length mismatch: got %zu, "
                          "expected %u\n", full.size, want);
            if (sp < 0)
                test_fail("malformed cmdline-check command line\n");

            filler = cl->text + sp + 1;
            for (j = 0; filler[j]; ++j) {
                char expect = 'A' + (j % 26);

                if (filler[j] != expect)
                    test_fail("command line corrupted at filler offset "
                              "%zu\n", j);
            }
            checked = true;
        }
    }

    if (checked)
        print("kernel info expectations OK\n");
}

static void attribute_array_verify(struct ultra_boot_context *bctx)
{
    struct ultra_platform_info_attribute *pi = NULL;
    struct ultra_kernel_info_attribute *ki = NULL;
    struct ultra_command_line_attribute *cl = NULL;
    struct ultra_framebuffer_attribute *fb = NULL;
    struct ultra_memory_map_attribute *mm = NULL;
    struct ultra_apm_attribute *apm_info = NULL;
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

        case ULTRA_ATTRIBUTE_APM_INFO:
            apm_info = cursor;
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
    validate_ki_expectations(ki, cl);
    validate_modules(modules_begin, module_count, mm, pi);

    if (apm_info)
        validate_apm_info(apm_info);

    print("\nLoader info: %s (version %d.%d) on %s\n",
          pi->loader_name, pi->loader_major, pi->loader_minor,
          platform_to_string(pi->platform_type));
}

int main(struct ultra_boot_context *bctx, uint32_t magic)
{
    test_ctl_init(bctx);
    print("============== BEGINNING OF KERNEL LOG =============\n");

    if (magic != ULTRA_MAGIC)
        test_fail("invalid magic %u\n", magic);

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

