#pragma once
#include "attributes.h"
#include <stdarg.h>

void print(const char *msg, ...);
void vprint(const char *msg, va_list vlist);
