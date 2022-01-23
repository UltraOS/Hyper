#include "filesystem.h"
#include "common/string_view.h"
#include "common/types.h"
#include "common/ctype.h"
#include "common/conversions.h"
#include "filesystem/fat32/fat32.h"

static struct disk_services *backend;

struct disk_services *filesystem_set_backend(struct disk_services* srvc)
{
    struct disk_services *prev = backend;
    backend = srvc;
    return prev;
}

struct disk_services *filesystem_backend()
{
    return backend;
}

bool split_prefix_and_path(struct string_view str, struct string_view *prefix, struct string_view *path)
{
    struct string_view delim = SV("::");
    ssize_t pref_idx = sv_find(str, delim, 0);

    if (pref_idx < 0) {
        sv_clear(prefix);
        *path = str;
    } else {
        *prefix = (struct string_view) { str.text, pref_idx };
        *path = (struct string_view) { str.text + pref_idx + 2, str.size - pref_idx - 2 };
    }

    return true;
}

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

#define CHARS_PER_GUID 32
#define CHARS_PER_HEX_BYTE 2

static bool extract_numeric_prefix(struct string_view *str, struct string_view *prefix, bool allow_hex, size_t size)
{
    prefix->text = str->text;
    prefix->size = 0;

    for (;;) {
        char c;

        if (size && prefix->size == size)
            break;

        if (sv_empty(*str))
            break;

        c = tolower(str->text[0]);

        if ((c >= '0' && c <= '9') || (allow_hex && (c >= 'a' && c <= 'z'))) {
            sv_extend_by(prefix, 1);
            sv_offset_by(str, 1);
            continue;
        }

        break;
    }

    return !sv_empty(*str) && (size && str->size == size);
}

bool parse_guid(struct string_view *str, struct guid *guid)
{
    size_t i;

    if (str->size != CHARS_PER_GUID)
        return false;

    if (!str_to_u32((struct string_view) { str->text, 4 * CHARS_PER_HEX_BYTE }, &guid->data1))
        return false;
    sv_offset_by(str, 4 * CHARS_PER_HEX_BYTE);

    if (!str_to_u16((struct string_view) { str->text, 2 * CHARS_PER_HEX_BYTE }, &guid->data2))
        return false;
    sv_offset_by(str, 2 * CHARS_PER_HEX_BYTE);

    if (!str_to_u16((struct string_view) { str->text, 2 * CHARS_PER_HEX_BYTE }, &guid->data3))
        return false;
    sv_offset_by(str, 2 * CHARS_PER_HEX_BYTE);

    for (i = 0; i < 8; ++i) {
        if (!str_to_u8((struct string_view) { str->text, 1 * CHARS_PER_HEX_BYTE }, &guid->data4[i]))
            return false;

        sv_offset_by(str, 1 * CHARS_PER_HEX_BYTE);
    }

    return true;
}

bool parse_path(struct string_view path, struct full_path *out_path)
{
    // path relative to config disk
    if (sv_starts_with(path, SV("/")) || sv_starts_with(path, SV("::/"))) {
        out_path->disk_id_type = DISK_IDENTIFIER_ORIGIN;
        out_path->partition_id_type = PARTITION_IDENTIFIER_ORIGIN;

        sv_offset_by(&path, path.text[0] == ':' ? 2 : 0);
        out_path->path_within_partition = path;
        return true;
    }

    if (sv_starts_with(path, SV("DISKUUID"))) {
        struct string_view prefix;

        sv_offset_by(&path, 8);
        if (!extract_numeric_prefix(&path, &prefix, true, CHARS_PER_GUID))
            return false;

        out_path->disk_id_type = DISK_IDENTIFIER_UUID;
        if (!parse_guid(&prefix, &out_path->disk_guid))
            return false;
    } else if (sv_starts_with(path, SV("DISK"))) {
        struct string_view prefix;

        sv_offset_by(&path, 4);
        if (!extract_numeric_prefix(&path, &prefix, false, 0))
            return false;
        if (!str_to_u32(prefix, &out_path->disk_index))
            return false;

        out_path->disk_id_type = DISK_IDENTIFIER_INDEX;
    } else { // invalid prefix
        return false;
    }

    if (sv_starts_with(path, SV("GPTUUID"))) {
        struct string_view prefix;

        sv_offset_by(&path, 7);
        if (!extract_numeric_prefix(&path, &prefix, true, CHARS_PER_GUID))
            return false;

        out_path->partition_id_type = PARTITION_IDENTIFIER_GPT_UUID;
        if (!parse_guid(&prefix, &out_path->partition_guid))
            return false;
    } else if (sv_starts_with(path, SV("MBR")) || sv_starts_with(path, SV("GPT"))) {
        struct string_view prefix;

        sv_offset_by(&path, 3);
        if (!extract_numeric_prefix(&path, &prefix, false, 0))
            return false;
        if (!str_to_u32(prefix, &out_path->partition_index))
            return false;

        out_path->partition_id_type = path.text[0] == 'M' ? PARTITION_IDENTIFIER_MBR_INDEX :
                                                            PARTITION_IDENTIFIER_GPT_INDEX;
    } else if (sv_starts_with(path, SV("::/"))) {
        // GPT disks cannot be treated as a raw device
        if (out_path->disk_id_type != DISK_IDENTIFIER_INDEX)
            return false;

        out_path->partition_id_type = PARTITION_IDENTIFIER_RAW;
    } else {
        return false;
    }

    if (!sv_starts_with(path, SV("::/")))
        return false;

    sv_offset_by(&path, 2);
    out_path->path_within_partition = path;
    return true;
}

struct filesystem *fs_try_detect(const struct disk *d, struct range lba_range, void *first_page)
{
    if (!backend)
        return NULL;

    return try_create_fat32(d, lba_range, first_page);
}
