#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <stdarg.h>
#include <inttypes.h>

extern char mbr_data[];
extern unsigned long mbr_size;

extern char iso_mbr_data[];
extern unsigned long iso_mbr_size;

extern char stage2_data[];
extern unsigned long stage2_size;

#define MBR_BLOCK_SIZE               512
#define MBR_MAGIC                    0xAA55
#define OFFSET_TO_MBR_MAGIC          510
#define OFFSET_TO_MBR_PARTITION_LIST 0x01BE

struct mbr_partition_entry {
    uint8_t status;
    uint8_t chs_begin[3];
    uint8_t type;
    uint8_t chs_end[3];
    uint32_t first_block;
    uint32_t block_count;
};
#define MBR_PARTITION_COUNT 4

_Noreturn
#ifdef __clang__
__attribute__((format(printf, 1, 2)))
#elif defined(__GNUC__)
__attribute__((format(gnu_printf, 1, 2)))
#endif
void panic(const char *err, ...)
{
    va_list vl;

    va_start(vl, err);
    vfprintf(stderr, err, vl);
    va_end(vl);

    exit(1);
}

FILE *safe_open(const char *path, const char *mode)
{
    FILE *out = fopen(path, mode);
    if (out)
        return out;

    panic("Failed to open file %s: %s\n", path, strerror(errno));
}

void safe_read_at(FILE *f, size_t offset, void *buf, size_t bytes)
{
    if (fseek(f, (long)offset, SEEK_SET) != 0)
        panic("Failed to seek file to %zu:%s\n", offset, strerror(errno));

    if (fread(buf, 1, bytes, f) != bytes)
        panic("Failed to read %zu bytes at %zu: %s\n", bytes, offset, strerror(errno));
}

void safe_write_at(FILE *f, size_t offset, void *buf, size_t bytes)
{
    if (fseek(f, (long)offset, SEEK_SET) != 0)
        panic("Failed to seek file to %zu:%s\n", offset, strerror(errno));

    if (fwrite(buf, 1, bytes, f) != bytes)
        panic("Failed to write %zu bytes at %zu: %s\n", bytes, offset, strerror(errno));
}

void read_mbr_partition_list(FILE *img, struct mbr_partition_entry *buffer)
{
    uint16_t mbr_magic;

    safe_read_at(img, OFFSET_TO_MBR_MAGIC, &mbr_magic, sizeof(mbr_magic));

    if (mbr_magic != MBR_MAGIC)
        panic("Invalid MBR magic, expected 0x%04X got 0x%04hX\n", MBR_MAGIC, mbr_magic);

    safe_read_at(img, OFFSET_TO_MBR_PARTITION_LIST, buffer,
                 MBR_PARTITION_COUNT * sizeof(struct mbr_partition_entry));
}

void ensure_stage2_fits(struct mbr_partition_entry *partitions)
{
    size_t i;
    uint64_t lowest_block = UINT64_MAX;
    uint64_t gap_size;

    for (i = 0; i < MBR_PARTITION_COUNT; ++i) {
        uint32_t first_block = partitions[i].first_block;

        if (first_block && (first_block < lowest_block))
            lowest_block = first_block;
    }

    if (lowest_block == UINT64_MAX)
        panic("Please create at least one partition before attempting to install");

    gap_size = (lowest_block - 1) * MBR_BLOCK_SIZE;

    if (gap_size < stage2_size) {
        panic("Not enough space between MBR and the first partition to fit stage2!\n"
              "Need at least %lu, have %"PRIu64"\n", stage2_size, gap_size);
    }
}

void write_mbr(FILE *f, struct mbr_partition_entry *partitions, bool is_iso)
{
    void *data = is_iso ? iso_mbr_data : mbr_data;
    size_t size = is_iso ? iso_mbr_size : mbr_size;

    safe_write_at(f, 0, data, size);
    safe_write_at(f, OFFSET_TO_MBR_PARTITION_LIST, partitions,
                  sizeof(struct mbr_partition_entry) * MBR_PARTITION_COUNT);
}

void write_stage2(FILE *f)
{
    safe_write_at(f, MBR_BLOCK_SIZE, stage2_data, stage2_size);
}

void write_hyper(FILE *img, struct mbr_partition_entry *mbr_partitions, bool is_iso)
{
    write_mbr(img, mbr_partitions, is_iso);

    if (!is_iso)
        write_stage2(img);
}

#define ISO9660_LOGICAL_SECTOR_SIZE 2048
#define ISO9660_SYSTEM_AREA_BLOCKS 16
#define ISO9660_PVD_OFF (ISO9660_LOGICAL_SECTOR_SIZE * ISO9660_SYSTEM_AREA_BLOCKS + 1)
#define ISO9660_IDENTIFIER "CD001"

bool is_iso_disk(FILE *img)
{
    char data[sizeof(ISO9660_IDENTIFIER) - 1];
    safe_read_at(img, ISO9660_PVD_OFF, data, sizeof(ISO9660_IDENTIFIER) - 1);

    return memcmp(data, ISO9660_IDENTIFIER, sizeof(ISO9660_IDENTIFIER) - 1) == 0;
}

// "EFI PART"
#define GPT_SIGNATURE 0x5452415020494645

void ensure_no_gpt(FILE *img)
{
    uint64_t signature;

    safe_read_at(img, 512, &signature, sizeof(signature));
    if (signature == GPT_SIGNATURE)
        goto out_gpt;

    safe_read_at(img, 4096, &signature, sizeof(signature));
    if (signature == GPT_SIGNATURE)
        goto out_gpt;

    return;

out_gpt:
    panic("Installing to GPT images is currently not supported\n");
}

int main(int argc, char **argv)
{
    struct mbr_partition_entry mbr_partitions[MBR_PARTITION_COUNT];
    bool is_iso;
    FILE *img;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <path-where-to-install>\n", argv[0]);
        return 1;
    }
    img = safe_open(argv[1], "r+b");

    read_mbr_partition_list(img, mbr_partitions);
    is_iso = is_iso_disk(img);

    if (!is_iso) {
        // Currently unsupported
        ensure_no_gpt(img);
        ensure_stage2_fits(mbr_partitions);
    }

    write_hyper(img, mbr_partitions, is_iso);
    fclose(img);
    return 0;
}
