cmake_minimum_required(VERSION 3.16)
 
if (NOT PLATFORM)
    set(PLATFORM "BIOS")
elseif ((NOT PLATFORM MATCHES "BIOS") AND (NOT PLATFORM MATCHES "UEFI"))
    message(FATAL_ERROR "Unknown platform ${PLATFORM}")
endif ()

if (PLATFORM MATCHES "UEFI")
    message(FATAL_ERROR "UEFI is currently not supported!")
endif ()

set(COMPILER_PREFIX "i686")

execute_process(
    COMMAND /bin/bash ${CMAKE_SOURCE_DIR}/Toolchain/build.sh
    RESULT_VARIABLE TOOLCHAIN_BUILD_FAILED
)

if (NOT WIN32)
    if (TOOLCHAIN_BUILD_FAILED)
        message(FATAL_ERROR "-- Toolchain build error.")
    endif()
endif()

# dirty hack because it tries to compile with -lc and crt0.o
set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_CXX_COMPILER_WORKS 1)

set(TOOLCHAIN_ROOT ${CMAKE_SOURCE_DIR}/Toolchain/)
set(TOOLCHAIN_PATH ${TOOLCHAIN_ROOT}/bin)
set(PATH_TO_FILT "${TOOLCHAIN_PATH}/${COMPILER_PREFIX}-elf-c++filt")
set(TOOLCHAIN_PREFIX ${TOOLCHAIN_PATH}/${COMPILER_PREFIX}-elf-)

set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_LINKER ${TOOLCHAIN_PREFIX}ld)
set(CMAKE_RANLIB ${TOOLCHAIN_PREFIX}gcc-ranlib)
set(CMAKE_STRIP ${TOOLCHAIN_PREFIX}strip)
set(CMAKE_AR ${TOOLCHAIN_PREFIX}gcc-ar)

project(Hyper C CXX ASM)

set(ENV{HYPER_PLATFORM} ${PLATFORM})

if (WIN32)
    add_compile_options("/std:c++latest")
endif (WIN32)

if (PLATFORM MATCHES "BIOS")
    add_subdirectory(BIOS)
endif ()

add_subdirectory(Loader)
