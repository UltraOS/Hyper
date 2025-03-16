# Not even going to attempt to abstract this away, the difference is TOO big
if (HYPER_TOOLCHAIN STREQUAL "clang")
    target_link_options(
        ${LOADER_EXECUTABLE}
        PRIVATE
        /subsystem:efi_application
        /entry:EfiMain
    )
else ()
    target_link_options(
        ${LOADER_EXECUTABLE}
        PRIVATE
        --subsystem 10
        --entry EfiMain
        --strip-all
        --script ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld
        --orphan-handling=error
        -nostdlib
    )
endif ()

add_loader_c_flags(
    -fno-stack-protector
    -mno-stack-arg-probe
)

target_sources(
    ${LOADER_EXECUTABLE}
    PRIVATE
    uefi_disk_services.c
    uefi_entry.c
    uefi_find.c
    uefi_helpers.c
    uefi_memory_services.c
    uefi_video_services.c
    relocator.c
    stubs.c
)
