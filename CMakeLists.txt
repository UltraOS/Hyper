cmake_minimum_required(VERSION 3.16)

if (HYPER_PLATFORM)
    string(TOLOWER ${HYPER_PLATFORM} HYPER_PLATFORM)
endif ()

if (NOT HYPER_PLATFORM)
    set(HYPER_PLATFORM "bios")
elseif ((NOT HYPER_PLATFORM STREQUAL "bios") AND
        (NOT HYPER_PLATFORM STREQUAL "uefi"))
    message(FATAL_ERROR "Unknown platform '${HYPER_PLATFORM}'")
endif ()

if (HYPER_PLATFORM STREQUAL "uefi")
    set(COMPILER_PREFIX x86_64-w64-mingw32)
    set(CMAKE_SYSTEM_PROCESSOR x86_64)
else ()
    set(COMPILER_PREFIX i686-elf)
    set(CMAKE_SYSTEM_PROCESSOR i686)
endif ()

execute_process(
    COMMAND python3 ${CMAKE_SOURCE_DIR}/toolchain/build.py ${HYPER_PLATFORM}
    RESULT_VARIABLE TOOLCHAIN_BUILD_FAILED
    OUTPUT_FILE "/dev/stdout"
)
if (TOOLCHAIN_BUILD_FAILED)
    message(FATAL_ERROR "-- Toolchain build error.")
endif()

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSROOT "")

# Do this because cmake attempts to link against -lc and crt0
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(TOOLCHAIN_ROOT ${CMAKE_SOURCE_DIR}/toolchain/tools_${HYPER_PLATFORM})
set(TOOLCHAIN_PATH ${TOOLCHAIN_ROOT}/bin)
set(TOOLCHAIN_PREFIX ${TOOLCHAIN_PATH}/${COMPILER_PREFIX}-)

set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_LINKER ${TOOLCHAIN_PREFIX}ld)

# Have to set this manually as cmake hardcodes '-f<native-binary-format>'
set(
    CMAKE_ASM_NASM_COMPILE_OBJECT
    "<CMAKE_ASM_NASM_COMPILER> <DEFINES> <INCLUDES> <FLAGS> -o <OBJECT> <SOURCE>"
)

project(Hyper C ASM_NASM)

add_subdirectory(loader)

if (HYPER_PLATFORM STREQUAL "bios")
    add_subdirectory(bios)

    set(MBR_FULL_PATH "${MBR_PATH}/${MBR_BINARY}")
    set(ISO_MBR_FULL_PATH "${MBR_PATH}/${ISO_MBR_BINARY}")
    set(ISO_BOOT_REC_FULL_PATH "${MBR_PATH}/${ISO_BOOT_REC_BINARY}")
    set(STAGE2_FULL_PATH "${STAGE2_PATH}/${STAGE2_BINARY}")
    set(ISO_STAGE2_FULL_PATH "${STAGE2_PATH}/hyper_iso_boot")

    add_custom_command(
        OUTPUT
        ${ISO_STAGE2_FULL_PATH}
        COMMAND
        cat ${ISO_BOOT_REC_FULL_PATH} ${STAGE2_FULL_PATH} > ${ISO_STAGE2_FULL_PATH}
        DEPENDS
        ${ISO_BOOT_REC_BINARY} ${STAGE2_BINARY}
        COMMENT
        "Generating iso stage2 loader"
    )
    add_custom_target(hyper_iso_boot ALL DEPENDS ${ISO_STAGE2_FULL_PATH})

    include(ExternalProject)
    ExternalProject_Add(
        installer
        SOURCE_DIR
        ${PROJECT_SOURCE_DIR}/installer
        CMAKE_ARGS
        -DMBR_PATH=${MBR_FULL_PATH}
        -DISO_MBR_PATH=${ISO_MBR_FULL_PATH}
        -DSTAGE2_PATH=${STAGE2_FULL_PATH}
        INSTALL_COMMAND
        cmake -E echo "Skipping install step"
    )
    ExternalProject_Add_StepDependencies(
        installer
        build
        ${MBR_BINARY}
        ${ISO_MBR_BINARY}
        ${STAGE2_BINARY}
    )
endif()
