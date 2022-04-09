cmake_minimum_required(VERSION 3.16)

project(HyperInstaller C)

set(OUTPUT_DIRECTORY                       "${CMAKE_CURRENT_SOURCE_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY         "${OUTPUT_DIRECTORY}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${OUTPUT_DIRECTORY}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${OUTPUT_DIRECTORY}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG   "${OUTPUT_DIRECTORY}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${OUTPUT_DIRECTORY}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG   "${OUTPUT_DIRECTORY}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${OUTPUT_DIRECTORY}")

if (NOT MBR_PATH)
    message(FATAL_ERROR "MBR_PATH must be specified")
endif ()

if (NOT ISO_MBR_PATH)
    message(FATAL_ERROR "ISO_MBR_PATH must be specified")
endif ()

if (NOT STAGE2_PATH)
    message(FATAL_ERROR "STAGE2_PATH must be specified")
endif ()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2")

set(MBR_EMBEDDED_PATH "${PROJECT_SOURCE_DIR}/mbr_embedded.c")
set(ISO_MBR_EMBEDDED_PATH "${PROJECT_SOURCE_DIR}/iso_mbr_embedded.c")
set(STAGE2_EMBEDDED_PATH  "${PROJECT_SOURCE_DIR}/stage2_embedded.c")

add_custom_command(
    OUTPUT ${MBR_EMBEDDED_PATH}
    COMMAND python3 ${PROJECT_SOURCE_DIR}/dump_as_array.py ${MBR_PATH} ${MBR_EMBEDDED_PATH} mbr
    DEPENDS ${MBR_PATH}
)

add_custom_command(
    OUTPUT ${ISO_MBR_EMBEDDED_PATH}
    COMMAND python3 ${PROJECT_SOURCE_DIR}/dump_as_array.py ${ISO_MBR_PATH} ${ISO_MBR_EMBEDDED_PATH} iso_mbr
    DEPENDS ${ISO_MBR_PATH}
)

add_custom_command(
    OUTPUT ${STAGE2_EMBEDDED_PATH}
    COMMAND python3 ${PROJECT_SOURCE_DIR}/dump_as_array.py ${STAGE2_PATH} ${STAGE2_EMBEDDED_PATH} stage2
    DEPENDS ${STAGE2_PATH}
)

add_executable(hyper_install hyper_install.c ${MBR_EMBEDDED_PATH} ${ISO_MBR_EMBEDDED_PATH} ${STAGE2_EMBEDDED_PATH})
set_property(TARGET hyper_install PROPERTY C_STANDARD 11)
if (MSVC)
    target_compile_options(hyper_install PRIVATE /W4 /WX)
else ()
    target_compile_options(hyper_install PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif ()

if (UNIX AND NOT APPLE)
    target_link_libraries(hyper_install -static)
endif ()
