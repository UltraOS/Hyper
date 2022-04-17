#define MSG_FMT(msg) "ULTRA-PROT: " msg

#include "common/bug.h"
#include "common/cpuid.h"
#include "common/helpers.h"
#include "common/constants.h"
#include "common/format.h"
#include "common/log.h"
#include "common/minmax.h"
#include "common/dynamic_buffer.h"

#include "ultra.h"
#include "ultra_protocol/ultra_protocol.h"
#include "elf/elf.h"
#include "filesystem/filesystem_table.h"
#include "allocator.h"
#include "virtual_memory.h"
#include "handover.h"
#include "hyper.h"
#include "services.h"
#include "video_services.h"

struct binary_options {
    struct full_path path;
    bool allocate_anywhere;
};

static void get_binary_options(struct config *cfg, struct loadable_entry *le, struct binary_options *opts)
{
    struct value binary_val;
    struct string_view string_path;

    CFG_MANDATORY_GET_ONE_OF(VALUE_STRING | VALUE_OBJECT, cfg, le, SV("binary"), &binary_val);

    if (value_is_object(&binary_val)) {
        CFG_MANDATORY_GET(string, cfg, &binary_val, SV("path"), &string_path);
        cfg_get_bool(cfg, &binary_val, SV("allocate-anywhere"), &opts->allocate_anywhere);
    } else {
        string_path = binary_val.as_string;
    }

    if (!parse_path(string_path, &opts->path))
        oops("invalid binary path %pSV\n", &string_path);
}

static uint32_t module_get_size(struct config *cfg, struct value *module_value)
{
    const uint32_t type_mask = VALUE_STRING | VALUE_UNSIGNED | VALUE_NONE;
    struct value size_value;

    if (!cfg_get_one_of(cfg, module_value, SV("size"), type_mask, &size_value) ||
        value_is_null(&size_value))
        return 0;

    if (value_is_string(&size_value)) {
        if (!sv_equals(size_value.as_string, SV("auto")))
            oops("invalid value for module/size \"%pSV\"\n", &size_value.as_string);
        return 0;
    }

    return size_value.as_unsigned;
}

static uint32_t module_get_type(struct config *cfg, struct value *module_value)
{
    const uint32_t type_mask = VALUE_STRING | VALUE_NONE;
    struct value type_value;

    if (!cfg_get_one_of(cfg, module_value, SV("type"), type_mask, &type_value) ||
        value_is_null(&type_value) || sv_equals(type_value.as_string, SV("file")))
        return ULTRA_MODULE_TYPE_FILE;

    if (sv_equals(type_value.as_string, SV("memory")))
        return ULTRA_MODULE_TYPE_MEMORY;

    oops("invalid value for module/type \"%pSV\"\n", &type_value.as_string);
}

static u64 module_get_load_address(struct config *cfg, struct value *module_value)
{
    const uint32_t type_mask = VALUE_STRING | VALUE_UNSIGNED | VALUE_NONE;
    struct value load_at_value;

    if (!cfg_get_one_of(cfg, module_value, SV("load-at"), type_mask, &load_at_value) ||
        value_is_null(&load_at_value))
        return 0;

    if (value_is_string(&load_at_value)) {
        if (!sv_equals(load_at_value.as_string, SV("auto")))
            oops("invalid value for module/load-at \"%pSV\"\n", &load_at_value.as_string);
        return 0;
    }

    return load_at_value.as_unsigned;
}

