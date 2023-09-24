#pragma once
#include <stdarg.h>

#define TEST_PASS_MARKER0 0xCA
#define TEST_PASS_MARKER1 0xFE
#define TEST_PASS_MARKER2 0xBA
#define TEST_PASS_MARKER3 0xBE

#define TEST_FAIL_MARKER0 0xDE
#define TEST_FAIL_MARKER1 0xAD
#define TEST_FAIL_MARKER2 0xBE
#define TEST_FAIL_MARKER3 0xEF

void test_vfail(const char *reason, va_list vlist);
