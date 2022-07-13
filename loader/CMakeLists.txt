cmake_minimum_required(VERSION 3.16)

project(Loader ASM_NASM C)

if (HYPER_PLATFORM MATCHES bios)
    set(CMAKE_ASM_NASM_FLAGS -felf32)
    set(LOADER_EXECUTABLE hyper)
    set(LINK_FLAGS "-T${PROJECT_SOURCE_DIR}/${HYPER_PLATFORM}/linker.ld" -Wl,--oformat,binary)
elseif (HYPER_PLATFORM MATCHES uefi)
    set(CMAKE_ASM_NASM_FLAGS -fwin64)
    set(ADDITIONAL_FLAGS -fno-stack-protector)
    set(LOADER_EXECUTABLE BOOTX64.EFI)
    set(LINK_FLAGS -Wl,-dll -Wl,--subsystem,10 -e EfiMain -s -Wl,-Bsymbolic -shared)
else ()
    message(FATAL_ERROR "Unknown platform ${HYPER_PLATFORM}")
endif ()

macro(add_loader_c_flag FLAG)
    target_compile_options(
        ${LOADER_EXECUTABLE}
        PRIVATE
        $<$<COMPILE_LANGUAGE:C>:${FLAG}>
    )
endmacro()

FILE(GLOB C_FILES "${PROJECT_SOURCE_DIR}/*c"                    "${PROJECT_SOURCE_DIR}/*h"
                  "${PROJECT_SOURCE_DIR}/common/*c"             "${PROJECT_SOURCE_DIR}/common/*h"
                  "${PROJECT_SOURCE_DIR}/elf/*c"                "${PROJECT_SOURCE_DIR}/elf/*h"
                  "${PROJECT_SOURCE_DIR}/filesystem/*c"         "${PROJECT_SOURCE_DIR}/filesystem/*h"
                  "${PROJECT_SOURCE_DIR}/filesystem/fat/*c"     "${PROJECT_SOURCE_DIR}/filesystem/fat/*h"
                  "${PROJECT_SOURCE_DIR}/filesystem/iso9660/*c" "${PROJECT_SOURCE_DIR}/filesystem/iso9660/*h"
                  "${PROJECT_SOURCE_DIR}/protocols/*c"          "${PROJECT_SOURCE_DIR}/protocols/*h"
                  "${PROJECT_SOURCE_DIR}/${HYPER_PLATFORM}/*c"  "${PROJECT_SOURCE_DIR}/${HYPER_PLATFORM}/*h")
FILE(GLOB ASM_FILES "${PROJECT_SOURCE_DIR}/${HYPER_PLATFORM}/*asm")
set(LOADER_SOURCES ${C_FILES} ${ASM_FILES})

add_executable(${LOADER_EXECUTABLE} ${LOADER_SOURCES})

target_include_directories(
    ${LOADER_EXECUTABLE}
    PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/${HYPER_PLATFORM}/include"
)

target_link_libraries(
    ${LOADER_EXECUTABLE}
    PRIVATE
    gcc
)

add_loader_c_flag(-std=gnu17)
add_loader_c_flag(-ffreestanding)
add_loader_c_flag(-O2)
add_loader_c_flag(-Wall)
add_loader_c_flag(-Wextra)
add_loader_c_flag(-Werror)
add_loader_c_flag(-nostdlib)

target_link_options(
    ${LOADER_EXECUTABLE}
    PRIVATE
    ${LINK_FLAGS}
    -nostdlib
    -Wl,--orphan-handling=error
)

set(STAGE2_BINARY ${LOADER_EXECUTABLE} PARENT_SCOPE)
set(STAGE2_PATH ${PROJECT_BINARY_DIR} PARENT_SCOPE)