static void module_load(struct config *cfg, struct value *module_value, struct ultra_module_info_attribute *attrs)
{
    bool has_path;
    struct string_view str_path, module_name = { 0 };
    size_t module_pages, module_size = 0;
    uint32_t module_type = ULTRA_MODULE_TYPE_FILE;
    u64 load_address = 0;
    void *module_data;

    static int module_idx = 0;
    ++module_idx;

    if (value_is_object(module_value)) {
        cfg_get_string(cfg, module_value, SV("name"), &module_name);
        has_path = cfg_get_string(cfg, module_value, SV("path"), &str_path);
        module_size = module_get_size(cfg, module_value);
        module_type = module_get_type(cfg, module_value);
        load_address = module_get_load_address(cfg, module_value);
    } else {
        str_path = module_value->as_string;
        has_path = true;
    }

    if (sv_empty(module_name)) {
        snprintf(attrs->name, sizeof(attrs->name), "unnamed_module%d", module_idx);
    } else {
        if (module_name.size >= sizeof(attrs->name))
            oops("module name \"%pSV\" is too long (%zu vs max %zu)\n",
                 &module_name, module_name.size, sizeof(attrs->name) - 1);

        memcpy(attrs->name, module_name.text, module_name.size);
        attrs->name[module_name.size] = '\0';
    }

    print_info("loading module \"%s\"...\n", attrs->name);

    if (module_type == ULTRA_MODULE_TYPE_FILE) {
        struct full_path path;
        struct file *module_file;
        const struct fs_entry *fse;
        size_t bytes_to_read;

        if (!has_path)
            cfg_oops_no_mandatory_key(SV("path"));

        if (!parse_path(str_path, &path))
            oops("invalid module path %pSV\n", &str_path);

        fse = fs_by_full_path(&path);
        if (!fse)
            oops("no such disk/partition %pSV\n", &str_path);

        module_file = fse->fs->open(fse->fs, path.path_within_partition);
        if (!module_file)
            oops("no such file %pSV\n", &path.path_within_partition);

        bytes_to_read = module_file->size;

        if (!module_size) {
            module_size = bytes_to_read;
        } else if (module_size < bytes_to_read) {
            bytes_to_read = module_size;
        }

        module_pages = CEILING_DIVIDE(module_size, PAGE_SIZE);

        // TODO: check this doesn't go above 4GB
        if (load_address)
            module_data = allocate_critical_pages_with_type_at(load_address, module_pages, ULTRA_MEMORY_TYPE_MODULE);
        else
            module_data = allocate_critical_pages_with_type(module_pages, ULTRA_MEMORY_TYPE_MODULE);

        if (!module_file->read(module_file, module_data, 0, bytes_to_read))
            oops("failed to read module file\n");

        memzero(module_data + bytes_to_read, (module_pages * PAGE_SIZE) - bytes_to_read);
        fse->fs->close(module_file);
    } else { // module_type == ULTRA_MODULE_TYPE_MEMORY
        if (!module_size)
            oops("module size cannot be 0 for type \"memory\"\n");

        module_pages = CEILING_DIVIDE(module_size, PAGE_SIZE);

        // TODO: check this doesn't go above 4GB
        if (load_address)
            module_data = allocate_critical_pages_with_type_at(load_address, module_pages, ULTRA_MEMORY_TYPE_MODULE);
        else
            module_data = allocate_critical_pages_with_type(module_pages, ULTRA_MEMORY_TYPE_MODULE);

        memzero(module_data, module_pages * PAGE_SIZE);
    }

    attrs->header = (struct ultra_attribute_header) {
        .type = ULTRA_ATTRIBUTE_MODULE_INFO,
        .size = sizeof(struct ultra_module_info_attribute)
    };

    attrs->address = (ptr_t)module_data;
    attrs->type = module_type;
    attrs->size = module_size;
}

struct kernel_info {
    struct binary_options bin_opts;
    struct binary_info bin_info;
    void *elf_blob;
    size_t blob_size;
};

