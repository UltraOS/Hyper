#include "common/ctype.h"
#include "common/conversions.h"

#include "filesystem/path.h"

bool next_path_node(struct string_view *path, struct string_view *node)
{
    struct string_view sep = SV("/");
    ssize_t path_end;
    *node = *path;

    while (node->size && node->text[0] == '/')
        sv_offset_by(node, 1);

    if (!node->size)
        return false;

    path_end = sv_find(*node, sep, 0);
    if (path_end != -1) {
        const char *end = &node->text[path_end];
        path->size -= end - path->text;
        path->text = end;
        node->size = path_end;
    } else {
        path->size = 0;
    }

    return true;
}

static bool path_consume_numeric_sequence(struct string_view *str, u32 *out)
{
    struct string_view prefix_str = { str->text, 0 };

    for (;;) {
        char c;

        if (sv_empty(*str))
            break;

        c = tolower(str->text[0]);

        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')) {
            sv_extend_by(&prefix_str, 1);
            sv_offset_by(str, 1);
            continue;
        }

        break;
    }

    return !sv_empty(prefix_str) && str_to_u32_with_base(prefix_str, out, 16);
}

// 4 dashes + 32 characters, e.g E0E0D5FB-48FA-4428-B73D-43D3F7E49A8A
#define CHARS_PER_GUID (32 + 4)
#define CHARS_PER_HEX_BYTE 2

static bool consume_guid_part(struct string_view *str, void *out, u8 width, bool has_dash)
{
    u16 str_len = CHARS_PER_HEX_BYTE * width;
    bool res;

    switch (width) {
    case 1:
        res = str_to_u8_with_base((struct string_view) { str->text, str_len }, out, 16);
        break;
    case 2:
        res = str_to_u16_with_base((struct string_view) { str->text, str_len }, out, 16);
        break;
    case 4:
        res = str_to_u32_with_base((struct string_view) { str->text, str_len }, out, 16);
        break;
    default:
        BUG();
    }

    sv_offset_by(str, str_len + has_dash);
    return res;
}

static bool consume_guid(struct string_view *str, struct guid *guid)
{
    size_t i;

    if (str->size < CHARS_PER_GUID)
        return false;

    if (!consume_guid_part(str, &guid->data1, sizeof(u32), true))
        return false;

    if (!consume_guid_part(str, &guid->data2, sizeof(u16), true))
        return false;

    if (!consume_guid_part(str, &guid->data3, sizeof(u16), true))
        return false;

    for (i = 0; i < 8; ++i) {
        if (!consume_guid_part(str, &guid->data4[i], sizeof(u8), i == 1))
            return false;
    }

    return true;
}

static bool path_skip_dash(struct string_view *path)
{
    if (sv_empty(*path))
        return false;

    sv_offset_by(path, 1);
    return true;
}

#define DISKUUID_STR SV("DISKUUID")
#define DISK_STR     SV("DISK")

static bool path_consume_disk_identifier(struct string_view *path, struct full_path *out_path)
{
    if (sv_starts_with(*path, DISKUUID_STR)) {
        sv_offset_by(path, DISKUUID_STR.size);

        if (!consume_guid(path, &out_path->disk_guid))
            return false;

        out_path->disk_id_type = DISK_IDENTIFIER_UUID;
        return path_skip_dash(path);
    }

    if (sv_starts_with(*path, DISK_STR)) {
        sv_offset_by(path, DISK_STR.size);

        if (!path_consume_numeric_sequence(path, &out_path->disk_index))
            return false;

        out_path->disk_id_type = DISK_IDENTIFIER_INDEX;
        return path_skip_dash(path);
    }

    return false;
}

#define PARTUUID_STR SV("PARTUUID-")
#define PART_STR     SV("PART")

static bool path_consume_partition_identifier(struct string_view *path, struct full_path *out_path)
{
    if (sv_starts_with(*path, PARTUUID_STR)) {
        sv_offset_by(path, PARTUUID_STR.size);

        out_path->partition_id_type = PARTITION_IDENTIFIER_UUID;
        return consume_guid(path, &out_path->partition_guid);
    }

    if (sv_starts_with(*path, PART_STR)) {
        sv_offset_by(path, PART_STR.size);

        out_path->partition_id_type = PARTITION_IDENTIFIER_INDEX;
        return path_consume_numeric_sequence(path, &out_path->partition_index);
    }

    if (sv_starts_with(*path, SV("::/"))) {
        // GPT disks cannot be treated as unpartitioned media
        if (out_path->disk_id_type != DISK_IDENTIFIER_INDEX)
            return false;

        out_path->partition_id_type = PARTITION_IDENTIFIER_RAW;
        return true;
    }

    return false;
}

bool path_parse(struct string_view path, struct full_path *out_path)
{
    // path relative to config disk
    if (sv_starts_with(path, SV("/")) || sv_starts_with(path, SV("::/"))) {
        out_path->disk_id_type = DISK_IDENTIFIER_ORIGIN;
        out_path->partition_id_type = PARTITION_IDENTIFIER_ORIGIN;

        sv_offset_by(&path, path.text[0] == ':' ? 2 : 0);
        out_path->path_within_partition = path;
        return true;
    }

    if (!path_consume_disk_identifier(&path, out_path))
        return false;

    if (!path_consume_partition_identifier(&path, out_path))
        return false;

    if (!sv_starts_with(path, SV("::/")))
        return false;

    sv_offset_by(&path, 2);
    if (path.size >= MAX_PATH_SIZE) {
        oops("path \"%pSV\" is too big (%zu vs max %u)\n",
             &path, path.size, MAX_PATH_SIZE);
    }

    out_path->path_within_partition = path;
    return true;
}

 struct file *path_open(struct filesystem *fs, struct string_view path)
{
    struct dir_iter_ctx ctx;
    struct dir_rec rec;
    struct string_view node;
    bool node_found = false, is_dir = true;

    fs->iter_ctx_init(fs, &ctx, NULL);

    while (next_path_node(&path, &node)) {
        if (sv_equals(node, SV(".")))
            continue;
        if (!is_dir)
            return NULL;

        node_found = false;

        while (fs->next_dir_rec(fs, &ctx, &rec)) {
            struct string_view req_view = { rec.name, rec.name_len };

            if (!sv_equals(req_view, node))
                continue;

            node_found = true;
            is_dir = dir_rec_is_subdir(&rec);
            break;
        }

        if (!node_found)
            break;

        fs->iter_ctx_init(fs, &ctx, &rec);
    }

    if (!node_found || is_dir)
        return NULL;

    return fs->open_file(fs, &rec);
}
