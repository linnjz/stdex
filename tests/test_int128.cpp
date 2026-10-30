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

// test_int128.cpp
//
// Acceptance for the stdex::uint128_t/int128_t representation, operators,
// mixed signed/unsigned arithmetic, the Rust-style
// checked_/overflowing_/wrapping_/saturating_ arithmetic families, the exact
// <numeric>/<bit> overloads.

#include <array>
#include <compare>
#include <cstdint>
#include <cstdio>
#include <format>
#include <limits>
#include <locale>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "hash.hpp"
#include "int128.hpp"

namespace
{
int g_pass = 0;
int g_fail = 0;

#define CHECK(cond)                                                              \
  do {                                                                           \
    if (cond) {                                                                  \
      ++g_pass;                                                                  \
    } else {                                                                     \
      ++g_fail;                                                                  \
      if (g_fail < 25) {                                                         \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
      }                                                                          \
    }                                                                            \
  } while (0)

std::uint64_t g_state = 0x9E37'79B9'7F4A'7C15ULL;

std::uint64_t next_rand() noexcept
{
  std::uint64_t z = (g_state += 0x9E37'79B9'7F4A'7C15ULL);
  z               = (z ^ (z >> 30U)) * 0xBF58'476D'1CE4'E5B9ULL;
  z               = (z ^ (z >> 27U)) * 0x94D0'49BB'1331'11EBULL;
  return z ^ (z >> 31U);
}

stdex::uint128_t random_uint128() noexcept
{
  return stdex::uint128_t::from_words(next_rand(), next_rand());
}

// ---------------------------------------------------------------------------
// Portable reference helpers (independent of the implementation under test).
// ---------------------------------------------------------------------------

stdex::uint128_t add_ref(stdex::uint128_t const lhs, stdex::uint128_t const rhs, bool &carry) noexcept
{
  std::uint64_t const low   = lhs.lo() + rhs.lo();
  bool const low_carry      = low < lhs.lo();
  std::uint64_t const high  = lhs.hi() + rhs.hi() + (low_carry ? 1ULL : 0ULL);
  carry                     = !low_carry ? high < lhs.hi() : high <= lhs.hi();
  return stdex::uint128_t::from_words(low, high);
}

stdex::uint128_t sub_ref(stdex::uint128_t const lhs, stdex::uint128_t const rhs, bool &borrow) noexcept
{
  std::uint64_t const low  = lhs.lo() - rhs.lo();
  bool const low_borrow    = lhs.lo() < rhs.lo();
  std::uint64_t const high = lhs.hi() - rhs.hi() - (low_borrow ? 1ULL : 0ULL);
  borrow                   = lhs.hi() < rhs.hi() || (lhs.hi() == rhs.hi() && low_borrow);
  return stdex::uint128_t::from_words(low, high);
}

/// Schoolbook 128x128 -> 256 multiplication over 32-bit limbs.
void mul_full_ref(stdex::uint128_t const lhs,
                  stdex::uint128_t const rhs,
                  stdex::uint128_t &low,
                  stdex::uint128_t &high) noexcept
{
  std::uint32_t const a[4] = { static_cast<std::uint32_t>(lhs.lo()),
                               static_cast<std::uint32_t>(lhs.lo() >> 32U),
                               static_cast<std::uint32_t>(lhs.hi()),
                               static_cast<std::uint32_t>(lhs.hi() >> 32U) };
  std::uint32_t const b[4] = { static_cast<std::uint32_t>(rhs.lo()),
                               static_cast<std::uint32_t>(rhs.lo() >> 32U),
                               static_cast<std::uint32_t>(rhs.hi()),
                               static_cast<std::uint32_t>(rhs.hi() >> 32U) };
  std::uint32_t product[8] = {};

  for (unsigned i = 0; i < 4; ++i) {
    std::uint64_t carry = 0;
    for (unsigned j = 0; j < 4; ++j) {
      std::uint64_t const current =
          static_cast<std::uint64_t>(a[i]) * b[j] + product[i + j] + carry;
      product[i + j] = static_cast<std::uint32_t>(current);
      carry          = current >> 32U;
    }
    product[i + 4] = static_cast<std::uint32_t>(carry);
  }

  std::uint64_t const low_low  = static_cast<std::uint64_t>(product[0]) | (static_cast<std::uint64_t>(product[1]) << 32U);
  std::uint64_t const low_high = static_cast<std::uint64_t>(product[2]) | (static_cast<std::uint64_t>(product[3]) << 32U);
  std::uint64_t const high_low = static_cast<std::uint64_t>(product[4]) | (static_cast<std::uint64_t>(product[5]) << 32U);
  std::uint64_t const high_high = static_cast<std::uint64_t>(product[6]) | (static_cast<std::uint64_t>(product[7]) << 32U);
  low                           = stdex::uint128_t::from_words(low_low, low_high);
  high                          = stdex::uint128_t::from_words(high_low, high_high);
}

stdex::uint128_t shl_ref(stdex::uint128_t const value, unsigned int const count) noexcept
{
  if (count == 0) return value;
  if (count >= 128U) return stdex::uint128_t {};
  if (count >= 64U) {
    return stdex::uint128_t::from_words(0, value.lo() << (count - 64U));
  }
  return stdex::uint128_t::from_words(value.lo() << count,
                                      (value.hi() << count) | (value.lo() >> (64U - count)));
}

stdex::uint128_t shr_ref(stdex::uint128_t const value, unsigned int const count) noexcept
{
  if (count == 0) return value;
  if (count >= 128U) return stdex::uint128_t {};
  if (count >= 64U) {
    return stdex::uint128_t::from_words(value.hi() >> (count - 64U), 0);
  }
  return stdex::uint128_t::from_words((value.lo() >> count) | (value.hi() << (64U - count)),
                                      value.hi() >> count);
}

stdex::uint128_t int_bits(stdex::int128_t const value) noexcept
{
  return stdex::uint128_t::from_words(value.lo(), value.hi());
}

stdex::int128_t int_from_bits(stdex::uint128_t const bits) noexcept
{
  return stdex::int128_t(bits);
}

stdex::uint128_t int_negate_ref(stdex::uint128_t const bits) noexcept
{
  bool borrow = false;
  return sub_ref(stdex::uint128_t {}, bits, borrow);
}

stdex::uint128_t int_abs_bits(stdex::int128_t const value) noexcept
{
  stdex::uint128_t const bits = int_bits(value);
  return value.is_negative() ? int_negate_ref(bits) : bits;
}

stdex::uint128_t int_asr_ref(stdex::int128_t const value, unsigned int const count) noexcept
{
  stdex::uint128_t const bits = int_bits(value);
  bool const negative         = value.is_negative();
  if (count == 0) return bits;
  if (count >= 128U) return negative ? ~stdex::uint128_t {} : stdex::uint128_t {};
  if (count >= 64U) {
    std::uint64_t low = bits.hi() >> (count - 64U);
    if (negative && count != 64U) {
      low |= ~0ULL << (128U - count);
    }
    return stdex::uint128_t::from_words(low, negative ? ~0ULL : 0ULL);
  }
  std::uint64_t const high = (bits.hi() >> count) |
                             (negative ? (~0ULL << (64U - count)) : 0ULL);
  std::uint64_t const low = (bits.lo() >> count) | (bits.hi() << (64U - count));
  return stdex::uint128_t::from_words(low, high);
}

bool int_add_ref(stdex::int128_t const lhs,
                 stdex::int128_t const rhs,
                 stdex::uint128_t &result) noexcept
{
  bool carry = false;
  result     = add_ref(int_bits(lhs), int_bits(rhs), carry);
  bool const lhs_negative = lhs.is_negative();
  bool const rhs_negative = rhs.is_negative();
  bool const sum_negative = int_from_bits(result).is_negative();
  return lhs_negative == rhs_negative && sum_negative != lhs_negative;
}

bool int_sub_ref(stdex::int128_t const lhs,
                 stdex::int128_t const rhs,
                 stdex::uint128_t &result) noexcept
{
  bool borrow = false;
  result      = sub_ref(int_bits(lhs), int_bits(rhs), borrow);
  bool const lhs_negative = lhs.is_negative();
  bool const rhs_negative = rhs.is_negative();
  bool const sum_negative = int_from_bits(result).is_negative();
  return lhs_negative != rhs_negative && sum_negative != lhs_negative;
}

bool int_mul_ref(stdex::int128_t const lhs,
                 stdex::int128_t const rhs,
                 stdex::uint128_t &result) noexcept
{
  stdex::uint128_t const lhs_abs = int_abs_bits(lhs);
  stdex::uint128_t const rhs_abs = int_abs_bits(rhs);
  bool const negative = lhs.is_negative() != rhs.is_negative();

  stdex::uint128_t low {};
  stdex::uint128_t high {};
  mul_full_ref(lhs_abs, rhs_abs, low, high);

  stdex::uint128_t const limit =
      negative ? stdex::uint128_t::from_words(0, 0x8000'0000'0000'0000ULL)
               : stdex::uint128_t::from_words(~0ULL, 0x7FFF'FFFF'FFFF'FFFFULL);
  bool const overflow = !high.is_zero() || low > limit;
  result              = negative ? int_negate_ref(low) : low;
  return overflow;
}

void int_div_ref(stdex::int128_t const lhs,
                 stdex::int128_t const rhs,
                 stdex::int128_t &quotient,
                 stdex::int128_t &remainder,
                 bool &overflow) noexcept
{
  stdex::int128_t const min = stdex::int128_t::from_words(0, 0x8000'0000'0000'0000ULL);
  stdex::int128_t const neg_one = stdex::int128_t::from_i64(-1);
  overflow = lhs == min && rhs == neg_one;
  if (overflow) {
    quotient  = min;
    remainder = stdex::int128_t {};
    return;
  }

  stdex::uint128_t const lhs_abs = int_abs_bits(lhs);
  stdex::uint128_t const rhs_abs = int_abs_bits(rhs);
  stdex::uint128_t const q_abs   = lhs_abs / rhs_abs;
  stdex::uint128_t const r_abs   = lhs_abs % rhs_abs;
  quotient =
      (lhs.is_negative() != rhs.is_negative()) ? int_from_bits(int_negate_ref(q_abs)) : int_from_bits(q_abs);
  remainder = lhs.is_negative() ? int_from_bits(int_negate_ref(r_abs)) : int_from_bits(r_abs);
}

// ---------------------------------------------------------------------------
// Compile-time checks
// ---------------------------------------------------------------------------

using stdex::int128_t;
using stdex::uint128_t;

int128_t random_int128() noexcept
{
  return int128_t(random_uint128());
}

template <typename T>
concept uint128_shiftable = requires(uint128_t const value, T const count) {
  value << count;
  value >> count;
};

static_assert(uint128_shiftable<unsigned int>);
static_assert(uint128_shiftable<unsigned long long>);
static_assert(uint128_shiftable<std::uint8_t>);
static_assert(uint128_shiftable<int>);
static_assert(uint128_shiftable<long long>);
static_assert(uint128_shiftable<int128_t>);
static_assert(!uint128_shiftable<bool>);

static_assert(sizeof(uint128_t) == 16);
static_assert(alignof(uint128_t) == 16);
static_assert(std::is_trivially_copyable_v<uint128_t>);
static_assert(std::is_standard_layout_v<uint128_t>);

// Parked: int128.hpp does not specialize the <type_traits> integral traits.
// static_assert(std::is_arithmetic_v<uint128_t>);
// static_assert(std::is_integral_v<uint128_t>);
// static_assert(std::is_unsigned_v<uint128_t>);
// static_assert(!std::is_signed_v<uint128_t>);
// static_assert(std::unsigned_integral<uint128_t>);
// static_assert(!std::signed_integral<uint128_t>);
// static_assert(std::is_same_v<std::make_signed_t<uint128_t>, int128_t>);
static_assert(std::is_same_v<std::common_type_t<int128_t, uint128_t>, uint128_t>);
// static_assert(std::is_integral_v<const uint128_t>);
// static_assert(std::unsigned_integral<volatile uint128_t>);

constexpr uint128_t cu_max = ~uint128_t {};
constexpr uint128_t cu_one = uint128_t::from_u64(1);
constexpr uint128_t cu_two = uint128_t::from_u64(2);
constexpr uint128_t cu_three = uint128_t::from_u64(3);

static_assert(uint128_t {}.is_zero());
static_assert(!cu_one.is_zero());
static_assert((cu_one + cu_two) == cu_three);
static_assert((cu_three - cu_one) == cu_two);
static_assert((cu_two * cu_three) == uint128_t::from_u64(6));
static_assert((uint128_t::from_u64(100) / uint128_t::from_u64(7)) == uint128_t::from_u64(14));
static_assert((uint128_t::from_u64(100) % uint128_t::from_u64(7)) == cu_two);
static_assert((cu_one << 127U).hi() == 0x8000'0000'0000'0000ULL);
static_assert(((cu_one << 127U) >> 127U) == cu_one);
static_assert(!cu_one.checked_shl(128U).has_value());
static_assert(!cu_one.checked_shr(128U).has_value());
static_assert(cu_one.checked_shl(127U).value() == (cu_one << 127U));
static_assert(cu_one.unchecked_shl(127U) == (cu_one << 127U));
static_assert(cu_two.unchecked_shr(1U) == cu_one);
static_assert((cu_one << 127ULL).hi() == 0x8000'0000'0000'0000ULL);
static_assert((cu_one << 1) == cu_two);
static_assert((cu_two >> 1) == cu_one);
static_assert((cu_one << int128_t::from_u64(1)) == cu_two);
static_assert((cu_two >> uint128_t::from_u64(1)) == cu_one);
static_assert(cu_one.checked_shl(uint128_t::from_u64(127)) == (cu_one << 127U));
static_assert(!cu_one.checked_shl(int128_t::from_i64(-1)).has_value());
static_assert(!cu_one.checked_shl(uint128_t::from_words(0, 1)).has_value());
static_assert(cu_one.wrapping_shl(int128_t::from_i64(-1)) == (cu_one << 127U));
static_assert(cu_one.wrapping_shr(uint128_t::from_words(0, 1)) == cu_one);
static_assert(cu_one.overflowing_shl(int128_t::from_i64(-1)) ==
              (std::pair<uint128_t, bool> { cu_one << 127U, true }));
static_assert(cu_one.overflowing_shl(129U) == (std::pair<uint128_t, bool> { cu_two, true }));
static_assert(cu_two.overflowing_shr(129U) == (std::pair<uint128_t, bool> { cu_one, true }));
static_assert(cu_one.overflowing_shl(3U) == (std::pair<uint128_t, bool> { uint128_t::from_u64(8), false }));
static_assert(cu_one.wrapping_shl(128U) == cu_one);
static_assert(cu_one.wrapping_shl(129U) == cu_two);
static_assert(cu_two.wrapping_shr(129U) == cu_one);
static_assert((~cu_one) == cu_max.wrapping_sub(cu_one));
static_assert(!uint128_t {});

static_assert(cu_max.wrapping_add(cu_one).is_zero());
static_assert(uint128_t {}.wrapping_sub(cu_one) == cu_max);
static_assert(cu_max.wrapping_mul(cu_max) == cu_one);
static_assert(cu_one.wrapping_neg() == cu_max);
static_assert(cu_one.wrapping_div(cu_one) == cu_one);
static_assert(cu_one.wrapping_rem(cu_one).is_zero());

static_assert(cu_two.checked_add(cu_three) == uint128_t::from_u64(5));
static_assert(!cu_max.checked_add(cu_one).has_value());
static_assert(uint128_t {}.checked_sub(cu_one) == std::nullopt);
static_assert(!cu_max.checked_mul(cu_two).has_value());
static_assert(cu_two.checked_mul(cu_three) == uint128_t::from_u64(6));
static_assert(!cu_two.checked_div(uint128_t {}).has_value());
static_assert(cu_two.checked_div(cu_one) == cu_two);
static_assert(!cu_two.checked_rem(uint128_t {}).has_value());
static_assert(!cu_two.checked_divmod(uint128_t {}).has_value());
static_assert(cu_two.checked_divmod(cu_one) == (stdex::divmod_result { cu_two, uint128_t {} }));
static_assert(cu_one.checked_divmod(cu_two) == (stdex::divmod_result { uint128_t {}, cu_one }));
static_assert(cu_two.unchecked_divmod(cu_one) == (stdex::divmod_result { cu_two, uint128_t {} }));
static_assert(cu_three.unchecked_divmod(cu_two) == (stdex::divmod_result { cu_one, cu_one }));
static_assert(uint128_t {}.checked_neg().has_value());
static_assert(!cu_one.checked_neg().has_value());

static_assert(cu_max.overflowing_add(cu_one) == std::pair<uint128_t, bool> { uint128_t {}, true });
static_assert(cu_two.overflowing_mul(cu_three) == std::pair<uint128_t, bool> { uint128_t::from_u64(6), false });
static_assert(cu_one.overflowing_neg() == std::pair<uint128_t, bool> { cu_max, true });
static_assert(uint128_t {}.overflowing_neg().second == false);
static_assert(cu_one.overflowing_div(cu_one) == std::pair<uint128_t, bool> { cu_one, false });

static_assert(cu_max.saturating_add(cu_one) == cu_max);
static_assert(uint128_t {}.saturating_sub(cu_one).is_zero());
static_assert(cu_max.saturating_mul(cu_two) == cu_max);
static_assert(cu_two.saturating_mul(cu_three) == uint128_t::from_u64(6));
static_assert(cu_one.saturating_neg().is_zero());
static_assert(cu_one.saturating_div(cu_one) == cu_one);

constexpr auto kLeProbe = [] {
  std::array<std::uint8_t, 16> bytes {};
  uint128_t::from_words(0x0123'4567'89AB'CDEFULL, 0xFEDC'BA98'7654'3210ULL).to_le_bytes(bytes.data());
  return bytes;
}();
static_assert(kLeProbe[0] == 0xEF && kLeProbe[7] == 0x01 && kLeProbe[8] == 0x10 && kLeProbe[15] == 0xFE);
static_assert(uint128_t::from_le_bytes(kLeProbe.data()) ==
              uint128_t::from_words(0x0123'4567'89AB'CDEFULL, 0xFEDC'BA98'7654'3210ULL));

template <typename T>
concept int128_shiftable = requires(int128_t const value, T const count) {
  value << count;
  value >> count;
};

static_assert(int128_shiftable<unsigned int>);
static_assert(int128_shiftable<unsigned long long>);
static_assert(int128_shiftable<std::uint8_t>);
static_assert(int128_shiftable<int>);
static_assert(int128_shiftable<long long>);
static_assert(int128_shiftable<uint128_t>);
static_assert(!int128_shiftable<bool>);

static_assert(sizeof(int128_t) == 16);
static_assert(alignof(int128_t) == 16);
static_assert(std::is_trivially_copyable_v<int128_t>);
static_assert(std::is_standard_layout_v<int128_t>);

// Parked: int128.hpp does not specialize the <type_traits> integral traits.
// static_assert(std::is_arithmetic_v<int128_t>);
// static_assert(std::is_integral_v<int128_t>);
// static_assert(std::is_signed_v<int128_t>);
// static_assert(!std::is_unsigned_v<int128_t>);
// static_assert(std::signed_integral<int128_t>);
// static_assert(!std::unsigned_integral<int128_t>);
// static_assert(std::is_same_v<std::make_unsigned_t<int128_t>, uint128_t>);
static_assert(std::is_same_v<std::common_type_t<int128_t, uint128_t>, uint128_t>);
// static_assert(std::is_integral_v<const int128_t>);

constexpr int128_t ci_zero = int128_t {};
constexpr int128_t ci_one = int128_t::from_u64(1);
constexpr int128_t ci_two = int128_t::from_u64(2);
constexpr int128_t ci_three = int128_t::from_u64(3);
constexpr int128_t ci_neg_one = int128_t::from_i64(-1);
constexpr int128_t ci_min = int128_t::from_words(0, 0x8000'0000'0000'0000ULL);
constexpr int128_t ci_max = int128_t::from_words(~0ULL, 0x7FFF'FFFF'FFFF'FFFFULL);

static_assert(ci_zero.is_zero());
static_assert(!ci_one.is_zero());
static_assert(!ci_zero.is_negative());
static_assert(ci_neg_one.is_negative());
static_assert(ci_min.is_negative());
static_assert(!ci_max.is_negative());
static_assert(static_cast<std::int64_t>(ci_neg_one) == -1);
static_assert(static_cast<std::uint64_t>(ci_neg_one) == ~0ULL);
static_assert(static_cast<int128_t>(cu_max) == ci_neg_one);
static_assert(static_cast<uint128_t>(ci_neg_one) == cu_max);
static_assert(!int128_t {});

static_assert((ci_one + ci_two) == ci_three);
static_assert((ci_two - ci_three) == ci_neg_one);
static_assert((ci_two * ci_three) == int128_t::from_u64(6));
static_assert((int128_t::from_i64(-7) / int128_t::from_i64(3)) == int128_t::from_i64(-2));
static_assert((int128_t::from_i64(-7) % int128_t::from_i64(3)) == ci_neg_one);
static_assert((int128_t::from_i64(7) / int128_t::from_i64(-3)) == int128_t::from_i64(-2));
static_assert((int128_t::from_i64(7) % int128_t::from_i64(-3)) == ci_one);
static_assert((ci_neg_one >> 1U) == ci_neg_one);
static_assert((ci_min >> 127U) == ci_neg_one);
static_assert((ci_min >> 64U) == int128_t::from_words(0x8000'0000'0000'0000ULL, ~0ULL));
static_assert((ci_one << 126U) == int128_t::from_words(0, 0x4000'0000'0000'0000ULL));
static_assert((ci_one << 1) == ci_two);
static_assert((ci_neg_one >> 1) == ci_neg_one);
static_assert((ci_neg_one >> uint128_t::from_u64(1)) == ci_neg_one);
static_assert(ci_neg_one.checked_shr(int128_t::from_u64(127)) == ci_neg_one);
static_assert(!ci_one.checked_shl(int128_t::from_i64(-1)).has_value());
static_assert(ci_one.wrapping_shr(int128_t::from_i64(-1)).is_zero());
static_assert(ci_neg_one.wrapping_shr(int128_t::from_i64(-1)) == ci_neg_one);
static_assert(uint128_t {}.equal_min());
static_assert(!cu_one.equal_min());
static_assert(!uint128_t {}.division_overflow(cu_max));
static_assert(ci_min.equal_min());
static_assert(!ci_neg_one.equal_min());

// Parked (step 3): stdex::div is not implemented yet.
static_assert(stdex::abs(ci_neg_one) == ci_one);
static_assert(stdex::abs(ci_min) == ci_min);
// static_assert(stdex::div(ci_neg_one, ci_two).quot.is_zero());
// static_assert(stdex::div(ci_neg_one, ci_two).rem == ci_neg_one);
// static_assert(stdex::div(uint128_t::from_u64(7), cu_two).quot == cu_three);
// static_assert(stdex::div(uint128_t::from_u64(7), cu_two).rem == cu_one);
static_assert(stdex::gcd(uint128_t::from_u64(12), uint128_t::from_u64(18)) == uint128_t::from_u64(6));
static_assert(stdex::gcd(int128_t::from_i64(-12), int128_t::from_i64(18)) == int128_t::from_u64(6));
static_assert(stdex::gcd(12, uint128_t::from_u64(18)) == uint128_t::from_u64(6));
static_assert(stdex::lcm(uint128_t::from_u64(4), uint128_t::from_u64(6)) == uint128_t::from_u64(12));
static_assert(stdex::lcm(int128_t::from_i64(-4), int128_t::from_i64(6)) == int128_t::from_u64(12));
static_assert(stdex::lcm(4, int128_t::from_i64(6)) == int128_t::from_u64(12));
// static_assert(stdex::div(cu_max, cu_one).quot == cu_max);
// static_assert(stdex::div(ci_min, ci_one).quot == ci_min);
static_assert(stdex::gcd(cu_max, cu_max) == cu_max);
static_assert(stdex::gcd(cu_max, cu_max - cu_one) == cu_one);
static_assert(stdex::gcd(ci_min, ci_max) == ci_one);
static_assert(stdex::lcm(cu_max, cu_max) == cu_max);
static_assert(stdex::lcm(ci_max, ci_max) == ci_max);
// Unrepresentable results are precondition violations (assert/abort), not 0:
// gcd(ci_min, ci_min) == 2^127, lcm(cu_max, cu_max - 1) overflows 128 bits,
// and lcm(ci_max, ci_max - 1) == 2^127 * (2^127 - 1) > INT128_MAX.
static_assert(stdex::midpoint(uint128_t {}, cu_max) == uint128_t::from_words(~0ULL, 0x7FFF'FFFF'FFFF'FFFFULL));
static_assert(stdex::midpoint(cu_max, uint128_t {}) == uint128_t::from_words(0, 0x8000'0000'0000'0000ULL));
static_assert(stdex::midpoint(ci_min, ci_max) == ci_neg_one);
static_assert(stdex::midpoint(ci_max, ci_min) == ci_zero);
static_assert(stdex::midpoint(ci_zero, ci_min) == int128_t::from_words(0, 0xC000'0000'0000'0000ULL));
static_assert(stdex::midpoint(ci_min, ci_zero) == int128_t::from_words(0, 0xC000'0000'0000'0000ULL));
static_assert(stdex::pow(uint128_t::from_u64(2), 10) == uint128_t::from_u64(1024));
static_assert(stdex::pow(uint128_t::from_u64(2), uint128_t::from_u64(10)) == uint128_t::from_u64(1024));
static_assert(stdex::pow(int128_t::from_i64(-2), 3) == int128_t::from_i64(-8));
static_assert(stdex::pow(int128_t::from_i64(-2), 4) == int128_t::from_i64(16));
static_assert(stdex::pow(int128_t::from_i64(-1), -3) == int128_t::from_i64(-1));
static_assert(stdex::pow(int128_t::from_i64(-1), -2) == int128_t::from_i64(1));
static_assert(stdex::pow(int128_t::from_u64(1), -5) == int128_t::from_u64(1));
static_assert(stdex::pow(uint128_t::from_u64(1), -5) == uint128_t::from_u64(1));
static_assert(stdex::pow(int128_t::from_i64(2), -1).is_zero());
static_assert(stdex::pow(int128_t::from_i64(-2), -1).is_zero());
static_assert(stdex::pow(int128_t::from_i64(0), 0) == int128_t::from_u64(1));
static_assert(stdex::pow(int128_t::from_i64(0), -1).is_zero());
static_assert(stdex::pow(uint128_t::from_u64(0), 0) == uint128_t::from_u64(1));
static_assert(stdex::pow(uint128_t::from_u64(0), 5).is_zero());
static_assert(stdex::pow(uint128_t::from_u64(2), 127) == uint128_t::from_words(0, 0x8000'0000'0000'0000ULL));
static_assert(stdex::pow(uint128_t::from_u64(2), 128).is_zero());
static_assert(stdex::pow(int128_t::from_i64(2), 126) == int128_t::from_words(0, 0x4000'0000'0000'0000ULL));
static_assert(stdex::pow(int128_t::from_i64(2), 127).is_zero());
static_assert(stdex::pow(int128_t::from_i64(-2), 127) == int128_t::from_words(0, 0x8000'0000'0000'0000ULL));
static_assert(stdex::pow(int128_t::from_i64(-2), 128).is_zero());
static_assert(stdex::pow(ci_min, 1) == ci_min);
static_assert(stdex::pow(ci_min, 0) == ci_one);
static_assert(stdex::pow(ci_min, -1).is_zero());
static_assert(ci_min.division_overflow(ci_neg_one));
static_assert(!ci_min.division_overflow(ci_one));
static_assert(ci_neg_one.checked_shr(127U) == ci_neg_one);
static_assert(!ci_neg_one.checked_shr(128U).has_value());
static_assert(ci_neg_one.overflowing_shr(129U) == (std::pair<int128_t, bool> { ci_neg_one, true }));
static_assert(ci_neg_one.wrapping_shr(129U) == ci_neg_one);

static_assert(ci_max.wrapping_add(ci_one) == ci_min);
static_assert(ci_max.overflowing_add(ci_one) == (std::pair<int128_t, bool> { ci_min, true }));
static_assert(!ci_max.checked_add(ci_one).has_value());
static_assert(ci_max.saturating_add(ci_one) == ci_max);
static_assert(ci_min.wrapping_sub(ci_one) == ci_max);
static_assert(ci_min.overflowing_sub(ci_one) == (std::pair<int128_t, bool> { ci_max, true }));
static_assert(!ci_min.checked_sub(ci_one).has_value());
static_assert(ci_min.saturating_sub(ci_one) == ci_min);
static_assert(ci_max.wrapping_mul(ci_two) == int128_t::from_i64(-2));
static_assert(ci_max.overflowing_mul(ci_two) == (std::pair<int128_t, bool> { int128_t::from_i64(-2), true }));
static_assert(!ci_max.checked_mul(ci_two).has_value());
static_assert(ci_max.saturating_mul(ci_two) == ci_max);
static_assert(ci_min.saturating_mul(ci_neg_one) == ci_max);
static_assert(ci_min.wrapping_neg() == ci_min);
static_assert(ci_min.overflowing_neg() == (std::pair<int128_t, bool> { ci_min, true }));
static_assert(!ci_min.checked_neg().has_value());
static_assert(ci_min.saturating_neg() == ci_max);
static_assert(ci_neg_one.saturating_neg() == ci_one);

static_assert(ci_min.wrapping_div(ci_neg_one) == ci_min);
static_assert(ci_min.wrapping_rem(ci_neg_one) == ci_zero);
static_assert(ci_min.overflowing_div(ci_neg_one) == (std::pair<int128_t, bool> { ci_min, true }));
static_assert(ci_min.overflowing_rem(ci_neg_one) == (std::pair<int128_t, bool> { ci_zero, true }));
static_assert(!ci_min.checked_div(ci_neg_one).has_value());
static_assert(!ci_min.checked_rem(ci_neg_one).has_value());
static_assert(ci_min.saturating_div(ci_neg_one) == ci_max);
static_assert(ci_neg_one.checked_div(ci_zero) == std::nullopt);
static_assert(ci_neg_one.checked_rem(ci_zero) == std::nullopt);
static_assert(!ci_neg_one.checked_divmod(ci_zero).has_value());
static_assert(!ci_min.checked_divmod(ci_neg_one).has_value());
static_assert(ci_min.checked_divmod(ci_one) == (stdex::divmod_result { ci_min, ci_zero }));
static_assert(ci_neg_one.checked_divmod(ci_two) == (stdex::divmod_result { ci_zero, ci_neg_one }));
static_assert(ci_min.unchecked_divmod(ci_one) == (stdex::divmod_result { ci_min, ci_zero }));
static_assert(ci_neg_one.unchecked_divmod(ci_two) == (stdex::divmod_result { ci_zero, ci_neg_one }));
static_assert(ci_neg_one.unchecked_divmod(ci_neg_one) == (stdex::divmod_result { ci_one, ci_zero }));

static_assert(ci_neg_one < ci_zero);
static_assert(ci_min < ci_max);
static_assert((ci_min <=> ci_max) == std::strong_ordering::less);
static_assert((ci_neg_one <=> ci_neg_one) == std::strong_ordering::equal);

static_assert(std::is_same_v<decltype(ci_one + cu_one), uint128_t>);
static_assert(std::is_same_v<decltype(ci_one == cu_one), bool>);
static_assert(std::is_same_v<decltype(ci_one < cu_one), bool>);
static_assert(std::is_same_v<decltype(ci_one <=> cu_one), std::strong_ordering>);
static_assert(ci_one + cu_two == cu_three);
static_assert(cu_two + ci_one == cu_three);
static_assert(cu_three - ci_one == cu_two);
static_assert(ci_three - cu_one == cu_two);
static_assert(ci_two * cu_three == uint128_t::from_u64(6));
static_assert(ci_three / cu_two == cu_one);
static_assert(ci_three % cu_two == cu_one);
static_assert((ci_three & cu_one) == cu_one);
static_assert((ci_three | cu_one) == cu_three);
static_assert((ci_three ^ cu_one) == cu_two);
static_assert(ci_neg_one == cu_max);
static_assert(cu_max == ci_neg_one);
static_assert(ci_neg_one != cu_one);
static_assert(!(ci_neg_one < cu_one));
static_assert(cu_one < ci_neg_one);
static_assert((ci_neg_one <=> cu_one) == std::strong_ordering::greater);
static_assert((cu_one <=> ci_neg_one) == std::strong_ordering::less);

constexpr auto kIntLeProbe = [] {
  std::array<std::uint8_t, 16> bytes {};
  int128_t::from_i64(-2).to_le_bytes(bytes.data());
  return bytes;
}();
static_assert(kIntLeProbe[0] == 0xFE && kIntLeProbe[7] == 0xFF && kIntLeProbe[8] == 0xFF && kIntLeProbe[15] == 0xFF);
static_assert(int128_t::from_le_bytes(kIntLeProbe.data()) == int128_t::from_i64(-2));

#if defined(__SIZEOF_INT128__)
#  if defined(__GNUC__) && !defined(__clang__)
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wpedantic"
#  endif
constexpr __uint128_t native_from(uint128_t const value) noexcept
{
  return (static_cast<__uint128_t>(value.hi()) << 64U) | static_cast<__uint128_t>(value.lo());
}

[[maybe_unused]] constexpr uint128_t from_native(__uint128_t const value) noexcept
{
  return uint128_t::from_words(static_cast<std::uint64_t>(value), static_cast<std::uint64_t>(value >> 64U));
}

[[maybe_unused]] __uint128_t native_from(int128_t const value) noexcept
{
  return native_from(int_bits(value));
}

[[maybe_unused]] __int128 native_signed_from(int128_t const value) noexcept
{
  return static_cast<__int128>(native_from(int_bits(value)));
}

[[maybe_unused]] int128_t from_native_signed(__int128 const value) noexcept
{
  return int_from_bits(from_native(static_cast<__uint128_t>(value)));
}
#  if defined(__GNUC__) && !defined(__clang__)
#    pragma GCC diagnostic pop
#  endif
#endif

template <class CharT>
struct test_numpunct : std::numpunct<CharT>
{
  std::string grouping;
  CharT separator;

  test_numpunct(std::string group, CharT thousands_sep)
      : grouping(std::move(group)), separator(thousands_sep)
  {
  }

protected:
  CharT do_thousands_sep() const override { return separator; }
  std::string do_grouping() const override { return grouping; }
};

template <class... Args>
bool format_throws(std::string_view format, Args &&...args)
{
  try {
    (void)std::vformat(format, std::make_format_args(args...));
  } catch (std::format_error const &) {
    return true;
  } catch (...) {
    return false;
  }
  return false;
}

template <class... Args>
bool format_throws(std::wstring_view format, Args &&...args)
{
  try {
    (void)std::vformat(format, std::make_wformat_args(args...));
  } catch (std::format_error const &) {
    return true;
  } catch (...) {
    return false;
  }
  return false;
}

} // namespace

int main()
{
  std::printf("=== int128 uint128_t tests ===\n");

  // --- construction / conversions / bytes ---------------------------------
  {
    uint128_t const value = uint128_t::from_words(0x0123'4567'89AB'CDEFULL, 0xFEDC'BA98'7654'3210ULL);
    CHECK(value.lo() == 0x0123'4567'89AB'CDEFULL);
    CHECK(value.hi() == 0xFEDC'BA98'7654'3210ULL);
    CHECK(static_cast<std::uint64_t>(value) == 0x0123'4567'89AB'CDEFULL);
    CHECK(static_cast<std::uint32_t>(value) == 0x89AB'CDEFU);
    CHECK(static_cast<std::uint8_t>(value) == 0xEFU);

    std::uint8_t le[16] {};
    value.to_le_bytes(le);
    CHECK(le[0] == 0xEFU);
    CHECK(le[7] == 0x01U);
    CHECK(le[8] == 0x10U);
    CHECK(le[15] == 0xFEU);
    CHECK(uint128_t::from_le_bytes(le) == value);

    std::uint8_t be[16] {};
    value.to_be_bytes(be);
    CHECK(be[0] == 0xFEU);
    CHECK(be[15] == 0xEFU);
    CHECK(uint128_t::from_be_bytes(be) == value);

    CHECK(uint128_t::from_i64(-1).lo() == ~0ULL);
    CHECK(uint128_t::from_i64(-1).hi() == ~0ULL);
    CHECK(uint128_t::from_u64(0xFFFF'FFFF'FFFF'FFFFULL).hi() == 0);
    CHECK(static_cast<std::int32_t>(uint128_t::from_i64(-1)) == -1);
    CHECK(static_cast<std::int8_t>(uint128_t::from_i64(0x1FF)) == -1);
    CHECK(static_cast<std::uint16_t>(value) == 0xCDEFU);

  }

  // --- explicit arithmetic modes against portable references ---------------
  for (int iteration = 0; iteration < 50000; ++iteration) {
    uint128_t const lhs = random_uint128();
    uint128_t const rhs = random_uint128();

    bool carry = false;
    uint128_t const sum = add_ref(lhs, rhs, carry);
    CHECK(lhs.wrapping_add(rhs) == sum);
    CHECK(lhs.overflowing_add(rhs).first == sum);
    CHECK(lhs.overflowing_add(rhs).second == carry);
    auto const checked_sum = lhs.checked_add(rhs);
    CHECK(checked_sum.has_value() == !carry);
    if (checked_sum) {
      CHECK(*checked_sum == sum);
    }
    CHECK(lhs.saturating_add(rhs) == (carry ? cu_max : sum));
    if (!carry) {
      CHECK((lhs + rhs) == sum);
    }

    bool borrow = false;
    uint128_t const difference = sub_ref(lhs, rhs, borrow);
    CHECK(lhs.wrapping_sub(rhs) == difference);
    CHECK(lhs.overflowing_sub(rhs).first == difference);
    CHECK(lhs.overflowing_sub(rhs).second == borrow);
    auto const checked_difference = lhs.checked_sub(rhs);
    CHECK(checked_difference.has_value() == !borrow);
    if (checked_difference) {
      CHECK(*checked_difference == difference);
    }
    CHECK(lhs.saturating_sub(rhs) == (borrow ? uint128_t {} : difference));
    if (!borrow) {
      CHECK((lhs - rhs) == difference);
    }

    uint128_t product_low {};
    uint128_t product_high {};
    mul_full_ref(lhs, rhs, product_low, product_high);
    bool const product_overflows = !product_high.is_zero();
    CHECK(lhs.wrapping_mul(rhs) == product_low);
    CHECK(lhs.overflowing_mul(rhs).first == product_low);
    CHECK(lhs.overflowing_mul(rhs).second == product_overflows);
    auto const checked_product = lhs.checked_mul(rhs);
    CHECK(checked_product.has_value() == !product_overflows);
    if (checked_product) {
      CHECK(*checked_product == product_low);
    }
    CHECK(lhs.saturating_mul(rhs) == (product_overflows ? cu_max : product_low));
    if (!product_overflows) {
      CHECK((lhs * rhs) == product_low);
    }

    uint128_t const negated = (~lhs).wrapping_add(cu_one);
    CHECK(lhs.wrapping_neg() == negated);
    CHECK(lhs.overflowing_neg().first == negated);
    CHECK(lhs.overflowing_neg().second == !lhs.is_zero());
    CHECK(lhs.checked_neg().has_value() == lhs.is_zero());
    CHECK(lhs.saturating_neg().is_zero());

    if (!rhs.is_zero()) {
      uint128_t const quotient  = lhs / rhs;
      uint128_t const remainder = lhs % rhs;
      CHECK(remainder < rhs);
      CHECK(quotient * rhs + remainder == lhs);

      CHECK(lhs.checked_div(rhs) == quotient);
      CHECK(lhs.checked_rem(rhs) == remainder);
      CHECK(lhs.unchecked_div(rhs) == quotient);
      CHECK(lhs.unchecked_rem(rhs) == remainder);
      CHECK(lhs.checked_divmod(rhs) == (stdex::divmod_result { quotient, remainder }));
      CHECK(lhs.unchecked_divmod(rhs) == (stdex::divmod_result { quotient, remainder }));
      CHECK(lhs.overflowing_div(rhs) == (std::pair<uint128_t, bool> { quotient, false }));
      CHECK(lhs.overflowing_rem(rhs) == (std::pair<uint128_t, bool> { remainder, false }));
      CHECK(lhs.wrapping_div(rhs) == quotient);
      CHECK(lhs.wrapping_rem(rhs) == remainder);
      CHECK(lhs.saturating_div(rhs) == quotient);
    } else {
      CHECK(!lhs.checked_div(rhs).has_value());
      CHECK(!lhs.checked_rem(rhs).has_value());
      CHECK(!lhs.checked_divmod(rhs).has_value());
    }

#if !STDEX_INT128_HAS_OVERFLOW_CHECK
    CHECK((lhs + rhs) == sum);
    CHECK((lhs - rhs) == difference);
    CHECK((lhs * rhs) == product_low);
#endif

    unsigned int const shift = static_cast<unsigned int>(next_rand() % 128U);
    uint128_t const shifted_left = shl_ref(lhs, shift);
    uint128_t const shifted_right = shr_ref(lhs, shift);
    CHECK((lhs << shift) == shifted_left);
    CHECK((lhs >> shift) == shifted_right);

    CHECK(lhs.checked_shl(shift) == shifted_left);
    CHECK(lhs.checked_shr(shift) == shifted_right);
    CHECK(!lhs.checked_shl(shift + 128U).has_value());
    CHECK(!lhs.checked_shr(shift + 128U).has_value());

    CHECK(lhs.overflowing_shl(shift) == (std::pair<uint128_t, bool> { shifted_left, false }));
    CHECK(lhs.overflowing_shr(shift) == (std::pair<uint128_t, bool> { shifted_right, false }));
    CHECK(lhs.overflowing_shl(shift + 128U) == (std::pair<uint128_t, bool> { shifted_left, true }));
    CHECK(lhs.overflowing_shr(shift + 128U) == (std::pair<uint128_t, bool> { shifted_right, true }));

    CHECK(lhs.wrapping_shl(shift) == shifted_left);
    CHECK(lhs.wrapping_shr(shift) == shifted_right);
    CHECK(lhs.wrapping_shl(shift + 128U) == shifted_left);
    CHECK(lhs.wrapping_shr(shift + 128U) == shifted_right);

    CHECK((lhs & rhs) == uint128_t::from_words(lhs.lo() & rhs.lo(), lhs.hi() & rhs.hi()));
    CHECK((lhs | rhs) == uint128_t::from_words(lhs.lo() | rhs.lo(), lhs.hi() | rhs.hi()));
    CHECK((lhs ^ rhs) == uint128_t::from_words(lhs.lo() ^ rhs.lo(), lhs.hi() ^ rhs.hi()));
    CHECK((~lhs) == uint128_t::from_words(~lhs.lo(), ~lhs.hi()));
    CHECK(!lhs == lhs.is_zero());

    if (lhs < rhs) {
      CHECK((lhs <=> rhs) == std::strong_ordering::less);
      CHECK(lhs != rhs);
      CHECK(!(lhs > rhs));
    } else if (lhs > rhs) {
      CHECK((lhs <=> rhs) == std::strong_ordering::greater);
      CHECK(lhs != rhs);
      CHECK(!(lhs < rhs));
    } else {
      CHECK((lhs <=> rhs) == std::strong_ordering::equal);
      CHECK(lhs == rhs);
    }
  }

  // --- unsigned division/remainder normalization regressions --------------
  for (std::uint64_t high = 1; high <= 64; ++high) {
    for (std::uint64_t index = 0; index < 1024; ++index) {
      uint128_t const rhs = uint128_t::from_words(index * 0x9E37'79B9'7F4A'7C15ULL, high);
      uint128_t const lhs = cu_max - uint128_t::from_u64(index);
      uint128_t const quotient  = lhs / rhs;
      uint128_t const remainder = lhs % rhs;
      CHECK(remainder < rhs);
      CHECK(quotient * rhs + remainder == lhs);
    }
  }
  for (std::uint64_t high = 1; high <= 64; ++high) {
    for (std::uint64_t index = 0; index < 1024; ++index) {
      uint128_t const lhs = uint128_t::from_words(index * 0x9E37'79B9'7F4A'7C15ULL, high);
      uint128_t const rhs = uint128_t::from_words(index * 0xBF58'476D'1CE4'E5B9ULL, high);
      uint128_t const quotient  = lhs / rhs;
      uint128_t const remainder = lhs % rhs;
      CHECK(remainder < rhs);
      CHECK(quotient * rhs + remainder == lhs);
    }
  }

  // --- overflow-checked operators in release mode -------------------------
#if !STDEX_INT128_HAS_OVERFLOW_CHECK
  CHECK((cu_max + cu_one).is_zero());
  CHECK((uint128_t {} - cu_one) == cu_max);
  CHECK((cu_max * cu_max) == cu_one);
  CHECK((-cu_one) == cu_max);
  CHECK((cu_one << 128U) == cu_one);
  CHECK((cu_one << 129U) == cu_two);
  CHECK((cu_one >> 128U) == cu_one);
  CHECK((cu_two >> 129U) == cu_one);
  {
    uint128_t counter {};
    --counter;
    CHECK(counter == cu_max);
  }
#else
  CHECK((-uint128_t {}).is_zero());
#endif

  // --- overflow / saturation edge cases -----------------------------------
  {
    uint128_t const max = cu_max;
    CHECK(stdex::uint128_t::from_u64(2).saturating_mul(stdex::uint128_t::from_u64(3)) ==
          stdex::uint128_t::from_u64(6));
    CHECK(max.saturating_add(cu_one) == max);
    CHECK(uint128_t {}.saturating_sub(cu_one).is_zero());
    CHECK(max.saturating_mul(cu_two) == max);
    CHECK(max.wrapping_add(cu_one).is_zero());
    CHECK(uint128_t {}.wrapping_sub(cu_one) == max);
    CHECK(max.wrapping_mul(max) == cu_one);
    CHECK(cu_one.wrapping_neg() == max);
    CHECK(max.overflowing_add(cu_one) == (std::pair<uint128_t, bool> { uint128_t {}, true }));
    CHECK(max.overflowing_mul(max) == (std::pair<uint128_t, bool> { cu_one, true }));
    CHECK(cu_one.overflowing_neg() == (std::pair<uint128_t, bool> { max, true }));

    CHECK(!max.checked_add(cu_one).has_value());
    CHECK(!max.checked_mul(cu_two).has_value());
    CHECK(max.checked_div(uint128_t {}).has_value() == false);
    CHECK(!cu_one.checked_neg().has_value());
    CHECK(uint128_t {}.checked_neg().value().is_zero());
  }

  // --- shift edge cases ----------------------------------------------------
  {
    uint128_t const value = uint128_t::from_words(0x0123'4567'89AB'CDEFULL, 0xFEDC'BA98'7654'3210ULL);
    CHECK(value.checked_shl(0) == value);
    CHECK(value.checked_shl(63) == shl_ref(value, 63));
    CHECK(value.checked_shl(64) == shl_ref(value, 64));
    CHECK(value.checked_shl(127) == shl_ref(value, 127));
    CHECK(!value.checked_shl(128).has_value());
    CHECK(!value.checked_shl(255).has_value());
    CHECK(value.checked_shr(64) == shr_ref(value, 64));
    CHECK(value.checked_shr(127) == shr_ref(value, 127));
    CHECK(!value.checked_shr(128).has_value());

    CHECK(value.unchecked_shl(0) == value);
    CHECK(value.unchecked_shl(63) == shl_ref(value, 63));
    CHECK(value.unchecked_shl(64) == shl_ref(value, 64));
    CHECK(value.unchecked_shl(127) == shl_ref(value, 127));
    CHECK(value.unchecked_shr(63) == shr_ref(value, 63));
    CHECK(value.unchecked_shr(64) == shr_ref(value, 64));
    CHECK(value.unchecked_shr(127) == shr_ref(value, 127));
    CHECK((value << 64ULL) == shl_ref(value, 64));
    CHECK((value >> 64ULL) == shr_ref(value, 64));

    CHECK(value.overflowing_shl(128) == (std::pair<uint128_t, bool> { value, true }));
    CHECK(value.overflowing_shl(192) == (std::pair<uint128_t, bool> { shl_ref(value, 64), true }));
    CHECK(value.overflowing_shl(256) == (std::pair<uint128_t, bool> { value, true }));
    CHECK(value.overflowing_shr(129) == (std::pair<uint128_t, bool> { shr_ref(value, 1), true }));

    CHECK(value.wrapping_shl(128) == value);
    CHECK(value.wrapping_shl(192) == shl_ref(value, 64));
    CHECK(value.wrapping_shl(256) == value);
    CHECK(value.wrapping_shl(4097) == shl_ref(value, 1));
    CHECK(value.wrapping_shr(128) == value);
    CHECK(value.wrapping_shr(255) == shr_ref(value, 127));
    CHECK(value.wrapping_shr(4098) == shr_ref(value, 2));
  }

  // --- compound assignment / increment / decrement ------------------------
  {
    uint128_t value = uint128_t::from_u64(100);
    value += uint128_t::from_u64(20);
    CHECK(value == uint128_t::from_u64(120));
    value -= uint128_t::from_u64(20);
    CHECK(value == uint128_t::from_u64(100));
    value *= uint128_t::from_u64(3);
    CHECK(value == uint128_t::from_u64(300));
    value /= uint128_t::from_u64(7);
    CHECK(value == uint128_t::from_u64(42));
    value %= uint128_t::from_u64(5);
    CHECK(value == uint128_t::from_u64(2));
    value <<= 4U;
    CHECK(value == uint128_t::from_u64(32));
    value >>= 3U;
    CHECK(value == uint128_t::from_u64(4));
    value |= uint128_t::from_u64(3);
    CHECK(value == uint128_t::from_u64(7));
    value &= uint128_t::from_u64(6);
    CHECK(value == uint128_t::from_u64(6));
    value ^= uint128_t::from_u64(3);
    CHECK(value == uint128_t::from_u64(5));

    uint128_t counter {};
    CHECK(++counter == cu_one);
    CHECK(counter++ == cu_one);
    CHECK(counter == cu_two);
    CHECK(--counter == cu_one);
    CHECK(counter-- == cu_one);
    CHECK(counter.is_zero());
  }

  // --- mixed integral operands -------------------------------------------
  {
    uint128_t const value = uint128_t::from_u64(100);
    CHECK(value + 5 == uint128_t::from_u64(105));
    CHECK(5 + value == uint128_t::from_u64(105));
    CHECK(value - 5 == uint128_t::from_u64(95));
    CHECK(value * 3 == uint128_t::from_u64(300));
    CHECK(value / 7 == uint128_t::from_u64(14));
    CHECK(value % 7 == uint128_t::from_u64(2));
    CHECK(value == 100);
    CHECK(100 == value);
    CHECK(value != 99);
    CHECK(value < 101);
    CHECK(value >= 100U);

    CHECK(value.wrapping_add(-5) == uint128_t::from_u64(95));
    CHECK(value.checked_add(5) == uint128_t::from_u64(105));
    CHECK(!value.checked_add(-1).has_value());
  }

  // --- int128 arithmetic modes against portable references ----------------
  for (int iteration = 0; iteration < 50000; ++iteration) {
    int128_t const lhs = random_int128();
    int128_t const rhs = random_int128();

    uint128_t sum_bits {};
    bool const add_overflows = int_add_ref(lhs, rhs, sum_bits);
    int128_t const sum = int_from_bits(sum_bits);
    CHECK(lhs.wrapping_add(rhs) == sum);
    CHECK(lhs.overflowing_add(rhs) == (std::pair<int128_t, bool> { sum, add_overflows }));
    auto const checked_sum = lhs.checked_add(rhs);
    CHECK(checked_sum.has_value() == !add_overflows);
    if (checked_sum) {
      CHECK(*checked_sum == sum);
    }
    CHECK(lhs.saturating_add(rhs) ==
          (add_overflows ? (rhs.is_negative() ? ci_min : ci_max) : sum));
    if (!add_overflows) {
      CHECK((lhs + rhs) == sum);
    }

    uint128_t difference_bits {};
    bool const sub_overflows = int_sub_ref(lhs, rhs, difference_bits);
    int128_t const difference = int_from_bits(difference_bits);
    CHECK(lhs.wrapping_sub(rhs) == difference);
    CHECK(lhs.overflowing_sub(rhs) == (std::pair<int128_t, bool> { difference, sub_overflows }));
    auto const checked_difference = lhs.checked_sub(rhs);
    CHECK(checked_difference.has_value() == !sub_overflows);
    if (checked_difference) {
      CHECK(*checked_difference == difference);
    }
    CHECK(lhs.saturating_sub(rhs) ==
          (sub_overflows ? (rhs.is_negative() ? ci_max : ci_min) : difference));
    if (!sub_overflows) {
      CHECK((lhs - rhs) == difference);
    }

    uint128_t product_bits {};
    bool const product_overflows = int_mul_ref(lhs, rhs, product_bits);
    int128_t const product = int_from_bits(product_bits);
    bool const product_negative = lhs.is_negative() != rhs.is_negative();
    CHECK(lhs.wrapping_mul(rhs) == product);
    CHECK(lhs.overflowing_mul(rhs) == (std::pair<int128_t, bool> { product, product_overflows }));
    auto const checked_product = lhs.checked_mul(rhs);
    CHECK(checked_product.has_value() == !product_overflows);
    if (checked_product) {
      CHECK(*checked_product == product);
    }
    CHECK(lhs.saturating_mul(rhs) ==
          (product_overflows ? (product_negative ? ci_min : ci_max) : product));
    if (!product_overflows) {
      CHECK((lhs * rhs) == product);
    }

    uint128_t const negated_bits = int_negate_ref(int_bits(lhs));
    int128_t const negated      = int_from_bits(negated_bits);
    bool const negate_overflows = lhs == ci_min;
    CHECK(lhs.wrapping_neg() == negated);
    CHECK(lhs.overflowing_neg() == (std::pair<int128_t, bool> { negated, negate_overflows }));
    CHECK(lhs.checked_neg().has_value() == !negate_overflows);
    CHECK(lhs.saturating_neg() == (negate_overflows ? ci_max : negated));
    if (!negate_overflows) {
      CHECK((-lhs) == negated);
    }

    if (!rhs.is_zero()) {
      int128_t quotient {};
      int128_t remainder {};
      bool division_overflows = false;
      int_div_ref(lhs, rhs, quotient, remainder, division_overflows);
      CHECK(lhs.wrapping_div(rhs) == quotient);
      CHECK(lhs.wrapping_rem(rhs) == remainder);
      CHECK(lhs.overflowing_div(rhs) == (std::pair<int128_t, bool> { quotient, division_overflows }));
      CHECK(lhs.overflowing_rem(rhs) == (std::pair<int128_t, bool> { remainder, division_overflows }));
      CHECK(lhs.checked_div(rhs).has_value() == !division_overflows);
      CHECK(lhs.checked_rem(rhs).has_value() == !division_overflows);
      if (lhs.checked_div(rhs)) {
        CHECK(*lhs.checked_div(rhs) == quotient);
      }
      if (lhs.checked_rem(rhs)) {
        CHECK(*lhs.checked_rem(rhs) == remainder);
      }
      CHECK(lhs.checked_divmod(rhs).has_value() == !division_overflows);
      if (lhs.checked_divmod(rhs)) {
        CHECK(*lhs.checked_divmod(rhs) == (stdex::divmod_result { quotient, remainder }));
      }
      if (!division_overflows) {
        CHECK(lhs.unchecked_divmod(rhs) == (stdex::divmod_result { quotient, remainder }));
      }
      CHECK(lhs.saturating_div(rhs) == (division_overflows ? ci_max : quotient));
      if (!division_overflows) {
        CHECK((lhs / rhs) == quotient);
        CHECK((lhs % rhs) == remainder);
      }
    } else {
      CHECK(!lhs.checked_div(rhs).has_value());
      CHECK(!lhs.checked_rem(rhs).has_value());
      CHECK(!lhs.checked_divmod(rhs).has_value());
    }

    unsigned int const shift = static_cast<unsigned int>(next_rand() % 128U);
    int128_t const shifted_left = int_from_bits(shl_ref(int_bits(lhs), shift));
    int128_t const shifted_right = int_from_bits(int_asr_ref(lhs, shift));
    CHECK((lhs << shift) == shifted_left);
    CHECK((lhs >> shift) == shifted_right);
    CHECK(lhs.checked_shl(shift) == shifted_left);
    CHECK(lhs.checked_shr(shift) == shifted_right);
    CHECK(!lhs.checked_shl(shift + 128U).has_value());
    CHECK(!lhs.checked_shr(shift + 128U).has_value());
    CHECK(lhs.overflowing_shl(shift) == (std::pair<int128_t, bool> { shifted_left, false }));
    CHECK(lhs.overflowing_shr(shift) == (std::pair<int128_t, bool> { shifted_right, false }));
    CHECK(lhs.overflowing_shl(shift + 128U) == (std::pair<int128_t, bool> { shifted_left, true }));
    CHECK(lhs.overflowing_shr(shift + 128U) == (std::pair<int128_t, bool> { shifted_right, true }));
    CHECK(lhs.wrapping_shl(shift) == shifted_left);
    CHECK(lhs.wrapping_shr(shift) == shifted_right);
    CHECK(lhs.wrapping_shl(shift + 128U) == shifted_left);
    CHECK(lhs.wrapping_shr(shift + 128U) == shifted_right);

    uint128_t const lhs_bits = int_bits(lhs);
    uint128_t const rhs_bits = int_bits(rhs);
    CHECK((lhs & rhs) == int_from_bits(lhs_bits & rhs_bits));
    CHECK((lhs | rhs) == int_from_bits(lhs_bits | rhs_bits));
    CHECK((lhs ^ rhs) == int_from_bits(lhs_bits ^ rhs_bits));
    CHECK((~lhs) == int_from_bits(~lhs_bits));
    CHECK(!lhs == lhs.is_zero());

    if (lhs < rhs) {
      CHECK((lhs <=> rhs) == std::strong_ordering::less);
      CHECK(lhs != rhs);
      CHECK(!(lhs > rhs));
    } else if (lhs > rhs) {
      CHECK((lhs <=> rhs) == std::strong_ordering::greater);
      CHECK(lhs != rhs);
      CHECK(!(lhs < rhs));
    } else {
      CHECK((lhs <=> rhs) == std::strong_ordering::equal);
      CHECK(lhs == rhs);
    }
  }

  // --- int128 overflow / saturation edge cases ----------------------------
  {
    CHECK(ci_max.checked_add(ci_one) == std::nullopt);
    CHECK(ci_min.checked_sub(ci_one) == std::nullopt);
    CHECK(ci_max.checked_mul(ci_two) == std::nullopt);
    CHECK(ci_min.checked_neg() == std::nullopt);
    CHECK(ci_min.checked_div(ci_neg_one) == std::nullopt);
    CHECK(ci_min.checked_rem(ci_neg_one) == std::nullopt);

    CHECK(ci_max.saturating_add(ci_one) == ci_max);
    CHECK(ci_min.saturating_sub(ci_one) == ci_min);
    CHECK(ci_max.saturating_mul(ci_two) == ci_max);
    CHECK(ci_min.saturating_mul(ci_two) == ci_min);
    CHECK(ci_min.saturating_mul(ci_neg_one) == ci_max);
    CHECK(ci_min.saturating_neg() == ci_max);
    CHECK(ci_min.saturating_div(ci_neg_one) == ci_max);

    CHECK(ci_min.wrapping_div(ci_neg_one) == ci_min);
    CHECK(ci_min.wrapping_rem(ci_neg_one) == ci_zero);
    CHECK(ci_min.overflowing_div(ci_neg_one) == (std::pair<int128_t, bool> { ci_min, true }));
    CHECK(ci_min.overflowing_rem(ci_neg_one) == (std::pair<int128_t, bool> { ci_zero, true }));

    CHECK((ci_min / ci_two) == int128_t::from_words(0, 0xC000'0000'0000'0000ULL));
    CHECK((ci_min % ci_two) == ci_zero);
    CHECK((ci_min >> 64U) == int128_t::from_words(0x8000'0000'0000'0000ULL, ~0ULL));
    CHECK((ci_min >> 127U) == ci_neg_one);
    CHECK((ci_min >> 126U) == int128_t::from_i64(-2));
  }

  // --- int128 construction / conversions / bytes --------------------------
  {
    int128_t const value = int128_t::from_words(0x0123'4567'89AB'CDEFULL, 0xFEDC'BA98'7654'3210ULL);
    CHECK(value.lo() == 0x0123'4567'89AB'CDEFULL);
    CHECK(value.hi() == 0xFEDC'BA98'7654'3210ULL);
    CHECK(value.is_negative());
    CHECK(static_cast<std::int64_t>(value) == static_cast<std::int64_t>(0x0123'4567'89AB'CDEFULL));
    CHECK(static_cast<std::uint32_t>(value) == 0x89AB'CDEFU);
    CHECK(int128_t::from_i64(-1) == ci_neg_one);
    CHECK(int128_t::from_u64(0xFFFF'FFFF'FFFF'FFFFULL) == int128_t::from_words(~0ULL, 0));
    CHECK(int128_t::from_integral(std::int8_t { -1 }) == ci_neg_one);
    CHECK(int128_t::from_integral(std::uint8_t { 0xFF }) == int128_t::from_u64(0xFF));
    CHECK(static_cast<std::int8_t>(int128_t::from_i64(0x1FF)) == -1);

    std::uint8_t le[16] {};
    int128_t::from_i64(-2).to_le_bytes(le);
    CHECK(le[0] == 0xFEU && le[1] == 0xFFU && le[15] == 0xFFU);
    CHECK(int128_t::from_le_bytes(le) == int128_t::from_i64(-2));

    std::uint8_t be[16] {};
    int128_t::from_i64(-2).to_be_bytes(be);
    CHECK(be[0] == 0xFFU && be[14] == 0xFFU && be[15] == 0xFEU);
    CHECK(int128_t::from_be_bytes(be) == int128_t::from_i64(-2));

    CHECK(static_cast<int128_t>(cu_max) == ci_neg_one);
    CHECK(static_cast<uint128_t>(ci_neg_one) == cu_max);

  }

  // --- mixed signed / unsigned operands -----------------------------------
  {
    CHECK(ci_one + cu_two == cu_three);
    CHECK(cu_two + ci_one == cu_three);
    CHECK(cu_three - ci_one == cu_two);
    CHECK(ci_three - cu_one == cu_two);
    CHECK(ci_two * cu_three == uint128_t::from_u64(6));
    CHECK(ci_three / cu_two == cu_one);
    CHECK(cu_three / ci_two == cu_one);
    CHECK(ci_three % cu_two == cu_one);
    CHECK(cu_three % ci_two == cu_one);

    int128_t const negative = int128_t::from_i64(-8);
    CHECK((negative & cu_max) == uint128_t::from_words(0xFFFF'FFFF'FFFF'FFF8ULL, ~0ULL));
    CHECK((negative | uint128_t::from_u64(7)) == cu_max);
    CHECK((negative ^ uint128_t::from_u64(7)) == cu_max);

    CHECK(ci_neg_one == cu_max);
    CHECK(cu_max == ci_neg_one);
    CHECK(ci_neg_one != cu_one);
    CHECK(!(ci_neg_one < cu_one));
    CHECK(cu_one < ci_neg_one);
    CHECK((ci_neg_one <=> cu_one) == std::strong_ordering::greater);
    CHECK((cu_one <=> ci_neg_one) == std::strong_ordering::less);
  }

  // --- <numeric> and <cstdlib> algorithms ----------------------------------
  // Parked (step 3): stdex::div is not implemented yet.
  {
    CHECK(stdex::gcd(uint128_t {}, cu_max) == cu_max);
    CHECK(stdex::gcd(ci_zero, int128_t::from_i64(-12)) == int128_t::from_u64(12));
    CHECK(stdex::lcm(uint128_t {}, cu_max).is_zero());
    CHECK(stdex::lcm(ci_zero, ci_max).is_zero());
    CHECK(stdex::abs(int128_t::from_i64(-7)) == int128_t::from_i64(7));
    CHECK(stdex::abs(-2.5) == 2.5);
    CHECK(stdex::abs(-2.5F) == 2.5F);
    CHECK(stdex::pow(2.0, 10.0) == 1024.0);
    CHECK(stdex::pow(2.0F, 3.0F) == 8.0F);
    CHECK(stdex::pow(2, 10) == 1024.0);

    for (std::int64_t base = -5; base <= 5; ++base) {
      for (unsigned exponent = 0; exponent <= 128; ++exponent) {
        int128_t expected = int128_t::from_u64(1);
        bool overflow = false;
        for (unsigned power = 0; power < exponent; ++power) {
          auto const product = expected.overflowing_mul(int128_t::from_i64(base));
          if (product.second) {
            overflow = true;
            break;
          }
          expected = product.first;
        }
        CHECK(stdex::pow(int128_t::from_i64(base), exponent) == (overflow ? int128_t {} : expected));
      }
    }

    for (std::uint64_t base = 0; base <= 5; ++base) {
      for (unsigned exponent = 0; exponent <= 128; ++exponent) {
        uint128_t expected = uint128_t::from_u64(1);
        bool overflow = false;
        for (unsigned power = 0; power < exponent; ++power) {
          auto const product = expected.overflowing_mul(uint128_t::from_u64(base));
          if (product.second) {
            overflow = true;
            break;
          }
          expected = product.first;
        }
        CHECK(stdex::pow(uint128_t::from_u64(base), exponent) == (overflow ? uint128_t {} : expected));
      }
    }

    for (int iteration = 0; iteration < 20000; ++iteration) {
      std::uint64_t const a = next_rand();
      std::uint64_t const b = next_rand() | 1U;
      uint128_t const lhs   = uint128_t::from_u64(a);
      uint128_t const rhs   = uint128_t::from_u64(b);

      // stdex::uint128_div_t const qr = stdex::div(lhs, rhs);
      // CHECK(qr.quot == uint128_t::from_u64(a / b));
      // CHECK(qr.rem == uint128_t::from_u64(a % b));
      CHECK(stdex::gcd(lhs, rhs) == uint128_t::from_u64(std::gcd(a, b)));
      CHECK(stdex::lcm(lhs, rhs) ==
            uint128_t::from_u64(a / std::gcd(a, b)) * uint128_t::from_u64(b));
      CHECK(stdex::midpoint(lhs, rhs) == uint128_t::from_u64(std::midpoint(a, b)));
      CHECK(stdex::midpoint(rhs, lhs) == uint128_t::from_u64(std::midpoint(b, a)));
    }

    for (int iteration = 0; iteration < 20000; ++iteration) {
      std::int64_t const a =
          static_cast<std::int64_t>(next_rand() % 2000001U) - 1000000;
      std::int64_t const b =
          static_cast<std::int64_t>((next_rand() % 1999999U) | 1U) - 1000000;
      int128_t const lhs = int128_t::from_i64(a);
      int128_t const rhs = int128_t::from_i64(b);

      // stdex::int128_div_t const qr = stdex::div(lhs, rhs);
      // CHECK(qr.quot == int128_t::from_i64(a / b));
      // CHECK(qr.rem == int128_t::from_i64(a % b));
      CHECK(stdex::abs(lhs) == int128_t::from_i64(a < 0 ? -a : a));
      CHECK(stdex::gcd(lhs, rhs) == int128_t::from_i64(std::gcd(a, b)));
      CHECK(stdex::lcm(lhs, rhs) == int128_t::from_i64(std::lcm(a, b)));
      CHECK(stdex::midpoint(lhs, rhs) == int128_t::from_i64(std::midpoint(a, b)));
      CHECK(stdex::midpoint(rhs, lhs) == int128_t::from_i64(std::midpoint(b, a)));
    }
  }

  // --- integer formatter ---------------------------------------------------
  {
    uint128_t const f42 = uint128_t::from_u64(42);
    uint128_t const f65 = uint128_t::from_u64(65);
    int128_t const fn42 = int128_t::from_i64(-42);

    CHECK(std::format("{}", uint128_t {}) == "0");
    CHECK(std::format("{:d}", f42) == "42");
    CHECK(std::format("{:d}", fn42) == "-42");
    CHECK(std::format("{:+d}", int128_t::from_i64(42)) == "+42");
    CHECK(std::format("{: d}", int128_t::from_i64(42)) == " 42");
    CHECK(std::format("{:+d}", fn42) == "-42");
    CHECK(std::format("{:+}", f42) == "+42");
    CHECK(std::format("{: }", f42) == " 42");

    CHECK(std::format("{:b}", f42) == "101010");
    CHECK(std::format("{:B}", f42) == "101010");
    CHECK(std::format("{:#b}", f42) == "0b101010");
    CHECK(std::format("{:#B}", f42) == "0B101010");
    CHECK(std::format("{:o}", f42) == "52");
    CHECK(std::format("{:#o}", f42) == "052");
    CHECK(std::format("{:#o}", uint128_t {}) == "0");
    CHECK(std::format("{:x}", f42) == "2a");
    CHECK(std::format("{:#x}", f42) == "0x2a");
    CHECK(std::format("{:X}", f42) == "2A");
    CHECK(std::format("{:#X}", f42) == "0X2A");
    CHECK(std::format("{:#x}", uint128_t {}) == "0x0");
    CHECK(std::format("{:#b}", uint128_t {}) == "0b0");
    CHECK(std::format("{:#d}", f42) == "42");

    CHECK(std::format("{:x}", int128_t::from_i64(-1)) == "-1");
    CHECK(std::format("{:#x}", int128_t::from_i64(-1)) == "-0x1");
    CHECK(std::format("{:o}", fn42) == "-52");
    CHECK(std::format("{:b}", fn42) == "-101010");
    CHECK(std::format("{:#b}", fn42) == "-0b101010");
    CHECK(std::format("{:#X}", int128_t::from_i64(-255)) == "-0XFF");

    CHECK(std::format("{:5}", f42) == "   42");
    CHECK(std::format("{:<5}", f42) == "42   ");
    CHECK(std::format("{:^5}", f42) == " 42  ");
    CHECK(std::format("{:*^10}", f42) == "****42****");
    CHECK(std::format("{:*^+10}", f42) == "***+42****");
    CHECK(std::format("{:*<5}", fn42) == "-42**");
    CHECK(std::format("{:*>5}", fn42) == "**-42");
    CHECK(std::format("{:05}", f42) == "00042");
    CHECK(std::format("{:05}", fn42) == "-0042");
    CHECK(std::format("{:+05}", f42) == "+0042");
    CHECK(std::format("{: 05}", f42) == " 0042");
    CHECK(std::format("{:>05}", f42) == "   42");
    CHECK(std::format("{:^05}", f42) == " 42  ");
    CHECK(std::format("{:0>5}", f42) == "00042");
    CHECK(std::format("{:#08x}", f42) == "0x00002a");
    CHECK(std::format("{:#010x}", f42) == "0x0000002a");
    CHECK(std::format("{:#010b}", uint128_t::from_u64(5)) == "0b00000101");
    CHECK(std::format("{:#010o}", uint128_t::from_u64(8)) == "0000000010");
    CHECK(std::format("{:#010X}", uint128_t::from_u64(0xAB)) == "0X000000AB");
    CHECK(std::format("{:08x}", fn42) == "-000002a");
    CHECK(std::format("{:08o}", fn42) == "-0000052");
    CHECK(std::format("{:08b}", fn42) == "-0101010");

    std::locale const comma_locale(
        std::locale::classic(), new test_numpunct<char>(std::string("\3"), ','));
    std::locale const indian_locale(
        std::locale::classic(), new test_numpunct<char>(std::string("\3\2"), ','));
    std::locale const unlimited_locale(
        std::locale::classic(), new test_numpunct<char>(std::string("\3\0", 2), ','));
    std::locale const wide_locale(
        std::locale::classic(), new test_numpunct<wchar_t>(std::string("\3"), L','));

    uint128_t const f1234567 = uint128_t::from_u64(1'234'567);
    CHECK(std::format(comma_locale, "{}", f1234567) == "1234567");
    CHECK(std::format(std::locale::classic(), "{:L}", f1234567) == "1234567");
    CHECK(std::format(comma_locale, "{:L}", f1234567) == "1,234,567");
    CHECK(std::format(comma_locale, "{:Ld}", f1234567) == "1,234,567");
    CHECK(std::format(comma_locale, "{:Lx}", f1234567) == "12d,687");
    CHECK(std::format(comma_locale, "{:LX}", f1234567) == "12D,687");
    CHECK(std::format(comma_locale, "{:Lb}", f1234567) ==
          "100,101,101,011,010,000,111");
    CHECK(std::format(comma_locale, "{:Lo}", f1234567) == "4,553,207");
    CHECK(std::format(comma_locale, "{:#Lx}", f1234567) == "0x12d,687");
    CHECK(std::format(comma_locale, "{:#Lb}", f1234567) ==
          "0b100,101,101,011,010,000,111");
    CHECK(std::format(comma_locale, "{:#Lo}", f1234567) == "04,553,207");
    CHECK(std::format(comma_locale, "{:#LX}", f1234567) == "0X12D,687");
    CHECK(std::format(comma_locale, "{:+L}", f1234567) == "+1,234,567");
    CHECK(std::format(comma_locale, "{: L}", f1234567) == " 1,234,567");
    CHECK(std::format(comma_locale, "{:015L}", f1234567) == "0000001,234,567");
    CHECK(std::format(comma_locale, "{:15L}", f1234567) == "      1,234,567");
    CHECK(std::format(comma_locale, "{:*^15L}", f1234567) == "***1,234,567***");
    CHECK(std::format(comma_locale, "{:L}", int128_t::from_i64(-1'234'567)) ==
          "-1,234,567");
    CHECK(std::format(comma_locale, "{:Lx}", int128_t::from_i64(-1'234'567)) ==
          "-12d,687");
    CHECK(std::format(comma_locale, "{:Lc}", f65) == "A");
    CHECK(std::format(indian_locale, "{:L}", uint128_t::from_u64(123'456'789)) ==
          "12,34,56,789");
    CHECK(std::format(indian_locale, "{:L}", f1234567) == "12,34,567");
    CHECK(std::format(unlimited_locale, "{:L}", f1234567) == "1234,567");
    CHECK(std::format(unlimited_locale, "{:L}", uint128_t::from_u64(123'456'789)) ==
          "123456,789");
    CHECK(std::format(wide_locale, L"{:L}", f1234567) == L"1,234,567");

    CHECK(std::format(L"{:d}", f42) == L"42");
    CHECK(std::format(L"{:#010x}", f42) == L"0x0000002a");
    CHECK(std::format(L"{:*^10}", fn42) == L"***-42****");
    CHECK(std::format(L"{:c}", f65) == L"A");
    CHECK(std::format(L"{:5c}", f65) == L"A    ");

    CHECK(std::format("{:{}}", f42, 10) == "        42");
    CHECK(std::format("{:*^{}}", f42, 10) == "****42****");
    CHECK(std::format("{0:{1}}", fn42, 10) == "       -42");
    CHECK(std::format("{:{}}", f42, 0) == "42");
    CHECK(std::format("{:{}}", f42, 5U) == "   42");
    CHECK(std::format("{:{}}", f42, 5L) == "   42");
    CHECK(std::format("{:{}}", f42, 5LL) == "   42");
    CHECK(std::format("{:{}}", f42, std::uint64_t { 5 }) == "   42");
    CHECK(std::format("{:{}}", f42, 5ULL) == "   42");
    CHECK(std::format("{:{}c}", f65, 5) == "A    ");

    uint128_t const big =
        uint128_t::from_words(0x0123'4567'89AB'CDEFULL, 0xFEDC'BA98'7654'3210ULL);
    CHECK(std::format("{}", big) == "338770000845734292516042252062085074415");
    CHECK(std::format("{:x}", big) == "fedcba98765432100123456789abcdef");
    CHECK(std::format("{:X}", big) == "FEDCBA98765432100123456789ABCDEF");
    CHECK(std::format("{:o}", big) ==
          "3766713523035452062040004432126361152746757");
    CHECK(std::format("{:b}", big) ==
          "11111110110111001011101010011000011101100101010000110010000100000000000100100011010001010110011110001001101010111100110111101111");
    CHECK(std::format("{}", cu_max) == "340282366920938463463374607431768211455");
    CHECK(std::format("{:x}", cu_max) == std::string(32, 'f'));
    CHECK(std::format("{:X}", cu_max) == std::string(32, 'F'));
    CHECK(std::format("{:o}", cu_max) == "3" + std::string(42, '7'));
    CHECK(std::format("{:b}", cu_max) == std::string(128, '1'));
    CHECK(std::format("{}", ci_max) == "170141183460469231731687303715884105727");
    CHECK(std::format("{}", ci_min) == "-170141183460469231731687303715884105728");
    CHECK(std::format("{:x}", ci_max) == "7" + std::string(31, 'f'));
    CHECK(std::format("{:x}", ci_min) == "-8" + std::string(31, '0'));
    CHECK(std::format("{:o}", ci_max) == "1" + std::string(42, '7'));
    CHECK(std::format("{:o}", ci_min) == "-2" + std::string(42, '0'));
    CHECK(std::format("{:b}", ci_max) == std::string(127, '1'));
    CHECK(std::format("{:b}", ci_min) == "-1" + std::string(127, '0'));

    CHECK(std::format("{:c}", f65) == "A");
    CHECK(std::format("{:5c}", f65) == "A    ");
    CHECK(std::format("{:>5c}", f65) == "    A");
    CHECK(std::format("{:^5c}", f65) == "  A  ");
    CHECK(std::format("{:*^5c}", f65) == "**A**");
    if constexpr (std::is_signed_v<char>) {
      CHECK(std::format("{:c}", int128_t::from_i64(-1)) ==
            std::string(1, static_cast<char>(-1)));
      CHECK(std::format("{:c}", int128_t::from_i64(-128)) ==
            std::string(1, static_cast<char>(-128)));
      CHECK(format_throws("{:c}", int128_t::from_i64(-129)));
    } else {
      CHECK(format_throws("{:c}", int128_t::from_i64(-1)));
    }
    CHECK(std::format("{:c}", uint128_t::from_u64(
                                 static_cast<std::uint64_t>(std::numeric_limits<char>::max()))) ==
          std::string(1, static_cast<char>(std::numeric_limits<char>::max())));
    CHECK(format_throws("{:c}", uint128_t::from_u64(
                                   static_cast<std::uint64_t>(std::numeric_limits<char>::max()) + 1ULL)));
    CHECK(format_throws("{:c}", int128_t::from_i64(128)));
    CHECK(format_throws("{:c}", uint128_t::from_words(0, 1)));

    uint128_t const wide_too_large = uint128_t::from_u64(
        static_cast<std::uint64_t>(std::numeric_limits<wchar_t>::max()) + 1ULL);
    CHECK(format_throws(L"{:c}", wide_too_large));

    int negative_width = -1;
    bool bool_width = true;
    char char_width = '5';
    double double_width = 5.0;
    std::uint64_t huge_width = 1ULL << 63U;
    CHECK(format_throws("{:{}}", f42, negative_width));
    CHECK(format_throws("{:{}}", f42, bool_width));
    CHECK(format_throws("{:{}}", f42, char_width));
    CHECK(format_throws("{:{}}", f42, double_width));
    CHECK(format_throws("{:{}}", f42, huge_width));
    CHECK(format_throws("{:{}c}", f65, negative_width));
    CHECK(format_throws("{:99999999999999999999}", f42));
    CHECK(format_throws("{:n}", f42));
    CHECK(format_throws("{:.2}", f42));
    CHECK(format_throws("{:s}", f42));
    CHECK(format_throws("{:+c}", f65));
    CHECK(format_throws("{:#c}", f65));
    CHECK(format_throws("{:0c}", f65));
    CHECK(format_throws("{:L-c}", f65));
    CHECK(format_throws("{:L c}", f65));
    CHECK(format_throws("{:L#c}", f65));
    CHECK(format_throws("{:L0c}", f65));
    CHECK(format_throws("{:cL}", f65));
  }

  // --- int128 compound assignment / increment / decrement -----------------
  {
    int128_t value = int128_t::from_i64(-100);
    value += int128_t::from_i64(20);
    CHECK(value == int128_t::from_i64(-80));
    value -= int128_t::from_i64(20);
    CHECK(value == int128_t::from_i64(-100));
    value *= int128_t::from_i64(3);
    CHECK(value == int128_t::from_i64(-300));
    value /= int128_t::from_i64(7);
    CHECK(value == int128_t::from_i64(-42));
    value %= int128_t::from_i64(5);
    CHECK(value == int128_t::from_i64(-2));
    value <<= 4U;
    CHECK(value == int128_t::from_i64(-32));
    value >>= 3U;
    CHECK(value == int128_t::from_i64(-4));
    value |= int128_t::from_i64(3);
    CHECK(value == int128_t::from_i64(-1));
    value &= int128_t::from_i64(-2);
    CHECK(value == int128_t::from_i64(-2));
    value ^= int128_t::from_i64(1);
    CHECK(value == int128_t::from_i64(-1));

    int128_t counter {};
    CHECK(++counter == ci_one);
    CHECK(counter++ == ci_one);
    CHECK(counter == ci_two);
    CHECK(--counter == ci_one);
    CHECK(counter-- == ci_one);
    CHECK(counter.is_zero());
  }

#if defined(__SIZEOF_INT128__)
  // Differential fuzz against the compiler's native unsigned 128-bit
  // arithmetic where it exists.
  for (int iteration = 0; iteration < 100000; ++iteration) {
    uint128_t const lhs  = random_uint128();
    uint128_t const rhs  = random_uint128();
    __uint128_t const nlhs = native_from(lhs);
    __uint128_t const nrhs = native_from(rhs);

    CHECK(native_from(lhs.wrapping_add(rhs)) == nlhs + nrhs);
    CHECK(native_from(lhs.wrapping_sub(rhs)) == nlhs - nrhs);
    CHECK(native_from(lhs.wrapping_mul(rhs)) == nlhs * nrhs);
    CHECK(native_from(lhs.wrapping_neg()) == -nlhs);
    CHECK(native_from(lhs & rhs) == (nlhs & nrhs));
    CHECK(native_from(lhs | rhs) == (nlhs | nrhs));
    CHECK(native_from(lhs ^ rhs) == (nlhs ^ nrhs));
    CHECK(native_from(~lhs) == ~nlhs);

    unsigned int const shift = static_cast<unsigned int>(next_rand() % 128U);
    CHECK(native_from(lhs << shift) == (nlhs << shift));
    CHECK(native_from(lhs >> shift) == (nlhs >> shift));
    CHECK(((lhs <=> rhs) < 0) == (nlhs < nrhs));

    if (!rhs.is_zero()) {
      CHECK(native_from(lhs / rhs) == nlhs / nrhs);
      CHECK(native_from(lhs % rhs) == nlhs % nrhs);
    }
  }

  // Differential fuzz against the compiler's native signed 128-bit
  // arithmetic where it exists. Wrapping results are compared through the
  // unsigned bit pattern to avoid signed-overflow UB in the reference.
  for (int iteration = 0; iteration < 100000; ++iteration) {
    int128_t const lhs = random_int128();
    int128_t const rhs = random_int128();
    __uint128_t const nlhs = native_from(int_bits(lhs));
    __uint128_t const nrhs = native_from(int_bits(rhs));

    CHECK(native_from(lhs.wrapping_add(rhs)) == nlhs + nrhs);
    CHECK(native_from(lhs.wrapping_sub(rhs)) == nlhs - nrhs);
    CHECK(native_from(lhs.wrapping_mul(rhs)) == nlhs * nrhs);
    CHECK(native_from(lhs.wrapping_neg()) == -nlhs);
    CHECK(native_from(lhs & rhs) == (nlhs & nrhs));
    CHECK(native_from(lhs | rhs) == (nlhs | nrhs));
    CHECK(native_from(lhs ^ rhs) == (nlhs ^ nrhs));
    CHECK(native_from(~lhs) == ~nlhs);

    unsigned int const shift = static_cast<unsigned int>(next_rand() % 128U);
    CHECK(native_from(lhs << shift) == (nlhs << shift));
    CHECK(native_from(lhs >> shift) ==
          static_cast<__uint128_t>(native_signed_from(lhs) >> shift));
    CHECK(((lhs <=> rhs) < 0) == (native_signed_from(lhs) < native_signed_from(rhs)));

    if (!rhs.is_zero()) {
      bool const division_overflows = lhs == ci_min && rhs == ci_neg_one;
      if (!division_overflows) {
        CHECK(lhs.wrapping_div(rhs) ==
              from_native_signed(native_signed_from(lhs) / native_signed_from(rhs)));
        CHECK(lhs.wrapping_rem(rhs) ==
              from_native_signed(native_signed_from(lhs) % native_signed_from(rhs)));
      }
    }
  }
#endif

  std::printf("\n===============================\n");
  std::printf("PASS: %d   FAIL: %d\n", g_pass, g_fail);
  std::printf("===============================\n");
  return g_fail == 0 ? 0 : 1;
}
