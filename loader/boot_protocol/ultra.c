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
    opts->loc = fse->loc;
}

/*
 * A module is emitted as a fixed ultra_module_info_attribute optionally followed
 * by a variable length, NUL-terminated description. The modules are collected
 * into a scratch buffer before the final attribute array is sized, so keep the
 * (already copied out) description alongside each attribute until then.
 */
struct pending_module {
    struct string_view description;

    // Keep last: its trailing description[] flexible array stays unused here.
    struct ultra_module_info_attribute attr;
};

/*
 * Like the command line, the protocol imposes no limit on the description, but
 * cap it at something sane so the aligned attribute size stays well within the
 * uint32_t header.size field.
 */
#define MAX_MODULE_DESCRIPTION_LEN (128 * MB)

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

/*
 * The description outlives cfg_release(), so copy it into loader-reclaimable
 * storage now (like the command line does) instead of holding a reference into
 * the soon-to-be-freed configuration. An absent or empty description yields a
 * zeroed string_view, which makes the module emit no description at all.
 */
static struct string_view module_get_description(struct config *cfg,
                                                 struct value *module_value)
{
    struct string_view desc;
    char *storage;

    if (!cfg_get_string(cfg, module_value, SV("description"), &desc) ||
        sv_empty(desc))
        return (struct string_view) { 0 };

    if (desc.size > MAX_MODULE_DESCRIPTION_LEN)
        oops("module description is too big %zu vs max %zu\n",
             desc.size, (size_t)MAX_MODULE_DESCRIPTION_LEN);

    storage = allocate_critical_bytes(desc.size);
    memcpy(storage, desc.text, desc.size);

    return (struct string_view) { storage, desc.size };
}

static void module_load(struct config *cfg, struct value *module_value,
                        struct pending_module *pm, u64 ceiling)
{
    struct ultra_module_info_attribute *attrs = &pm->attr;
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
        pm->description = module_get_description(cfg, module_value);
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

struct requested_video_mode {
    u32 width, height, bpp;
    u16 format;

    // Whether the config pinned an exact resolution / bpp
    bool has_resolution;
    bool has_bpp;

    // No framebuffer wanted at all
    bool none;
};

#define VIDEO_MODE_KEY SV("video-mode")

static void video_mode_from_value(struct config *cfg, struct value *val,
                                  struct requested_video_mode *mode)
{
    u64 cfg_width = 0, cfg_height = 0, cfg_bpp;
    bool got_width, got_height;
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

    got_width = cfg_get_unsigned(cfg, val, SV("width"), &cfg_width);
    got_height = cfg_get_unsigned(cfg, val, SV("height"), &cfg_height);
    if (got_width != got_height)
        oops("video-mode requires both width and height, or neither\n");

    if (got_width) {
        mode->width = cfg_width;
        mode->height = cfg_height;
        mode->has_resolution = true;
    }

    if (cfg_get_unsigned(cfg, val, SV("bpp"), &cfg_bpp)) {
        mode->bpp = cfg_bpp;
        mode->has_bpp = true;
    }

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
}

#define DEFAULT_BPP 32

// Prefer the mode with the largest area, breaking ties by higher bpp.
static bool vm_is_preferable(const struct video_mode *m,
                             const struct video_mode *best)
{
    u64 m_area = (u64)m->width * m->height;
    u64 best_area = (u64)best->width * best->height;

    if (m_area != best_area)
        return m_area > best_area;

    return m->bpp > best->bpp;
}

/*
 * Pick the "largest" available mode (by area, then bpp), optionally capped to a
 * maximum resolution and/or filtered by an exact format or bpp. A zero cap or
 * FB_FORMAT_INVALID format means no constraint.
 */
static bool pick_largest_mode(u32 max_width, u32 max_height, u16 want_format,
                              u32 want_bpp, struct video_mode *out)
{
    size_t i, count = vs_get_mode_count();
    struct video_mode best = { 0 };
    bool found = false;

    for (i = 0; i < count; ++i) {
        struct video_mode m;
        vs_query_mode(i, &m);

        if (want_format != FB_FORMAT_INVALID && m.format != want_format)
            continue;
        if (want_bpp && m.bpp != want_bpp)
            continue;
        if (max_width && (m.width > max_width || m.height > max_height))
            continue;

        if (found && !vm_is_preferable(&m, &best))
            continue;

        best = m;
        found = true;
    }

    *out = best;
    return found;
}

static bool pick_exact_mode(const struct requested_video_mode *rm,
                            struct video_mode *out)
{
    size_t i, count = vs_get_mode_count();
    struct video_mode best = { 0 };
    bool found = false;

