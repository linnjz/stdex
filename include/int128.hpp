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

#pragma once

#include <cstdint> // uint64_t
#include <compare> // std::strong_ordering

#if defined(_MSC_VER)
#  include <intrin.h>
#  if defined(_M_X64) && !defined(_M_ARM64EC)
#    pragma intrinsic(_umul128)
#  endif
#  if defined(_M_ARM64) || defined(_M_ARM64EC)
#    pragma intrinsic(__umulh)
#  endif
#endif

#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wattributes"
#elif defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wattributes"
#elif defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable : 5030)
#endif

namespace stdex
{
static_assert(sizeof(uint64_t) == 8);

struct uint128_t
{
  uint64_t low;
  uint64_t high;

  friend constexpr bool operator== (uint128_t, uint128_t) noexcept = default;

  friend constexpr std::strong_ordering operator<=> (uint128_t lhs, uint128_t rhs) noexcept
  {
    if (lhs.high != rhs.high) return lhs.high <=> rhs.high;
    return lhs.low <=> rhs.low;
  }
};

[[nodiscard, msvc::forceinline, gnu::always_inline]]
constexpr uint128_t umul128_portable(uint64_t const lhs, uint64_t const rhs) noexcept
{
  // Split into 32-bit half-words: x = x_low + x_high * 2^32
  uint64_t const lhs_low  = static_cast<uint32_t>(lhs);
  uint64_t const lhs_high = lhs >> 32U;
  uint64_t const rhs_low  = static_cast<uint32_t>(rhs);
  uint64_t const rhs_high = rhs >> 32U;

  // Four 32x32 -> 64 partial products
  uint64_t const low_product  = lhs_low * rhs_low;   // Contributes to bits [0, 64)
  uint64_t const cross1       = lhs_low * rhs_high;  // Contributes to bits [32, 96)
  uint64_t const cross2       = lhs_high * rhs_low;  // Contributes to bits [32, 96)
  uint64_t const high_product = lhs_high * rhs_high; // Contributes to bits [64, 128)

  // Start the high 64 bits from high_product, then accumulate the high 32 bits of the cross terms.
  uint64_t high = high_product;
  high += cross1 >> 32U;
  high += cross2 >> 32U;

  // Start the low 64 bits from low_product, and add in the low 32 bits of the cross terms shifted left by 32.
  // The middle carry is carried naturally by a 64-bit temporary:
  // the part of middle above 2^32 is the carry.
  uint64_t const middle = (low_product >> 32U) + static_cast<uint32_t>(cross1) + static_cast<uint32_t>(cross2);

  uint64_t const low = (middle << 32U) + static_cast<uint32_t>(low_product);
  high += middle >> 32U;

  return { low, high };
}

[[nodiscard, msvc::forceinline, gnu::always_inline]] 
constexpr uint64_t umulh_portable(uint64_t const lhs, uint64_t const rhs) noexcept
{
  return umul128_portable(lhs, rhs).high;
}

[[nodiscard, msvc::forceinline, gnu::always_inline]] 
constexpr uint128_t umul128(uint64_t const lhs, uint64_t const rhs) noexcept
{
#if defined(_MSC_VER) && !defined(__clang__) && (defined(_M_X64) || defined(_M_ARM64) || defined(_M_ARM64EC))
  if consteval {
    return umul128_portable(lhs, rhs);
  } else {
#  if defined(_M_X64) && !defined(_M_ARM64EC)
    uint64_t high {};
    uint64_t const low = _umul128(lhs, rhs, &high);
    return { low, high };
#  else
    return { lhs * rhs, __umulh(lhs, rhs) };
#  endif
  }
#elif defined(__SIZEOF_INT128__)
  auto const product = static_cast<__uint128_t>(lhs) * static_cast<__uint128_t>(rhs);
  return { static_cast<uint64_t>(product), static_cast<uint64_t>(product >> 64U) };
#else
  return umul128_portable(lhs, rhs);
#endif
}

[[nodiscard, msvc::forceinline, gnu::always_inline]] 
constexpr uint64_t umulh(uint64_t const lhs, uint64_t const rhs) noexcept
{
#if defined(_MSC_VER) && !defined(__clang__) && (defined(_M_X64) || defined(_M_ARM64) || defined(_M_ARM64EC))
  if consteval {
    return umulh_portable(lhs, rhs);
  } else {
#  if defined(_M_X64) && !defined(_M_ARM64EC)
    uint64_t high {};
    (void) _umul128(lhs, rhs, &high);
    return high;
#  else
    return __umulh(lhs, rhs);
#  endif
  }
#elif defined(__SIZEOF_INT128__)
  return static_cast<uint64_t>((static_cast<__uint128_t>(lhs) * static_cast<__uint128_t>(rhs)) >> 64U);
#else
  return umulh_portable(lhs, rhs);
#endif
}

[[nodiscard, msvc::forceinline, gnu::always_inline]]
constexpr uint128_t add128_portable(uint128_t const value, uint64_t const addend) noexcept
{
  uint64_t const sum_lo = value.low + addend;
  uint64_t const carry  = (sum_lo < value.low) ? 1U : 0U;
  return { sum_lo, value.high + carry };
}

[[nodiscard, msvc::forceinline, gnu::always_inline]] 
constexpr uint128_t add128(uint128_t const value, uint64_t const addend) noexcept
{
#if defined(_MSC_VER) && !defined(__clang__) && defined(_M_X64) && !defined(_M_ARM64EC)
  if consteval {
    return add128_portable(value, addend);
  } else {
    uint128_t sum {};
    unsigned char const carry = _addcarry_u64(0, value.low, addend, &sum.low);
    _addcarry_u64(carry, value.high, 0, &sum.high);
    return sum;
  }
#else
  return add128_portable(value, addend);
#endif
}

/// \pre divisor != 0
[[nodiscard, msvc::forceinline, gnu::always_inline]]
constexpr uint128_t div128_portable(uint128_t const dividend, uint64_t const divisor) noexcept
{
  // Binary long division: quotient_high is exact, quotient_low is built bit by bit.
  // The shifted remainder is 65 bits when divisor > 2^63, so the bit shifted out
  // is kept separately instead of being dropped.
  uint64_t const quotient_hi = dividend.high / divisor;
  uint64_t rem               = dividend.high % divisor;
  uint64_t lo                = dividend.low;
  uint64_t quotient_lo {};

  for (int i = 63; i >= 0; --i) {
    uint64_t const overflow = rem >> 63U;
    rem                     = (rem << 1U) | (lo >> 63U);
    lo <<= 1U;
    if (overflow != 0 || rem >= divisor) {
      rem -= divisor;
      quotient_lo |= 1ULL << i;
    }
  }
  return { quotient_lo, quotient_hi };
}

/// \pre divisor != 0
[[nodiscard, msvc::forceinline, gnu::always_inline]] 
constexpr uint128_t div128(uint128_t const dividend, uint64_t const divisor) noexcept
{
#if defined(_MSC_VER) && !defined(__clang__) && defined(_M_X64) && !defined(_M_ARM64EC)
  if consteval {
    return div128_portable(dividend, divisor);
  } else {
    // remainder_high < divisor by construction, so the 64-bit quotient cannot overflow.
    uint128_t quotient {};
    quotient.high         = dividend.high / divisor;
    uint64_t const rem_hi = dividend.high % divisor;
    uint64_t remainder {};
    quotient.low = _udiv128(rem_hi, dividend.low, divisor, &remainder);
    return quotient;
  }
#elif defined(__SIZEOF_INT128__)
  if consteval {
    return div128_portable(dividend, divisor);
  } else {
    auto const wide = (static_cast<__uint128_t>(dividend.high) << 64U) | dividend.low;
    auto const quot = wide / divisor;
    return { static_cast<uint64_t>(quot), static_cast<uint64_t>(quot >> 64U) };
  }
#else
  return div128_portable(dividend, divisor);
#endif
}

[[nodiscard, msvc::forceinline, gnu::always_inline]]
constexpr uint128_t mul128_u64_lo(uint128_t const value, uint64_t const multiplier) noexcept
{
  // Low 128 bits of the 192-bit product: high half overflow beyond bit 128 is discarded.
  uint64_t const product_lo = value.low * multiplier;
  uint64_t const product_hi = umulh(value.low, multiplier) + value.high * multiplier;
  return { product_lo, product_hi };
}

[[nodiscard, msvc::forceinline, gnu::always_inline]]
constexpr uint64_t mul128_u64_hi(uint128_t const value, uint64_t const multiplier) noexcept
{
  // High 64 bits of the 192-bit product, via split halves rejoined with add128.
  uint64_t const bottom_hi = umulh(value.low, multiplier);
  uint64_t const top_hi    = umulh(value.high, multiplier);
  uint64_t const top_lo    = value.high * multiplier;
  return add128({ top_lo, top_hi }, bottom_hi).high;
}

[[nodiscard, msvc::forceinline, gnu::always_inline]] 
constexpr uint64_t mix(uint64_t const lhs, uint64_t const rhs) noexcept
{
  auto const [low, high] = umul128(lhs, rhs);
  return low ^ high;
}

[[msvc::forceinline, gnu::always_inline]] 
constexpr void mum(uint64_t &lhs, uint64_t &rhs) noexcept
{
  auto const product = umul128(lhs, rhs);
  lhs                = product.low;
  rhs                = product.high;
}
} // namespace stdex

#if defined(__clang__)
#  pragma clang diagnostic pop
#elif defined(__GNUC__)
#  pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#  pragma warning(pop)
#endif
