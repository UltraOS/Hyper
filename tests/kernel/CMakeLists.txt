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
set(CMAKE_LINK_DEPENDS_USE_LINKER FALSE)
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

    set(FULL_LOCAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/${ARG_LOCAL_PATH}")

    if (NOT EXISTS "${FULL_LOCAL_PATH}")
        add_custom_command(
            OUTPUT "${FULL_LOCAL_PATH}"
            COMMAND ${CMAKE_COMMAND} -E make_directory ${FULL_LOCAL_PATH}
        )

        set(FILE_DEPENDENCIES "DEPENDS;${FULL_LOCAL_PATH}")
    endif ()

    add_custom_command(
        OUTPUT "${FULL_LOCAL_PATH}/${ARG_FILE}"
        COMMAND ${CMAKE_COMMAND} -E copy
                ${LOADER_ROOT_DIR}/${ARG_PATH}/${ARG_FILE}
                ${FULL_LOCAL_PATH}
        ${FILE_DEPENDENCIES}
    )
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
LOADER_FILE(PATH "include/common" FILE "align.h")
LOADER_FILE(PATH "include/common" FILE "range.h")
LOADER_FILE(PATH "arch/x86/include/" FILE "pio.h" LOCAL_PATH "include")
LOADER_FILE(PATH "common" FILE "string.c")
LOADER_FILE(PATH "common" FILE "string_view.c")
LOADER_FILE(PATH "common" FILE "format.c")
LOADER_FILE(PATH "common" FILE "conversions.c")
LOADER_FILE(PATH "."      FILE "gcc_builtins.c")
LOADER_FILE(PATH "arch/x86/include/arch" FILE "constants.h" LOCAL_PATH "include/arch")
LOADER_FILE(PATH "boot_protocol/ultra_protocol" FILE "ultra_protocol.h" LOCAL_PATH "include")

get_property(EXTERNAL_LOADER_FILES_LOCAL GLOBAL PROPERTY EXTERNAL_LOADER_FILES)
add_custom_target(external_files DEPENDS "${EXTERNAL_LOADER_FILES_LOCAL}")

set(COMMON_C_FLAGS
    -std=gnu17
    -ffreestanding
    -O0
    -ggdb
    -Wall -Wextra -Werror
    -nostdlib
    -static
    -Wno-unused-variable
    -Wno-unused-function
)

function(add_kernel KERNEL_NAME TARGET_ARCH LINK_SCRIPT)
    add_executable(${KERNEL_NAME})
    add_dependencies(${KERNEL_NAME} external_files)
    set(KERNEL_ADDITIONAL_FLAGS "${ARGN}")

    if (TARGET_ARCH STREQUAL "i686" OR TARGET_ARCH STREQUAL "amd64")
        set(ARCH_TEST_CTL_FILE test_ctl_x86.c)
        set(KERNEL_ADDITIONAL_FLAGS
           ${KERNEL_ADDITIONAL_FLAGS}
           -mno-80387
           -mno-mmx
           -mno-3dnow
           -mno-sse
           -mno-sse2
        )
    elseif (TARGET_ARCH STREQUAL "aarch64")
        set(ARCH_TEST_CTL_FILE test_ctl_aarch64.c)
        set(KERNEL_ADDITIONAL_FLAGS
            ${KERNEL_ADDITIONAL_FLAGS}
            -mgeneral-regs-only)
        set(KERNEL_ADDITIONAL_LINK_FLAGS
            ${KERNEL_ADDITIONAL_LINK_FLAGS}
            -z max-page-size=0x1000)
    endif ()

    target_sources(
        ${KERNEL_NAME}
        PRIVATE
        fb_font.c
        fb_tty.c
        gcc_builtins.c
        kernel.c
        ultra_helpers.c
        common/log.c
        test_ctl_generic.c
        ${ARCH_TEST_CTL_FILE}
        ${EXTERNAL_LOADER_FILES_LOCAL}
    )

    target_compile_options(
        ${KERNEL_NAME}
        PRIVATE
        ${COMMON_C_FLAGS}
        --target=${TARGET_ARCH}-none-none
        ${KERNEL_ADDITIONAL_FLAGS}
    )

    target_link_options(
        ${KERNEL_NAME}
        PRIVATE
        -nostdlib
        "-T${PROJECT_SOURCE_DIR}/${LINK_SCRIPT}"
        ${KERNEL_ADDITIONAL_LINK_FLAGS}
    )

    target_include_directories(
        ${KERNEL_NAME}
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
endfunction()

add_kernel(kernel_i686_lower_half i686 link_1mb_generic.ld)
add_kernel(kernel_i686_higher_half i686 link_i686_higher_half.ld)
add_kernel(kernel_amd64_lower_half amd64 link_1mb_generic.ld)
add_kernel(kernel_amd64_higher_half amd64 link_generic_64bit_higher_half.ld -mcmodel=kernel)
add_kernel(kernel_aarch64_lower_half aarch64 link_1gb_generic.ld)
add_kernel(kernel_aarch64_higher_half aarch64 link_generic_64bit_higher_half.ld)

