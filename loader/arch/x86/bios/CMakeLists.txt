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
    apm.c
    bios_disk_services.c
    bios_entry.c
    bios_find.c
    bios_handover.c
    bios_memory_services.c
    bios_pxe_services.c
    bios_video_services.c
    scratch_buffer.c
)

add_subdirectory(boot_record)

set(MBR_FULL_PATH "${MBR_PATH}/${MBR_BINARY}")
set(ISO_MBR_FULL_PATH "${MBR_PATH}/${ISO_MBR_BINARY}")
set(ISO_BOOT_REC_FULL_PATH "${MBR_PATH}/${ISO_BOOT_REC_BINARY}")
set(PXE_BOOT_REC_FULL_PATH "${MBR_PATH}/${PXE_BOOT_REC_BINARY}")
set(STAGE2_FULL_PATH "${STAGE2_PATH}/${STAGE2_BINARY}")
set(ISO_STAGE2_FULL_PATH "${STAGE2_PATH}/hyper_iso_boot")
set(PXE_STAGE2_FULL_PATH "${STAGE2_PATH}/hyper_pxe")

add_custom_command(
    OUTPUT
    ${ISO_STAGE2_FULL_PATH}
    COMMAND
    "${CMAKE_COMMAND}" -E cat ${ISO_BOOT_REC_FULL_PATH} ${STAGE2_FULL_PATH} > ${ISO_STAGE2_FULL_PATH}
    DEPENDS
    ${ISO_BOOT_REC_BINARY} ${STAGE2_BINARY}
    COMMENT
    "Generating iso stage2 loader"
)
add_custom_target(hyper_iso_boot ALL DEPENDS ${ISO_STAGE2_FULL_PATH})

# PXE network bootable image: the PXE boot record concatenated with stage2.
# PXE loads the whole blob at MBR_LOAD_BASE, landing stage2 at STAGE2_LOAD_BASE.
add_custom_command(
    OUTPUT
    ${PXE_STAGE2_FULL_PATH}
    COMMAND
    "${CMAKE_COMMAND}" -E cat ${PXE_BOOT_REC_FULL_PATH} ${STAGE2_FULL_PATH} > ${PXE_STAGE2_FULL_PATH}
    DEPENDS
    ${PXE_BOOT_REC_BINARY} ${STAGE2_BINARY}
    COMMENT
    "Generating pxe stage2 loader"
)
add_custom_target(hyper_pxe ALL DEPENDS ${PXE_STAGE2_FULL_PATH})

# The installer is a self-contained Python script rather than a compiled binary:
# a template (installer/hyper_install.py.in) populated with the boot blobs as
# base64 at build time. This drops the host-toolchain build entirely and lets the
# resulting hyper_install ship to any platform as-is, needing only a Python 3
# interpreter.
set(INSTALLER_DIR "${PROJECT_SOURCE_DIR}/installer")
set(INSTALLER_OUTPUT "${INSTALLER_DIR}/hyper_install")

add_custom_command(
    OUTPUT
    ${INSTALLER_OUTPUT}
    COMMAND
    python3 ${INSTALLER_DIR}/embed_blobs.py
            --template ${INSTALLER_DIR}/hyper_install.py.in
            --output ${INSTALLER_OUTPUT}
            --mbr ${MBR_FULL_PATH}
            --iso-mbr ${ISO_MBR_FULL_PATH}
            --stage2 ${STAGE2_FULL_PATH}
    DEPENDS
    ${MBR_BINARY} ${ISO_MBR_BINARY} ${STAGE2_BINARY}
    ${INSTALLER_DIR}/embed_blobs.py
    ${INSTALLER_DIR}/hyper_install.py.in
    COMMENT
    "Generating hyper_install Python installer"
)
add_custom_target(installer ALL DEPENDS ${INSTALLER_OUTPUT})
