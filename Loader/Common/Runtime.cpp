#include "Runtime.h"
#include "Logger.h"

[[noreturn]] void on_assertion_failed(const char* message, const char* file, const char* function, u32 line)
{
    logger::error("Assertion failed!\nexpression: ", message, "\nat ", file, ":", line, " -> ", function);
    hang();
}

[[noreturn]] void panic(const char* message)
{
    logger::error("PANIC!", "\n", message);
    hang();
}
