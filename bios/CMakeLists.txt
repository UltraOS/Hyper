cmake_minimum_required(VERSION 3.10)

project(MBR ASM_NASM)

# Have to set this manually as cmake hardcodes '-f<native-binary-format>'
set(
    CMAKE_ASM_NASM_COMPILE_OBJECT
    "<CMAKE_ASM_NASM_COMPILER> -fbin <DEFINES> <INCLUDES> <FLAGS> -o <OBJECT> <SOURCE>"
)

# Prevent cmake from attempting to link the flat binary
set(CMAKE_ASM_NASM_LINK_EXECUTABLE "cp <OBJECTS> <TARGET>")

add_executable(hyper_mbr hyper_boot_record.asm)

add_executable(hyper_iso_mbr hyper_boot_record.asm)
target_compile_definitions(hyper_iso_mbr PUBLIC HYPER_ISO_MBR)

add_executable(hyper_iso_boot_rec hyper_boot_record.asm)
target_compile_definitions(hyper_iso_boot_rec PUBLIC HYPER_ISO_BOOT_RECORD)

set(MBR_BINARY          hyper_mbr             PARENT_SCOPE)
set(ISO_MBR_BINARY      hyper_iso_mbr         PARENT_SCOPE)
set(ISO_BOOT_REC_BINARY hyper_iso_boot_rec    PARENT_SCOPE)
set(MBR_PATH            ${PROJECT_BINARY_DIR} PARENT_SCOPE)
