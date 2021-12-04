#include "Runtime.h"
#include "Panic.h"

[[noreturn]] void on_assertion_failed(const char* message, const char* file, const char* function, u32 line)
{
    panic("Assertion failed!\nexpression: {}\nat {}:{} -> {}",
          message, file, line, function);
}
