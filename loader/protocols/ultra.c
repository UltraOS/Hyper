#include "ultra.h"
#include "elf/elf.h"
#include "common/bug.h"
#include "common/cpuid.h"
#include "common/helpers.h"
#include "common/constants.h"
#include "common/format.h"
#include "common/log.h"
#include "filesystem/filesystem_table.h"
#include "allocator.h"
#include "virtual_memory.h"
#include "handover.h"

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
        CFG_MANDATORY_GET(string, cfg, le, SV("path"), &string_path);
        opts->allocate_anywhere = false;
        cfg_get_bool(cfg, le, SV("load-anywhere"), &opts->allocate_anywhere);
    } else {
        string_path = binary_val.as_string;
    }

    if (!parse_path(string_path, &opts->path))
        oops("invalid binary path %pSV", &string_path);
}

static void module_load(struct config *cfg, struct value *module_value, struct module_info_attribute *attrs)
{
    struct full_path path;
    const struct fs_entry *fse;
    struct string_view str_path, module_name = { 0 };
    struct file *module_file;
    size_t file_pages;
    void *module_data;

    static int module_idx = 0;
    ++module_idx;

    if (value_is_object(module_value)) {
        cfg_get_string(cfg, module_value, SV("name"), &module_name);
        CFG_MANDATORY_GET(string, cfg, module_value, SV("path"), &str_path);
    } else {
        str_path = module_value->as_string;
    }

    if (sv_empty(module_name))
        snprintf(attrs->name, sizeof(attrs->name), "unnamed_module%d", module_idx);

    if (!parse_path(str_path, &path))
        oops("invalid module path %pSV", &str_path);

    fse = fs_by_full_path(&path);
    if (!fse)
        oops("invalid module path %pSV", &str_path);

    module_file = fse->fs->open(fse->fs, path.path_within_partition);
    if (!module_file)
        oops("invalid module path %pSV", &str_path);

    file_pages = CEILING_DIVIDE(module_file->size, PAGE_SIZE);
    module_data = allocate_critical_pages_with_type(file_pages, MEMORY_TYPE_MODULE);

    if (!module_file->read(module_file, module_data, 0, module_file->size))
        oops("failed to read module file");

    *attrs = (struct module_info_attribute) {
        .header = { ATTRIBUTE_MODULE_INFO, sizeof(struct module_info_attribute) },
        .physical_address = (ptr_t)module_data,
        .length = module_file->size
    };

    fse->fs->close(fse->fs, module_file);
}

void load_kernel(struct config *cfg, struct loadable_entry *entry, struct binary_info *info)
{
    struct binary_options opts;
    const struct fs_entry *fse;
    struct file *f;
    void *file_data;
    u8 bitness;
    struct load_result res;

    get_binary_options(cfg, entry, &opts);
    fse = fs_by_full_path(&opts.path);

    f = fse->fs->open(fse->fs, opts.path.path_within_partition);
    if (!f)
        oops("failed to open %pSV", &opts.path.path_within_partition);

    file_data = allocate_critical_bytes(f->size);

    if (!f->read(f, file_data, 0, f->size))
        oops("failed to read file");

    bitness = elf_bitness(file_data, f->size);

    if (!bitness || (bitness != 32 && bitness != 64))
        oops("invalid ELF bitness");

    if (opts.allocate_anywhere && bitness != 64)
        oops("allocate-anywhere is only allowed for 64 bit kernels");

    if (bitness == 64 && !cpu_supports_long_mode())
        oops("attempted to load a 64 bit kernel on a CPU without long mode support");

    if (!elf_load(file_data, f->size, bitness == 64, opts.allocate_anywhere, &res))
        oops("failed to load kernel binary: %pSV", &res.error_msg);

    *info = res.info;
}

enum video_mode_constraint {
    VIDEO_MODE_CONSTRAINT_EXACTLY,
    VIDEO_MODE_CONSTRAINT_AT_LEAST,
};

struct requested_video_mode {
    u32 width, height, bpp;
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
            oops("invalid value for \"video-mode\": %pSV", &val->as_string);

