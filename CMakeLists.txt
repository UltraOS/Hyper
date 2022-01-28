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
    set(COMPILER_PREFIX "mingw64")
else ()
    set(COMPILER_PREFIX "i686-elf")
endif ()

if (NOT WIN32)
    execute_process(
        COMMAND /bin/bash ${CMAKE_SOURCE_DIR}/toolchain/build.sh ${PLATFORM}
        RESULT_VARIABLE TOOLCHAIN_BUILD_FAILED
    )
    if (TOOLCHAIN_BUILD_FAILED)
        message(FATAL_ERROR "-- Toolchain build error.")
    endif()

    # dirty hack because it tries to compile with -lc and crt0.o
    set(CMAKE_C_COMPILER_WORKS 1)

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
else ()
    add_compile_options("/std:c++latest")
endif()

project(Hyper C ASM)

set(ENV{HYPER_PLATFORM} ${PLATFORM})

add_subdirectory(loader)

if (PLATFORM MATCHES "bios")
    add_subdirectory(bios)
    set(MBR_FULL_PATH "${MBR_PATH}/${MBR_BINARY}")
    set(STAGE2_FULL_PATH "${STAGE2_PATH}/${STAGE2_BINARY}")

    add_custom_command(
        OUTPUT ${PROJECT_SOURCE_DIR}/installer/hyper_install
        COMMAND /bin/bash ${PROJECT_SOURCE_DIR}/installer/build.sh ${MBR_FULL_PATH} ${STAGE2_FULL_PATH}
        DEPENDS ${MBR_FULL_PATH} ${STAGE2_BINARY}
    )
    add_custom_target(installer ALL DEPENDS ${PROJECT_SOURCE_DIR}/installer/hyper_install)
endif()