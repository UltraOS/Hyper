#define MSG_FMT(x) "ISO9660: " x

#include "iso9660.h"
#include "iso9660_structures.h"
#include "allocator.h"
#include "common/constants.h"
#include "common/log.h"
#include "common/minmax.h"
#include "common/ctype.h"
#include "filesystem/block_cache.h"

struct iso9660_fs {
    struct filesystem f;

    u32 root_block;
    u32 root_size;
    u32 volume_size;

    u8 block_shift;
    u8 su_off; // 0xFF -> no SU

    struct block_cache dir_cache;
    struct block_cache ca_cache;
};

struct iso9660_dir {
    struct iso9660_fs *fs;

    u64 base_off;
    u64 cur_off;
    u64 size;
};
// Must be page-aligned
#define DIRECTORY_CACHE_SIZE PAGE_SIZE
#define CA_CACHE_SIZE        PAGE_SIZE

static bool dir_eof(struct iso9660_dir *dir)
{
    return dir->cur_off == dir->size;
}

static bool dir_consume_bytes(struct iso9660_dir *dir, u64 bytes)
{
    u64 bytes_left = dir->size - dir->cur_off;
    BUG_ON(bytes_left > dir->size);

    if (unlikely(bytes_left < bytes)) {
        print_warn("corrupted directory record? size: %llu with %llu left\n",
                   bytes, bytes_left);
        return false;
    }

    dir->cur_off += bytes;
    return true;
}

struct su_iteration_ctx {
    struct iso9660_fs *fs;

    // Inline directory SU area
    char *inline_data;

    size_t len;
    u64 base_off;
    u64 cur_off;

    u64 next_ca_off;
    u32 next_ca_len;

    bool is_in_ca;
};

struct iso9660_file {
    struct file f;
    u32 first_block;
};

#define ISO9660_MAX_NAME_LEN 255

struct iso9660_dir_entry {
    char name[ISO9660_MAX_NAME_LEN];
    u8 name_length;

#define DE_SUBDIRECTORY    (1 << 1)
    u8 flags;

    u32 first_block;
    u64 size;
};

static struct filesystem *iso9660_init(const struct disk *d, struct iso_pvd *pvd);

struct filesystem *try_create_iso9660(const struct disk *d, struct block_cache *bc)
{
    struct filesystem *ret = NULL;
    struct iso_vd *vd;
    size_t cur_off;

    // Technically possible and could be valid, but we don't support it
    if (unlikely(disk_block_size(d) > 2048))
        return NULL;

    cur_off = ISO9660_LOGICAL_SECTOR_SIZE * ISO9660_SYSTEM_AREA_BLOCKS;

    for (;;) {
        if (!block_cache_take_ref(bc, (void**)&vd, cur_off, sizeof(struct iso_vd)))
            return NULL;

        if (memcmp(vd->standard_identifier, ISO9660_IDENTIFIER,
                   sizeof(vd->standard_identifier)) != 0)
            goto out;

        enum vd_type type = ecma119_get_711(vd->descriptor_type_711);

        // We don't check supplementary for now because we don't support joliet
        if (type == VD_TYPE_PRIMARY)
            break;

        if (type == VD_TYPE_TERMINATOR)
            goto out;

        cur_off += sizeof(struct iso_vd);
        block_cache_release_ref(bc);
    }

    ret = iso9660_init(d, (struct iso_pvd*)vd);

out:
    block_cache_release_ref(bc);
    return ret;
}

static bool iso9660_read(struct iso9660_fs *fs, void *buf, u64 offset, size_t bytes)
{
    struct disk_services *sv = filesystem_backend();
    if (unlikely(!sv))
        return false;

    return sv->read(fs->f.d.handle, buf, offset, bytes);
}

static bool iso9660_read_file(struct file* f, void *buffer, u64 offset, u32 size)
{
    struct iso9660_fs *fs = container_of(f->fs, struct iso9660_fs, f);
    struct iso9660_file *isf = container_of(f, struct iso9660_file, f);
    u64 final_offset = (isf->first_block << f->fs->d.block_shift) + offset;

    check_read(f, offset, size);
    return iso9660_read(fs, buffer, final_offset, size);
}