    for (i = 0; i < count; ++i) {
        struct video_mode m;
        vs_query_mode(i, &m);

        if (m.width != rm->width || m.height != rm->height)
            continue;
        if (rm->format != FB_FORMAT_INVALID && m.format != rm->format)
            continue;
        if (rm->has_bpp && m.bpp != rm->bpp)
            continue;

        // Among modes of the requested resolution prefer the highest bpp
        if (found && m.bpp <= best.bpp)
            continue;

        best = m;
        found = true;
    }

    *out = best;
    return found;
}

/*
 * Auto mode policy, used when the config doesn't pin an exact resolution:
 *   - if the display's native resolution is known (EDID), use the largest mode
 *     that fits within it;
 *   - otherwise, if the firmware already has an active framebuffer (e.g. the
 *     UEFI GOP, usually already at native), keep it as is;
 *   - otherwise (e.g. BIOS, which boots in text mode), pick the largest mode.
 */
static bool pick_auto_mode(const struct requested_video_mode *rm,
                           struct video_mode *out)
{
    u32 want_bpp = rm->has_bpp ? rm->bpp : 0;
    struct resolution native;

    if (vs_query_native_resolution(&native) &&
        pick_largest_mode(native.width, native.height, rm->format, want_bpp,
                          out))
        return true;

    if (!rm->has_bpp && rm->format == FB_FORMAT_INVALID &&
        vs_get_current_mode(out))
        return true;

    return pick_largest_mode(0, 0, rm->format, want_bpp, out);
}

static bool set_video_mode(struct config *cfg, struct loadable_entry *entry,
                           struct ultra_framebuffer *out_fb)
{
    struct value video_mode_val;
    struct video_mode picked;
    struct requested_video_mode rm = {
        .bpp = DEFAULT_BPP,
        .format = FB_FORMAT_INVALID,
    };
    struct framebuffer fb;
    bool picked_ok;

    if (cfg_get_one_of(cfg, entry, VIDEO_MODE_KEY,
                       VALUE_OBJECT | VALUE_STRING | VALUE_NONE,
                       &video_mode_val)) {
        video_mode_from_value(cfg, &video_mode_val, &rm);
    }

    if (rm.none)
        return false;

    if (rm.has_resolution) {
        picked_ok = pick_exact_mode(&rm, &picked);
        if (!picked_ok)
            oops("no video mode matching %ux%u available\n",
                 rm.width, rm.height);
    } else {
        picked_ok = pick_auto_mode(&rm, &picked);
        if (!picked_ok)
            oops("no usable video mode available\n");
    }

    print_info("picked video mode %ux%u @ %u bpp\n",
               picked.width, picked.height, picked.bpp);

