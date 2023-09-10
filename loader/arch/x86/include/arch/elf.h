#pragma once

#include <elf.h>

#define ARCH_STRUCT_VIEW(arch, data, type, action) \
    switch (arch) {                                \
    case ELF_ARCH_I386: {                          \
        const struct Elf32_##type *view = data;    \
        action                                     \
        break;                                     \
    }                                              \
    case ELF_ARCH_AMD64: {                         \
        const struct Elf64_##type *view = data;    \
        action                                     \
        break;                                     \
    }                                              \
    default:                                       \
        BUG();                                     \
}
