target_sources(
    ${LOADER_EXECUTABLE}
    PRIVATE
    conversions.c
    dynamic_buffer.c
    format.c
    log.c
    panic.c
    string.c
    string_view.c
    rw_helpers.c
)

# The word-at-a-time loops in string.c must not be "optimized" back into calls
# to memcpy()/memset()/strlen() by the compiler's loop-idiom pass. Disable that
# recognition just for this translation unit.
if (HYPER_TOOLCHAIN STREQUAL "gcc")
    set_source_files_properties(
        string.c PROPERTIES
        COMPILE_OPTIONS "-fno-tree-loop-distribute-patterns"
    )
elseif (HYPER_TOOLCHAIN STREQUAL "clang")
    set_source_files_properties(
        string.c PROPERTIES
        COMPILE_OPTIONS "-fno-builtin"
    )
endif ()