void load_kernel(struct config *cfg, struct loadable_entry *entry, struct kernel_info *info)
{
    const struct fs_entry *fse;
    struct file *f;
    u8 bitness;
    struct load_result res = { 0 };

    get_binary_options(cfg, entry, &info->bin_opts);
    fse = fs_by_full_path(&info->bin_opts.path);

    f = fse->fs->open(fse->fs, info->bin_opts.path.path_within_partition);
    if (!f)
        oops("failed to open %pSV\n", &info->bin_opts.path.path_within_partition);

    info->blob_size = f->size;
    info->elf_blob = allocate_critical_bytes(info->blob_size);

    if (!f->read(f, info->elf_blob, 0, info->blob_size))
        oops("failed to read file\n");

    bitness = elf_bitness(info->elf_blob, info->blob_size);

    if (!bitness || (bitness != 32 && bitness != 64))
        oops("invalid ELF bitness\n");

    if (info->bin_opts.allocate_anywhere && bitness != 64)
        oops("allocate-anywhere is only allowed for 64 bit kernels\n");

    if (bitness == 64 && !cpu_supports_long_mode())
        oops("attempted to load a 64 bit kernel on a CPU without long mode support\n");

    if (!elf_load(info->elf_blob, info->blob_size, bitness == 64, info->bin_opts.allocate_anywhere,
                  ULTRA_MEMORY_TYPE_KERNEL_BINARY, &res))
        oops("failed to load kernel binary: %s\n", res.error_msg);

    fse->fs->close(f);
    info->bin_info = res.info;
}

enum video_mode_constraint {
    VIDEO_MODE_CONSTRAINT_EXACTLY,
    VIDEO_MODE_CONSTRAINT_AT_LEAST,
};

struct requested_video_mode {
    u32 width, height, bpp;
    u16 format;
    enum video_mode_constraint constraint;
    bool none;
};
#define VM_EQUALS(l, r) ((l).width == (r).width && (l).height == (r).height && (l).bpp == (r).bpp)
#define VM_GREATER_OR_EQUAL(l, r) ((l).width >= (r).width && (l).height >= (r).height && (l).bpp >= (r).bpp)
#define VM_LESS_OR_EQUAL(l, r) ((l).width <= (r).width && (l).height <= (r).height)

void video_mode_from_value(struct config *cfg, struct value *val, struct requested_video_mode *mode)
{
    u64 cfg_width, cfg_height, cfg_bpp;
    struct string_view constraint_str;
    struct string_view format_str;

    if (value_is_null(val)) {
        mode->none = true;
        return;
    }

    if (value_is_string(val)) {
        if (sv_equals(val->as_string, SV("unset"))) {
            mode->none = true;
            return;
        }

        if (!sv_equals(val->as_string, SV("auto")))
            oops("invalid value for \"video-mode\": %pSV\n", &val->as_string);

        return;
    }

    if (cfg_get_unsigned(cfg, val, SV("width"), &cfg_width))
        mode->width = cfg_width;
    if (cfg_get_unsigned(cfg, val, SV("height"), &cfg_height))
        mode->height = cfg_height;
    if (cfg_get_unsigned(cfg, val, SV("bpp"), &cfg_bpp))
        mode->bpp = cfg_bpp;

    if (cfg_get_string(cfg, val, SV("format"), &format_str)) {
        if (sv_equals_caseless(format_str, SV("rgb888")))
            mode->format = FB_FORMAT_RGB888;
        else if (sv_equals_caseless(format_str, SV("bgr888")))
            mode->format = FB_FORMAT_BGR888;
        else if (sv_equals_caseless(format_str, SV("rgbx8888")))
            mode->format = FB_FORMAT_RGBX8888;
        else if (sv_equals_caseless(format_str, SV("xrgb8888")))
            mode->format = FB_FORMAT_XRGB8888;
        else if (!sv_equals_caseless(format_str, SV("auto")))
            oops("Unsupported video-mode format '%pSV'\n", &format_str);
    }

    if (cfg_get_string(cfg, val, SV("constraint"), &constraint_str)) {
        if (sv_equals(constraint_str, SV("at-least")))
            mode->constraint = VIDEO_MODE_CONSTRAINT_AT_LEAST;
        else if (sv_equals(constraint_str, SV("exactly")))
            mode->constraint = VIDEO_MODE_CONSTRAINT_EXACTLY;
        else
            oops("invalid video mode constraint %pSV\n", &constraint_str);
    }
}

#define DEFAULT_WIDTH 1024
#define DEFAULT_HEIGHT 768
#define DEFAULT_BPP 32

