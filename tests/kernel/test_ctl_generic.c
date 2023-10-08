#include <stdarg.h>
#include "common/attributes.h"
#include "common/string_ex.h"

#include "fb_tty.h"
#include "test_ctl.h"
#include "test_ctl_impl.h"
#include "ultra_helpers.h"

#define TEST_PASS_MARKER0 0xCA
#define TEST_PASS_MARKER1 0xFE
#define TEST_PASS_MARKER2 0xBA
#define TEST_PASS_MARKER3 0xBE

#define TEST_FAIL_MARKER0 0xDE
#define TEST_FAIL_MARKER1 0xAD
#define TEST_FAIL_MARKER2 0xBE
#define TEST_FAIL_MARKER3 0xEF

bool g_should_shutdown = true;

void test_pass(void)
{
    print("TEST PASS!\n");

    arch_put_byte(TEST_PASS_MARKER0);
    arch_put_byte(TEST_PASS_MARKER1);
    arch_put_byte(TEST_PASS_MARKER2);
    arch_put_byte(TEST_PASS_MARKER3);
    arch_hang_or_shutdown();
}

void test_vfail(const char *reason, va_list vlist)
{
    print("TEST FAIL!\n");
    vprint(reason, vlist);

    arch_put_byte(TEST_FAIL_MARKER0);
    arch_put_byte(TEST_FAIL_MARKER1);
    arch_put_byte(TEST_FAIL_MARKER2);
    arch_put_byte(TEST_FAIL_MARKER3);
    arch_hang_or_shutdown();
}

void test_fail(const char *reason, ...)
{
    va_list vlist;
    va_start(vlist, reason);
    test_vfail(reason, vlist);
    va_end(vlist);
}

void panic(const char *reason, ...)
{
    va_list vlist;
    va_start(vlist, reason);
    test_vfail(reason, vlist);
    va_end(vlist);
}

void oops(const char *reason, ...)
{
    va_list vlist;
    va_start(vlist, reason);
    test_vfail(reason, vlist);
    va_end(vlist);
}

WEAK
void arch_write_string(const char *str, size_t count)
{
    while (count--)
        arch_put_byte(*str++);
}

void test_write_string(const char *str, size_t count)
{
    arch_write_string(str, count);
    fb_tty_write(str, count);
}

WEAK
void arch_test_ctl_init(struct ultra_boot_context *bctx)
{
    UNUSED(bctx);
}

void test_ctl_init(struct ultra_boot_context *bctx)
{
    struct ultra_command_line_attribute *cmdline;

    cmdline = (struct ultra_command_line_attribute*)find_attr(bctx, ULTRA_ATTRIBUTE_COMMAND_LINE);
    if (cmdline)
        g_should_shutdown = strcmp(cmdline->text, "no-shutdown") != 0;

    arch_test_ctl_init(bctx);
}
