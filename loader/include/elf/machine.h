#pragma once
#include "structures.h"
#include "elf.h"

bool elf_machine_to_arch(Elf32_Half machine, enum elf_arch *out_arch,
                         u8 *out_expected_ptr_width);