bool set_video_mode(struct config *cfg, struct loadable_entry *entry,
                    struct ultra_framebuffer *out_fb)
{
    struct value video_mode_val;
    struct video_mode picked_vm;
    size_t mode_count, mode_idx;
    bool did_pick = false;
    struct resolution native_res = {
        .width = DEFAULT_WIDTH,
        .height = DEFAULT_HEIGHT
    };
    struct requested_video_mode rm = {
        .width = DEFAULT_WIDTH,
        .height = DEFAULT_HEIGHT,
        .bpp = DEFAULT_BPP,
        .format = FB_FORMAT_INVALID,
        .constraint = VIDEO_MODE_CONSTRAINT_AT_LEAST
    };
    struct framebuffer fb;

    if (cfg_get_one_of(cfg, entry, SV("video-mode"), VALUE_OBJECT | VALUE_STRING | VALUE_NONE,
                       &video_mode_val)) {
        video_mode_from_value(cfg, &video_mode_val, &rm);
    }

    if (rm.none)
        return false;

    vs_query_native_resolution(&native_res);
    mode_count = vs_get_mode_count();

    for (mode_idx = 0; mode_idx < mode_count; ++mode_idx) {
        struct video_mode m;
        vs_query_mode(mode_idx, &m);

        if (rm.format != FB_FORMAT_INVALID && m.format != rm.format)
            continue;

        if (rm.constraint == VIDEO_MODE_CONSTRAINT_EXACTLY && VM_EQUALS(m, rm)) {
            picked_vm = m;
            did_pick = true;
            break;
        }

        if (VM_GREATER_OR_EQUAL(m, rm) && VM_LESS_OR_EQUAL(m, native_res)) {
            picked_vm = m;
            did_pick = true;
        }
    }

    if (!did_pick) {
        oops("failed to pick a video mode according to constraints (%ux%u %u bpp)\n",
             rm.width, rm.height, rm.bpp);
    }

    print_info("picked video mode %ux%u @ %u bpp\n", picked_vm.width, picked_vm.height, picked_vm.bpp);

    if (!vs_set_mode(picked_vm.id, &fb))
        oops("failed to set picked video mode\n");

    BUILD_BUG_ON(sizeof(*out_fb) != sizeof(fb));
    memcpy(out_fb, &fb, sizeof(fb));

    return true;
}

struct attribute_array_spec {
    bool higher_half_pointers;
    bool fb_present;
    bool cmdline_present;

    struct ultra_framebuffer fb;

    struct string_view cmdline;
    struct kernel_info kern_info;

    struct dynamic_buffer module_buf;

    u64 stack_address;
    ptr_t acpi_rsdp_address;
};

struct handover_info {
    size_t memory_map_handover_key;
    u64 attribute_array_address;
};

static void ultra_memory_map_entry_convert(struct memory_map_entry *entry, void *buf)
{
    struct ultra_memory_map_entry *ue = buf;

    ue->physical_address = entry->physical_address;
    ue->size = entry->size_in_bytes;

    // Direct mapping
    if (entry->type <= ULTRA_MEMORY_TYPE_NVS || (entry->type >= ULTRA_MEMORY_TYPE_LOADER_RECLAIMABLE)) {
        ue->type = entry->type;
    } else {
        ue->type = ULTRA_MEMORY_TYPE_RESERVED;
    }
}

#define ULTRA_MAJOR 1
#define ULTRA_MINOR 0

static void *write_context_header(struct ultra_boot_context *ctx, uint32_t** attr_count)
{
    ctx->protocol_major = ULTRA_MAJOR;
    ctx->protocol_minor = ULTRA_MINOR;
    *attr_count = &ctx->attribute_count;

    return ++ctx;
}

static void *write_platform_info(struct ultra_platform_info_attribute *pi, u64 rsdp_address)
{
    pi->header.type = ULTRA_ATTRIBUTE_PLATFORM_INFO;
    pi->header.size = sizeof(struct ultra_platform_info_attribute);
    pi->platform_type = services_get_provider() == SERVICE_PROVIDER_BIOS ? ULTRA_PLATFORM_BIOS : ULTRA_PLATFORM_UEFI;
    pi->loader_major = HYPER_MAJOR;
    pi->loader_minor = HYPER_MINOR;
    pi->acpi_rsdp_address = rsdp_address;
    sv_terminated_copy(pi->loader_name, HYPER_BRAND_STRING);

    return ++pi;
}

