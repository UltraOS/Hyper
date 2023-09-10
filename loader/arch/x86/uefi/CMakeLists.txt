add_loader_c_flags(
    -mno-80387
    -mno-mmx
    -mno-3dnow
    -mno-sse
    -mno-sse2
)
add_loader_nasm_flags(-fwin64)

target_sources(
    ${LOADER_EXECUTABLE}
    PRIVATE
    uefi_handover.c
    uefi_handover.asm
)