static struct file *iso9660_do_open_file(struct filesystem *fs, u32 first_block, u32 file_size)
{
    struct iso9660_file *f = allocate_bytes(sizeof(struct iso9660_file));
    if (unlikely(!f))
        return NULL;

    *f = (struct iso9660_file) {
        .f = {
            .fs = fs,
            .size = file_size,
            .read = iso9660_read_file,
        },
        .first_block = first_block
    };

    return &f->f;
}

static bool dir_skip_to(struct iso9660_dir *dir, size_t off)
{
    // No more entries left
    if (dir->size <= off || ((dir->size - off) < sizeof(struct iso_dir_record))) {
        dir->cur_off = dir->size;
        return false;
    }

    dir->cur_off = off;
    return true;
}

static bool directory_fetch_raw_entry(struct iso9660_dir *dir, struct iso_dir_record **out_rec)
{
    struct block_cache *cache = &dir->fs->dir_cache;
    size_t aligned_off, rec_len_min, rec_len, rec_len_max;
    u8 ident_len;
    struct iso_dir_record *dr;
    bool ret = false;

    for (;;) {
        if (dir_eof(dir))
            return false;

        aligned_off = ALIGN_UP(dir->cur_off, 1 << dir->fs->block_shift);
        rec_len_max = MIN(dir->size, aligned_off) - dir->cur_off;
        if (rec_len_max == 0)
            rec_len_max = 255;

        if (rec_len_max <= sizeof(struct iso_dir_record)) {
            dir_skip_to(dir, aligned_off);
            continue;
        }

        block_cache_take_ref(cache, (void**)&dr, dir->base_off + dir->cur_off, rec_len_max);
        rec_len = ecma119_get_711(dr->record_length_711);

        // Either EOF or we're too close to the next sector
        if (rec_len == 0) {
            block_cache_release_ref(cache);

            // Enough space but no record, assume EOF
            if (rec_len_max == 255) {
                dir->cur_off = dir->size;
                return false;
            }

            dir_skip_to(dir, aligned_off);
            continue;
        }

        ident_len = ecma119_get_711(dr->identifier_length_711);
        if ((ident_len & 1) == 0)
            ident_len++;

        rec_len_min = sizeof(struct iso_dir_record) + ident_len;

        if (unlikely(rec_len > rec_len_max || rec_len < rec_len_min)) {
            print_warn("invalid record len %zu (expected min %zu max %zu)\n", rec_len, rec_len_min, rec_len_max);
            goto out;
        }

        if (!dir_consume_bytes(dir, rec_len))
            goto out;

        *out_rec = dr;
        ret = true;
        goto out;
    }

out:
    block_cache_release_ref(cache);
    return ret;
}

#define MAX_SANE_CHAIN_LEN 200

static bool dir_read_multiext_size(struct iso9660_dir *dir, u64 *out_file_size)
{
    size_t records_read = 0;
    struct iso_dir_record *dr;
    u8 flags;

    for (;;) {
        if (unlikely(records_read == MAX_SANE_CHAIN_LEN)) {
            print_warn("record chain is too long (>200), ignoring");
            return false;
        }

        if (!directory_fetch_raw_entry(dir, &dr))
            return false;

        flags = ecma119_get_711(dr->flags_711);
        *out_file_size += ecma119_get_733(dr->data_length_733);
        records_read++;

        if (!(flags & ISO9660_MULTI_EXT))
            return true;
    }
}

static void su_invalidate_ctx(struct su_iteration_ctx *ctx)
{
    memzero(ctx, sizeof(*ctx));
}

static void su_finalize_ctx(struct su_iteration_ctx *ctx)
{
    if (ctx->is_in_ca && ctx->cur_off)
        block_cache_release_ref(&ctx->fs->ca_cache);

    su_invalidate_ctx(ctx);
}

static bool su_switch_to_next_ca(struct su_iteration_ctx *ctx)
{
    if (!ctx->next_ca_len) {
        su_finalize_ctx(ctx);
        return false;
    }

    ctx->len = ctx->next_ca_len;
    ctx->cur_off = 0;
    ctx->base_off = ctx->next_ca_off;
    ctx->is_in_ca = true;

    ctx->next_ca_len = ctx->next_ca_off = 0;

    return true;
}

#define LEN_SUE_IDX 2
#define VER_SUE_IDX 3

