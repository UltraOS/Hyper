#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

extern char mbr_data[];
extern uint32_t mbr_size;

extern char stage2_data[];
extern uint32_t stage2_size;

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

bool read_at(FILE *f, size_t offset, void *buf, size_t bytes)
{
    if (fseek(f, offset, SEEK_SET) != 0) {
        fprintf(stderr, "Failed to seek file to %zu:%s\n", offset, strerror(errno));
        return false;
    } 

    if (fread(buf, 1, bytes, f) != bytes) {
        fprintf(stderr, "Failed to read %zu bytes at %zu: %s\n", bytes, offset, strerror(errno));
        return false;
    }

    return true;
}

bool write_at(FILE *f, size_t offset, void *buf, size_t bytes)
{
    if (fseek(f, offset, SEEK_SET) != 0) {
        fprintf(stderr, "Failed to seek file to %zu:%s\n", offset, strerror(errno));
        return false;
    } 

    if (fwrite(buf, 1, bytes, f) != bytes) {
        fprintf(stderr, "Failed to write %zu bytes at %zu: %s\n", bytes, offset, strerror(errno));
        return false;
    }

    return true;
}

bool read_mbr_partition_list(const char *path, struct mbr_partition_entry *buffer)
{
    FILE *f = fopen(path, "rb");
    uint16_t mbr_magic;
    size_t bytes_read;
    bool ret = false;
    
    if (!f) {
        fprintf(stderr, "Failed to open %s: %s\n", path, strerror(errno));
        goto out;
    }

    if (!read_at(f, OFFSET_TO_MBR_MAGIC, &mbr_magic, sizeof(mbr_magic)))
        goto out;

    if (mbr_magic != MBR_MAGIC) {
        fprintf(stderr, "Invalid MBR magic, expected 0x%04hX got 0x%04hX\n", MBR_MAGIC, mbr_magic);
        goto out;
    }

    if (!read_at(f, OFFSET_TO_MBR_PARTITION_LIST, buffer, MBR_PARTITION_COUNT * sizeof(struct mbr_partition_entry)))
        goto out;

    ret = true;

out:
    if (f)
        fclose(f);

    return ret;
}

bool check_stage2_fits(struct mbr_partition_entry *partitons)
{
    size_t i = 0;
    uint32_t lowest_block = UINT32_MAX;
    size_t gap_size;

    for (i = 0; i < MBR_PARTITION_COUNT; ++i) {
        uint32_t first_block = partitons[i].first_block;

        if (partitons[i].first_block < lowest_block)
            lowest_block = partitons[i].first_block;
    }

    gap_size = (lowest_block - 1) * MBR_BLOCK_SIZE;

    if (gap_size < stage2_size) {
        fprintf(stderr, "Not enough space between MBR and the first partition to fit stage2!\n"
                        "Need at least %u, have %zu\n", stage2_size, gap_size);
        return false;
    }

    return true;
}

bool write_mbr(FILE *f, struct mbr_partition_entry *partitions)
{
    if (!write_at(f, 0, mbr_data, mbr_size))
        return false;

    return write_at(f, OFFSET_TO_MBR_PARTITION_LIST, partitions,
                    sizeof(struct mbr_partition_entry) * MBR_PARTITION_COUNT);
}

bool write_stage2(FILE *f)
{
    return write_at(f, MBR_BLOCK_SIZE, stage2_data, stage2_size);
}

bool write_hyper(const char *path, struct mbr_partition_entry *mbr_partitions)
{
    FILE *f = fopen(path, "r+b");
    bool ret = false;

    if (!f) {
        fprintf(stderr, "Failed to open %s: %s\n", path, strerror(errno));
        goto out;
    }

    if (!write_mbr(f, mbr_partitions))
        goto out;
    
    if (!write_stage2(f))
        goto out;

    ret = true;

out:
    if (f)
       fclose(f);
    return ret;
}

int main(int argc, char **argv)
{
    struct mbr_partition_entry mbr_partitions[MBR_PARTITION_COUNT];

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <path-where-to-install>\n", argv[0]);
        return 1;
    }

    if (!read_mbr_partition_list(argv[1], mbr_partitions))
        return 1;

    if (!check_stage2_fits(mbr_partitions))
        return 1;

    if (!write_hyper(argv[1], mbr_partitions))
        return 1;

    return 0;
}
