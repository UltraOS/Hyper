#pragma once

#include "Common/Types.h"

[[noreturn]] void kernel_handover32(u32 entrypoint, u32 esp, u32 arg0, u32 arg1);
[[noreturn]] void kernel_handover64(u64 entrypoint, u64 rsp, u64 cr3, u64 arg0, u64 arg1);
