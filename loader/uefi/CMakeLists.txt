target_link_options(
    ${LOADER_EXECUTABLE}
    PRIVATE
    -Wl,--subsystem,10
    -Wl,--entry,EfiMain
    -Wl,--strip-all
    -Wl,--script,${CMAKE_CURRENT_SOURCE_DIR}/linker.ld
    -Wl,--orphan-handling=error

    # Workaround for a "feature" where it appends .exe to the output file
    -Wl,--output,${LOADER_EXECUTABLE}

    -nostdlib
)

add_loader_nasm_flag(-fwin64)
add_loader_c_flag(-fno-stack-protector)
add_loader_c_flag(-mno-stack-arg-probe)

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
