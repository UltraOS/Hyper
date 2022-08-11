cmake_minimum_required(VERSION 3.16)

project(Loader ASM_NASM C)

set(LOADER_EXECUTABLE "hyper_${HYPER_PLATFORM}")

macro(add_loader_lang_flag LANG FLAG)
    target_compile_options(
        ${LOADER_EXECUTABLE}
        PRIVATE
        $<$<COMPILE_LANGUAGE:${LANG}>:${FLAG}>
    )
endmacro()

macro(add_loader_c_flag FLAG)
    add_loader_lang_flag(C ${FLAG})
endmacro()

macro(add_loader_nasm_flag FLAG)
    add_loader_lang_flag(ASM_NASM ${FLAG})
endmacro()

add_executable(
    ${LOADER_EXECUTABLE}
    allocator.c
    config.c
    edid.c
    loader.c
    memory_services.c
    services_impl.c
    virtual_memory.c
)

target_include_directories(
    ${LOADER_EXECUTABLE}
    PRIVATE
    include
    "${HYPER_PLATFORM}/include"
)

if (HYPER_TOOLCHAIN STREQUAL "gcc")
    execute_process(
        COMMAND
        ${CMAKE_C_COMPILER} -print-libgcc-file-name
        OUTPUT_VARIABLE
        LIBGCC_PATH
        RESULT_VARIABLE
        LIBGCC_RESULT
    )
    if (NOT LIBGCC_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to detect libgcc path")
    endif ()

    string(STRIP "${LIBGCC_PATH}" LIBGCC_PATH)

    target_link_libraries(
        ${LOADER_EXECUTABLE}
        PRIVATE
        ${LIBGCC_PATH}
    )
else ()
    target_sources(
        ${LOADER_EXECUTABLE}
        PRIVATE
        gcc_builtins.c
    )
endif ()

add_loader_c_flag(-std=gnu17)
add_loader_c_flag(-ffreestanding)
add_loader_c_flag(-O2)
add_loader_c_flag(-Wall)
add_loader_c_flag(-Wextra)
add_loader_c_flag(-Werror)
add_loader_c_flag(-nostdlib)

add_subdirectory(boot_protocol)
add_subdirectory(common)
add_subdirectory(elf)
add_subdirectory(filesystem)
add_subdirectory(${HYPER_PLATFORM})

set(STAGE2_BINARY ${LOADER_EXECUTABLE} PARENT_SCOPE)
set(STAGE2_PATH ${PROJECT_BINARY_DIR} PARENT_SCOPE)