static void *write_kernel_info_attribute(struct ultra_kernel_info_attribute *attr, const struct kernel_info *ki)
{
    struct string_view path_str = ki->bin_opts.path.path_within_partition;
    u32 partition_type = ki->bin_opts.path.partition_id_type;

    if (partition_type == PARTITION_IDENTIFIER_ORIGIN) {
        switch (get_origin_fs()->entry_type) {
        case FSE_TYPE_RAW:
            partition_type = ULTRA_PARTITION_TYPE_RAW;
            break;
        case FSE_TYPE_MBR:
            partition_type = ULTRA_PARTITION_TYPE_MBR;
            break;
        case FSE_TYPE_GPT:
            partition_type = ULTRA_PARTITION_TYPE_GPT;
            break;
        default:
            BUG();
        }
    }

    attr->header = (struct ultra_attribute_header) {
        .type = ULTRA_ATTRIBUTE_KERNEL_INFO,
        .size = sizeof(struct ultra_kernel_info_attribute)
    };
    attr->physical_base = ki->bin_info.physical_base;
    attr->virtual_base = ki->bin_info.virtual_base;
    attr->size = ki->bin_info.physical_ceiling - ki->bin_info.physical_base;
    attr->partition_type = partition_type;
    attr->partition_index = ki->bin_opts.path.partition_index;

    BUILD_BUG_ON(sizeof(attr->disk_guid) != sizeof(ki->bin_opts.path.disk_guid));
    memcpy(&attr->disk_guid, &ki->bin_opts.path.disk_guid, sizeof(attr->disk_guid));
    memcpy(&attr->partition_guid, &ki->bin_opts.path.partition_guid, sizeof(attr->partition_guid));

    BUG_ON(path_str.size > (sizeof(attr->fs_path) - 1));
    memcpy(attr->fs_path, path_str.text, path_str.size);
    attr->fs_path[path_str.size] = '\0';

    return ++attr;
}

static void *write_framebuffer(struct ultra_framebuffer_attribute *fb_attr, const struct attribute_array_spec *spec)
{
    fb_attr->header.type = ULTRA_ATTRIBUTE_FRAMEBUFFER_INFO;
    fb_attr->header.size = sizeof(struct ultra_framebuffer_attribute);
    fb_attr->fb = spec->fb;

    if (spec->higher_half_pointers)
        fb_attr->fb.address += DIRECT_MAP_BASE;

    return ++fb_attr;
}

static void *write_memory_map_header(struct ultra_memory_map_attribute *mm, size_t entry_count)
{
    mm->header.type = ULTRA_ATTRIBUTE_MEMORY_MAP;
    mm->header.size = sizeof(struct ultra_memory_map_attribute) + entry_count * sizeof(struct ultra_memory_map_entry);
    return ++mm;
}

void build_attribute_array(const struct attribute_array_spec *spec,
                           struct handover_info *hi)
{
    u32 cmdline_aligned_length = 0;
    size_t mm_entry_count, bytes_needed = 0;
    void *attr_ptr;
    uint32_t *attr_count;

    if (spec->cmdline_present) {
        cmdline_aligned_length += sizeof(struct ultra_attribute_header);
        cmdline_aligned_length += spec->cmdline.size + 1;
        cmdline_aligned_length = ALIGN_UP(cmdline_aligned_length, 8);
    }

    bytes_needed += sizeof(struct ultra_boot_context);
    bytes_needed += sizeof(struct ultra_platform_info_attribute);
    bytes_needed += sizeof(struct ultra_kernel_info_attribute);
    bytes_needed += spec->module_buf.size * sizeof(struct ultra_module_info_attribute);
    bytes_needed += cmdline_aligned_length;
    bytes_needed += spec->fb_present * sizeof(struct ultra_framebuffer_attribute);
    bytes_needed += sizeof(struct ultra_memory_map_attribute);

