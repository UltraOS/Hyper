#include "Panic.h"
#include "Runtime.h"

u8 in_panic_depth = 0;

void do_panic()
{
    hang();
}
