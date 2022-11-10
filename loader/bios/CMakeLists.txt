target_link_options(
    ${LOADER_EXECUTABLE}
    PRIVATE
    --oformat binary
    -T${CMAKE_CURRENT_SOURCE_DIR}/linker.ld
    -nostdlib
    --orphan-handling=error
)

add_loader_nasm_flags(-felf32)

add_loader_c_flags(
    -mno-80387
    -mno-mmx
    -mno-3dnow
    -mno-sse
    -mno-sse2
)

target_sources(
    ${LOADER_EXECUTABLE}
    PRIVATE
    a20.asm
    bios_call.asm
    bios_entry.asm
    bios_handover.asm
)

target_sources(
    ${LOADER_EXECUTABLE}
    PRIVATE
    bios_disk_services.c
    bios_entry.c
    bios_find_rsdp.c
    bios_handover.c
    bios_memory_services.c
    bios_video_services.c
)