    /*
     * Attempt to allocate the storage for attribute array while having enough space for the memory map
     * (which is changed every time we allocate/free more memory)
     */
    for (;;) {
        size_t bytes_for_this_allocation, mm_entry_count_new, key = 0;

        // Add 1 to give some leeway for memory map growth after the next allocation
        mm_entry_count = ms_copy_map(NULL, 0, 0, &key, NULL) + 1;
        bytes_for_this_allocation = bytes_needed + mm_entry_count * sizeof(struct ultra_memory_map_entry);

        // FIXME: this should probably do page granularity allocations
        hi->attribute_array_address = (u32)(ptr_t)allocate_critical_bytes(bytes_for_this_allocation);

        // Check if memory map had to grow to store the previous allocation
        mm_entry_count_new = ms_copy_map(NULL, 0, 0, &key, NULL);

        if (mm_entry_count < mm_entry_count_new) {
            free_bytes((void*)(ptr_t)hi->attribute_array_address, bytes_for_this_allocation);
            continue;
        }

        mm_entry_count = mm_entry_count_new;
        memzero((void*)(ptr_t)hi->attribute_array_address, bytes_for_this_allocation);
        break;
    }

    attr_ptr = (void*)(ptr_t)hi->attribute_array_address;
    attr_ptr = write_context_header(attr_ptr, &attr_count);

    attr_ptr = write_platform_info(attr_ptr, spec->acpi_rsdp_address);
    *attr_count += 1;

    attr_ptr = write_kernel_info_attribute(attr_ptr, &spec->kern_info);
    *attr_count += 1;

    if (spec->module_buf.size) {
        size_t bytes_for_modules = spec->module_buf.size * sizeof(struct ultra_module_info_attribute);
        memcpy(attr_ptr, spec->module_buf.buf, bytes_for_modules);
        attr_ptr += bytes_for_modules;
        *attr_count += spec->module_buf.size;
    }

    if (spec->cmdline_present) {
        *(struct ultra_command_line_attribute*)attr_ptr = (struct ultra_command_line_attribute) {
            .header = { ULTRA_ATTRIBUTE_COMMAND_LINE, cmdline_aligned_length },
        };

        // Copy the cmdline string & null terminate
        memcpy(attr_ptr + sizeof(struct ultra_command_line_attribute), spec->cmdline.text, spec->cmdline.size);
        *((char*)attr_ptr + sizeof(struct ultra_attribute_header) + spec->cmdline.size) = '\0';

        attr_ptr += cmdline_aligned_length;
        *attr_count += 1;
    }

    if (spec->fb_present) {
        attr_ptr = write_framebuffer(attr_ptr, spec);
        *attr_count += 1;
    }

    attr_ptr = write_memory_map_header(attr_ptr, mm_entry_count);
    *attr_count += 1;
    ms_copy_map(attr_ptr, mm_entry_count, sizeof(struct ultra_memory_map_entry),
                &hi->memory_map_handover_key, ultra_memory_map_entry_convert);
    attr_ptr += mm_entry_count * sizeof(struct ultra_memory_map_entry);
}

