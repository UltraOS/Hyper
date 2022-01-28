cmake_minimum_required(VERSION 3.10)

project(MBR)

enable_language(ASM_NASM)
set(CMAKE_ASM_NASM_COMPILE_OBJECT "<CMAKE_ASM_NASM_COMPILER> -D ULTRA_${ARCH} <INCLUDES> <FLAGS> -o <OBJECT> <SOURCE>")

# A workaround to make cmake not try to link our flat binary file
set(CMAKE_ASM_NASM_LINK_EXECUTABLE "cp <OBJECTS> <TARGET>")

# This allows us to produce flat binary files as opposed to elf
add_compile_options(
    "$<$<COMPILE_LANGUAGE:ASM_NASM>:-f $<IF:$<BOOL:$<TARGET_PROPERTY:NASM_OBJ_FORMAT>>, \
    $<TARGET_PROPERTY:NASM_OBJ_FORMAT>, ${CMAKE_ASM_NASM_OBJECT_FORMAT}>>"
)

add_executable(hyper_mbr hyper_mbr.asm)
set_target_properties(hyper_mbr PROPERTIES NASM_OBJ_FORMAT bin)

set(MBR_BINARY hyper_mbr           PARENT_SCOPE)
set(MBR_PATH ${PROJECT_BINARY_DIR} PARENT_SCOPE)
