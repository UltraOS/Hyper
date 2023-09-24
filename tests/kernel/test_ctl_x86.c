#include <stdarg.h>
#include "pio.h"
#include "common/string_ex.h"
#include "fb_tty.h"
#include "test_ctl.h"
#include "test_ctl_impl.h"
#include "ultra_protocol.h"
#include "ultra_helpers.h"

static int is_in_hypervisor_state = -1;
static bool should_shutdown = true;

#define HYPERVISOR_BIT (1 << 31)

static bool is_in_hypervisor(void)
{
    if (is_in_hypervisor_state == -1) {
        u32 a, b, c, d;

        asm volatile("cpuid"
            : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
            : "a"(1), "c"(0));
        is_in_hypervisor_state = c & HYPERVISOR_BIT;
    }

    return is_in_hypervisor_state;
}

void test_ctl_init(struct ultra_boot_context *bctx)
{
    struct ultra_command_line_attribute *cmdline;

    cmdline = (struct ultra_command_line_attribute*)find_attr(bctx, ULTRA_ATTRIBUTE_COMMAND_LINE);
    if (cmdline)
        should_shutdown = strcmp(cmdline->text, "no-shutdown") != 0;
}

static inline void put_0xe9(char c)
{
    out8(0xE9, c);
}

static inline void write_0xe9(const char *str, size_t len)
{
    while (len--)
        put_0xe9(*str++);
}

void test_write_string(const char *str, size_t count)
{
    if (is_in_hypervisor())
        write_0xe9(str, count);

    fb_tty_write(str, count);
}

// Try various methods, then give up
void vm_shutdown()
{
    out16(0xB004, 0x2000);
    out16(0x604,  0x2000);
    out16(0x4004, 0x3400);

    for (;;) asm volatile("hlt" ::: "memory");
}

void test_vfail(const char *reason, va_list vlist)
{
    print("TEST FAIL!\n");
    vprint(reason, vlist);

    if (is_in_hypervisor() && should_shutdown) {
        put_0xe9(TEST_FAIL_MARKER0);
        put_0xe9(TEST_FAIL_MARKER1);
        put_0xe9(TEST_FAIL_MARKER2);
        put_0xe9(TEST_FAIL_MARKER3);

        vm_shutdown();
    }

    for (;;) asm volatile("hlt" ::: "memory");
}

void test_pass()
{
    print("TEST PASS!\n");

    if (is_in_hypervisor() && should_shutdown) {
        put_0xe9(TEST_PASS_MARKER0);
        put_0xe9(TEST_PASS_MARKER1);
        put_0xe9(TEST_PASS_MARKER2);
        put_0xe9(TEST_PASS_MARKER3);

        vm_shutdown();
    }

    for (;;) asm volatile("hlt" ::: "memory");
}
