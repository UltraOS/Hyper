cmake_minimum_required(VERSION 3.16)

if (HYPER_PLATFORM)
    string(TOLOWER ${HYPER_PLATFORM} HYPER_PLATFORM)
endif ()

if (HYPER_ARCH)
    string(TOLOWER ${HYPER_ARCH} HYPER_ARCH)
endif ()

if (NOT HYPER_ARCH)
    set(HYPER_ARCH "i686")
elseif ((NOT HYPER_ARCH STREQUAL "i686") AND
        (NOT HYPER_ARCH STREQUAL "amd64") AND
        (NOT HYPER_ARCH STREQUAL "aarch64"))
    message(FATAL_ERROR "Unknown architecture '${HYPER_ARCH}'")
endif ()

if (NOT HYPER_PLATFORM)
    if (HYPER_ARCH AND NOT HYPER_ARCH STREQUAL "i686")
        set(HYPER_PLATFORM "uefi")
    else ()
        set(HYPER_ARCH "i686")
        set(HYPER_PLATFORM "bios")
    endif ()
elseif ((NOT HYPER_PLATFORM STREQUAL "bios") AND
        (NOT HYPER_PLATFORM STREQUAL "uefi"))
    message(FATAL_ERROR "Unknown platform '${HYPER_PLATFORM}'")
endif ()

if (HYPER_TOOLCHAIN)
    string(TOLOWER ${HYPER_TOOLCHAIN} HYPER_TOOLCHAIN)
endif ()

if (NOT HYPER_TOOLCHAIN)
    set(HYPER_TOOLCHAIN "gcc")
elseif (NOT HYPER_TOOLCHAIN STREQUAL "gcc" AND
        NOT HYPER_TOOLCHAIN STREQUAL "clang")
    message(FATAL_ERROR "Unknown toolchain '${HYPER_TOOLCHAIN}'")
endif ()

if ($ENV{CLION_IDE})
    set(HYPER_TOOLCHAIN dummy)
    message("Detected CLion, using a dummy toolchain file")
endif ()

set(HYPER_TOOLCHAIN_DIR "${CMAKE_SOURCE_DIR}/toolchain")
include("${HYPER_TOOLCHAIN_DIR}/toolchain_${HYPER_TOOLCHAIN}.cmake")

project(Hyper C)

add_subdirectory(loader)
