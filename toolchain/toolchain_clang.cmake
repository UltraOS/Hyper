if (APPLE)
    execute_process(
        COMMAND
        brew --prefix lld
        OUTPUT_VARIABLE
        BREW_LLD_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set(LLD_LINKER "${BREW_LLD_PREFIX}/bin/ld.lld")
else ()
    set(LLD_LINKER "lld")
endif ()

# Do this because cmake attempts to link against -lc and crt0
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

if (HYPER_PLATFORM STREQUAL "uefi" AND HYPER_ARCH STREQUAL "amd64")
    set(HYPER_TRIPLET_PREFIX "x86_64")
else ()
    set(HYPER_TRIPLET_PREFIX ${HYPER_ARCH})
endif ()

if (HYPER_PLATFORM STREQUAL "uefi")
    set(HYPER_LLD_FLAVOR link)
    set(HYPER_LLD_OUT_TARGET "/out:<TARGET>")
    set(HYPER_TRIPLET_POSTFIX "pc-win32-coff")
elseif (HYPER_PLATFORM STREQUAL "bios")
    set(HYPER_LLD_FLAVOR ld)
    set(HYPER_LLD_OUT_TARGET "-o <TARGET>")
    set(HYPER_TRIPLET_POSTFIX "none-none")
else ()
    message(FATAL_ERROR "Platform ${HYPER_PLATFORM} is not supported")
endif ()

set(HYPER_COMPILER_PREFIX "${HYPER_TRIPLET_PREFIX}-${HYPER_TRIPLET_POSTFIX}")

set(CMAKE_C_LINK_EXECUTABLE
    "<CMAKE_LINKER> -flavor ${HYPER_LLD_FLAVOR} <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> \
     <OBJECTS> ${HYPER_LLD_OUT_TARGET} <LINK_LIBRARIES>")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --target=${HYPER_COMPILER_PREFIX}")

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR ${HYPER_ARCH})
set(CMAKE_SYSROOT "")

set(CMAKE_C_COMPILER clang)
set(CMAKE_LINKER ${LLD_LINKER})