/*
 * If the remaining allocated space following the last recorded System Use Entry in a System
 * Use field or Continuation Area is less than four bytes long, it cannot contain a System
 * Use Entry and shall be ignored.
 */
#define SUE_MIN_LEN 4

static void on_invalid_su_entry_len(struct su_iteration_ctx *ctx, size_t reported_len, size_t expected_len)
{
    print_warn("invalid SU entry len %zu, expected max %zu\n", reported_len, expected_len);
    su_invalidate_ctx(ctx);
}

static bool next_inline_su_entry(struct su_iteration_ctx *ctx, char **out_ptr)
{
    size_t bytes_left = ctx->len - ctx->cur_off;

    char *su_ptr = ctx->inline_data + ctx->cur_off;
    u8 reported_len = su_ptr[LEN_SUE_IDX];

    if (reported_len > bytes_left) {
        on_invalid_su_entry_len(ctx, reported_len, bytes_left);
        return false;
    }

    ctx->cur_off += reported_len;
    bytes_left = ctx->len - ctx->cur_off;

    if (bytes_left < SUE_MIN_LEN)
        ctx->cur_off = ctx->len;

    *out_ptr = su_ptr;
    return true;
}

static bool next_ca_su_entry(struct su_iteration_ctx *ctx, char **out_ptr)
{
    struct block_cache *cache = &ctx->fs->ca_cache;
    size_t bytes_left = ctx->len - ctx->cur_off;
    u64 take_off = ctx->base_off + ctx->cur_off;
    char *su_ptr;
    u8 reported_len;

    if (ctx->is_in_ca && ctx->cur_off)
        block_cache_release_ref(&ctx->fs->ca_cache);

    if (!block_cache_take_ref(cache, (void**)&su_ptr, take_off, LEN_SUE_IDX + 1))
        goto err_out;

    reported_len = su_ptr[LEN_SUE_IDX];
    block_cache_release_ref(cache);

    if (reported_len > bytes_left) {
        on_invalid_su_entry_len(ctx, reported_len, bytes_left);
        return false;
    }

    ctx->cur_off += reported_len;
    bytes_left = ctx->len - ctx->cur_off;
    if (bytes_left < SUE_MIN_LEN)
        ctx->cur_off = ctx->len;

    if (!block_cache_take_ref(cache, (void**)&su_ptr, take_off, reported_len))
        goto err_out;

    *out_ptr = su_ptr;
    return true;

err_out:
    su_invalidate_ctx(ctx);
    return false;
}

#define SU_SIG(a, b) ((a) | ((u16)(b) << 8))

static u16 su_get_signature(const char *sue)
{
    return SU_SIG(sue[0], sue[1]);
}

static bool sue_validate_version(const char *sue)
{
    if (sue[VER_SUE_IDX] != 1) {
        struct string_view ver_view = { sue, 2 };
        print_warn("unexpected '%pSV' version %d\n", &ver_view, sue[VER_SUE_IDX]);
        return false;
    }

    return true;
}

static bool sue_validate_len(const char *sue, u8 expected)
{
    if (sue[LEN_SUE_IDX] != expected) {
        struct string_view ver_view = { sue, 2 };
        print_warn("unexpected '%pSV' len %d, expected %d\n", &ver_view,
                   sue[LEN_SUE_IDX], expected);
        return false;
    }

    return true;
}

#define SUE_CE_LEN 28

#define SUE_CE_BLOCK_IDX 4
#define SUE_CE_OFF_IDX   12
#define SUE_CE_LEN_IDX   20

static void su_handle_ce(struct su_iteration_ctx *ctx, char *sue)
{
    if (!sue_validate_version(sue))
        return;

    if (!sue_validate_len(sue, SUE_CE_LEN))
        return;

    if (ctx->next_ca_len)
        print_warn("multiple CEs in one su field, dropping previous");

    ctx->next_ca_off = ecma119_get_733(&sue[SUE_CE_BLOCK_IDX]) << ctx->fs->block_shift;
    ctx->next_ca_off += ecma119_get_733(&sue[SUE_CE_OFF_IDX]);
    print("next ca off is %llu\n", ctx->next_ca_off);

    ctx->next_ca_len = ecma119_get_733(&sue[SUE_CE_LEN_IDX]);
}