u64 build_page_table(struct binary_info *bi, u64 max_address, bool higher_half_exclusive, bool null_guard)
{
    struct page_table pt;
    u64 max_address_rounded_up;

    if (bi->bitness != 64)
        return 0;

    max_address_rounded_up = HUGE_PAGE_ROUND_UP(max_address);
    max_address_rounded_up = MAX(4ull * GB, max_address_rounded_up);
    print_info("going to map physical up to 0x%016llX\n", max_address_rounded_up);

    pt.root = (u64*)allocate_critical_pages(1);
    pt.levels = 4;
    memzero(pt.root, PAGE_SIZE);

    // direct map higher half
    map_critical_huge_pages(&pt, DIRECT_MAP_BASE , 0x0000000000000000,
                            max_address_rounded_up / HUGE_PAGE_SIZE);

    if (!higher_half_exclusive) {
        u64 base = 0x0000000000000000;
        size_t pages_to_map;

        /*
         * Don't use huge pages for the first 2M in case there's a null guard,
         * we only want to unmap the first 4K page.
         */
        if (null_guard) {
            base += PAGE_SIZE;
            pages_to_map = ((2 * MB) / PAGE_SIZE) - 1;
            map_critical_pages(&pt, base, base, pages_to_map);
            base = 2 * MB;
        }

        pages_to_map = (max_address_rounded_up - base) / HUGE_PAGE_SIZE;
        map_critical_huge_pages(&pt, base, base, pages_to_map);
    } else {
        // steal the direct mapping from higher half, we're gonna unmap it later
        pt.root[0] = pt.root[256];
    }

    /*
     * If kernel had allocate-anywhere set to on, map virtual base to physical base,
     * otherwise simply direct map fist 2 gigabytes of physical.
     */
    if (!bi->kernel_range_is_direct_map) {
        size_t pages = bi->physical_ceiling - bi->physical_base;
        pages = CEILING_DIVIDE(pages, PAGE_SIZE);
        map_critical_pages(&pt, bi->virtual_base, bi->physical_base, pages);
    } else {
        map_critical_huge_pages(&pt, HIGHER_HALF_BASE, 0x0000000000000000,
                                (2ull * GB) / HUGE_PAGE_SIZE);
    }

    return (ptr_t)pt.root;
}

u64 pick_stack(struct config *cfg, struct loadable_entry *le)
{
    struct value val;
    u64 address = 0;
    size_t size = 16 * KB;
    bool has_val;

    has_val = cfg_get_one_of(cfg, le, SV("stack"), VALUE_STRING | VALUE_OBJECT, &val);

    if (has_val && value_is_object(&val)) {
        struct value alloc_at_val, size_val;
        bool has_alloc_at, has_size;

        has_alloc_at = cfg_get_one_of(cfg, le, SV("allocate-at"), VALUE_STRING | VALUE_UNSIGNED, &alloc_at_val);
        has_size = cfg_get_one_of(cfg, le, SV("size"), VALUE_STRING | VALUE_UNSIGNED, &size_val);

        if (has_alloc_at && value_is_string(&alloc_at_val)) {
            if (!sv_equals(alloc_at_val.as_string, SV("anywhere")))
                oops("invalid value for \"allocate-at\": %pSV\n", &alloc_at_val.as_string);
        } else if (has_alloc_at) { // unsigned
            address = alloc_at_val.as_unsigned;
        }

        if (has_size && value_is_string(&size_val)) {
            if (!sv_equals(size_val.as_string, SV("auto")))
                oops("invalid value for \"size\": %pSV\n", &size_val.as_string);
        } else if (has_size) { // unsigned
            size = size_val.as_unsigned;
        }
    } else if (has_val) { // string
        if (!sv_equals(val.as_string, SV("auto")))
            oops("invalid value for \"stack\": %pSV\n", &val.as_string);
    }

    size_t pages = CEILING_DIVIDE(size, PAGE_SIZE);

    if (address)
        allocate_critical_pages_with_type_at(address, pages, ULTRA_MEMORY_TYPE_KERNEL_STACK);
    else
        address = (ptr_t)allocate_critical_pages_with_type(pages, ULTRA_MEMORY_TYPE_KERNEL_STACK);

    address += pages * PAGE_SIZE;
    return address;
}

static struct ultra_module_info_attribute *module_alloc(struct dynamic_buffer *buf)
{
    void *out = dynamic_buffer_slot_alloc(buf);
    DIE_ON(!out);

    return out;
}

static bool load_kernel_as_module(struct config *cfg, struct loadable_entry *le, struct attribute_array_spec *spec)
{
    bool kernel_as_module = false;
    struct ultra_module_info_attribute *mi;

    cfg_get_bool(cfg, le, SV("kernel-as-module"), &kernel_as_module);
    if (!kernel_as_module)
        return false;

    mi = module_alloc(&spec->module_buf);
    *mi = (struct ultra_module_info_attribute) {
        .header = {
            ULTRA_ATTRIBUTE_MODULE_INFO,
            sizeof(struct ultra_module_info_attribute)
        },
        .type = ULTRA_MODULE_TYPE_FILE,
        .address = (ptr_t)spec->kern_info.elf_blob,
        .size = spec->kern_info.blob_size
    };
    sv_terminated_copy(mi->name, SV("__KERNEL__"));

    return true;
}