        return;
    }

    if (cfg_get_unsigned(cfg, val, SV("width"), &cfg_width))
        mode->width = cfg_width;
    if (cfg_get_unsigned(cfg, val, SV("height"), &cfg_height))
        mode->height = cfg_height;
    if (cfg_get_unsigned(cfg, val, SV("bpp"), &cfg_bpp))
        mode->bpp = cfg_bpp;

    if (cfg_get_string(cfg, val, SV("constraint"), &constraint_str)) {
        if (sv_equals(constraint_str, SV("at-least")))
            mode->constraint = VIDEO_MODE_CONSTRAINT_AT_LEAST;
        else if (sv_equals(constraint_str, SV("exactly")))
            mode->constraint = VIDEO_MODE_CONSTRAINT_EXACTLY;
        else
            oops("invalid video mode constraint %pSV", &constraint_str);
    }
}

#define DEFAULT_WIDTH 1024
#define DEFAULT_HEIGHT 768
#define DEFAULT_BPP 32

void set_video_mode(struct config *cfg, struct loadable_entry *entry,
                    struct video_services *vs, struct framebuffer *out_fb)
{
    struct value video_mode_val;
    struct video_mode picked_vm, *mode_list;
    size_t mode_count, mode_idx;
    bool did_pick = false;
    struct resolution native_res;
    struct requested_video_mode rm = {
        .width = DEFAULT_WIDTH,
        .height = DEFAULT_HEIGHT,
        .bpp = DEFAULT_BPP,
        .constraint = VIDEO_MODE_CONSTRAINT_AT_LEAST
    };

    if (cfg_get_one_of(cfg, entry, SV("video-mode"), VALUE_OBJECT | VALUE_STRING | VALUE_NONE,
                       &video_mode_val)) {
        video_mode_from_value(cfg, &video_mode_val, &rm);
    }

    if (rm.none)
        return;

    // Assume defaults if query fails
    if (!vs->query_resolution(&native_res))
        native_res = (struct resolution) { DEFAULT_WIDTH, DEFAULT_HEIGHT };

    mode_list = vs->list_modes(&mode_count);

    for (mode_idx = 0; mode_idx < mode_count; ++mode_idx) {
        struct video_mode *m = &mode_list[mode_idx];

        if (rm.constraint == VIDEO_MODE_CONSTRAINT_EXACTLY && VM_EQUALS(*m, rm)) {
            picked_vm = *m;
            did_pick = true;
            break;
        }

        if (VM_GREATER_OR_EQUAL(*m, rm) && VM_LESS_OR_EQUAL(*m, native_res)) {
            picked_vm = *m;
            did_pick = true;
        }
    }

    if (!did_pick) {
        oops("failed to pick a video mode according to constraints (%ux%u %u bpp)",
             rm.width, rm.height, rm.bpp);
    }

    print_info("picked video mode %ux%u %u bpp", picked_vm.width, picked_vm.height, picked_vm.bpp);

    if (!vs->set_mode(picked_vm.id, out_fb))
        oops("failed to set picked video mode");
}

struct attribute_array_spec {
    struct framebuffer fb;
    struct binary_info bin_info;
    u64 stack_addr;
    struct module_info_attribute *modules;
    size_t module_count;
    struct string_view cmdline;
};

struct handover_info {
    size_t memory_map_handover_key;
    u32 attribute_array_address;
};
#define LOAD_NAME_STRING "HyperLoader v0.1"

void build_attribute_array(const struct attribute_array_spec *spec, struct memory_services *ms,
                           struct handover_info *hi)
{
    struct platform_info_attribute pi_attr = {
        .header = { ATTRIBUTE_PLATFORM_INFO, sizeof(pi_attr) },
        .platform_type = PLATFORM_BIOS, // add a way to detect later
        .loader_major = 0,
        .loader_minor = 1,
    };
    struct string_view loader_name_str = SV(LOAD_NAME_STRING);

