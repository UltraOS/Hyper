#pragma once

#include <stdarg.h>
#include "attributes.h"

// Inspired by linux kern_levels
#define LOG_LEVEL_PREFIX "\x1"
#define LOG_INFO LOG_LEVEL_PREFIX "1"
#define LOG_WARN LOG_LEVEL_PREFIX "2"
#define LOG_ERR  LOG_LEVEL_PREFIX "3"

enum log_level {
    LOG_LEVEL_INFO = 1,
    LOG_LEVEL_WARN = 2,
    LOG_LEVEL_ERR = 3,
};

enum log_level logger_set_level(enum log_level level);

void logger_init(void);

void vprintlvl(enum log_level, const char *msg, va_list vlist);
void vprint(const char *msg, va_list vlist);

PRINTF_DECL(2, 3)
void printlvl(enum log_level, const char *msg, ...);

PRINTF_DECL(1, 2)
void print(const char *msg, ...);

#ifndef MSG_FMT
#define MSG_FMT(msg) msg
#endif


#ifdef HYPER_STRIP_INFO_LOG
PRINTF_DECL(1, 2)
static inline void print_info(const char *msg, ...)
{
    UNUSED(msg);
}
#else
#define print_info(msg, ...) print((LOG_INFO MSG_FMT(msg)), ##__VA_ARGS__)
#endif

#define print_dbg(cond, msg, ...)    \
    if (cond)                        \
        print_info(msg, __VA_ARGS__)

#define print_warn(msg, ...) print((LOG_WARN MSG_FMT(msg)), ##__VA_ARGS__)
#define print_err(msg, ...)  print((LOG_ERR MSG_FMT(msg)), ##__VA_ARGS__)
