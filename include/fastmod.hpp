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
#include <cstdint>  // std::uint64_t, std::uint32_t, int32_t
#include <limits>   // std::numeric_limits

#include "int128.hpp" // stdex::uint128_t, stdex::umul128, stdex::uadd128, stdex::udiv128,
                      // stdex::umul128_lo, stdex::umul128_hi

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
/// \pre d != 0
[[nodiscard]] constexpr std::uint64_t computeM_u32(std::uint32_t const d) noexcept
{
  return 0xFF'FF'FF'FF'FF'FF'FF'FFULL / d + 1;
}

/// \pre d != 0, M == computeM_u32(d)
[[nodiscard]] constexpr std::uint32_t fastmod_u32(std::uint32_t const a, std::uint64_t const m, std::uint32_t const d) noexcept
{
  std::uint64_t const lowbits = m * a;
  return static_cast<std::uint32_t>(umul128(lowbits, d).hi());
}

/// \pre d > 1, M == computeM_u32(d)
[[nodiscard]] constexpr std::uint32_t fastdiv_u32(std::uint32_t const a, std::uint64_t const m) noexcept
{
  return static_cast<std::uint32_t>(umul128(m, a).hi());
}

/// \pre M == computeM_u32(d) for some d != 0
[[nodiscard]] constexpr bool is_divisible(std::uint32_t const n, std::uint64_t const m) noexcept
{
  return n * m <= m - 1;
}

/// \pre d != 0 && d != min()
[[nodiscard]] constexpr std::uint64_t computeM_s32(int32_t d) noexcept
{
  if (d < 0) d = -d;
  std::uint32_t const positive = static_cast<std::uint32_t>(d);
  return 0xFF'FF'FF'FF'FF'FF'FF'FFULL / positive + 1 + (((positive & (positive - 1)) == 0) ? 1 : 0);
}

[[nodiscard]] constexpr std::uint64_t mul128_s32(std::uint64_t const lowbits, int32_t const d) noexcept
{
  std::uint64_t const m = static_cast<std::uint64_t>(static_cast<std::int64_t>(d));
  return umul128(lowbits, m).hi() - (lowbits & (0 - (m >> 63)));
}

/// \pre positive_d == abs(d), M == computeM_s32(d)
[[nodiscard]] constexpr int32_t fastmod_s32(int32_t const a, std::uint64_t const m, int32_t const positive_d) noexcept
{
  std::uint64_t const lowbits = m * static_cast<std::uint64_t>(static_cast<int64_t>(a));
  int32_t const highbits      = static_cast<int32_t>(umul128(lowbits, static_cast<std::uint64_t>(static_cast<std::uint32_t>(positive_d))).hi());
  return highbits - ((positive_d - 1) & (a >> 31));
}

/// \pre d not in {-1, 1, min()}, M == computeM_s32(d)
[[nodiscard]] constexpr int32_t fastdiv_s32(int32_t const a, std::uint64_t const m, int32_t const d) noexcept
{
  std::uint64_t highbits = mul128_s32(m, a);
  highbits += (a < 0 ? 1U : 0U);
  return d < 0 ? -static_cast<int32_t>(highbits) : static_cast<int32_t>(highbits);
}

/// \pre d != 0
[[nodiscard]]
#if !defined(DEBUG) && !defined(_DEBUG)
[[msvc::flatten, gnu::flatten]]
#endif
constexpr uint128_t computeM_u64(std::uint64_t const d) noexcept
{
  // ceil(2^128 / d) == floor((2^128 - 1) / d) + 1
  return uadd128(udiv128({ 0xFF'FF'FF'FF'FF'FF'FF'FFULL, 0xFF'FF'FF'FF'FF'FF'FF'FFULL }, d), 1);
}

/// \pre d != 0, M == computeM_u64(d)
[[nodiscard]]
#if !defined(DEBUG) && !defined(_DEBUG)
[[msvc::flatten, gnu::flatten]]
#endif
constexpr std::uint64_t fastmod_u64(std::uint64_t const a, uint128_t const m, std::uint64_t const d) noexcept
{
  return umul128_hi(umul128_lo(m, a), d).hi();
}

