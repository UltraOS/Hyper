/*
 * https://gcc.gnu.org/onlinedocs/gccint/Integer-library-routines.html
 * We implement these ourselves as clang doesn't exactly offer an easy
 * way to use an existing clang_rt for arch-none-none type targets.
 */

#include "common/types.h"
#include "common/bug.h"

#define QWORD_HI(x) (((unsigned int)(((x) & 0xFFFFFFFF00000000) >> 32)))
#define QWORD_LO(x) (((unsigned int)(((x) & 0x00000000FFFFFFFF)      )))

#ifdef __i386__

// https://en.wikipedia.org/wiki/Division_algorithm#Long_division
static u64 do_64bit_division(u64 a, u64 b, u64 *c)
{
    u64 quotient = 0, remainder = 0;
    u32 bit = 64 - __builtin_clzll(a);

    while (bit-- > 0) {
        remainder = (remainder << 1 | ((a >> bit) & 1));

        if (remainder >= b) {
            remainder -= b;
            quotient |= 1ull << bit;
        }
    }
    
    if (c)
        *c = remainder;

    return quotient;
}

/*
 * Documentation says this takes in an "unsigned long", which is a lie.
 * It takes in an 8 byte integer for all inputs & outputs.
 * 
 * NOTE: none of the other overloads of these functions are ever referenced,
 *       so they're not implemented here.
 */
u64 __udivmoddi4(u64 a, u64 b, u64 *c)
{
    if (b > a) {
        if (c)
            *c = a;

        return 0;
    }

    if (b == a) {
        if (c)
            *c = 0;

        return 1;
    }

    if (!QWORD_HI(b)) {
        BUG_ON(b == 0);

        if (b == 1) {
            if (c)
                *c = 0;
        
            return a;
        }


        if (!QWORD_HI(a)) {
            u32 lo_a = QWORD_LO(a);
            u32 lo_b = QWORD_LO(b);

            if (c)
                *c = lo_a % lo_b;

            return lo_a / lo_b;
        }
    }

    // All fast paths failed, do a full 64 bit division
    return do_64bit_division(a, b, c);
}

u64 __umoddi3(u64 a, u64 b)
{
    u64 c;

    __udivmoddi4(a, b, &c);
    return c;
}

u64 __udivdi3(u64 a, u64 b)
{
    return __udivmoddi4(a, b, NULL);
}
#endif