    struct framebuffer_attribute fb_attr = {
        .header = { ATTRIBUTE_FRAMEBUFFER_INFO, sizeof(fb_attr) },
        .fb = spec->fb
    };
    struct command_line_attribute cmdline_attr = {
        .header = { ATTRIBUTE_COMMAND_LINE, 0 },
        .text_length = spec->cmdline.size
    };
    u32 cmdline_aligned_length = sizeof(struct attribute_header);
    size_t memory_map_reserved_size, bytes_copied, bytes_needed = 0;
    void *attr_ptr;

    struct memory_map_attribute mm_attribute = {
        .header = { ATTRIBUTE_MEMORY_MAP, 0 }
    };

    memcpy(pi_attr.loader_name, loader_name_str.text, loader_name_str.size + 1);

    if (!sv_empty(spec->cmdline)) {
        cmdline_aligned_length += spec->cmdline.size;
        size_t remainder = cmdline_aligned_length % 8;

        if (remainder)
            cmdline_aligned_length += 8 - remainder;
    }
    cmdline_attr.header.size_in_bytes = cmdline_aligned_length;

    bytes_needed += sizeof(struct platform_info_attribute);
    bytes_needed += sizeof(struct kernel_info_attribute);
    bytes_needed += spec->module_count * sizeof(struct module_info_attribute);
    bytes_needed += cmdline_aligned_length;
    bytes_needed += sizeof(struct framebuffer_attribute);

    // Attempt to allocate the storage for attribute array while having enough space for the memory map
    // (which is changed every time we allocate/free more memory)
    for (;;) {
        size_t memory_map_size_new, key = 0;
        memory_map_reserved_size = ms->copy_map(NULL, 0, &key);

        // give some leeway for memory map growth after the next allocation
        memory_map_reserved_size += sizeof(struct memory_map_entry);

        hi->attribute_array_address = (u32)allocate_critical_bytes(bytes_needed + memory_map_reserved_size);

        // Check if memory map had to grow to store the previous allocation
        memory_map_size_new = ms->copy_map(NULL, 0, &key);
        if (memory_map_reserved_size >= memory_map_size_new)
            break;

        free_bytes((void*)hi->attribute_array_address, bytes_needed + memory_map_reserved_size);
    }

    attr_ptr = (void*)hi->attribute_array_address;

    memcpy(attr_ptr, &pi_attr, sizeof(struct platform_info_attribute));
    attr_ptr += sizeof(struct platform_info_attribute);

    if (spec->module_count) {
        size_t bytes_for_modules = spec->module_count * sizeof(struct module_info_attribute);
        memcpy(attr_ptr, spec->modules, bytes_for_modules);
        attr_ptr += bytes_for_modules;
    }

    // TODO: consider not copying this if cmdline is empty
    memcpy(attr_ptr, &cmdline_attr, sizeof(struct command_line_attribute));

    // TODO: null terminate
    if (spec->cmdline.size)
        memcpy(attr_ptr + sizeof(struct command_line_attribute), spec->cmdline.text, spec->cmdline.size);

    attr_ptr += cmdline_attr.header.size_in_bytes;

    // TODO: consider not copying this if video-mode was not requested
    memcpy(attr_ptr, &fb_attr, sizeof(struct framebuffer_attribute));
    attr_ptr += sizeof(struct framebuffer_attribute);

    memcpy(attr_ptr, &mm_attribute, sizeof(struct memory_map_attribute));
    attr_ptr += sizeof(struct memory_map_attribute);

    bytes_copied = ms->copy_map(attr_ptr, memory_map_reserved_size, &hi->memory_map_handover_key);

    ((struct memory_map_attribute*)attr_ptr)->header.size_in_bytes = bytes_copied + sizeof(struct memory_map_attribute);
    attr_ptr += bytes_copied;
}

