#pragma once

#include "config.h"
#include "services.h"

NORETURN
void ultra_protocol_load(struct config *cfg, struct loadable_entry *entry, struct services *srvc);
