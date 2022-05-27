#pragma once

#define PACKED __attribute__((packed))

#define NORETURN _Noreturn
#define PRINTF_DECL(fmt_idx, args_idx) __attribute__((format(gnu_printf, fmt_idx, args_idx)))

#define likely(expr)   __builtin_expect(!!(expr), 1)
#define unlikely(expr) __builtin_expect(!!(expr), 0)

#define ALWAYS_INLINE inline __attribute__((always_inline))

#define ERROR_EMITTER(msg) __attribute__((__error__(msg)))
