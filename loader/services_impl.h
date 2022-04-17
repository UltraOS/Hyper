#pragma once

#include "common/attributes.h"
#include "common/types.h"

NORETURN
void on_service_use_after_exit(const char *func);

extern bool services_offline;

#define SERVICE_FUNCTION()                           \
    do {                                             \
        if (unlikely(services_offline))              \
            on_service_use_after_exit(__FUNCTION__); \
    } while (0)
