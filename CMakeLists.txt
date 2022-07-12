cmake_minimum_required(VERSION 3.16)

if (PLATFORM)
    string(TOLOWER ${PLATFORM} PLATFORM)
endif ()

if (NOT PLATFORM)
    set(PLATFORM "bios")
elseif ((NOT PLATFORM MATCHES "bios") AND (NOT PLATFORM MATCHES "uefi"))
    message(FATAL_ERROR "Unknown platform ${PLATFORM}")
endif ()

if (PLATFORM MATCHES "uefi")
    set(COMPILER_PREFIX "x86_64-w64-mingw32")
else ()
    set(COMPILER_PREFIX "i686-elf")
endif ()

if (NOT WIN32)
    execute_process(
        COMMAND python3 ${CMAKE_SOURCE_DIR}/toolchain/build.py ${PLATFORM}
        RESULT_VARIABLE TOOLCHAIN_BUILD_FAILED
        OUTPUT_FILE "/dev/stdout"
    )
    if (TOOLCHAIN_BUILD_FAILED)
        message(FATAL_ERROR "-- Toolchain build error.")
    endif()

    # dirty hack because it tries to compile with -lc and crt0.o
    set(CMAKE_C_COMPILER_WORKS 1)
    set(CMAKE_CXX_COMPILER_WORKS 1)

    set(TOOLCHAIN_ROOT ${CMAKE_SOURCE_DIR}/toolchain/tools_${PLATFORM})
    set(TOOLCHAIN_PATH ${TOOLCHAIN_ROOT}/bin)
    set(PATH_TO_FILT "${TOOLCHAIN_PATH}/${COMPILER_PREFIX}-c++filt")
    set(TOOLCHAIN_PREFIX ${TOOLCHAIN_PATH}/${COMPILER_PREFIX}-)

    set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}gcc)
    set(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc)
    set(CMAKE_LINKER ${TOOLCHAIN_PREFIX}ld)
    set(CMAKE_RANLIB ${TOOLCHAIN_PREFIX}gcc-ranlib)
    set(CMAKE_STRIP ${TOOLCHAIN_PREFIX}strip)
    set(CMAKE_AR ${TOOLCHAIN_PREFIX}gcc-ar)

    # We override CMAKE_C_COMPILE_OBJECT, which breaks this option
    set(CMAKE_DEPENDS_USE_COMPILER FALSE)
else ()
    add_compile_options("/std:c++latest")
endif()

project(Hyper C ASM_NASM)

set(ENV{HYPER_PLATFORM} ${PLATFORM})

add_subdirectory(loader)

if (PLATFORM MATCHES "bios")
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
