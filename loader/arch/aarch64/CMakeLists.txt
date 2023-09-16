target_sources(
    ${LOADER_EXECUTABLE}
    PRIVATE
    handover.asm
    handover_impl.c
    elf.c
    virtual_memory.c
)

add_loader_c_flags(-mgeneral-regs-only -mno-red-zone)

target_include_directories(
    ${LOADER_EXECUTABLE}
    PRIVATE
    include
)

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${HYPER_PLATFORM}")
    add_subdirectory(${HYPER_PLATFORM})
endif ()

add_subdirectory(boot_protocol)