static bool next_su_entry(struct su_iteration_ctx *ctx, char **out_ptr)
{
    bool res;

    if (ctx->cur_off == ctx->len && !su_switch_to_next_ca(ctx))
        return false;

    res = ctx->is_in_ca ? next_ca_su_entry(ctx, out_ptr) :
                          next_inline_su_entry(ctx, out_ptr);

    struct string_view su = { *out_ptr, 2};
    print_info("SU: %pSV, off %llu, len: %zu\n", &su, ctx->cur_off, ctx->len);

    if (res) {
        // Continuation area
        if (su_get_signature(*out_ptr) == SU_SIG('C', 'E')) {
            su_handle_ce(ctx, *out_ptr);
            return next_su_entry(ctx, out_ptr);
        }

        // SU field terminator
        if (su_get_signature(*out_ptr) == SU_SIG('S', 'T') &&
            sue_validate_version(*out_ptr)) {
            su_switch_to_next_ca(ctx);
            return next_su_entry(ctx, out_ptr);
        }
    }

    return res;
}

#define SUE_NM_FLAGS_IDX 4
#define SUE_NM_FLAG_CONTINUE (1 << 0)
#define SUE_NM_FLAG_CURDIR   (1 << 1)
#define SUE_NM_FLAG_PARDIR   (1 << 2)

#define SUE_NM_MIN_LEN 5

static bool find_rock_ridge_name(struct iso9660_fs *fs, char *su_area, size_t su_len, char *out, u8 *out_len)
{
    struct su_iteration_ctx sctx = {
        .fs = fs,
        .inline_data = su_area,
        .len = su_len,
    };
    char *sue;
    *out_len = 0;
    bool ret = false;

    while (next_su_entry(&sctx, &sue)) {
        u8 this_len, max_len;

        if (su_get_signature(sue) != SU_SIG('N', 'M'))
            continue;

        if (!sue_validate_version(sue))
            goto out;

        this_len = sue[LEN_SUE_IDX];
        if (this_len < SUE_NM_MIN_LEN) {
            print_warn("invalid 'NM' len %d\n", this_len);
            goto out;
        }

        if (sue[SUE_NM_FLAGS_IDX] & (SUE_NM_FLAG_CURDIR | SUE_NM_FLAG_PARDIR))
            goto out;

        this_len -= SUE_NM_MIN_LEN;
        sue += SUE_NM_MIN_LEN;

        max_len = ISO9660_MAX_NAME_LEN - *out_len;
        if (max_len == 0) {
            print_warn("RR name is too long, ignoring");
            goto out;
        }

        memcpy(out, sue, this_len);
        out += this_len;
        *out_len += this_len;

        if (sue[SUE_NM_FLAGS_IDX] & SUE_NM_FLAG_CONTINUE)
            continue;

        break;
    }
    ret = *out_len != 0;

out:
    su_invalidate_ctx(&sctx);
    return ret;
}

static void *record_get_su_area(struct iso_dir_record *rec, size_t *out_len)
{
    size_t ident_len = ecma119_get_711(rec->identifier_length_711);

    if ((ident_len & 1) == 0)
        ident_len++;

    *out_len = ecma119_get_711(rec->record_length_711) - ident_len - sizeof(struct iso_dir_record);
    return rec->identifier + ident_len;
}

#define ISO9660_CURDIR_NAME_BYTE 0
#define ISO9660_PARDIR_NAME_BYTE 1

static bool record_read_identifier(struct iso_dir_record *rec, char *out, u8 *out_len)
{
    u8 i, ident_len = ecma119_get_711(rec->identifier_length_711);
    if (!ident_len)
        return false;

    if (rec->identifier[0] == ISO9660_CURDIR_NAME_BYTE) {
        out[0] = '.';
        *out_len = 1;
        return true;
    }

    if (rec->identifier[0] == ISO9660_PARDIR_NAME_BYTE) {
        out[0] = '.';
        out[1] = '.';
        *out_len = 2;
        return true;
    }

    for (i = 0; i < ident_len; ++i) {
        char cur = rec->identifier[i];
        char next = (ident_len - i) > 1 ? rec->identifier[i + 1] : '\0';

        // A file without an extension
        if (cur == '.' && next == ';')
            break;

        if (cur == ';')
            break;

        // Assume lowercase for all files
        out[i] = tolower(cur);
    }

    *out_len = i;
    return true;
}

