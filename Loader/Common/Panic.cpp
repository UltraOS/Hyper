#include "Panic.h"
#include "Runtime.h"

bool is_in_panic = false;

void do_panic()
{
    hang();
}
