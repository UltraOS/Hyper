#include "filesystem/pxe.h"
#include "common/helpers.h"
#include "filesystem/filesystem.h"
#include "pxe_services.h"
#include "allocator.h"

struct pxe_file {
    struct file base_file;
    u8 data[];
};

static size_t pxe_file_struct_size(struct pxe_file *file)
{
    return sizeof(*file) + file->base_file.size;
}

static struct file *pxe_open_file(struct filesystem *fs, struct string_view path)
{
    struct pxe_file *out_file;
    u64 file_size;

    if (!pxe_get_file_size(path, &file_size))
        return NULL;

    out_file = allocate_bytes(sizeof(*out_file) + file_size);
    if (!out_file)
        return NULL;

    out_file->base_file.fs = fs;
    out_file->base_file.size = file_size;

    if (!pxe_read_file(path, out_file->data, file_size)) {
        free_bytes(out_file, pxe_file_struct_size(out_file));
        return NULL;
    }

    return &out_file->base_file;
}

static void pxe_close_file(struct file *file)
{
    struct pxe_file *pfile;

    pfile = container_of(file, struct pxe_file, base_file);
    free_bytes(pfile, pxe_file_struct_size(pfile));
}

static bool pxe_read(struct file *file, void *buffer, u64 offset, u32 bytes)
{
    struct pxe_file *pfile;

    pfile = container_of(file, struct pxe_file, base_file);
    memcpy(buffer, pfile->data + offset, bytes);

    return true;
}

struct filesystem g_pxe_fs = {
    .open_file_direct = pxe_open_file,
    .close_file = pxe_close_file,
    .read_file = pxe_read,
    .block_shift = 9,
};
