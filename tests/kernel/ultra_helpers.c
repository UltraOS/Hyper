#include <stddef.h>
#include "ultra_helpers.h"

struct ultra_attribute_header *find_attr(struct ultra_boot_context *ctx,
                                         uint32_t type)
{
    struct ultra_attribute_header *ret = ctx->attributes;
    size_t i;

    // Guaranteed to be the first attribute
    if (type == ULTRA_ATTRIBUTE_PLATFORM_INFO)
        return ret;

    ret = ULTRA_NEXT_ATTRIBUTE(ret);

    // Guaranteed to be the second attribute
    if (type == ULTRA_ATTRIBUTE_KERNEL_INFO)
        return ret;

    ret = ULTRA_NEXT_ATTRIBUTE(ret);

    for (i = 2; i < ctx->attribute_count; ++i, ret = ULTRA_NEXT_ATTRIBUTE(ret)) {
        if (ret->type == type)
            return ret;
    }

    return NULL;
}
