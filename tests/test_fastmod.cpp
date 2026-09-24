/*
 * Copyright (c) 2026 LinnJz
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// test_fastmod.cpp
//
// Verifies stdex::fastmod (include/fastmod.hpp, C++23 port) and the int128
// primitives it stands on (include/int128.hpp) against the native %, /
// oracle (self-contained, always authoritative).
//
// Deterministic SplitMix64 stream: no randomness, no flakiness.
#include <cstdint>
#include <cstdio>

#include "fastmod.hpp" // stdex port under test

namespace
{
int g_pass = 0;
int g_fail = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (cond) {                                                              \
      ++g_pass;                                                              \
    } else {                                                                 \
      ++g_fail;                                                              \
      if (g_fail < 20) {                                                     \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
      }                                                                      \
    }                                                                        \
  } while (0)

uint64_t rng_state = 0x9E37'79B9'7F4A'7C15ULL;
uint64_t next_rand()
{
  uint64_t z = (rng_state += 0x9E37'79B9'7F4A'7C15ULL);
  z = (z ^ (z >> 30U)) * 0xBF58'476D'1CE4'E5B9ULL;
  z = (z ^ (z >> 27U)) * 0x94D0'49BB'1331'11EBULL;
  return z ^ (z >> 31U);
}
} // namespace

// Compile-time proof: magic precomputation and the full API evaluate constant.
static_assert(stdex::computeM_u32(10) == 0xFFFF'FFFF'FFFF'FFFFULL / 10 + 1);
static_assert(stdex::fastmod_u32(100, stdex::computeM_u32(7), 7) == 100 % 7);
static_assert(stdex::fastdiv_u32(100, stdex::computeM_u32(7)) == 100 / 7);
static_assert(stdex::is_divisible(14, stdex::computeM_u32(7)));
static_assert(!stdex::is_divisible(15, stdex::computeM_u32(7)));
static_assert(stdex::fastmod<7>(100) == 100 % 7);
static_assert(stdex::fastdiv<7>(100) == 100 / 7);
static_assert(stdex::fastmod_s32(-100, stdex::computeM_s32(7), 7) == -100 % 7);
static_assert(stdex::fastdiv_s32(100, stdex::computeM_s32(7), 7) == 100 / 7);
static_assert(stdex::fastmod_u64(100, stdex::computeM_u64(7), 7) == 100 % 7);
static_assert(stdex::fastdiv_u64(100, stdex::computeM_u64(7)) == 100 / 7);
static_assert(stdex::is_divisible_u64(14, stdex::computeM_u64(7)));
static_assert(!stdex::is_divisible_u64(15, stdex::computeM_u64(7)));
static_assert(stdex::fastmod<7ULL>(100ULL) == 100ULL % 7ULL);
static_assert(stdex::fastdiv<7ULL>(100ULL) == 100ULL / 7ULL);
static_assert(stdex::fastmod<1ULL>(12345ULL) == 0ULL);
static_assert(stdex::fastmod<1000000007ULL>(123456789012345ULL) == 123456789012345ULL % 1000000007ULL);
static_assert(stdex::fastdiv<1000000007ULL>(123456789012345ULL) == 123456789012345ULL / 1000000007ULL);
static_assert(stdex::add128({ 0xFFFF'FFFF'FFFF'FFFFULL, 0x1234ULL }, 1).low == 0);
static_assert(stdex::add128({ 0xFFFF'FFFF'FFFF'FFFFULL, 0x1234ULL }, 1).high == 0x1235ULL);
static_assert((stdex::uint128_t { 1, 2 } <=> stdex::uint128_t { 1, 2 }) == 0);
static_assert((stdex::uint128_t { 2, 2 } <=> stdex::uint128_t { 1, 2 }) > 0);
static_assert((stdex::uint128_t { 0, 3 } <=> stdex::uint128_t { 0xFFFF'FFFF'FFFF'FFFFULL, 2 }) > 0);
static_assert(stdex::div128({ 100, 0 }, 7).low == 100 / 7);
static_assert(stdex::div128({ 100, 0 }, 7).high == 0);

bool test_u32()
{
  int const fail_mark = g_fail;
  uint32_t const divisors[] = { 1, 2, 3, 4, 5, 7, 16, 100, 1000, 65535, 65536,
                                0x7FFF'FFFFU, 0x8000'0000U, 0xFFFF'FFFFU };
  for (uint32_t d : divisors) {
    uint64_t const ref_m = stdex::computeM_u32(d);
    uint32_t const values[] = { 0, 1, 2, 3, static_cast<uint32_t>(d - 1), d,
                                static_cast<uint32_t>(d + 1), 0x7FFF'FFFFU, 0x8000'0000U,
                                0xFFFF'FFFFU, static_cast<uint32_t>(next_rand()),
                                static_cast<uint32_t>(next_rand()) };
    for (uint32_t a : values) {
      CHECK(stdex::fastmod_u32(a, ref_m, d) == a % d);
      if (d > 1) {
        CHECK(stdex::fastdiv_u32(a, ref_m) == a / d);
      }
      CHECK(stdex::is_divisible(a, ref_m) == (a % d == 0));
    }
  }
  for (int i = 0; i < 3000; ++i) {
    uint32_t d = static_cast<uint32_t>(next_rand() | 1U);
    if (d == 0) d = 1;
    uint32_t const a = static_cast<uint32_t>(next_rand());
    uint64_t const m = stdex::computeM_u32(d);
    CHECK(stdex::fastmod_u32(a, m, d) == a % d);
    if (d > 1) CHECK(stdex::fastdiv_u32(a, m) == a / d);
    CHECK(stdex::is_divisible(a, m) == (a % d == 0));
  }
  return g_fail == fail_mark;
}

bool test_s32()
{
  int const fail_mark = g_fail;
  int32_t const divisors[] = { 1, -1, 2, -2, 3, -3, 7, -7, 100, -100,
                               127, -128, 2147483647, -2147483647 };
  for (int32_t d : divisors) {
    int32_t const ad = (d < 0) ? -d : d;
    uint64_t const m = stdex::computeM_s32(d);
    int32_t const values[] = { 0, 1, -1, 2, -2, 127, -128, 2147483647, -2147483647,
                               static_cast<int32_t>(next_rand()), static_cast<int32_t>(next_rand()) };
    for (int32_t a : values) {
      if (a == -2147483647 - 1 && d == -1) continue; // UB in native % too
      CHECK(stdex::fastmod_s32(a, m, ad) == a % d);
      if (d != -1 && d != 1) {
        CHECK(stdex::fastdiv_s32(a, m, d) == a / d);
      }
    }
  }
  for (int i = 0; i < 2000; ++i) {
    int32_t d = static_cast<int32_t>(next_rand() | 1U);
    if (d == 0 || d == -2147483647 - 1) d = 3;
    int32_t const ad = (d < 0) ? -d : d;
    int32_t const a = static_cast<int32_t>(next_rand());
    if (a == -2147483647 - 1 && d == -1) continue;
    uint64_t const m = stdex::computeM_s32(d);
    CHECK(stdex::fastmod_s32(a, m, ad) == a % d);
    if (d != -1 && d != 1) CHECK(stdex::fastdiv_s32(a, m, d) == a / d);
  }
  return g_fail == fail_mark;
}

bool test_u64()
{
  int const fail_mark = g_fail;
  uint64_t const divisors[] = { 1, 2, 3, 5, 7, 16, 100, 1000, 1000000007ULL,
                                0x1'0000'0000ULL, 0x1'0000'0001ULL,
                                0x7FFF'FFFF'FFFF'FFFFULL, 0x8000'0000'0000'0000ULL,
                                0xFFFF'FFFF'FFFF'FFFFULL, 18446744073709551557ULL };
  for (uint64_t d : divisors) {
    stdex::uint128_t const m = stdex::computeM_u64(d);
    uint64_t const values[] = { 0, 1, 2, d - 1, d, d + 1, 0x7FFF'FFFF'FFFF'FFFFULL,
                                0x8000'0000'0000'0000ULL, 0xFFFF'FFFF'FFFF'FFFFULL,
                                next_rand(), next_rand() };
    for (uint64_t a : values) {
      CHECK(stdex::fastmod_u64(a, m, d) == a % d);
      if (d > 1) CHECK(stdex::fastdiv_u64(a, m) == a / d);
      CHECK(stdex::is_divisible_u64(a, m) == (a % d == 0));
    }
  }
  for (int i = 0; i < 3000; ++i) {
    uint64_t d = next_rand() | 1ULL;
    if (d == 0) d = 1;
    uint64_t const a = next_rand();
    stdex::uint128_t const m = stdex::computeM_u64(d);
    CHECK(stdex::fastmod_u64(a, m, d) == a % d);
    if (d > 1) CHECK(stdex::fastdiv_u64(a, m) == a / d);
    CHECK(stdex::is_divisible_u64(a, m) == (a % d == 0));
  }
  return g_fail == fail_mark;
}

bool test_int128_primitives()
{
  int const fail_mark = g_fail;
  for (int i = 0; i < 2000; ++i) {
    stdex::uint128_t const v { next_rand(), next_rand() };
    uint64_t const add = next_rand();

    // add128 vs independent carry computation.
    {
      uint64_t const exp_lo = v.low + add;
      uint64_t const exp_hi = v.high + ((exp_lo < v.low) ? 1ULL : 0ULL);
      stdex::uint128_t const got = stdex::add128(v, add);
      CHECK(got.low == exp_lo);
      CHECK(got.high == exp_hi);
      CHECK(got.low == stdex::add128_portable(v, add).low);
      CHECK(got.high == stdex::add128_portable(v, add).high);
    }

    // div128: q = div(v, d) iff q*d + r == v with r < d (128-bit multiply-back).
    {
      uint64_t d = next_rand() | 1ULL;
      if (d == 0) d = 1;
      stdex::uint128_t const q = stdex::div128(v, d);
      CHECK(q.low == stdex::div128_portable(v, d).low);
      CHECK(q.high == stdex::div128_portable(v, d).high);
      stdex::uint128_t const prod = stdex::mul128_u64_lo(q, d);
      uint64_t const r_lo = v.low - prod.low;
      uint64_t const borrow = (v.low < prod.low) ? 1ULL : 0ULL;
      uint64_t const r_hi = v.high - prod.high - borrow;
      CHECK(r_hi == 0ULL);
      if (r_hi == 0ULL) CHECK(r_lo < d);
    }

    // mul128_u64_lo: zero-high reduces to the 64x64 path; pure-high is exact.
    {
      uint64_t const x = next_rand();
      uint64_t const mul = next_rand();
      stdex::uint128_t const a = stdex::mul128_u64_lo({ x, 0 }, mul);
      stdex::uint128_t const b = stdex::umul128(x, mul);
      CHECK(a.low == b.low);
      CHECK(a.high == b.high);
      stdex::uint128_t const c = stdex::mul128_u64_lo({ 0, x }, mul);
      CHECK(c.low == 0ULL);
      CHECK(c.high == x * mul);
      // Distributivity mod 2^128: lo((h+l)*m) == lo(h*m) + lo(l*m).
      stdex::uint128_t const h { 0, x };
      stdex::uint128_t const l { mul, 0 };
      stdex::uint128_t const s = stdex::add128(h, l.low);
      stdex::uint128_t const lhs = stdex::mul128_u64_lo(s, 0x9E37'79B9'7F4A'7C15ULL);
      stdex::uint128_t const ph = stdex::mul128_u64_lo(h, 0x9E37'79B9'7F4A'7C15ULL);
      stdex::uint128_t const pl = stdex::mul128_u64_lo(l, 0x9E37'79B9'7F4A'7C15ULL);
      uint64_t const sum_lo = ph.low + pl.low;
      uint64_t const sum_hi = ph.high + pl.high + ((sum_lo < ph.low) ? 1ULL : 0ULL);
      CHECK(lhs.low == sum_lo);
      CHECK(lhs.high == sum_hi);
    }

    // operator<=> vs long-hand comparison.
    {
      stdex::uint128_t const w { next_rand(), next_rand() };
      bool const exp = (v.high > w.high) || (v.high == w.high && v.low > w.low);
      CHECK(((v <=> w) > 0) == exp);
      CHECK(((v <=> v) > 0) == false);
    }
  }
#ifdef __SIZEOF_INT128__
  // Where __uint128_t exists (clang/gcc): direct differential for the primitives.
  for (int i = 0; i < 2000; ++i) {
    stdex::uint128_t const v { next_rand(), next_rand() };
    uint64_t const add = next_rand();
    __uint128_t const wide = (static_cast<__uint128_t>(v.high) << 64U) | v.low;
    __uint128_t const sum = wide + add;
    CHECK(stdex::add128(v, add).low == static_cast<uint64_t>(sum));
    CHECK(stdex::add128(v, add).high == static_cast<uint64_t>(sum >> 64U));
    uint64_t d = next_rand() | 1ULL;
    if (d == 0) d = 1;
    __uint128_t const quot = wide / d;
    CHECK(stdex::div128(v, d).low == static_cast<uint64_t>(quot));
    CHECK(stdex::div128(v, d).high == static_cast<uint64_t>(quot >> 64U));
    uint64_t const mul = next_rand();
    __uint128_t const prod_lo = wide * mul;
    CHECK(stdex::mul128_u64_lo(v, mul).low == static_cast<uint64_t>(prod_lo));
    CHECK(stdex::mul128_u64_lo(v, mul).high == static_cast<uint64_t>(prod_lo >> 64U));
  }
#endif
  return g_fail == fail_mark;
}

bool test_templates()
{
  int const fail_mark = g_fail;
  CHECK(stdex::fastmod<7>(100) == 100 % 7);
  CHECK(stdex::fastdiv<7>(100) == 100 / 7);
  CHECK(stdex::fastmod<100>(12345) == 12345 % 100);
  CHECK(stdex::fastdiv<100>(12345) == 12345 / 100);
  CHECK(stdex::fastmod<-7>(-100) == (-100) % (-7));
  CHECK(stdex::fastdiv<-7>(100) == 100 / (-7));
  CHECK(stdex::fastmod<7ULL>(100ULL) == 100ULL % 7ULL);
  CHECK(stdex::fastdiv<7ULL>(100ULL) == 100ULL / 7ULL);
  CHECK(stdex::fastmod<1000000007ULL>(123456789012345ULL) == 123456789012345ULL % 1000000007ULL);
  CHECK(stdex::fastdiv<1000000007ULL>(123456789012345ULL) == 123456789012345ULL / 1000000007ULL);
  return g_fail == fail_mark;
}

int main()
{
  std::printf("\n=== fastmod u32 ===\n");
  test_u32();
  std::printf("\n=== fastmod s32 ===\n");
  test_s32();
  std::printf("\n=== fastmod u64 ===\n");
  test_u64();
  std::printf("\n=== int128 primitives ===\n");
  test_int128_primitives();
  std::printf("\n=== fastmod/fastdiv templates ===\n");
  test_templates();

  std::printf("\n===============================\n");
  std::printf("PASS: %d   FAIL: %d\n", g_pass, g_fail);
  std::printf("===============================\n");
  return g_fail == 0 ? 0 : 1;
}
