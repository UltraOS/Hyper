#pragma once

#include <elf.h>

#define ARCH_STRUCT_VIEW(arch, data, type, action) \
    (void)arch;                                    \
    const struct Elf64_##type *view = data;        \
    action