/// \pre d > 1, M == computeM_u64(d)
[[nodiscard]]
#if !defined(DEBUG) && !defined(_DEBUG)
[[msvc::flatten, gnu::flatten]]
#endif
constexpr std::uint64_t fastdiv_u64(std::uint64_t const a, uint128_t const m) noexcept
{
  return umul128_hi(m, a).hi();
}

/// \pre M == computeM_u64(d) for some d != 0
[[nodiscard]] constexpr bool is_divisible_u64(std::uint64_t const n, uint128_t const m) noexcept
{
  uint128_t const product     = umul128_lo(m, n);
  std::uint64_t const mdec_lo = m.lo() - 1;
  std::uint64_t const mdec_hi = m.hi() - ((m.lo() == 0) ? 1U : 0U);
  return !(product > uint128_t { mdec_lo, mdec_hi });
}

template <typename T>
concept fastmod_divisor = std::integral<T> &&
                          ((std::unsigned_integral<T> && (sizeof(T) == 4 || sizeof(T) == 8)) ||
                           (std::signed_integral<T> && sizeof(T) == 4));

/// \pre d != 0
template <fastmod_divisor auto d>
[[nodiscard]] constexpr decltype(d) fastmod(std::same_as<decltype(d)> auto const x) noexcept
{
  static_assert(d != 0);
  if constexpr (std::unsigned_integral<decltype(d)> && sizeof(decltype(d)) == 8) {
    // Upstream omits this overload: its 128-bit magic is not portably constexpr.
    // Ours is, so the 64-bit divisor gets the same compile-time treatment.
    constexpr uint128_t m = computeM_u64(static_cast<std::uint64_t>(d));
    return static_cast<decltype(d)>(fastmod_u64(static_cast<std::uint64_t>(x), m, static_cast<std::uint64_t>(d)));
  } else if constexpr (std::signed_integral<decltype(d)> && sizeof(decltype(d)) == 4) {
    static_assert(d != std::numeric_limits<int32_t>::min());
    constexpr std::uint64_t m = computeM_s32(d);
    if constexpr (d < 0) {
      return fastmod_s32(x, m, -d);
    } else {
      return fastmod_s32(x, m, d);
    }
  } else {
    constexpr std::uint64_t m = computeM_u32(d);
    return fastmod_u32(x, m, d);
  }
}

/// \pre d != 0
template <fastmod_divisor auto d>
[[nodiscard]] constexpr decltype(d) fastdiv(std::same_as<decltype(d)> auto const x) noexcept
{
  static_assert(d != 0);
  if constexpr (std::unsigned_integral<decltype(d)> && sizeof(decltype(d)) == 8) {
    constexpr uint128_t m = computeM_u64(static_cast<std::uint64_t>(d));
    return static_cast<decltype(d)>(fastdiv_u64(static_cast<std::uint64_t>(x), m));
  } else if constexpr (std::signed_integral<decltype(d)> && sizeof(decltype(d)) == 4) {
    static_assert(d != std::numeric_limits<int32_t>::min());
    constexpr std::uint64_t m = computeM_s32(d);
    return fastdiv_s32(x, m, d);
  } else {
    constexpr std::uint64_t m = computeM_u32(d);
    return fastdiv_u32(x, m);
  }
}

template <fastmod_divisor T>
[[nodiscard]] constexpr T fastmod(T const x, T const d) noexcept
{
  if constexpr (std::unsigned_integral<T> && sizeof(T) == 8) {
    return fastmod_u64(x, computeM_u64(d), d);
  } else if constexpr (std::signed_integral<T> && sizeof(T) == 4) {
    std::uint64_t const m = computeM_s32(d);
    return fastmod_s32(x, m, d < 0 ? -d : d);
  } else {
    return fastmod_u32(x, computeM_u32(d), d);
  }
}

template <fastmod_divisor T>
[[nodiscard]] constexpr T fastdiv(T const x, T const d) noexcept
{
  if constexpr (std::unsigned_integral<T> && sizeof(T) == 8) {
    return fastdiv_u64(x, computeM_u64(d));
  } else if constexpr (std::signed_integral<T> && sizeof(T) == 4) {
    return fastdiv_s32(x, computeM_s32(d), d);
  } else {
    return fastdiv_u32(x, computeM_u32(d));
  }
}
} // namespace stdex

#if defined(__clang__)
#  pragma clang diagnostic pop
#elif defined(__GNUC__)
#  pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#  pragma warning(pop)
#endif
