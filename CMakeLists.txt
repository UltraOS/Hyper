cmake_minimum_required(VERSION 3.10)

project(HyperMBR)

enable_language(ASM_NASM)
set(CMAKE_ASM_NASM_COMPILE_OBJECT "<CMAKE_ASM_NASM_COMPILER> <INCLUDES> <FLAGS> -o <OBJECT> <SOURCE>")

# A workaround to make cmake not try to link our flat binary file
set(CMAKE_ASM_NASM_LINK_EXECUTABLE "cp <OBJECTS> <TARGET>")

# This allows us to produce flat binary files as opposed to elf
add_compile_options(
    "$<$<COMPILE_LANGUAGE:ASM_NASM>:-f $<IF:$<BOOL:$<TARGET_PROPERTY:NASM_OBJ_FORMAT>>, \
    $<TARGET_PROPERTY:NASM_OBJ_FORMAT>, ${CMAKE_ASM_NASM_OBJECT_FORMAT}>>"
)

add_executable(HyperMBR.bin  HyperMBR.asm)
set_target_properties(HyperMBR.bin PROPERTIES NASM_OBJ_FORMAT bin)
