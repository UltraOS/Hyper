#define MSG_FMT(msg) "ULTRA-PROT: " msg

#include "common/bug.h"
#include "common/helpers.h"
#include "common/constants.h"
#include "common/format.h"
#include "common/log.h"
#include "common/dynamic_buffer.h"
#include "common/align.h"
#include "common/minmax.h"

#include "boot_protocol.h"
#include "boot_protocol/ultra_impl.h"
#include "ultra_protocol/ultra_protocol.h"
#include "elf.h"
#include "filesystem/filesystem_table.h"
#include "allocator.h"
#include "virtual_memory.h"
#include "handover.h"
#include "hyper.h"
#include "services.h"
#include "video_services.h"

static void get_binary_options(struct config *cfg, struct loadable_entry *le,
                               struct binary_options *opts)
{
    struct value binary_val;
    struct string_view string_path;
    const struct fs_entry *fse;

    CFG_MANDATORY_GET_ONE_OF(VALUE_STRING | VALUE_OBJECT, cfg, le,
                             SV("binary"), &binary_val);

    if (value_is_object(&binary_val)) {
        CFG_MANDATORY_GET(string, cfg, &binary_val, SV("path"), &string_path);
        cfg_get_bool(cfg, &binary_val, SV("allocate-anywhere"),
                     &opts->allocate_anywhere);
    } else {
        string_path = binary_val.as_string;
    }

    if (!path_parse(string_path, &opts->path))
        cfg_oops_invalid_key_value(SV("binary/path"), string_path);

    fse = fst_fs_by_full_path(&opts->path);
    if (!fse)
        oops("no such disk/partition %pSV\n", &string_path);

    opts->fs = fse->fs;
}

#define SIZE_KEY SV("size")

static uint32_t module_get_size(struct config *cfg, struct value *module_value)
{
    const uint32_t type_mask = VALUE_STRING | VALUE_UNSIGNED | VALUE_NONE;
    struct value size_value;

    if (!cfg_get_one_of(cfg, module_value, SIZE_KEY, type_mask, &size_value) ||
        value_is_null(&size_value))
        return 0;

    if (value_is_string(&size_value)) {
        if (!sv_equals(size_value.as_string, SV("auto")))
            cfg_oops_invalid_key_value(SV("module/size"),
                                       size_value.as_string);
        return 0;
    }

    if (size_value.as_unsigned == 0)
        cfg_oops_invalid_key_value(SV("module/size"), SV("0"));

    return size_value.as_unsigned;
}

static uint32_t module_get_type(struct config *cfg, struct value *module_value)
{
    const uint32_t type_mask = VALUE_STRING | VALUE_NONE;
    struct value type_value;

    if (!cfg_get_one_of(cfg, module_value, SV("type"), type_mask, &type_value)
        || value_is_null(&type_value)
        || sv_equals(type_value.as_string, SV("file")))
        return ULTRA_MODULE_TYPE_FILE;

    if (sv_equals(type_value.as_string, SV("memory")))
        return ULTRA_MODULE_TYPE_MEMORY;

    cfg_oops_invalid_key_value(SV("module/type"), type_value.as_string);
}

static u64 module_get_load_address(struct config *cfg,
                                   struct value *module_value,
                                   bool *has_load_address)
{
    const uint32_t type_mask = VALUE_STRING | VALUE_UNSIGNED | VALUE_NONE;
    struct value load_at_value;

    if (!cfg_get_one_of(cfg, module_value, SV("load-at"), type_mask,
                        &load_at_value) || value_is_null(&load_at_value))
    {
        *has_load_address = false;
        return 0;
    }

    if (value_is_string(&load_at_value)) {
        if (!sv_equals(load_at_value.as_string, SV("auto"))) {
            cfg_oops_invalid_key_value(SV("module/load-at"),
                                       load_at_value.as_string);
        }

        *has_load_address = false;
        return 0;
    }

    *has_load_address = true;
    return load_at_value.as_unsigned;
}

static void *module_data_alloc(u64 addr, u64 ceiling, size_t size,
                               size_t zero_after_offset,
                               bool has_load_address)
{
    size_t zeroed_bytes;
    void *ret;
    struct allocation_spec as = {
        .addr = addr,
        .flags = ALLOCATE_CRITICAL,
        .type = ULTRA_MEMORY_TYPE_MODULE
    };

    as.pages = PAGE_ROUND_UP(size);
    zeroed_bytes = as.pages - zero_after_offset;
    as.pages >>= PAGE_SHIFT;

    if (has_load_address) {
        as.flags |= ALLOCATE_PRECISE;

        if ((addr + size) < addr) {
            oops("invalid module address 0x%016llX - size %zu combination\n",
                 addr, size);
        }

        if ((addr + size) > ceiling) {
            oops("module is too high in memory 0x%016llX "
                 "(ceiling: 0x%016llX)\n",
                 addr, ceiling);
        }

        if (range_outside_of_address_space(addr, size)) {
            oops("inaccessible module at 0x%016llX (%zu bytes)\n",
                 addr, size);
        }
    } else {
        as.ceiling = ceiling;
    }

    addr = allocate_pages_ex(&as);
    ret = ADDR_TO_PTR(addr);

    memzero(ret + zero_after_offset, zeroed_bytes);
    return ret;
}

