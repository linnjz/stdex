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

#include <concepts> // std::same_as
#include <cstdint>  // uint64_t, uint32_t, int32_t
#include <limits>   // std::numeric_limits

#include "int128.hpp" // stdex::uint128_t, stdex::umulh, stdex::add128, stdex::div128, 
                      // stdex::mul128_u64_lo, stdex::mul128_u64_hi

#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wattributes"
#elif defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wattributes"
#endif

namespace stdex
{
/// \pre d != 0
[[nodiscard, msvc::forceinline, gnu::always_inline]] constexpr uint64_t computeM_u32(uint32_t const d) noexcept
{
  return 0xFFFF'FFFF'FFFF'FFFFULL / d + 1;
}

/// \pre d != 0, M == computeM_u32(d)
[[nodiscard, msvc::forceinline, gnu::always_inline]] constexpr uint32_t fastmod_u32(uint32_t const a, uint64_t const m, uint32_t const d) noexcept
{
  uint64_t const lowbits = m * a;
  return static_cast<uint32_t>(umulh(lowbits, d));
}

/// \pre d > 1, M == computeM_u32(d)
[[nodiscard, msvc::forceinline, gnu::always_inline]] constexpr uint32_t fastdiv_u32(uint32_t const a, uint64_t const m) noexcept
{
  return static_cast<uint32_t>(umulh(m, a));
}

/// \pre M == computeM_u32(d) for some d != 0
[[nodiscard, msvc::forceinline, gnu::always_inline]] constexpr bool is_divisible(uint32_t const n, uint64_t const m) noexcept
{
  return n * m <= m - 1;
}

/// \pre d != 0 && d != min()
[[nodiscard, msvc::forceinline, gnu::always_inline]] constexpr uint64_t computeM_s32(int32_t d) noexcept
{
  if (d < 0) d = -d;
  uint32_t const positive = static_cast<uint32_t>(d);
  return 0xFFFF'FFFF'FFFF'FFFFULL / positive + 1 + (((positive & (positive - 1)) == 0) ? 1 : 0);
}

[[nodiscard, msvc::forceinline, gnu::always_inline]] constexpr uint64_t mul128_s32(uint64_t const lowbits, int32_t const d) noexcept
{
  if (d < 0) return umulh(lowbits, static_cast<uint64_t>(static_cast<int64_t>(d))) - lowbits;
  return umulh(lowbits, static_cast<uint64_t>(static_cast<uint32_t>(d)));
}

/// \pre positive_d == abs(d), M == computeM_s32(d)
[[nodiscard, msvc::forceinline, gnu::always_inline]] constexpr int32_t fastmod_s32(int32_t const a, uint64_t const m, int32_t const positive_d) noexcept
{
  uint64_t const lowbits  = m * static_cast<uint64_t>(static_cast<int64_t>(a));
  int32_t const highbits  = static_cast<int32_t>(umulh(lowbits, static_cast<uint64_t>(static_cast<uint32_t>(positive_d))));
  return highbits - ((positive_d - 1) & (a >> 31));
}

/// \pre d not in {-1, 1, min()}, M == computeM_s32(d)
[[nodiscard, msvc::forceinline, gnu::always_inline]] constexpr int32_t fastdiv_s32(int32_t const a, uint64_t const m, int32_t const d) noexcept
{
  uint64_t highbits = mul128_s32(m, a);
  highbits += (a < 0 ? 1U : 0U);
  if (d < 0) return -static_cast<int32_t>(highbits);
  return static_cast<int32_t>(highbits);
}

/// \pre d != 0
[[nodiscard, msvc::forceinline, gnu::always_inline]] constexpr uint128_t computeM_u64(uint64_t const d) noexcept
{
  // ceil(2^128 / d) == floor((2^128 - 1) / d) + 1
  return add128(div128({ 0xFFFF'FFFF'FFFF'FFFFULL, 0xFFFF'FFFF'FFFF'FFFFULL }, d), 1);
}

/// \pre d != 0, M == computeM_u64(d)
[[nodiscard, msvc::forceinline, gnu::always_inline]] constexpr uint64_t fastmod_u64(uint64_t const a, uint128_t const m, uint64_t const d) noexcept
{
  return mul128_u64_hi(mul128_u64_lo(m, a), d);
}

/// \pre d > 1, M == computeM_u64(d)
[[nodiscard, msvc::forceinline, gnu::always_inline]] constexpr uint64_t fastdiv_u64(uint64_t const a, uint128_t const m) noexcept
{
  return mul128_u64_hi(m, a);
}

/// \pre M == computeM_u64(d) for some d != 0
[[nodiscard, msvc::forceinline, gnu::always_inline]] constexpr bool is_divisible_u64(uint64_t const n, uint128_t const m) noexcept
{
  uint128_t const product  = mul128_u64_lo(m, n);
  uint64_t const mdec_lo   = m.low - 1;
  uint64_t const mdec_hi   = m.high - ((m.low == 0) ? 1U : 0U);
  return !(product > uint128_t { mdec_lo, mdec_hi });
}

template <typename T>
concept fastmod_divisor = std::same_as<T, uint32_t> || std::same_as<T, int32_t> || std::same_as<T, uint64_t>;

/// \pre d != 0
template <fastmod_divisor auto d>
[[nodiscard, msvc::forceinline, gnu::always_inline]]
constexpr decltype(d) fastmod(std::same_as<decltype(d)> auto const x) noexcept
{
  static_assert(d != 0);
  if constexpr (std::same_as<decltype(d), uint64_t>) {
    // Upstream omits this overload: its 128-bit magic is not portably constexpr.
    // Ours is, so the 64-bit divisor gets the same compile-time treatment.
    constexpr uint128_t m = computeM_u64(d);
    return fastmod_u64(x, m, d);
  } else if constexpr (std::same_as<decltype(d), int32_t>) {
    static_assert(d != std::numeric_limits<int32_t>::min());
    constexpr uint64_t m = computeM_s32(d);
    if constexpr (d < 0) {
      return fastmod_s32(x, m, -d);
    } else {
      return fastmod_s32(x, m, d);
    }
  } else {
    constexpr uint64_t m = computeM_u32(d);
    return fastmod_u32(x, m, d);
  }
}

/// \pre d != 0
template <fastmod_divisor auto d>
[[nodiscard, msvc::forceinline, gnu::always_inline]]
constexpr decltype(d) fastdiv(std::same_as<decltype(d)> auto const x) noexcept
{
  static_assert(d != 0);
  if constexpr (std::same_as<decltype(d), uint64_t>) {
    constexpr uint128_t m = computeM_u64(d);
    return fastdiv_u64(x, m);
  } else if constexpr (std::same_as<decltype(d), int32_t>) {
    static_assert(d != std::numeric_limits<int32_t>::min());
    constexpr uint64_t m = computeM_s32(d);
    return fastdiv_s32(x, m, d);
  } else {
    constexpr uint64_t m = computeM_u32(d);
    return fastdiv_u32(x, m);
  }
}
} // namespace stdex

#if defined(__clang__)
#  pragma clang diagnostic pop
#elif defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif
