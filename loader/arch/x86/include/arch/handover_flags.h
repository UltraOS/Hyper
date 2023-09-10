#pragma once

// x86 long mode enable
#define HO_X86_LME_BIT      28
#define HO_X86_LME          (1 << HO_X86_LME_BIT)

// x86 page size extension
#define HO_X86_PSE_BIT      29
#define HO_X86_PSE          (1 << HO_X86_PSE_BIT)

// x86 physical address extension
#define HO_X86_PAE_BIT      30
#define HO_X86_PAE          (1 << HO_X86_PAE_BIT)

// x86 57 bit linear address (5 level paging)
#define HO_X86_LA57_BIT     31
#define HO_X86_LA57         (1 << HO_X86_LA57_BIT)

u32 handover_flags_to_cr4(u32 flags);