static bool get_record_name(struct iso9660_fs *fs, struct iso_dir_record *rec, char *out, u8 *out_len)
{
    if (fs->su_off != 0xFF) {
        size_t su_len;
        void *su_area = record_get_su_area(rec, &su_len);

        su_area += fs->su_off;
        su_len -= MIN(fs->su_off, su_len);

        if (su_len > SUE_MIN_LEN && find_rock_ridge_name(fs, su_area, su_len, out, out_len))
            return true;
    }

    return record_read_identifier(rec, out, out_len);
}

static bool directory_next_entry(struct iso9660_dir *dir, struct iso9660_dir_entry *out_ent)
{
    struct iso_dir_record *dr;
    u8 flags;

    for (;;) {
        if (!directory_fetch_raw_entry(dir, &dr))
            return false;

        flags = ecma119_get_711(dr->flags_711);
        out_ent->first_block = ecma119_get_733(dr->location_of_extent_733) +
                               ecma119_get_711(dr->extended_attr_rec_length_711);
        out_ent->size = ecma119_get_733(dr->data_length_733);

        if (!get_record_name(dir->fs, dr, out_ent->name, &out_ent->name_length))
            return false;

        struct string_view name = {out_ent->name, out_ent->name_length};
        print("found dir name %pSV\n", &name);

        if ((flags & ISO9660_MULTI_EXT) && !dir_read_multiext_size(dir, &out_ent->size))
            return false;

        if ((flags & ISO9660_ASSOC_FILE) || (flags & ISO9660_HIDDEN_DIR))
            continue;

        if (flags & ISO9660_SUBDIR)
            out_ent->flags |= DE_SUBDIRECTORY;

        return true;
    }
}

static struct file *iso9660_open(struct filesystem *fs, struct string_view path)
{
    struct iso9660_fs *ifs = container_of(fs, struct iso9660_fs, f);
    struct string_view node;

    bool is_directory = true, node_found = false;
    u32 first_block = ifs->root_block;
    u32 file_size = ifs->root_size;

    struct iso9660_dir dir = {
        .fs = ifs,
        .base_off = first_block << ifs->block_shift,
        .size = file_size
    };

    while (next_path_node(&path, &node)) {
        struct iso9660_dir_entry ent;

        if (sv_equals(node, SV(".")))
            continue;
        if (!is_directory)
            return NULL;

        node_found = false;

        while (directory_next_entry(&dir, &ent)) {
            if (!sv_equals((struct string_view) { ent.name, ent.name_length }, node))
                continue;

            first_block = ent.first_block;
            file_size = ent.size;
            node_found = true;
            is_directory = ent.flags & DE_SUBDIRECTORY;
            break;
        }

        if (!node_found)
            break;

        dir.base_off = first_block << ifs->block_shift;
        dir.size = file_size;
        dir.cur_off = 0;
    }

    if (!node_found || is_directory)
        return NULL;

    return iso9660_do_open_file(&ifs->f, first_block, file_size);
}

void iso9660_close(struct file* f)
{
    struct iso9660_file *ifs = container_of(f, struct iso9660_file, f);
    free_bytes(ifs, sizeof(struct iso9660_file));
}

bool iso9660_refill_blocks(void *fs, void *buf, u64 block, size_t count)
{
    struct iso9660_fs *ifs = fs;
    struct disk_services *ds = filesystem_backend();

    if (unlikely(!ds))
        return false;

    return ds->read_blocks(ifs->f.d.handle, buf, block, count);
}

#define SUE_SP_CHECK_BYTE0_IDX 4
#define SUE_SP_CHECK_BYTE1_IDX 5
#define SUE_SP_LEN_SKP_IDX     6

#define SUE_SP_CHECK_BYTE0 0xBE
#define SUE_SP_CHECK_BYTE1 0xEF

static void su_init_from_sp_sue(struct iso9660_fs *fs, const char *sue)
{
    u8 cb0, cb1;

    if (!sue_validate_version(sue))
        return;

    cb0 = sue[SUE_SP_CHECK_BYTE0_IDX];
    cb1 = sue[SUE_SP_CHECK_BYTE1_IDX];

    if (cb0 != SUE_SP_CHECK_BYTE0 || cb1 != SUE_SP_CHECK_BYTE1) {
        print_warn("invalid SP check bytes 0x%02X%02X, expected 0xBEEF\n", cb0, cb1);
        return;
    }

    fs->su_off = sue[SUE_SP_LEN_SKP_IDX];
    if (fs->su_off > 200) {
        print_warn("bogus 'SP' LEN_SKP value %d, assuming 0", fs->su_off);
        fs->su_off = 0;
    }
}

