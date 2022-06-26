cmake_minimum_required(VERSION 3.16)

set(CMAKE_C_COMPILER "clang")

project(test_kernel C)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu17")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ffreestanding")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ggdb")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mno-mmx")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mno-3dnow")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mno-sse")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mno-sse2")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wextra")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -nostdlib")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -static -Wno-unused-variable -Wno-unused-function")

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
LOADER_FILE(PATH "protocols/ultra_protocol" FILE "ultra_protocol.h" LOCAL_PATH "include")

get_property(EXTERNAL_LOADER_FILES_LOCAL GLOBAL PROPERTY EXTERNAL_LOADER_FILES)
add_custom_target(external_files DEPENDS "${EXTERNAL_LOADER_FILES_LOCAL}")

FILE(GLOB SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.c"        "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
                       "${CMAKE_CURRENT_SOURCE_DIR}/common/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/common/*.h")
LIST(APPEND SOURCE_FILES "${EXTERNAL_LOADER_FILES_LOCAL}")

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")

add_executable(kernel_i686 ${SOURCE_FILES})
add_dependencies(kernel_i686 external_files)
set_target_properties(kernel_i686 PROPERTIES COMPILE_FLAGS "--target=i686-none-none")
set_target_properties(kernel_i686 PROPERTIES LINK_FLAGS "--target=i686-none-none -T${PROJECT_SOURCE_DIR}/link_1mb_generic.ld")

add_executable(kernel_amd64_lower_half ${SOURCE_FILES})
add_dependencies(kernel_amd64_lower_half external_files)
set_target_properties(kernel_amd64_lower_half PROPERTIES COMPILE_FLAGS "--target=x86_64-none-none")
set_target_properties(kernel_amd64_lower_half PROPERTIES LINK_FLAGS "--target=x86_64-none-none -T${PROJECT_SOURCE_DIR}/link_1mb_generic.ld")

add_executable(kernel_amd64_higher_half ${SOURCE_FILES})
add_dependencies(kernel_amd64_higher_half external_files)
set_target_properties(kernel_amd64_higher_half PROPERTIES COMPILE_FLAGS "--target=x86_64-none-none -mcmodel=kernel")
set_target_properties(kernel_amd64_higher_half PROPERTIES LINK_FLAGS "--target=x86_64-none-none -mcmodel=kernel -T${PROJECT_SOURCE_DIR}/link_amd64_higher_half.ld")
