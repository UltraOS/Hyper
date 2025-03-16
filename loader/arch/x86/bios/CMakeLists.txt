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
    bios_video_services.c
)

add_subdirectory(boot_record)

set(MBR_FULL_PATH "${MBR_PATH}/${MBR_BINARY}")
set(ISO_MBR_FULL_PATH "${MBR_PATH}/${ISO_MBR_BINARY}")
set(ISO_BOOT_REC_FULL_PATH "${MBR_PATH}/${ISO_BOOT_REC_BINARY}")
set(STAGE2_FULL_PATH "${STAGE2_PATH}/${STAGE2_BINARY}")
set(ISO_STAGE2_FULL_PATH "${STAGE2_PATH}/hyper_iso_boot")

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

include(ExternalProject)
ExternalProject_Add(
    installer
    SOURCE_DIR
    ${PROJECT_SOURCE_DIR}/installer
    BUILD_COMMAND
    "${CMAKE_MAKE_PROGRAM}"
    CMAKE_ARGS
    -DMBR_PATH=${MBR_FULL_PATH}
    -DISO_MBR_PATH=${ISO_MBR_FULL_PATH}
    -DSTAGE2_PATH=${STAGE2_FULL_PATH}
    -DCMAKE_MAKE_PROGRAM="${CMAKE_MAKE_PROGRAM}"
    # Don't inherit C compiler which may be tied to target rather than host.
    # There should ideally be a way to pass this through or otherwise request
    # A toolchain from the subproject.
    #-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DCMAKE_C_COMPILER_WORKS=1
    INSTALL_COMMAND
    cmake -E echo "Skipping install step"
)
ExternalProject_Add_StepDependencies(
    installer
    build
    ${MBR_BINARY}
    ${ISO_MBR_BINARY}
    ${STAGE2_BINARY}
)
