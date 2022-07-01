#pragma once

#include "common/string_view.h"
#include "common/helpers.h"

#define HYPER_MAJOR 0
#define HYPER_MINOR 3
#define HYPER_PATCH 1

#define MAKE_BRAND_STRING(mj, mi, pa) SV("HyperLoader v" TO_STR(mj) "." TO_STR(mi) "." TO_STR(pa))
#define HYPER_BRAND_STRING MAKE_BRAND_STRING(HYPER_MAJOR, HYPER_MINOR, HYPER_PATCH)
