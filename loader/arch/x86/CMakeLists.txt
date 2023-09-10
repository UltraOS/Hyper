macro(add_loader_nasm_flags)
    add_loader_lang_flags(ASM_NASM ${ARGN})
endmacro()

target_sources(
    ${LOADER_EXECUTABLE}
    PRIVATE
    handover_impl.c
    elf.c
    virtual_memory.c
)

target_include_directories(
    ${LOADER_EXECUTABLE}
    PRIVATE
    include
)

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${HYPER_PLATFORM}")
    add_subdirectory(${HYPER_PLATFORM})
endif ()

add_subdirectory(boot_protocol)
