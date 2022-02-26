cmake_minimum_required(VERSION 3.16)

set(ENV{HYPER_PLATFORM} ${PLATFORM})

project(Loader)
enable_language(ASM_NASM)
enable_language(C)

if (PLATFORM MATCHES "bios")
    set(CMAKE_ASM_NASM_FLAGS "-felf32")
    set(EXECUTABLE_NAME "hyper")
elseif (PLATFORM MATCHES "uefi")
    set(CMAKE_ASM_NASM_FLAGS "-fwin64")
    set(ADDITIONAL_FLAGS "-fno-stack-protector")
    set(EXECUTABLE_NAME "BOOTX64.EFI")
else ()
    message(FATAL_ERROR "Unknown platform ${PLATFORM}")
endif ()

# Don't set flags if we're building cmake purely for the sake of syntax highlighting,
# because obviously MSVC doesn't support these flags.
if (NOT WIN32)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu17")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ffreestanding")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wextra")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -nostdlib")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${ADDITIONAL_FLAGS}")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-builtin-macro-redefined -D'__FILE__=\"$(subst $(realpath ${CMAKE_SOURCE_DIR})/,,$(abspath $<))\"'")
    set(CMAKE_ASM_NASM_COMPILE_OBJECT "<CMAKE_ASM_NASM_COMPILER> <FLAGS> -o <OBJECT> <SOURCE>")
endif ()

FILE(GLOB C_FILES "${PROJECT_SOURCE_DIR}/*c"                    "${PROJECT_SOURCE_DIR}/*h"
                  "${PROJECT_SOURCE_DIR}/common/*c"             "${PROJECT_SOURCE_DIR}/common/*h"
                  "${PROJECT_SOURCE_DIR}/elf/*c"                "${PROJECT_SOURCE_DIR}/elf/*h"
                  "${PROJECT_SOURCE_DIR}/filesystem/*c"         "${PROJECT_SOURCE_DIR}/filesystem/*h"
                  "${PROJECT_SOURCE_DIR}/filesystem/fat32/*c"   "${PROJECT_SOURCE_DIR}/filesystem/fat32/*h"
                  "${PROJECT_SOURCE_DIR}/protocols/*c"          "${PROJECT_SOURCE_DIR}/protocols/*h"
                  "${PROJECT_SOURCE_DIR}/${PLATFORM}/*c"        "${PROJECT_SOURCE_DIR}/${PLATFORM}/*h")

FILE(GLOB ASM_FILES "${PROJECT_SOURCE_DIR}/${PLATFORM}/*asm")

set(LOADER_SOURCES ${C_FILES} ${ASM_FILES})

add_executable(${EXECUTABLE_NAME} ${LOADER_SOURCES})
target_include_directories(${EXECUTABLE_NAME} PRIVATE ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/..)
target_link_libraries(${EXECUTABLE_NAME} gcc)

if (PLATFORM MATCHES "bios")
    set_target_properties(${EXECUTABLE_NAME} PROPERTIES LINK_FLAGS "-T ${PROJECT_SOURCE_DIR}/${PLATFORM}/linker.ld -Wl,--oformat,binary")
else ()
    set_target_properties(${EXECUTABLE_NAME} PROPERTIES LINK_FLAGS "-Wl,-dll -Wl,--subsystem,10 -e EfiMain -s -Wl,-Bsymbolic -shared")
endif ()

set(STAGE2_BINARY ${EXECUTABLE_NAME}  PARENT_SCOPE)
set(STAGE2_PATH ${PROJECT_BINARY_DIR} PARENT_SCOPE)