static void module_load(struct config *cfg, struct value *module_value,
                        struct ultra_module_info_attribute *attrs, u64 ceiling)
{
    bool has_path, has_load_address = false;
    struct string_view str_path, module_name = { 0 };
    size_t module_size = 0;
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
        load_address = module_get_load_address(cfg, module_value,
                                               &has_load_address);
    } else {
        str_path = module_value->as_string;
        has_path = true;
    }

    if (sv_empty(module_name)) {
        snprintf(attrs->name, sizeof(attrs->name),
                 "unnamed_module%d", module_idx);
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

        if (!path_parse(str_path, &path))
            oops("invalid module path %pSV\n", &str_path);

        fse = fst_fs_by_full_path(&path);
        if (!fse)
            oops("no such disk/partition %pSV\n", &str_path);

        module_file = path_open(fse->fs, path.path_within_partition);
        if (!module_file)
            oops("no such file %pSV\n", &path.path_within_partition);

        bytes_to_read = module_file->size;

        if (!module_size) {
            module_size = bytes_to_read;
        } else if (module_size < bytes_to_read) {
            bytes_to_read = module_size;
        }

        module_data = module_data_alloc(load_address, ceiling, module_size,
                                        bytes_to_read, has_load_address);

        if (!module_file->fs->read_file(module_file, module_data, 0,
                                        bytes_to_read)) {
            oops("failed to read module file\n");
        }

        fse->fs->close_file(module_file);
    } else { // module_type == ULTRA_MODULE_TYPE_MEMORY
        if (!module_size)
            oops("module size cannot be \"auto\" for type \"memory\"\n");

        module_data = module_data_alloc(load_address, ceiling, module_size, 0,
                                        has_load_address);
    }

    attrs->address = (ptr_t)module_data;
    attrs->type = module_type;
    attrs->size = module_size;
}

static void load_kernel(struct config *cfg, struct loadable_entry *entry,
                        struct kernel_info *info)
{
    struct binary_options *bo = &info->bin_opts;
    struct handover_info *hi = &info->hi;
    struct elf_binary_info *bi = &info->bin_info;

    enum elf_arch arch;
    struct elf_error err = { 0 };
    struct elf_load_spec spec = {
        .memory_type = ULTRA_MEMORY_TYPE_KERNEL_BINARY,
    };

    get_binary_options(cfg, entry, bo);

    info->binary = path_open(bo->fs, bo->path.path_within_partition);
    if (!info->binary)
        oops("failed to open %pSV\n", &bo->path.path_within_partition);

    spec.io.binary = info->binary;

    if (!elf_init_io_cache(&spec.io, &err))
        goto elf_error;
    if (!elf_get_arch(&spec.io, &arch, &err))
        goto elf_error;

    spec.flags |= ELF_USE_VIRTUAL_ADDRESSES;
    if (bo->allocate_anywhere)
        spec.flags |= ELF_ALLOCATE_ANYWHERE;

    hi->flags |= ultra_get_flags_for_binary_options(bo, arch);
    handover_ensure_supported_flags(hi->flags);

    spec.binary_ceiling = ultra_max_binary_address(hi->flags);
    spec.higher_half_base = ultra_higher_half_base(hi->flags);

    if (!elf_load(&spec, &info->bin_info, &err))
        goto elf_error;

    hi->entrypoint = bi->entrypoint_address;
    info->is_higher_half = hi->entrypoint >= spec.higher_half_base;
    return;

elf_error:
    elf_pretty_print_error(&err, "failed to load kernel binary");
    loader_abort();
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

#define VM_EQUALS(l, r) ((l).width == (r).width && \
                         (l).height == (r).height && \
                         (l).bpp == (r).bpp)

#define VM_GREATER_OR_EQUAL(l, r) ((l).width >= (r).width && \
                                   (l).height >= (r).height && \
                                   (l).bpp >= (r).bpp)

#define VM_LESS_OR_EQUAL(l, r) ((l).width <= (r).width && \
                                (l).height <= (r).height)

#define VIDEO_MODE_KEY SV("video-mode")

static void video_mode_from_value(struct config *cfg, struct value *val,
                                  struct requested_video_mode *mode)
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
            cfg_oops_invalid_key_value(VIDEO_MODE_KEY, val->as_string);

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

static bool set_video_mode(struct config *cfg, struct loadable_entry *entry,
                           struct ultra_framebuffer *out_fb)
{
    struct value video_mode_val;
    struct video_mode picked_vm = { 0 };
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