    if (!vs_set_mode(picked.id, &fb))
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

/*
 * The loader and protocol GUID/IP types are deliberately kept layout-identical
 * so the fs_entry fields can be copied into the attribute verbatim. Guard that
 * assumption here instead of relying on it silently.
 */
BUILD_BUG_ON(sizeof(struct guid) != sizeof(struct ultra_guid));
BUILD_BUG_ON(offsetof(struct guid, data1) != offsetof(struct ultra_guid, data1));
BUILD_BUG_ON(offsetof(struct guid, data2) != offsetof(struct ultra_guid, data2));
BUILD_BUG_ON(offsetof(struct guid, data3) != offsetof(struct ultra_guid, data3));
BUILD_BUG_ON(offsetof(struct guid, data4) != offsetof(struct ultra_guid, data4));

BUILD_BUG_ON(sizeof(ipv4_addr) != sizeof(struct ultra_ipv4_addr));
BUILD_BUG_ON(sizeof(ipv6_addr) != sizeof(struct ultra_ipv6_addr));

static void*
write_kernel_info_attribute(struct ultra_kernel_info_attribute *attr,
                            const struct kernel_info *ki)
{
    const struct binary_options *bo = &ki->bin_opts;
    struct string_view path_str = bo->path.path_within_partition;
    const struct fs_location *loc = &bo->loc;

    attr->header = (struct ultra_attribute_header) {
        .type = ULTRA_ATTRIBUTE_KERNEL_INFO,
        .size = sizeof(struct ultra_kernel_info_attribute)
    };
    attr->physical_base = ki->bin_info.physical_base;
    attr->virtual_base = ki->bin_info.virtual_base;
    attr->size = ki->bin_info.physical_ceiling - ki->bin_info.physical_base;

    attr->disk_index = loc->disk_id;
    attr->partition_index = loc->partition_index;

    switch (loc->entry_type) {
    case FSE_TYPE_RAW:
        attr->partition_type = ULTRA_PARTITION_TYPE_RAW;
        break;
    case FSE_TYPE_MBR:
        attr->partition_type = ULTRA_PARTITION_TYPE_MBR;
        break;
    case FSE_TYPE_GPT:
        attr->partition_type = ULTRA_PARTITION_TYPE_GPT;
        memcpy(&attr->disk_guid, &loc->disk_guid, sizeof(attr->disk_guid));
        memcpy(&attr->partition_guid, &loc->partition_guid,
               sizeof(attr->partition_guid));
        break;
    case FSE_TYPE_PXE:
        if (loc->ip.type == IP_TYPE_V4) {
            memcpy(&attr->pxe_v4, &loc->ip.v4, IPV4_ADDR_LEN);
            attr->partition_type = ULTRA_PARTITION_TYPE_PXE_V4;
        } else {
            memcpy(&attr->pxe_v6, &loc->ip.v6, IPV6_ADDR_LEN);
            attr->partition_type = ULTRA_PARTITION_TYPE_PXE_V6;
        }
        break;
    default:
        BUG();
    }

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

static struct pending_module *module_at(const struct attribute_array_spec *spec,
                                        size_t i)
{
    return dynamic_buffer_get_slot(&spec->module_buf, i);
}

/*
 * On-array size of a single module attribute: the fixed struct plus, if a
 * description is present, its bytes and a terminating NUL, aligned up to 8 so
 * the next attribute stays aligned. A module without a description keeps the
 * bare struct size so ULTRA_MODULE_HAS_DESCRIPTION() reports false for it.
 */
static u32 module_attr_size(const struct pending_module *pm)
{
    size_t size = sizeof(struct ultra_module_info_attribute);

    if (pm->description.size)
        size += pm->description.size + 1;

    return ALIGN_UP(size, 8);
}

static void *write_module_info(void *attr_ptr, const struct pending_module *pm)
{
    struct ultra_module_info_attribute *attr = attr_ptr;
    u32 size = module_attr_size(pm);

    *attr = pm->attr;
    attr->header.size = size;

    // The array is pre-zeroed, so the alignment padding past the NUL stays zero.
    if (pm->description.size)
        sv_terminated_copy(attr->description, pm->description);

    return attr_ptr + size;
}

static ptr_t build_attribute_array(const struct attribute_array_spec *spec,
                                   u64 array_ceiling)
{
    u32 cmdline_aligned_length = 0;
    size_t i, mm_entry_count, pages_needed, bytes_needed = 0;
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
    for (i = 0; i < spec->module_buf.size; i++)
        bytes_needed += module_attr_size(module_at(spec, i));
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

    for (i = 0; i < spec->module_buf.size; i++) {
        attr_ptr = write_module_info(attr_ptr, module_at(spec, i));
        *attr_count += 1;
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

static struct pending_module *module_alloc(struct dynamic_buffer *buf)
{
    struct pending_module *pm;

    pm = dynamic_buffer_slot_alloc(buf);
    DIE_ON(!pm);

    *pm = (struct pending_module) {
        .attr.header = {
            ULTRA_ATTRIBUTE_MODULE_INFO,
            sizeof(struct ultra_module_info_attribute)
        },
    };
    return pm;
}

static void load_kernel_as_module(struct config *cfg, struct loadable_entry *le,
                                  struct attribute_array_spec *spec)
{
    bool kernel_as_module = false;
    struct pending_module *mi;
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
    mi->attr.type = ULTRA_MODULE_TYPE_FILE;
    mi->attr.address = (ptr_t)data;
    mi->attr.size = size;
    sv_terminated_copy(mi->attr.name, SV("__KERNEL__"));

    if (spec->higher_half_pointers)
        mi->attr.address += hi->direct_map_base;

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
        struct pending_module *mi;

        mi = module_alloc(&spec->module_buf);
        module_load(cfg, &module_value, mi, ultra_max_binary_address(hi->flags));

        if (spec->higher_half_pointers)
            mi->attr.address += hi->direct_map_base;
    } while (cfg_get_next_one_of(cfg, VALUE_STRING | VALUE_OBJECT,
                                 &module_value, true));
}

/*
 * The protocol itself imposes no limit on the command line length, but we still
 * cap it at something sane to avoid a runaway allocation and to keep the aligned
 * length (and therefore the attribute header size) comfortably within uint32_t.
 */
#define MAX_CMDLINE_LEN (128 * MB)

static bool get_cmdline(struct config *cfg, struct loadable_entry *le,
                        struct string_view *out_str)
{
    char *storage;

    if (!cfg_get_string(cfg, le, SV("cmdline"), out_str))
        return false;

    if (out_str->size > MAX_CMDLINE_LEN)
        oops("command line is too big %zu vs max %zu\n",
             out_str->size, (size_t)MAX_CMDLINE_LEN);

    /*
     * Copy the command line into a dynamically allocated buffer as we don't want
     * to keep a reference to a string inside the configuration file here since
     * it is freed before we build the attribute array. The storage is
     * loader-reclaimable, so the kernel gets it back once it has consumed the
     * command line attribute.
     */
    storage = allocate_critical_bytes(out_str->size + 1);
    memcpy(storage, out_str->text, out_str->size);
    storage[out_str->size] = '\0';

    out_str->text = storage;

    return true;
}

/*
 * Only conventional RAM is placed in the direct map. Reserved, device (MMIO)
 * and other non-RAM ranges are deliberately left out so we never map them as
 * writeback-cached; the kernel is expected to map such regions itself with the
 * appropriate caching type.
 */
static bool mme_is_ram(u64 type)
{
    switch (type) {
    case MEMORY_TYPE_FREE:
    case MEMORY_TYPE_ACPI_RECLAIMABLE:
    case MEMORY_TYPE_NVS:
        return true;
    default:
        /*
         * Loader-reclaimable memory and every protocol-specific type (modules,
         * kernel stack/binary, ...) is backed by conventional RAM.
         */
        return type >= MEMORY_TYPE_LOADER_RECLAIMABLE;
    }
}

struct direct_map_ctx {
    struct page_mapping_spec *spec;
    u64 direct_map_base;

    // Contiguous run of RAM entries accumulated so far, flushed on a gap
    u64 ram_begin;
    u64 ram_end;

    /*
     * 1 + the highest physical address each mapping is allowed to cover. The
     * i686 direct map for example can only reach 1 GiB, and its identity map
     * 3 GiB; on 64-bit these are effectively unbounded.
     */
    u64 higher_half_limit;
    u64 lower_half_limit;
};

/*
 * Map a physical range into the direct map (and the identity map, if a
 * lower-half limit is set) with the given page granularity.
 */
static void direct_map_one_range(struct direct_map_ctx *ctx, u64 begin,
                                 u64 end, enum page_type type)
{
    struct page_mapping_spec *spec = ctx->spec;
    u8 shift = type == PAGE_TYPE_HUGE ? huge_page_shift(spec->pt) : PAGE_SHIFT;

    if (end <= begin)
        return;

    spec->type = type;
    spec->physical_base = begin;

    if (begin < ctx->lower_half_limit) {
        spec->virtual_base = begin;
        spec->count = (MIN(end, ctx->lower_half_limit) - begin) >> shift;
        map_pages(spec);
    }

    if (begin < ctx->higher_half_limit) {
        spec->virtual_base = ctx->direct_map_base + begin;
        spec->count = (MIN(end, ctx->higher_half_limit) - begin) >> shift;
        map_pages(spec);
    }
}

/*
 * Map a single physical RAM range into the direct map (and the identity map,
 * if a lower-half limit is set). Huge pages only cover the aligned interior;
 * the unaligned edges fall back to small pages so the mapping never rounds
 * out over adjacent non-RAM memory, which may be device memory or not backed
 * at all. The first huge page is mapped separately with small pages, so
 * ranges are clipped to start above it here.
 */
static void direct_map_ram_range(struct direct_map_ctx *ctx, u64 begin, u64 end)
{
    struct page_table *pt = ctx->spec->pt;
    u64 huge_begin, huge_end;

    begin = MAX(begin, huge_page_size(pt));
    begin = PAGE_ROUND_DOWN(begin);
    end = PAGE_ROUND_UP(end);
    if (end <= begin)
        return;

    huge_begin = MIN(HUGE_PAGE_ROUND_UP(pt, begin), end);
    huge_end = MAX(HUGE_PAGE_ROUND_DOWN(pt, end), huge_begin);

    direct_map_one_range(ctx, begin, huge_begin, PAGE_TYPE_NORMAL);
    direct_map_one_range(ctx, huge_begin, huge_end, PAGE_TYPE_HUGE);
    direct_map_one_range(ctx, huge_end, end, PAGE_TYPE_NORMAL);
}

static bool direct_map_ram_entry(void *opaque, const struct memory_map_entry *me)
{
    struct direct_map_ctx *ctx = opaque;
    u64 begin, end;

    if (!mme_is_ram(me->type))
        return true;

    begin = me->physical_address;
    end = begin + me->size_in_bytes;

    /*
     * Coalesce contiguous RAM entries so that a type boundary inside a huge
     * page doesn't force both of its neighbors down to small pages.
     */
    if (ctx->ram_end == begin) {
        ctx->ram_end = end;
        return true;
    }

    direct_map_ram_range(ctx, ctx->ram_begin, ctx->ram_end);
    ctx->ram_begin = begin;
    ctx->ram_end = end;
    return true;
}

/*
 * The first huge page worth of physical memory is always mapped with small
 * pages: both to keep the NULL guard page small (so the kernel keeps access to
 * all RAM above 4K) and to avoid a huge page straddling the fixed MTRR ranges
 * in the low 1 MiB (see map_lower_huge_page's note below). It's mapped flat
 * rather than filtered by type as the legacy low memory is writeback anyway.
 */
static void map_low_small_pages(struct page_mapping_spec *spec,
                                u64 direct_map_base, bool map_lower,
                                bool null_guard)
{
    u64 hp = huge_page_size(spec->pt);

    spec->type = PAGE_TYPE_NORMAL;

    spec->physical_base = 0;
    spec->virtual_base = direct_map_base;
    spec->count = hp >> PAGE_SHIFT;
    map_pages(spec);

    if (!map_lower)
        return;

    spec->physical_base = null_guard ? PAGE_SIZE : 0;
    spec->virtual_base = spec->physical_base;
    spec->count = (hp - spec->physical_base) >> PAGE_SHIFT;
    map_pages(spec);
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
        .critical = true,
    };
    struct direct_map_ctx ctx = {
        .spec = &spec,
        .direct_map_base = hi->direct_map_base,
        .higher_half_limit = ultra_direct_map_max_size(hi->flags),
        .lower_half_limit = higher_half_exclusive ?
                                0 : ultra_identity_map_max_size(hi->flags),
    };

    hh_base = ultra_higher_half_base(hi->flags);
    page_table_init(
        spec.pt, type,
        handover_get_max_pt_address(ctx.direct_map_base, hi->flags)
    );

    /*
     * Map only the RAM regions from the memory map into the direct map (and,
     * unless higher-half-exclusive, the identity map), so reserved and device
     * memory never ends up writeback-cached in the direct map.
     */
    map_low_small_pages(&spec, ctx.direct_map_base, !higher_half_exclusive,
                        null_guard);
    mm_foreach_entry(direct_map_ram_entry, &ctx);
    direct_map_ram_range(&ctx, ctx.ram_begin, ctx.ram_end);

    if (higher_half_exclusive) {
        u64 root_cov = pt_level_entry_virtual_coverage(spec.pt,
                                                       spec.pt->levels - 1);
        u64 off, tramp_len = handover_get_minimum_map_length(ctx.direct_map_base,
                                                             hi->flags);

        /*
         * The handover trampoline runs identity mapped, so temporarily expose
         * the direct map's RAM at address zero by copying the root entries that
         * cover it. These are unmapped again right after control is handed off.
         */
        for (off = 0; off < tramp_len; off += root_cov) {
            map_copy_root_entry(spec.pt, ctx.direct_map_base + off,
                                         0x0000000000000000  + off);
        }
    }

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
    struct attribute_array_spec spec = { 0 };
    struct kernel_info *ki = &spec.kern_info;
    struct handover_info *hi = &ki->hi;
    u64 attr_arr_addr;

    dynamic_buffer_init(&spec.module_buf,
                        sizeof(struct pending_module), true);

    load_kernel(cfg, le, ki);
    build_page_table(cfg, le, &spec);

    spec.cmdline_present = get_cmdline(cfg, le, &spec.cmdline);

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
