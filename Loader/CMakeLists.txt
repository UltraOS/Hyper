cmake_minimum_required(VERSION 3.16)

set(ENV{HYPER_PLATFORM} ${PLATFORM})

project(Loader)
enable_language(ASM_NASM)
enable_language(CXX)

if (PLATFORM MATCHES "BIOS")
    set(CMAKE_ASM_NASM_FLAGS "-felf32")
    add_definitions(-DHYPER_PLATFORM_BIOS)
    set(PLATFORM_FOLDER "BIOS")
endif ()

# Don't set flags if we're building cmake purely for the sake of syntax highlighting,
# because obviously MSVC doesn't support these flags.
if (NOT WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
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
                    "${PROJECT_SOURCE_DIR}/${PLATFORM_FOLDER}/*cpp" "${PROJECT_SOURCE_DIR}/${PLATFORM_FOLDER}/*h")

FILE(GLOB ASM_FILES "${PROJECT_SOURCE_DIR}/${PLATFORM_FOLDER}/*asm")

set(LOADER_SOURCES ${CPP_FILES} ${ASM_FILES})
add_executable(Hyper ${LOADER_SOURCES})

target_include_directories(Hyper PRIVATE ${PROJECT_SOURCE_DIR})

if (PLATFORM MATCHES "BIOS")
    set_target_properties(Hyper PROPERTIES LINK_FLAGS "-T ${PROJECT_SOURCE_DIR}/${PLATFORM_FOLDER}/Linker.ld -Wl,--oformat,binary")
    target_link_libraries(Hyper gcc)
endif ()
