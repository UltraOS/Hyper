cmake_minimum_required(VERSION 3.16)

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
    elf.c
    loader.c
    memory_services.c
    services_impl.c
    virtual_memory.c
)

set(STAGE2_BINARY ${LOADER_EXECUTABLE})
set(STAGE2_PATH ${CMAKE_CURRENT_BINARY_DIR})

add_loader_option(
    HYPER_E9_LOG
    "Enables 0xE9 debug logging"
    OFF
)

add_loader_option(
    HYPER_SERIAL_LOG
    "Enables serial debug logging"
    OFF
)

add_loader_option(
    HYPER_SERIAL_BAUD_RATE
    "Sets the baud rate for serial debug logging"
    9600
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

set(HYPER_ARCH_SUBDIR ${HYPER_ARCH})

if (HYPER_ARCH STREQUAL "i686" OR HYPER_ARCH STREQUAL "amd64")
    # Have to set this manually as cmake hardcodes '-f<native-binary-format>'
    # NOTE: this is here as opposed to x86/CMakeLists.txt because of a CMake
    #       bug where it complains about a required internal variable not
    #       being set. That can be fixed by setting CMAKE_ASM_NASM_COMPILE_OBJECT
    #       to PARENT_SCOPE, however, that seems to produce bogus nasm command
    #       lines where include directories are not even preceded by -I.
    set(
        CMAKE_ASM_NASM_COMPILE_OBJECT
        "<CMAKE_ASM_NASM_COMPILER> <DEFINES> <INCLUDES> <FLAGS> -o <OBJECT> <SOURCE>"
    )
    enable_language(ASM_NASM)

    set(HYPER_ARCH_SUBDIR "x86")
elseif (HYPER_ARCH STREQUAL "aarch64")
    # Make all aarch64 files compile with -x assembler-with-cpp
    # This is easier than setting up a separate assembler toolchain in cmake
    # and also allows us to use proper C preprocessor on those files.
    # NOTE:
    # Yes, this has to be done this way here. At least on CMake 3.22.1 where the
    # properties are somehow not propagated to parent even with TARGET_DIRECTORY
    # or DIRECTORY properties being set at the call site. Using absolute paths
    # doesn't work either. For now just glob all aarch64 asm files recursively
    # and change their properties right here.
    file(
        GLOB_RECURSE
        AARCH64_ASM_FILES
        arch/${HYPER_ARCH_SUBDIR}/*.asm
    )

    foreach(AARCH64_ASM_FILE IN LISTS AARCH64_ASM_FILES)
        set_source_files_properties(
            ${AARCH64_ASM_FILE}
            PROPERTIES
            LANGUAGE
            C
            COMPILE_OPTIONS
            # We have to do armv8.1a because that includes FEAT_VHE, which in
            # turn allows us to load TTBR1_EL2 in the assembly
            "-xassembler-with-cpp;-march=armv8.1a"
        )
    endforeach ()
endif ()

add_subdirectory(arch/${HYPER_ARCH_SUBDIR})
add_subdirectory(boot_protocol)
add_subdirectory(common)
add_subdirectory(filesystem)

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${HYPER_PLATFORM}")
    add_subdirectory(${HYPER_PLATFORM})
endif ()
