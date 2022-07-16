cmake_minimum_required(VERSION 3.16)

if (APPLE)
    execute_process(
        COMMAND
        brew --prefix llvm
        OUTPUT_VARIABLE
        BREW_LLVM_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set(LLD_LINKER "${BREW_LLVM_PREFIX}/bin/ld.lld")
else ()
    set(LLD_LINKER "ld.lld")
endif ()

set(CMAKE_C_COMPILER "clang")
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSROOT "")
set(
    CMAKE_C_LINK_EXECUTABLE
    "${LLD_LINKER} <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> \
     <OBJECTS> -o <TARGET> <LINK_LIBRARIES>"
)

project(test_kernel C)

set(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(LOADER_ROOT_DIR "${PROJECT_ROOT}/loader")

function(LOADER_FILE)
    cmake_parse_arguments(
        ARG
        ""
        "PATH;FILE;LOCAL_PATH"
        ""
        ${ARGN}
    )

    if (NOT ARG_LOCAL_PATH)
        set(ARG_LOCAL_PATH ${ARG_PATH})
    endif ()

    get_property(EXTERNAL_LOADER_FILES_LOCAL GLOBAL PROPERTY EXTERNAL_LOADER_FILES)
    list(APPEND EXTERNAL_LOADER_FILES_LOCAL "${CMAKE_CURRENT_SOURCE_DIR}/${ARG_LOCAL_PATH}/${ARG_FILE}")
    set_property(GLOBAL PROPERTY EXTERNAL_LOADER_FILES ${EXTERNAL_LOADER_FILES_LOCAL})

    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/${ARG_LOCAL_PATH}/${ARG_FILE}"
        COMMAND ${CMAKE_COMMAND} -E copy
                ${LOADER_ROOT_DIR}/${ARG_PATH}/${ARG_FILE}
                ${CMAKE_CURRENT_SOURCE_DIR}/${ARG_LOCAL_PATH})
endfunction(LOADER_FILE)

LOADER_FILE(PATH "include/common" FILE "types.h")
LOADER_FILE(PATH "include/common" FILE "string.h")
LOADER_FILE(PATH "include/common" FILE "ctype.h")
LOADER_FILE(PATH "include/common" FILE "attributes.h")
LOADER_FILE(PATH "include/common" FILE "helpers.h")
LOADER_FILE(PATH "include/common" FILE "minmax.h")
LOADER_FILE(PATH "include/common" FILE "bug.h")
LOADER_FILE(PATH "include/common" FILE "conversions.h")
LOADER_FILE(PATH "include/common" FILE "panic.h")
LOADER_FILE(PATH "include/common" FILE "constants.h")
LOADER_FILE(PATH "include/common" FILE "string_view.h")
LOADER_FILE(PATH "include/common" FILE "format.h")
LOADER_FILE(PATH "include/common" FILE "cpuid.h")
LOADER_FILE(PATH "common" FILE "string.c")
LOADER_FILE(PATH "common" FILE "string_view.c")
LOADER_FILE(PATH "common" FILE "format.c")
LOADER_FILE(PATH "common" FILE "cpuid.c")
LOADER_FILE(PATH "common" FILE "conversions.c")
LOADER_FILE(PATH "boot_protocol/ultra_protocol" FILE "ultra_protocol.h" LOCAL_PATH "include")

get_property(EXTERNAL_LOADER_FILES_LOCAL GLOBAL PROPERTY EXTERNAL_LOADER_FILES)
add_custom_target(external_files DEPENDS "${EXTERNAL_LOADER_FILES_LOCAL}")

FILE(GLOB SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.c"        "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
                       "${CMAKE_CURRENT_SOURCE_DIR}/common/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/common/*.h")
LIST(APPEND SOURCE_FILES "${EXTERNAL_LOADER_FILES_LOCAL}")

set(COMMON_C_FLAGS
    -std=gnu17
    -ffreestanding
    -O0
    -ggdb
    -mno-mmx -mno-3dnow -mno-sse -mno-sse2
    -Wall -Wextra -Werror
    -nostdlib
    -static
    -Wno-unused-variable
    -Wno-unused-function
)

macro(add_kernel KERNEL_NAME TARGET_ARCH LINK_SCRIPT)
    add_executable(${KERNEL_NAME} ${SOURCE_FILES})
    add_dependencies(${KERNEL_NAME} external_files)

    target_compile_options(
        ${KERNEL_NAME}
        PRIVATE
        ${COMMON_C_FLAGS}
        --target=${TARGET_ARCH}-none-none
        ${ARGN}
    )

    target_link_options(
        ${KERNEL_NAME}
        PRIVATE
        -nostdlib
        "-T${PROJECT_SOURCE_DIR}/${LINK_SCRIPT}"
    )

    target_include_directories(
        ${KERNEL_NAME}
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
endmacro()

add_kernel(kernel_i686 i686 link_1mb_generic.ld)
add_kernel(kernel_amd64_lower_half amd64 link_1mb_generic.ld)
add_kernel(kernel_amd64_higher_half amd64 link_amd64_higher_half.ld -mcmodel=kernel)
