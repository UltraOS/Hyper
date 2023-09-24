#include <stdarg.h>
#include "common/types.h"
#include "common/attributes.h"
#include "common/log.h"

struct ultra_boot_context;
void test_ctl_init(struct ultra_boot_context*);

void test_write_string(const char *str, size_t count);

NORETURN
void test_fail(const char *reason, ...);

NORETURN
void test_vfail(const char *reason, va_list vlist);

NORETURN
void test_pass();

NORETURN
static inline void test_fail_on_non_unique(const char *arg)
{
    test_fail("encountered multiple '%s'\n", arg);
}

NORETURN
static inline void test_fail_on_no_mandatory(const char *arg)
{
    test_fail("missing mandatory '%s'\n", arg);
}