static bool su_init(struct iso9660_fs *fs)
{
    struct iso_dir_record *dr;
    struct su_iteration_ctx sc = {
        .fs = fs,
    };
    void *ca_cache_buf;
    char *su_entry;

    struct iso9660_dir d = {
        .fs = fs,
        .base_off = fs->root_block << fs->block_shift,
        .size = fs->root_size,
    };

    if (!directory_fetch_raw_entry(&d, &dr))
        return false;

    sc.inline_data = record_get_su_area(dr, &sc.len);
    if (sc.len < SUE_MIN_LEN)
        return true;

    if (!next_su_entry(&sc, &su_entry))
        return true;

    // Managed to fetch a valid SUE, but it's not 'SP'
    if (su_get_signature(su_entry) != SU_SIG('S', 'P')) {
        struct string_view sig_view = { su_entry, 2 };
        print_warn("invalid SUE signature, expected 'SP' got '%pSV', assuming LEN_SKP of 0\n", &sig_view);
        goto su_supported_out;
    }

    su_init_from_sp_sue(fs, su_entry);

su_supported_out:
    ca_cache_buf = allocate_pages(CA_CACHE_SIZE >> PAGE_SHIFT);
    if (unlikely(!ca_cache_buf))
        return false;

    block_cache_init(&fs->ca_cache, iso9660_refill_blocks, fs, fs->f.d.block_shift,
                     ca_cache_buf, CA_CACHE_SIZE >> fs->f.d.block_shift);
    return true;
}

static struct filesystem *iso9660_init(const struct disk *d, struct iso_pvd *pvd)
{
    u8 block_shift;
    u32 block_size, volume_size, root_block, root_size, root_last_block;
    void *dir_cache;
    struct iso_dir_record *rd = (struct iso_dir_record*)pvd->root_directory_entry;
    struct iso9660_fs *fs;

    block_size = ecma119_get_723(pvd->logical_block_size_723);

    switch (block_size) {
    case 2048:
        block_shift = 11;
        break;
    case 1024:
        block_shift = 10;
        break;
    case 512:
        block_shift = 9;
        break;
    default:
        print_warn("invalid block size %u, ignoring\n", block_size);
        return NULL;
    }

    volume_size = ecma119_get_733(pvd->volume_space_size_733);
    root_block = ecma119_get_733(rd->location_of_extent_733) +
                 ecma119_get_711(rd->extended_attr_rec_length_711);
    root_size = ecma119_get_733(rd->data_length_733);
    root_last_block = root_block + CEILING_DIVIDE(root_size, block_size);

    if (volume_size < root_last_block) {
        print_warn("invalid volume size: %u\n", volume_size);
        return NULL;
    }

    if (!root_size || root_last_block >= volume_size || root_last_block < root_block) {
        print_warn("invalid root directory, block: %u, size: %u\n", root_block, root_size);
        return NULL;
    }

    fs = allocate_pages(1);
    if (unlikely(!fs))
        return NULL;

    *fs = (struct iso9660_fs) {
        .f = {
            .d = *d,
            .lba_range = { 0, d->sectors },
            .open = iso9660_open,
            .close = iso9660_close
        },
        .root_block = root_block,
        .root_size = root_size,
        .volume_size = volume_size,
        .block_shift = block_shift,
        .su_off = 0xFF
    };

    dir_cache = allocate_pages(DIRECTORY_CACHE_SIZE >> PAGE_SHIFT);
    if (unlikely(!dir_cache))
        goto err_out;

    block_cache_init(&fs->dir_cache, iso9660_refill_blocks, fs, fs->f.d.block_shift,
                     dir_cache, DIRECTORY_CACHE_SIZE >> fs->f.d.block_shift);

    if (!su_init(fs))
        goto err_out;

    print_info("detected with block size %u, volume size %u\n", block_size, volume_size);
    return &fs->f;

err_out:
    free_pages(fs, 1);
    return NULL;
}