u64 build_page_table(struct binary_info *bi)
{
    struct page_table pt = {
        .root = (u64*)allocate_critical_pages(1),
        .levels = 4
    };

    if (bi->bitness != 64)
        return 0;

    // identity map bottom 4 gigabytes
    map_critical_huge_pages(&pt, 0x0000000000000000, 0x0000000000000000,
                            (4ull * GB) / HUGE_PAGE_SIZE);

    // direct map higher half
    map_critical_huge_pages(&pt, 0xFFFF800000000000 , 0x0000000000000000,
                            (4ull * GB) / HUGE_PAGE_SIZE);

    /*
     * If kernel had allocate-anywhere set to on, map virtual base to physical base,
     * otherwise simply direct map fist 2 gigabytes of physical.
     */
    if (bi->physical_valid) {
        size_t pages = bi->physical_ceiling - bi->physical_base;
        pages = CEILING_DIVIDE(pages, HUGE_PAGE_SIZE);
        map_critical_huge_pages(&pt, bi->virtual_base, bi->physical_base, pages);
    } else {
        map_critical_huge_pages(&pt, 0xFFFFFFFF80000000, 0x0000000000000000,
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
                oops("invalid value for \"allocate-at\": %pSV", &alloc_at_val.as_string);
        } else if (has_alloc_at) { // unsigned
            address = alloc_at_val.as_unsigned;
        }

        if (has_size && value_is_string(&size_val)) {
            if (!sv_equals(size_val.as_string, SV("auto")))
                oops("invalid value for \"size\": %pSV", &size_val.as_string);
        } else if (has_size) { // unsigned
            size = size_val.as_unsigned;
        }
    } else if (has_val) { // string
        if (!sv_equals(val.as_string, SV("auto")))
            oops("invalid value for \"stack\": %pSV", &val.as_string);
    }

    size_t pages = CEILING_DIVIDE(size, PAGE_SIZE);

    if (address) {
        allocate_critical_pages_with_type_at(address, pages, MEMORY_TYPE_KERNEL_STACK);
        return address;
    }

    return (ptr_t)allocate_critical_pages_with_type(pages, MEMORY_TYPE_KERNEL_STACK);
}

#define MODULES_PER_PAGE (PAGE_SIZE / sizeof(struct module_info_attribute))

void ultra_protocol_load(struct config *cfg, struct loadable_entry *le, struct services *sv)
{
    struct attribute_array_spec spec = { 0 };
    size_t modules_capacity = MODULES_PER_PAGE;
    spec.modules = allocate_critical_pages(1);
    struct value module_value;
    struct handover_info hi;
    u64 pt;
    bool handover_res;

    load_kernel(cfg, le, &spec.bin_info);

    cfg_get_string(cfg, le, SV("cmdline"), &spec.cmdline);

    if (cfg_get_first_one_of(cfg, le, SV("module"), VALUE_STRING | VALUE_OBJECT, &module_value)) {
        do {
            if (++spec.module_count == modules_capacity) {
                void *new_modules = allocate_critical_pages(modules_capacity + MODULES_PER_PAGE);
                memcpy(new_modules, spec.modules, modules_capacity);
                free_pages(spec.modules, (modules_capacity * sizeof(struct module_info_attribute)) / PAGE_SIZE);
                spec.modules = new_modules;
                modules_capacity += MODULES_PER_PAGE;
            }

            module_load(cfg, &module_value, &spec.modules[spec.module_count - 1]);
        } while (cfg_get_next_one_of(cfg, VALUE_STRING | VALUE_OBJECT, &module_value, true));
    }

    pt = build_page_table(&spec.bin_info);
    spec.stack_addr = pick_stack(cfg, le);

    /*
     * We cannot allocate any memory after this call, as memory map is now
     * saved inside the attribute array.
     */
    build_attribute_array(&spec, sv->ms, &hi);
    handover_res = sv->ms->handover(hi.memory_map_handover_key);
    BUG_ON(!handover_res);

    /*
     * Attempt to set video mode last, as we're not going to be able to use
     * legacy tty logging after that.
     */
    set_video_mode(cfg, le, sv->vs, &spec.fb);

    if (spec.bin_info.bitness == 32)
        kernel_handover32(spec.bin_info.entrypoint_address, spec.stack_addr, hi.attribute_array_address, ULTRA_MAGIC);

    kernel_handover64(spec.bin_info.entrypoint_address, spec.stack_addr, pt, hi.attribute_array_address, ULTRA_MAGIC);
}