static void load_all_modules(struct config *cfg, struct loadable_entry *le, struct attribute_array_spec *spec)
{
    struct value module_value;

    if (!cfg_get_first_one_of(cfg, le, SV("module"), VALUE_STRING | VALUE_OBJECT, &module_value))
        return;

    do {
        struct ultra_module_info_attribute *mi = module_alloc(&spec->module_buf);
        module_load(cfg, &module_value, mi);

        if (spec->higher_half_pointers)
            mi->address += DIRECT_MAP_BASE;
    } while (cfg_get_next_one_of(cfg, VALUE_STRING | VALUE_OBJECT, &module_value, true));
}

void ultra_protocol_load(struct config *cfg, struct loadable_entry *le)
{
    struct attribute_array_spec spec = { 0 };
    struct handover_info hi;
    u64 pt;
    bool handover_res, is_higher_half_kernel, is_higher_half_exclusive = false, null_guard = false;

    dynamic_buffer_init(&spec.module_buf, sizeof(struct ultra_module_info_attribute), true);

    load_kernel(cfg, le, &spec.kern_info);
    is_higher_half_kernel = spec.kern_info.bin_info.entrypoint_address >= HIGHER_HALF_BASE;
    cfg_get_bool(cfg, le, SV("higher-half-exclusive"), &is_higher_half_exclusive);
    cfg_get_bool(cfg, le, SV("null-guard"), &null_guard);

    if (!is_higher_half_kernel && is_higher_half_exclusive)
        oops("Higher half exclusive mode is only allowed for higher half kernels\n");

    spec.higher_half_pointers = is_higher_half_exclusive;
    spec.cmdline_present = cfg_get_string(cfg, le, SV("cmdline"), &spec.cmdline);

    if (!load_kernel_as_module(cfg, le, &spec)) {
        free_bytes(spec.kern_info.elf_blob, spec.kern_info.blob_size);
        spec.kern_info.elf_blob = NULL;
        spec.kern_info.blob_size = 0;
    }

    load_all_modules(cfg, le, &spec);
    pt = build_page_table(&spec.kern_info.bin_info, ms_get_highest_map_address(),
                          is_higher_half_exclusive, null_guard);
    spec.stack_address = pick_stack(cfg, le);
    spec.acpi_rsdp_address = services_find_rsdp();

   /*
    * Attempt to set video mode last, as we're not going to be able to use
    * legacy tty logging after that.
    */
    spec.fb_present = set_video_mode(cfg, le, &spec.fb);

    /*
     * We cannot allocate any memory after this call, as memory map is now
     * saved inside the attribute array.
     */
    build_attribute_array(&spec, &hi);

    // Exit all services before handover
    handover_res = services_exit_all(hi.memory_map_handover_key);
    BUG_ON(!handover_res);

    if (is_higher_half_kernel) {
        spec.stack_address += DIRECT_MAP_BASE;
        hi.attribute_array_address += DIRECT_MAP_BASE;
    }

    print_info("jumping to kernel: entry 0x%016llX, stack at 0x%016llX, boot context at 0x%016llX\n",
               spec.kern_info.bin_info.entrypoint_address, spec.stack_address,
               hi.attribute_array_address);

    if (spec.kern_info.bin_info.bitness == 32)
        kernel_handover32(spec.kern_info.bin_info.entrypoint_address, spec.stack_address,
                          (u32)hi.attribute_array_address, ULTRA_MAGIC);

    kernel_handover64(spec.kern_info.bin_info.entrypoint_address, spec.stack_address, pt,
                      hi.attribute_array_address, ULTRA_MAGIC, is_higher_half_exclusive);
}
