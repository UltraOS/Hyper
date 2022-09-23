#pragma once

#include <common/helpers.h>

#define PACKED __attribute__((packed))

#define NORETURN __attribute__((__noreturn__))

#ifdef __clang__
#define PRINTF_DECL(fmt_idx, args_idx) __attribute__((format(printf, fmt_idx, args_idx)))
#elif defined(__GNUC__)
#define PRINTF_DECL(fmt_idx, args_idx) __attribute__((format(gnu_printf, fmt_idx, args_idx)))
#else
#define PRINTF_DECL(fmt_idx, args_idx)
#endif

#define USED __attribute__((used))

#define ALWAYS_INLINE inline __attribute__((always_inline))

#define ERROR_EMITTER(msg) __attribute__((__error__(msg)))

#define STRING_SECTION(name) __attribute__((section(name)))
#define SECTION(name) STRING_SECTION(TO_STR(name))

#if defined(_MSC_VER)
#define MSVC_WRAP_CTOR_SECTION(name, letter) \
    ".rdata$" TO_STR(name) "_" TO_STR(letter)

#define CTOR_SECTION(name) STRING_SECTION(MSVC_WRAP_CTOR_SECTION(name, b))

#define CTOR_SECTION_DEFINE_ITERATOR(type, section)                       \
    __declspec(allocate(MSVC_WRAP_CTOR_SECTION(section, a)))              \
    static unsigned PASTE(section, _guard_label_begin) USED = 0xDEADBEEF; \
                                                                          \
    __declspec(allocate(MSVC_WRAP_CTOR_SECTION(section, a)))              \
    type PASTE(section, _begin)[] USED = {};                              \
                                                                          \
    __declspec(allocate(MSVC_WRAP_CTOR_SECTION(section, c)))              \
    type PASTE(section, _end)[] USED = {};                                \
                                                                          \
    __declspec(allocate(MSVC_WRAP_CTOR_SECTION(section, c)))              \
    static unsigned PASTE(section, _guard_lable_end) USED = 0xCAFEBABE;
#else
#define CTOR_SECTION_DEFINE_ITERATOR(type, section) \
    extern type PASTE(section, _begin)[];           \
    extern type PASTE(section, _end)[];

#define CTOR_SECTION(name) STRING_SECTION("." TO_STR(name))
#endif
