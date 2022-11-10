cmake_minimum_required(VERSION 3.16)

project(Loader ASM_NASM C)

set(LOADER_EXECUTABLE "hyper_${HYPER_PLATFORM}")

function(add_loader_lang_flags LANG)
    list(
         TRANSFORM ARGN
         REPLACE ".+" "$<$<COMPILE_LANGUAGE:${LANG}>:\\0>"
         OUTPUT_VARIABLE TRANSFORMED_FLAGS
    )

    target_compile_options(
        ${LOADER_EXECUTABLE}
        PRIVATE
        ${TRANSFORMED_FLAGS}
    )
endfunction()

macro(add_loader_c_flags)
    add_loader_lang_flags(C ${ARGN})
endmacro()

macro(add_loader_nasm_flags)
    add_loader_lang_flags(ASM_NASM ${ARGN})
endmacro()

macro(add_loader_definition VALUE)
    target_compile_definitions(
        ${LOADER_EXECUTABLE}
        PUBLIC
        ${VALUE}
    )
endmacro()

macro(add_loader_option OPTION DESCRIPTION DEFAULT_VALUE)
    option(${OPTION} ${DESCRIPTION} ${DEFAULT_VALUE})

    if (${OPTION})
        add_loader_definition(${OPTION})
    endif()
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

add_loader_option(
    HYPER_E9_LOG
    "Enables 0xE9 debug logging"
    OFF
)

add_loader_option(
    HYPER_ALLOCATION_AUDIT
    "Enables the dynamic memory allocation audit logging"
    OFF
)

add_loader_option(
    HYPER_STRIP_INFO_LOG
    "Strips all of the info-level logs improving performance & reducing the executable size"
    OFF
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

add_loader_c_flags(
    -std=gnu17
    -ffreestanding
    -O2
    -Wall -Wextra -Werror
    -nostdlib
)

add_subdirectory(boot_protocol)
add_subdirectory(common)
add_subdirectory(elf)
add_subdirectory(filesystem)
add_subdirectory(${HYPER_PLATFORM})

set(STAGE2_BINARY ${LOADER_EXECUTABLE} PARENT_SCOPE)
set(STAGE2_PATH ${PROJECT_BINARY_DIR} PARENT_SCOPE)
