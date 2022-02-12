#pragma once

#include "services.h"

struct memory_services *memory_services_init();

bool memory_services_release(size_t key);
