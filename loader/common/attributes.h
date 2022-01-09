#pragma once

#define PACKED __attribute__((packed))

#define NORETURN _Noreturn
#define PRINTF_DECL(fmt_idx, args_idx) __attribute__((format(printf, fmt_idx, args_idx)))

#define likely(expr)   __builtin_expect(!!(expr), 1)
#define unlikely(expr) __builtin_expect(!!(expr), 0)