    if (cfg_get_one_of(cfg, entry, VIDEO_MODE_KEY,
                       VALUE_OBJECT | VALUE_STRING | VALUE_NONE,
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

        if (rm.constraint == VIDEO_MODE_CONSTRAINT_EXACTLY &&
            VM_EQUALS(m, rm)) {
            picked_vm = m;
            did_pick = true;
            break;
        }

        if (!VM_LESS_OR_EQUAL(m, native_res))
            continue;

        if (!VM_GREATER_OR_EQUAL(m, rm))
            continue;

        if (did_pick && !VM_GREATER_OR_EQUAL(m, picked_vm))
            continue;

        picked_vm = m;
        did_pick = true;
    }

    if (!did_pick) {
        oops("failed to pick a video mode according to constraints "
             "(%ux%u %u bpp)\n", rm.width, rm.height, rm.bpp);
    }

    print_info("picked video mode %ux%u @ %u bpp\n",
               picked_vm.width, picked_vm.height, picked_vm.bpp);

    if (!vs_set_mode(picked_vm.id, &fb))
        oops("failed to set picked video mode\n");

    BUILD_BUG_ON(sizeof(*out_fb) != sizeof(fb));
    memcpy(out_fb, &fb, sizeof(fb));

    return true;
}

static bool apm_setup(struct config *cfg, struct loadable_entry *le,
                      struct apm_info *out_info)
{
    bool wants_apm = false;

    cfg_get_bool(cfg, le, SV("setup-apm"), &wants_apm);
    if (!wants_apm)
        return false;

    if (services_get_provider() != SERVICE_PROVIDER_BIOS) {
        print_info("ignoring request to set up APM on UEFI\n");
        return false;
    }

    return services_setup_apm(out_info);
}

struct attribute_array_spec {
    bool higher_half_pointers;
    bool fb_present;
    bool cmdline_present;
    bool apm_info_present;
    uint8_t page_table_depth;

    struct ultra_framebuffer fb;

    struct string_view cmdline;
    struct kernel_info kern_info;

    struct dynamic_buffer module_buf;

    struct apm_info apm_info;

