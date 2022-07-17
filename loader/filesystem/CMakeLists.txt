target_sources(
    ${LOADER_EXECUTABLE}
    PRIVATE
    block_cache.c
    bulk_read.c
    filesystem.c
    filesystem_table.c
    gpt.c
    mbr.c
    path.c
)

add_subdirectory(fat)
add_subdirectory(iso9660)
