// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// Integer division for the engine DLL, in place of the CRT's.
//
// 32-bit ARM Windows divides through helpers the compiler calls --
// __rt_udiv, __rt_sdiv and their 64-bit forms -- which the static CRT
// provides in hand-written assembly (libcmt's divide.obj). Two conditional
// branches in it reach their targets through relocations, and lld, linking a
// DLL of 200 MB, put a range-extension thunk on each: movw/movt r12 and an
// add to pc. A thunk may clobber r12 -- but __rt_sdiv keeps the signs of its
// operands in r12 across that branch, the one taken whenever the quotient is
// 0, and so returned the remainder of such a division negated: (32 % 127)
// came out as -32. ICU's hash tables index with that remainder; Chrome's
// first locale lookup read before a table and the app ended at start.
//
// Defining all four here keeps divide.obj out of the link (it defines all
// four, so none of it is needed). The 32-bit ones use the processor's own
// divide instructions, which every phone Windows 10 Mobile runs on has
// (Cortex-A7/A53/A57, Krait, Kryo); the 64-bit ones fall back to them when
// both operands fit in 32 bits, and divide by shifts and subtractions
// otherwise.
//
// The calling convention is the CRT's, not AAPCS:
//   __rt_udiv/__rt_sdiv     divisor r0, dividend r1 -> quotient r0,
//                           remainder r1
//   __rt_udiv64/__rt_sdiv64 divisor r0:r1, dividend r2:r3 -> quotient
//                           r0:r1, remainder r2:r3
// and a zero divisor raises STATUS_INTEGER_DIVIDE_BY_ZERO (udf #0xf9, the
// CRT's __brkdiv0).

#include <stdint.h>

extern "C" {

// Long division of 64-bit values; the quotient, and the remainder in |rem|.
// No 64-bit division operator in here: the compiler would call back into
// __rt_udiv64.
uint64_t VwUnsignedDivide64(uint64_t dividend,
                            uint64_t divisor,
                            uint64_t* rem) {
  if ((dividend >> 32) == 0 && (divisor >> 32) == 0) {
    const uint32_t n = static_cast<uint32_t>(dividend);
    const uint32_t d = static_cast<uint32_t>(divisor);
    // __rt_udiv, the hardware's.
    *rem = n % d;
    return n / d;
  }
  if (divisor > dividend) {
    *rem = dividend;
    return 0;
  }
  const int shift = __builtin_clzll(divisor) - __builtin_clzll(dividend);
  divisor <<= shift;
  uint64_t quotient = 0;
  for (int i = 0; i <= shift; ++i) {
    quotient <<= 1;
    if (dividend >= divisor) {
      dividend -= divisor;
      quotient |= 1;
    }
    divisor >>= 1;
  }
  *rem = dividend;
  return quotient;
}

// Rounds toward zero, the remainder with the dividend's sign, as C's / and %.
int64_t VwSignedDivide64(int64_t dividend, int64_t divisor, int64_t* rem) {
  const bool negative_dividend = dividend < 0;
  const bool negative_divisor = divisor < 0;
  const uint64_t n = negative_dividend ? 0 - static_cast<uint64_t>(dividend)
                                       : static_cast<uint64_t>(dividend);
  const uint64_t d = negative_divisor ? 0 - static_cast<uint64_t>(divisor)
                                      : static_cast<uint64_t>(divisor);
  uint64_t r = 0;
  uint64_t q = VwUnsignedDivide64(n, d, &r);
  if (negative_dividend != negative_divisor) {
    q = 0 - q;
  }
  if (negative_dividend) {
    r = 0 - r;
  }
  *rem = static_cast<int64_t>(r);
  return static_cast<int64_t>(q);
}

__attribute__((naked)) void __rt_udiv() {
  asm volatile(
      ".arch_extension idiv\n"
      "cbz r0, 1f\n"
      "udiv r2, r1, r0\n"
      "mls r1, r2, r0, r1\n"
      "mov r0, r2\n"
      "bx lr\n"
      "1:\n"
      ".inst.n 0xdef9\n");
}

__attribute__((naked)) void __rt_sdiv() {
  asm volatile(
      ".arch_extension idiv\n"
      "cbz r0, 1f\n"
      "sdiv r2, r1, r0\n"
      "mls r1, r2, r0, r1\n"
      "mov r0, r2\n"
      "bx lr\n"
      "1:\n"
      ".inst.n 0xdef9\n");
}

// The 64-bit ones: the operands swapped into AAPCS order and the remainder
// returned through a slot on the stack, for the functions above.
__attribute__((naked)) void __rt_udiv64() {
  asm volatile(
      "orrs r12, r0, r1\n"
      "beq 1f\n"
      "push {r4, r5, r11, lr}\n"
      "mov r4, r0\n"
      "mov r5, r1\n"
      "mov r0, r2\n"  // dividend
      "mov r1, r3\n"
      "mov r2, r4\n"  // divisor
      "mov r3, r5\n"
      "sub sp, sp, #16\n"
      "add r12, sp, #8\n"  // remainder slot
      "str r12, [sp]\n"
      "bl VwUnsignedDivide64\n"
      "ldrd r2, r3, [sp, #8]\n"
      "add sp, sp, #16\n"
      "pop {r4, r5, r11, pc}\n"
      "1:\n"
      ".inst.n 0xdef9\n");
}

__attribute__((naked)) void __rt_sdiv64() {
  asm volatile(
      "orrs r12, r0, r1\n"
      "beq 1f\n"
      "push {r4, r5, r11, lr}\n"
      "mov r4, r0\n"
      "mov r5, r1\n"
      "mov r0, r2\n"
      "mov r1, r3\n"
      "mov r2, r4\n"
      "mov r3, r5\n"
      "sub sp, sp, #16\n"
      "add r12, sp, #8\n"
      "str r12, [sp]\n"
      "bl VwSignedDivide64\n"
      "ldrd r2, r3, [sp, #8]\n"
      "add sp, sp, #16\n"
      "pop {r4, r5, r11, pc}\n"
      "1:\n"
      ".inst.n 0xdef9\n");
}

}  // extern "C"