    ptr_t acpi_rsdp_address;
    ptr_t dtb_address;
    ptr_t smbios_address;
};

static void ultra_memory_map_entry_convert(struct memory_map_entry *entry,
                                           void *buf)
{
    struct ultra_memory_map_entry *ue = buf;

    ue->physical_address = entry->physical_address;
    ue->size = entry->size_in_bytes;

    // Direct mapping
    if (entry->type <= MEMORY_TYPE_NVS ||
        entry->type >= ULTRA_MEMORY_TYPE_LOADER_RECLAIMABLE) {
        ue->type = entry->type;
    } else if (entry->type == MEMORY_TYPE_LOADER_RECLAIMABLE) {
        ue->type = ULTRA_MEMORY_TYPE_LOADER_RECLAIMABLE;
    } else {
        BUG();
    }
}

#define ULTRA_MAJOR 1
#define ULTRA_MINOR 0

static void *write_context_header(struct ultra_boot_context *ctx,
                                  uint32_t **attr_count)
{
    ctx->protocol_major = ULTRA_MAJOR;
    ctx->protocol_minor = ULTRA_MINOR;
    *attr_count = &ctx->attribute_count;

    return ++ctx;
}

static void *write_platform_info(struct ultra_platform_info_attribute *pi,
                                 const struct attribute_array_spec *spec)
{
    pi->header.type = ULTRA_ATTRIBUTE_PLATFORM_INFO;
    pi->header.size = sizeof(struct ultra_platform_info_attribute);
    pi->platform_type = services_get_provider() == SERVICE_PROVIDER_BIOS ?
                            ULTRA_PLATFORM_BIOS : ULTRA_PLATFORM_UEFI;
    pi->loader_major = HYPER_MAJOR;
    pi->loader_minor = HYPER_MINOR;
    pi->acpi_rsdp_address = spec->acpi_rsdp_address;
    pi->dtb_address = spec->dtb_address;
    pi->smbios_address = spec->smbios_address;
    pi->higher_half_base = spec->kern_info.hi.direct_map_base;
    pi->page_table_depth = spec->page_table_depth;
    sv_terminated_copy(pi->loader_name, HYPER_BRAND_STRING);

    return ++pi;
}

static void*
write_kernel_info_attribute(struct ultra_kernel_info_attribute *attr,
                            const struct kernel_info *ki)
{
    const struct full_path *fp = &ki->bin_opts.path;
    struct string_view path_str = fp->path_within_partition;
    u32 partition_type = fp->partition_id_type;

    if (partition_type == PARTITION_IDENTIFIER_ORIGIN) {
        const struct fs_entry *origin;

        origin = fst_get_origin();

        switch (origin->entry_type) {
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

        attr->disk_index = origin->disk_id;
        attr->partition_index = origin->partition_index;

        memcpy(&attr->disk_guid, &origin->disk_guid, sizeof(attr->disk_guid));
        memcpy(&attr->partition_guid, &origin->partition_guid,
               sizeof(attr->partition_guid));
    } else {
        attr->partition_index = fp->partition_index;
        attr->disk_index = fp->disk_index;

        BUILD_BUG_ON(sizeof(attr->disk_guid) != sizeof(fp->disk_guid));
        memcpy(&attr->disk_guid, &fp->disk_guid, sizeof(attr->disk_guid));
        memcpy(&attr->partition_guid, &fp->partition_guid,
               sizeof(attr->partition_guid));
    }

    attr->header = (struct ultra_attribute_header) {
        .type = ULTRA_ATTRIBUTE_KERNEL_INFO,
        .size = sizeof(struct ultra_kernel_info_attribute)
    };
    attr->physical_base = ki->bin_info.physical_base;
    attr->virtual_base = ki->bin_info.virtual_base;
    attr->size = ki->bin_info.physical_ceiling - ki->bin_info.physical_base;
    attr->partition_type = partition_type;

    BUG_ON(path_str.size > (sizeof(attr->fs_path) - 1));
    memcpy(attr->fs_path, path_str.text, path_str.size);
    attr->fs_path[path_str.size] = '\0';

    return ++attr;
}

static void *write_framebuffer(struct ultra_framebuffer_attribute *fb_attr,
                               const struct attribute_array_spec *spec)
{
    fb_attr->header.type = ULTRA_ATTRIBUTE_FRAMEBUFFER_INFO;
    fb_attr->header.size = sizeof(struct ultra_framebuffer_attribute);
    fb_attr->fb = spec->fb;
    return ++fb_attr;
}

static void *write_apm_info(struct ultra_apm_attribute *apm_attr,
                            const struct attribute_array_spec *spec)
{
    apm_attr->header.type = ULTRA_ATTRIBUTE_APM_INFO;
    apm_attr->header.size = sizeof(struct ultra_apm_attribute);

    BUILD_BUG_ON(sizeof(apm_attr->info) != sizeof(struct apm_info));
    memcpy(&apm_attr->info, &spec->apm_info, sizeof(struct apm_info));

    return ++apm_attr;
}

static void *write_memory_map(void *attr_ptr, size_t entry_count)
{
    struct ultra_memory_map_attribute *mm = attr_ptr;
    void *entry_ptr = attr_ptr + sizeof(*mm);
    size_t entries_bytes;

    entry_count = services_release_resources(
        entry_ptr, entry_count,
        sizeof(struct ultra_memory_map_entry),
        ultra_memory_map_entry_convert
    );

    entries_bytes = entry_count * sizeof(struct ultra_memory_map_entry);
    mm->header.type = ULTRA_ATTRIBUTE_MEMORY_MAP;
    mm->header.size = sizeof(struct ultra_memory_map_attribute);
    mm->header.size += entries_bytes;

    return entry_ptr + entries_bytes;
}

static void *write_command_line_attribute(void *attr_ptr,
                                          struct string_view cmdline,
                                          size_t aligned_len)
{
    struct ultra_command_line_attribute *cattr = attr_ptr;
    cattr->header.type = ULTRA_ATTRIBUTE_COMMAND_LINE;
    cattr->header.size = aligned_len;

    sv_terminated_copy(cattr->text, cmdline);

    return attr_ptr + aligned_len;
}

static ptr_t build_attribute_array(const struct attribute_array_spec *spec,
                                   u64 array_ceiling)
{
    u32 cmdline_aligned_length = 0;
    size_t mm_entry_count, pages_needed, bytes_needed = 0;
    void *attr_ptr;
    uint32_t *attr_count;
    ptr_t ret;

    if (spec->cmdline_present) {
        cmdline_aligned_length += sizeof(struct ultra_attribute_header);
        cmdline_aligned_length += spec->cmdline.size + 1;
        cmdline_aligned_length = ALIGN_UP(cmdline_aligned_length, 8);
    }

    bytes_needed += sizeof(struct ultra_boot_context);
    bytes_needed += sizeof(struct ultra_platform_info_attribute);
    bytes_needed += sizeof(struct ultra_kernel_info_attribute);
    bytes_needed += spec->module_buf.size *
                        sizeof(struct ultra_module_info_attribute);
    bytes_needed += cmdline_aligned_length;
    bytes_needed += spec->fb_present *
                        sizeof(struct ultra_framebuffer_attribute);
    bytes_needed += spec->apm_info_present *
                        sizeof(struct ultra_apm_attribute);
    bytes_needed += sizeof(struct ultra_memory_map_attribute);

    // Add 2 to give some leeway for memory map growth after the next allocation
    mm_entry_count = services_release_resources(NULL, 0, 0, NULL) + 2;

    // Calculate the final number of bytes we need for the attribute array
    bytes_needed += mm_entry_count * sizeof(struct ultra_memory_map_entry);
    pages_needed = PAGE_ROUND_UP(bytes_needed);

    // Calculate the real mme capacity after we round up to page size
    mm_entry_count += (pages_needed - bytes_needed) /
                        sizeof(struct ultra_memory_map_entry);
    pages_needed >>= PAGE_SHIFT;

    /*
     * Attempt to allocate the storage for attribute array while having enough
     * space for the memory map. (which is changed every time we allocate/free
     * more memory)
     */
    for (;;) {
        size_t mm_entry_count_new;
        struct allocation_spec as = {
            .ceiling = array_ceiling,
            .pages = pages_needed,
            .flags = ALLOCATE_CRITICAL
        };

        ret = allocate_pages_ex(&as);

        // Check if memory map had to grow to store the previous allocation
        mm_entry_count_new = services_release_resources(NULL, 0, 0, NULL);

        if (mm_entry_count < mm_entry_count_new) {
            mm_entry_count += PAGE_SIZE / sizeof(struct ultra_memory_map_entry);
            free_pages((void*)ret, pages_needed++);

            // Memory map grew by more than 170 entries after one allocation(??)
            BUG_ON(mm_entry_count <= mm_entry_count_new);
            continue;
        }

        mm_entry_count = mm_entry_count_new;
        memzero((void*)ret, pages_needed << PAGE_SHIFT);
        break;
    }

    attr_ptr = (void*)ret;
    attr_ptr = write_context_header(attr_ptr, &attr_count);

    attr_ptr = write_platform_info(attr_ptr, spec);
    *attr_count += 1;

    attr_ptr = write_kernel_info_attribute(attr_ptr, &spec->kern_info);
    *attr_count += 1;

    if (spec->module_buf.size) {
        size_t bytes_for_modules = spec->module_buf.size *
                        sizeof(struct ultra_module_info_attribute);
        memcpy(attr_ptr, spec->module_buf.buf, bytes_for_modules);
        attr_ptr += bytes_for_modules;
        *attr_count += spec->module_buf.size;
    }

    if (spec->cmdline_present) {
        attr_ptr = write_command_line_attribute(attr_ptr, spec->cmdline,
                                                cmdline_aligned_length);
        *attr_count += 1;
    }

    if (spec->fb_present) {
        attr_ptr = write_framebuffer(attr_ptr, spec);
        *attr_count += 1;
    }

    if (spec->apm_info_present) {
        attr_ptr = write_apm_info(attr_ptr, spec);
        *attr_count += 1;
    }

    attr_ptr = write_memory_map(attr_ptr, mm_entry_count);
    *attr_count += 1;
    return ret;
}

#define ALLOCATE_AT_KEY SV("allocate-at")
#define STACK_KEY SV("stack")

static void allocate_stack(struct config *cfg, struct loadable_entry *le,
                           struct handover_info *hi)
{
    struct value val;
    size_t size = 16 * KB;
    bool has_val;
    struct allocation_spec as = {
        .ceiling = ultra_max_binary_address(hi->flags),
        .flags = ALLOCATE_CRITICAL | ALLOCATE_STACK,
        .type = ULTRA_MEMORY_TYPE_KERNEL_STACK,
    };

    has_val = cfg_get_one_of(cfg, le, STACK_KEY,
                             VALUE_STRING | VALUE_OBJECT, &val);

    if (has_val && value_is_object(&val)) {
        struct value alloc_at_val, size_val;
        bool has_alloc_at, has_size;

        has_alloc_at = cfg_get_one_of(cfg, &val, ALLOCATE_AT_KEY,
                                      VALUE_STRING | VALUE_UNSIGNED,
                                      &alloc_at_val);
        has_size = cfg_get_one_of(cfg, &val, SIZE_KEY,
                                  VALUE_STRING | VALUE_UNSIGNED,
                                  &size_val);

        if (has_alloc_at && value_is_string(&alloc_at_val)) {
            if (!sv_equals(alloc_at_val.as_string, SV("anywhere"))) {
                cfg_oops_invalid_key_value(ALLOCATE_AT_KEY,
                                           alloc_at_val.as_string);
            }
        } else if (has_alloc_at) { // unsigned
            as.addr = alloc_at_val.as_unsigned;
            as.flags |= ALLOCATE_PRECISE;
        }

        if (has_size && value_is_string(&size_val)) {
            if (!sv_equals(size_val.as_string, SV("auto")))
                cfg_oops_invalid_key_value(SIZE_KEY, size_val.as_string);
        } else if (has_size) { // unsigned
            size = PAGE_ROUND_UP(size_val.as_unsigned);
        }

        if (unlikely(!size || (has_alloc_at && ((as.addr + size) < as.addr)))) {
            oops("invalid stack address (0x%016llX) + size (%zu) combination\n",
                 as.addr, size);
        }
    } else if (has_val) { // string
        if (!sv_equals(val.as_string, SV("auto")))
            cfg_oops_invalid_key_value(STACK_KEY, val.as_string);
    }

    as.pages = size >> PAGE_SHIFT;
    hi->stack = allocate_pages_ex(&as);
}

static struct ultra_module_info_attribute*
module_alloc(struct dynamic_buffer *buf)
{
    struct ultra_module_info_attribute *attr;

    attr = dynamic_buffer_slot_alloc(buf);
    DIE_ON(!attr);

    *attr = (struct ultra_module_info_attribute) {
        .header = {
            ULTRA_ATTRIBUTE_MODULE_INFO,
            sizeof(struct ultra_module_info_attribute)
        },
    };
    return attr;
}

static void load_kernel_as_module(struct config *cfg, struct loadable_entry *le,
                                  struct attribute_array_spec *spec)
{
    bool kernel_as_module = false;
    struct ultra_module_info_attribute *mi;
    struct kernel_info *ki = &spec->kern_info;
    struct handover_info *hi = &ki->hi;
    struct file *binary = ki->binary;
    void *data;
    size_t size;

    cfg_get_bool(cfg, le, SV("kernel-as-module"), &kernel_as_module);
    if (!kernel_as_module)
        goto out;

    size = binary->size;
    data = module_data_alloc(0, ultra_max_binary_address(hi->flags),
                             size, size, false);

    if (!binary->fs->read_file(binary, data, 0, size))
        oops("failed to read kernel binary\n");

    mi = module_alloc(&spec->module_buf);
    mi->type = ULTRA_MODULE_TYPE_FILE;
    mi->address = (ptr_t)data;
    mi->size = size;
    sv_terminated_copy(mi->name, SV("__KERNEL__"));

    if (spec->higher_half_pointers)
        mi->address += hi->direct_map_base;

out:
    ki->binary = NULL;
    binary->fs->close_file(binary);
}

static void load_all_modules(struct config *cfg, struct loadable_entry *le,
                             struct attribute_array_spec *spec)
{
    struct handover_info *hi = &spec->kern_info.hi;
    struct value module_value;

    if (!cfg_get_first_one_of(cfg, le, SV("module"),
                              VALUE_STRING | VALUE_OBJECT, &module_value))
        return;

    do {
        struct ultra_module_info_attribute *mi;

        mi = module_alloc(&spec->module_buf);
        module_load(cfg, &module_value, mi, ultra_max_binary_address(hi->flags));

        if (spec->higher_half_pointers)
            mi->address += hi->direct_map_base;
    } while (cfg_get_next_one_of(cfg, VALUE_STRING | VALUE_OBJECT,
                                 &module_value, true));
}

#define MAX_CMDLINE_LEN 512

static bool get_cmdline(struct config *cfg, struct loadable_entry *le,
                        char *storage, struct string_view *out_str)
{
    if (!cfg_get_string(cfg, le, SV("cmdline"), out_str))
        return false;

    if (out_str->size > MAX_CMDLINE_LEN)
        oops(
            "command line is too big %zu vs max " TO_STR(MAX_CMDLINE_LEN) "\n",
            out_str->size
    	);

    memcpy(storage, out_str->text, out_str->size);

    /*
     * Repoint the view to internal storage as we don't want to keep a
     * reference to a string inside the configuration file here as we
     * free it later on before building the attribute array.
     */
    out_str->text = storage;

    return true;
}

struct page_mapper_ctx {
    struct page_mapping_spec *spec;
    u64 direct_map_min_size;
    u64 direct_map_base;
    bool map_lower;
};

static bool do_map_high_memory(void *opaque, const struct memory_map_entry *me)
{
    struct page_mapper_ctx *ctx = opaque;
    struct page_mapping_spec *spec = ctx->spec;
    u64 aligned_begin, aligned_end;
    size_t page_count;

    aligned_end = me->physical_address + me->size_in_bytes;
    aligned_end = HUGE_PAGE_ROUND_UP(spec->pt, aligned_end);

    if (aligned_end <= ctx->direct_map_min_size)
        return true;

    aligned_begin = HUGE_PAGE_ROUND_DOWN(spec->pt, me->physical_address);
    aligned_begin = MAX(ctx->direct_map_min_size, aligned_begin);
    page_count = (aligned_end - aligned_begin) >> huge_page_shift(spec->pt);

    print_info("mapping high memory: 0x%016llX -> 0x%016llX (%zu pages)\n",
               aligned_begin, aligned_end, page_count);

    spec->virtual_base = aligned_begin;
    spec->physical_base = aligned_begin;
    spec->count = page_count;

    if (ctx->map_lower)
        map_pages(spec);

    spec->virtual_base += ctx->direct_map_base;
    map_pages(spec);

    return true;
}

/*
 * Always map the first 2/4MiB of physical memory with small pages.
 *
 * This makes it so our null guard page is always small so that the
 * guest kernel has access to all the physical memory above 4K.
 *
 * On x86, we also do this to avoid accidentally crossing any MTRR
 * boundaries with different cache types in the lower MiB.
 *
 * Intel® 64 and IA-32 Architectures Software Developer’s Manual:
 *
 * The Pentium 4, Intel Xeon, and P6 family processors provide special support
 * for the physical memory range from 0 to 4 MBytes, which is potentially mapped
 * by both the fixed and variable MTRRs. This support is invoked when a
 * Pentium 4, Intel Xeon, or P6 family processor detects a large page
 * overlapping the first 1 MByte of this memory range with a memory type that
 * conflicts with the fixed MTRRs. Here, the processor maps the memory range as
 * multiple 4-KByte pages within the TLB. This operation ensures correct
 * behavior at the cost of performance. To avoid this performance penalty,
 * operating-system software should reserve the large page option for regions
 * of memory at addresses greater than or equal to 4 MBytes.
 */
static void map_lower_huge_page(struct page_mapping_spec *spec, bool null_guard)
{
    size_t old_count = spec->count;
    size_t size_to_map = huge_page_size(spec->pt);

    spec->type = PAGE_TYPE_NORMAL;
    spec->physical_base = 0x0000000000000000;

    if (null_guard) {
        spec->physical_base += PAGE_SIZE;
        spec->virtual_base += PAGE_SIZE;
        size_to_map -= PAGE_SIZE;
    }
    spec->count = size_to_map >> PAGE_SHIFT;

    map_pages(spec);

    spec->type = PAGE_TYPE_HUGE;
    spec->physical_base += size_to_map;
    spec->virtual_base += size_to_map;
    spec->count = old_count - 1;
}

static void do_build_page_table(struct kernel_info *ki, enum pt_type type,
                                bool higher_half_exclusive, bool null_guard) {
    struct handover_info *hi = &ki->hi;
    struct elf_binary_info *bi = &ki->bin_info;
    u64 hh_base;

    struct page_mapping_spec spec = {
        .pt = &hi->pt,
        .type = PAGE_TYPE_HUGE,
        .critical = true,
    };
    struct page_mapper_ctx ctx = {
        .spec = &spec,
        .direct_map_base = hi->direct_map_base,
        .map_lower = !higher_half_exclusive,
    };
    u8 hp_shift;

    hh_base = ultra_higher_half_base(hi->flags);
    page_table_init(
        spec.pt, type,
        handover_get_max_pt_address(ctx.direct_map_base, hi->flags)
    );
    hp_shift = huge_page_shift(spec.pt);

    ctx.direct_map_min_size =
            handover_get_minimum_map_length(ctx.direct_map_base, hi->flags);
    ctx.direct_map_min_size =
            ultra_adjust_direct_map_min_size(ctx.direct_map_min_size,
                                             hi->flags);

    // Direct map higher half
    spec.virtual_base = ctx.direct_map_base;
    spec.count = ctx.direct_map_min_size >> hp_shift;

    map_lower_huge_page(&spec, false);
    map_pages(&spec);

    if (ctx.map_lower) {
        spec.virtual_base = 0x0000000000000000;
        spec.count =
            ultra_adjust_direct_map_min_size_for_lower_half(
                ctx.direct_map_min_size, hi->flags
            ) >> hp_shift;

        map_lower_huge_page(&spec, null_guard);
        map_pages(&spec);
    } else {
        u64 root_cov, off;
        root_cov = pt_level_entry_virtual_coverage(spec.pt,
                                                   spec.pt->levels - 1);

        // Steal the direct mapping from higher half, we're gonna unmap it later
        for (off = 0; off < ctx.direct_map_min_size; off += root_cov) {
            map_copy_root_entry(spec.pt, ctx.direct_map_base + off,
                                         0x0000000000000000  + off);
        }
    }

    if (ultra_should_map_high_memory(hi->flags))
        mm_foreach_entry(do_map_high_memory, &ctx);

    /*
     * If kernel had allocate-anywhere set to on, map virtual base to physical
     * base, otherwise simply direct map fist N gigabytes of physical.
     */
    if (ki->bin_opts.allocate_anywhere) {
        spec.physical_base = bi->physical_base;
        spec.virtual_base = bi->virtual_base;

        spec.count = PAGE_ROUND_UP(bi->physical_ceiling - bi->physical_base);
        spec.count >>= PAGE_SHIFT;

        spec.type = PAGE_TYPE_NORMAL;
        map_pages(&spec);
    } else if (hh_base != ctx.direct_map_base) {
        spec.virtual_base = hh_base;
        spec.count = ultra_higher_half_size(hi->flags);
        spec.count >>= huge_page_shift(spec.pt);

        map_lower_huge_page(&spec, false);
        map_pages(&spec);
    }
}

static void build_page_table(struct config *cfg, struct loadable_entry *le,
                             struct attribute_array_spec *spec)
{
    struct kernel_info *ki = &spec->kern_info;
    struct handover_info *hi = &ki->hi;
    bool is_higher_half_exclusive = false, null_guard = false;
    u64 pt_levels = 4;
    struct string_view constraint_str = SV("maximum");
    enum pt_constraint constraint = PT_CONSTRAINT_MAX;
    enum pt_type type;
    struct value pt_val;

    cfg_get_bool(cfg, le, SV("higher-half-exclusive"),
                 &is_higher_half_exclusive);

    if (!ki->is_higher_half && is_higher_half_exclusive)
        oops("higher half exclusive mode is only allowed for "
             "higher half kernels\n");

    if (is_higher_half_exclusive) {
        spec->higher_half_pointers = true;
        hi->flags |= HO_HIGHER_HALF_ONLY;
    }

    if (cfg_get_object(cfg, le, SV("page-table"), &pt_val)) {
        cfg_get_unsigned(cfg, &pt_val, SV("levels"), &pt_levels);
        cfg_get_bool(cfg, &pt_val, SV("null-guard"), &null_guard);
        cfg_get_string(cfg, &pt_val, SV("constraint"), &constraint_str);

        if (sv_equals_caseless(constraint_str, SV("maximum")))
            constraint = PT_CONSTRAINT_MAX;
        else if (sv_equals_caseless(constraint_str, SV("exactly")))
            constraint = PT_CONSTRAINT_EXACTLY;
        else if (sv_equals_caseless(constraint_str, SV("at-least")))
            constraint = PT_CONSTRAINT_AT_LEAST;
        else
            oops("invalid page-table constraint '%pSV'\n", &constraint_str);
    }

    if (!ultra_configure_pt_type(hi, pt_levels, constraint, &type))
        goto out_failed_constraint;

    spec->page_table_depth = pt_depth(type);
    if (pt_levels < spec->page_table_depth &&
        constraint != PT_CONSTRAINT_AT_LEAST) {
        oops("invalid page-table levels value %llu, expected minimum %d\n",
             pt_levels, spec->page_table_depth);
    }

    hi->direct_map_base = ultra_direct_map_base(hi->flags);
    do_build_page_table(ki, type, is_higher_half_exclusive, null_guard);

    return;

out_failed_constraint:
    oops("failed to satisfy page-table constraint '%pSV', "
         "%llu levels not supported\n", &constraint_str, pt_levels);
}

NORETURN
static void ultra_protocol_boot(struct config *cfg, struct loadable_entry *le)
{
    char cmdline_buf[MAX_CMDLINE_LEN];
    struct attribute_array_spec spec = { 0 };
    struct kernel_info *ki = &spec.kern_info;
    struct handover_info *hi = &ki->hi;
    u64 attr_arr_addr;

    dynamic_buffer_init(&spec.module_buf,
                        sizeof(struct ultra_module_info_attribute), true);

    load_kernel(cfg, le, ki);
    build_page_table(cfg, le, &spec);

    spec.cmdline_present = get_cmdline(cfg, le, cmdline_buf, &spec.cmdline);

    load_kernel_as_module(cfg, le, &spec);
    load_all_modules(cfg, le, &spec);
    allocate_stack(cfg, le, hi);
    spec.acpi_rsdp_address = services_find_rsdp();
    spec.dtb_address = services_find_dtb();
    spec.smbios_address = services_find_smbios();

    spec.apm_info_present = apm_setup(cfg, le, &spec.apm_info);

   /*
    * Attempt to set video mode last, as we're not going to be able to use
    * legacy tty logging after that.
    */
    spec.fb_present = set_video_mode(cfg, le, &spec.fb);

    // NOTE: no services must be used after this aside from memory allocation
    cfg_release(cfg);
    services_cleanup();

    handover_prepare_for(hi);

    /*
     * This also acquires the memory map, so we can no longer use
     * any services after this call.
     */
    attr_arr_addr = build_attribute_array(&spec,
                                          ultra_max_binary_address(hi->flags));

    if (ki->is_higher_half) {
        hi->stack += hi->direct_map_base;
        attr_arr_addr += hi->direct_map_base;
    }

    hi->arg0 = attr_arr_addr;
    hi->arg1 = ULTRA_MAGIC;

    print_info("jumping to kernel: entry 0x%016llX, stack at 0x%016llX, boot "
               "context at 0x%016llX\n", hi->entrypoint, hi->stack,
               attr_arr_addr);

    kernel_handover(hi);
}

static u64 ultra_known_mm_types[] = {
    MEMORY_TYPE_FREE,
    MEMORY_TYPE_RESERVED,
    MEMORY_TYPE_ACPI_RECLAIMABLE,
    MEMORY_TYPE_NVS,
    MEMORY_TYPE_LOADER_RECLAIMABLE,
    MEMORY_TYPE_INVALID,
};

static struct boot_protocol ultra_boot_protocol = {
    .name = SV("ultra"),
    .boot = ultra_protocol_boot,
    .known_mm_types = ultra_known_mm_types
};
DECLARE_BOOT_PROTOCOL(ultra_boot_protocol);
