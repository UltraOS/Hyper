cmake_minimum_required(VERSION 3.16)

set(ENV{HYPER_PLATFORM} ${PLATFORM})

project(Loader)
enable_language(ASM_NASM)
enable_language(CXX)

if (PLATFORM MATCHES "BIOS")
    set(CMAKE_ASM_NASM_FLAGS "-felf32")
    set(PLATFORM_FOLDER "BIOS")
    set(EXECUTABLE_NAME "Hyper")
elseif (PLATFORM MATCHES "UEFI")
    set(CMAKE_ASM_NASM_FLAGS "-fwin64")
    set(PLATFORM_FOLDER "UEFI")
    set(ADDITIONAL_FLAGS "-fno-stack-protector")
    set(EXECUTABLE_NAME "BOOTX64.EFI")
else ()
    message(FATAL_ERROR "Unknown platform ${PLATFORM}")
endif ()

# Don't set flags if we're building cmake purely for the sake of syntax highlighting,
# because obviously MSVC doesn't support these flags.
if (NOT WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffreestanding")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wextra")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-use-cxa-atexit")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -nostdlib")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${ADDITIONAL_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-builtin-macro-redefined -D'__FILE__=\"$(subst $(realpath ${CMAKE_SOURCE_DIR})/,,$(abspath $<))\"'")
    set(CMAKE_ASM_NASM_COMPILE_OBJECT "<CMAKE_ASM_NASM_COMPILER> <FLAGS> -o <OBJECT> <SOURCE>")
endif ()

FILE(GLOB CPP_FILES "${PROJECT_SOURCE_DIR}/*cpp"                    "${PROJECT_SOURCE_DIR}/*h"
                    "${PROJECT_SOURCE_DIR}/Common/*cpp"             "${PROJECT_SOURCE_DIR}/Common/*h"
                    "${PROJECT_SOURCE_DIR}/FileSystem/*cpp"         "${PROJECT_SOURCE_DIR}/FileSystem/*h"
                    "${PROJECT_SOURCE_DIR}/FileSystem/FAT32/*cpp"   "${PROJECT_SOURCE_DIR}/FileSystem/FAT32/*h"
                    "${PROJECT_SOURCE_DIR}/${PLATFORM_FOLDER}/*cpp" "${PROJECT_SOURCE_DIR}/${PLATFORM_FOLDER}/*h")

FILE(GLOB ASM_FILES "${PROJECT_SOURCE_DIR}/${PLATFORM_FOLDER}/*asm")

set(LOADER_SOURCES ${CPP_FILES} ${ASM_FILES})

add_executable(${EXECUTABLE_NAME} ${LOADER_SOURCES})

target_include_directories(${EXECUTABLE_NAME} PRIVATE ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/..)

if (PLATFORM MATCHES "BIOS")
    set_target_properties(${EXECUTABLE_NAME} PROPERTIES LINK_FLAGS "-T ${PROJECT_SOURCE_DIR}/${PLATFORM_FOLDER}/Linker.ld -Wl,--oformat,binary")
    target_link_libraries(${EXECUTABLE_NAME} gcc)
else ()
    set_target_properties(${EXECUTABLE_NAME} PROPERTIES LINK_FLAGS "-Wl,-dll -Wl,--subsystem,10 -e EfiMain -s -Wl,-Bsymbolic -shared")
endif ()
