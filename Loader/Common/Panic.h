#pragma once

#include "Logger.h"

extern u8 in_panic_depth;
[[noreturn]] void do_panic();

// Used to terminate the execution in case of an invalid state encountered at runtime.
// Some examples are: invalid allocation request, OOB access, invalid argument, failed assertion.
#define panic(reason, ...)                          \
    do {                                            \
        ++in_panic_depth;                           \
                                                    \
        if (in_panic_depth == 2) {                  \
            errorln("Panicked while inside panic"); \
        } else if (in_panic_depth >= 3) {           \
            do_panic();                             \
        }                                           \
                                                    \
       errorln("PANIC!");                           \
       errorln(reason, __VA_ARGS__);                \
       do_panic();                                  \
    } while (0)

// Used to terminate the execution in case of an inability to continue the loading process.
// Some examples are: OOM on critical allocation, any invalid user input, no configuration file found, etc.
#define unrecoverable_error(reason, ...) \
    do {                                 \
       errorln("Unrecoverable error!");  \
       errorln(reason, __VA_ARGS__);     \
       do_panic();                       \
    } while (0)
