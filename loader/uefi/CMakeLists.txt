target_link_options(
    ${LOADER_EXECUTABLE}
    PRIVATE
    -Wl,--subsystem,10
    -e EfiMain
    -s
    -T${CMAKE_CURRENT_SOURCE_DIR}/linker.ld
)

add_loader_nasm_flag(-fwin64)
add_loader_c_flag(-fno-stack-protector)

target_sources(
    ${LOADER_EXECUTABLE}
    PRIVATE
    uefi_handover.asm
)

target_sources(
    ${LOADER_EXECUTABLE}
    PRIVATE
    uefi_disk_services.c
    uefi_entry.c
    uefi_find_rsdp.c
    uefi_handover.c
    uefi_helpers.c
    uefi_memory_services.c
    uefi_video_services.c
)
