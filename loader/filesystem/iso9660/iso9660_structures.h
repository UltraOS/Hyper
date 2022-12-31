#pragma once

#include "common/types.h"
#include "common/bug.h"
#include "common/helpers.h"

/*
 * ---------------------------------------------------------------------------------------
 * Each Logical Sector shall consist of a number of bytes equal to 2048 or 2^n,
 * whichever is larger, where n is the largest integer such that 2^n is less than,
 * or equal to, the number of bytes in the Data Field of any sector recorded on the volume.
 * ---------------------------------------------------------------------------------------
 * Technically, the spec allows disks with a sector size larger than 2048, but we don't support them.
 */
#define ISO9660_LOGICAL_SECTOR_SIZE 2048
#define ISO9660_LOGICAL_SECTOR_SHIFT 11

#define ISO9660_SYSTEM_AREA_BLOCKS 16

#define ECMA119_BP(from, to) [to - from + 1]

// 7.1.1 8-bit unsigned numerical values
static inline u8 ecma119_get_711(void *field)
{
    return *(u8*)field;
}

// 7.1.2 8-bit signed numerical values
static inline i8 ecma119_get_712(void *field)
{
    return *(i8*)field;
}

// 7.3.1 Least significant byte first
static inline u32 ecma119_get_731(void *field)
{
    return *(u32*)field;
}

// 7.2.3 Both-byte orders (2 bytes)
static inline u16 ecma119_get_723(void *field)
{
    return *(u16*)field;
}

// 7.3.3 Both-byte orders (4 bytes)
static inline u32 ecma119_get_733(void *field)
{
    return *(u32*)field;
}

enum vd_type {
    VD_TYPE_BOOT_RECORD   = 0,
    VD_TYPE_PRIMARY       = 1,
    VD_TYPE_SUPPLEMENTARY = 2,
    VD_TYPE_PARTITION     = 3,
    VD_TYPE_TERMINATOR    = 255
};

#define ISO9660_IDENTIFIER "CD001"

struct PACKED iso9660_vd {
    u8 descriptor_type_711                  ECMA119_BP(1, 1);
    char standard_identifier                ECMA119_BP(2, 6);
    u8 volume_descriptor_version_711        ECMA119_BP(7, 7);
    u8 data                                 ECMA119_BP(8, 2048);
};

struct PACKED iso9660_pvd {
    u8 descriptor_type_711                  ECMA119_BP(1, 1);
    char standard_identifier                ECMA119_BP(2, 6);
    u8 volume_descriptor_version_711        ECMA119_BP(7, 7);
    u8 unused_field_1                       ECMA119_BP(8, 8);
    char system_identifier                  ECMA119_BP(9, 40);
    char volume_identifier                  ECMA119_BP(41, 72);
    u8 unused_field_2                       ECMA119_BP(73, 80);
    u8 volume_space_size_733                ECMA119_BP(81, 88);
    u8 unused_field_3                       ECMA119_BP(89, 120);
    u8 volume_set_size_723                  ECMA119_BP(121, 124);
    u8 volume_sequence_number_723           ECMA119_BP(125, 128);
    u8 logical_block_size_723               ECMA119_BP(129, 132);
    u8 path_table_size_733                  ECMA119_BP(133, 140);
    u8 type_l_path_table_location_731       ECMA119_BP(141, 144);
    u8 optional_le_path_table_location_731  ECMA119_BP(145, 148);
    u8 be_path_table_location_732           ECMA119_BP(149, 152);
    u8 optional_be_path_table_location_732  ECMA119_BP(153, 156);
    u8 root_directory_entry                 ECMA119_BP(157, 190);
    char volume_set_identifier              ECMA119_BP(191, 318);
    char publisher_identifier               ECMA119_BP(319, 446);
    char data_preparer_identifier           ECMA119_BP(447, 574);
    char application_identifier             ECMA119_BP(575, 702);
    char copyright_file_identifier          ECMA119_BP(703, 739);
    char abstract_file_identifier           ECMA119_BP(740, 776);
    char bibliographic_file_identifier      ECMA119_BP(777, 813);
    u8 volume_creation_date                 ECMA119_BP(814, 830);
    u8 volume_modification_date             ECMA119_BP(831, 847);
    u8 volume_expiration_date               ECMA119_BP(848, 864);
    u8 volume_effective_date                ECMA119_BP(865, 881);
    u8 file_structure_version               ECMA119_BP(882, 882);
    u8 reserved_field_1                     ECMA119_BP(883, 883);
    u8 application_used                     ECMA119_BP(884, 1395);
    u8 reserved_field_2                     ECMA119_BP(1396, 2048);
};
BUILD_BUG_ON(sizeof(struct iso9660_pvd) != 2048);

struct PACKED iso9660_dir_record {
    u8 record_length_711                    ECMA119_BP(1, 1);
    u8 extended_attr_rec_length_711         ECMA119_BP(2, 2);
    u8 location_of_extent_733               ECMA119_BP(3, 10);
    u8 data_length_733                      ECMA119_BP(11, 18);
    u8 date_and_time                        ECMA119_BP(19, 25);
    u8 flags_711                            ECMA119_BP(26, 26);
    u8 unit_size_711                        ECMA119_BP(27, 27);
    u8 interleave_gap_size_711              ECMA119_BP(28, 28);
    u8 volume_seq_num_723                   ECMA119_BP(29, 32);
    u8 identifier_length_711                ECMA119_BP(33, 33);
    char identifier[];
};
BUILD_BUG_ON(sizeof(struct iso9660_dir_record) != 33);

#define ISO9660_HIDDEN_DIR (1 << 0)
#define ISO9660_SUBDIR     (1 << 1)
#define ISO9660_ASSOC_FILE (1 << 2)
#define ISO9660_RECORD     (1 << 3)
#define ISO9660_PROT       (1 << 4)
#define ISO9660_MULTI_EXT  (1 << 7)
