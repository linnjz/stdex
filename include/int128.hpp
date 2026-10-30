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

#include <cassert>     // assert
#include <charconv>    // std::to_chars
#include <climits>     // CHAR_MAX
#include <cmath>       // std::abs, std::pow
#include <compare>     // std::strong_ordering
#include <concepts>    // std::integral
#include <cstdint>     // std::uint64_t
#include <cstdlib>     // std::abort
#include <format>      // std::formatter
#include <limits>      // std::numeric_limits
#include <locale>      // std::numpunct
#include <numeric>     // std::gcd, std::lcm, std::midpoint
#include <optional>    // std::optional
#include <string>      // std::basic_string
#include <system_error> // std::errc
#include <type_traits> // std::make_unsigned_t, std::remove_cvref_t
#include <utility>     // std::pair

/**
 * @brief Overflow checking policy.
 *
 * @details
 * The effective policy is selected once at header-include time:
 * - `-DSTDEX_INT128_OVERFLOW_CHECK=0/1`: forces a policy for every build type.
 * - `-DSTDEX_INT128_OVERFLOW_CHECK_DEBUG=0/1`
 * - `-DSTDEX_INT128_OVERFLOW_CHECK_RELEASE=0/1`
 *
 * @note
 * Checked overflow (signed and unsigned) calls
 * `int128_detail::overflow_failure()`, a non-constexpr function: a runtime
 * call aborts the process, while an overflow reached during constant
 * evaluation makes the expression non-constant (the same diagnostic style as
 * `std::is_constant_evaluated()` misuse).
 *
 * Unchecked arithmetic is defined to wrap; signed arithmetic wraps two's
 * complement instead of invoking C++ signed-overflow UB.
 *
 * The `checked_*`, `overflowing_*`, `wrapping_*` and `saturating_*` member
 * functions never consult this policy; their behavior is fixed.
 */

#if defined(STDEX_INT128_OVERFLOW_CHECK)
#  define STDEX_INT128_HAS_OVERFLOW_CHECK (STDEX_INT128_OVERFLOW_CHECK != 0)
#elif defined(NDEBUG)
#  if defined(STDEX_INT128_OVERFLOW_CHECK_RELEASE)
#    define STDEX_INT128_HAS_OVERFLOW_CHECK (STDEX_INT128_OVERFLOW_CHECK_RELEASE != 0)
#  else
#    define STDEX_INT128_HAS_OVERFLOW_CHECK 0
#  endif
#else
#  if defined(STDEX_INT128_OVERFLOW_CHECK_DEBUG)
#    define STDEX_INT128_HAS_OVERFLOW_CHECK (STDEX_INT128_OVERFLOW_CHECK_DEBUG != 0)
#  else
#    define STDEX_INT128_HAS_OVERFLOW_CHECK 1
#  endif
#endif

#if defined(__SIZEOF_INT128__)
#  define STDEX_INT128_HAS_NATIVE __SIZEOF_INT128__
#else
#  define STDEX_INT128_HAS_NATIVE 0
#endif

#if !STDEX_INT128_HAS_NATIVE && defined(_MSC_VER) && defined(_M_X64) && !defined(_M_ARM64EC)
#  define STDEX_INT128_HAS_MSVC_X64_INTRINSICS 1
#  include <intrin.h>
#else
#  define STDEX_INT128_HAS_MSVC_X64_INTRINSICS 0
#endif

#if !STDEX_INT128_HAS_NATIVE && defined(_MSC_VER) && defined(_M_ARM64) && !defined(_M_ARM64EC)
#  define STDEX_INT128_HAS_MSVC_ARM64_INTRINSICS 1
#  include <arm_neon.h>
#else
#  define STDEX_INT128_HAS_MSVC_ARM64_INTRINSICS 0
#endif

namespace stdex
{
struct uint128_t;
struct int128_t;

template <class T>
inline constexpr bool is_int128_v = false;

template <>
inline constexpr bool is_int128_v<uint128_t> = true;

template <>
inline constexpr bool is_int128_v<int128_t> = true;
} // namespace stdex

namespace std
{
template <> struct common_type<stdex::uint128_t, stdex::uint128_t> { using type = stdex::uint128_t; };
template <> struct common_type<stdex::int128_t,  stdex::int128_t>  { using type = stdex::int128_t;  };
template <> struct common_type<stdex::uint128_t, stdex::int128_t>  { using type = stdex::uint128_t; };
template <> struct common_type<stdex::int128_t,  stdex::uint128_t> { using type = stdex::uint128_t; };
} // namespace std

namespace stdex
{
template <class T>
concept standard_integral_operand = std::integral<T> &&
                                    !is_int128_v<std::remove_cv_t<T>> &&
                                    !std::same_as<std::remove_cv_t<T>, bool>;

template <class T>
concept extended_integral_operand = !std::is_reference_v<T> &&
                                    (standard_integral_operand<T> || is_int128_v<std::remove_cvref_t<T>>);

namespace int128_detail
{
template <class T>
concept shift_count = (std::integral<std::remove_cvref_t<T>> &&
                       !std::same_as<std::remove_cvref_t<T>, bool>) ||
                      is_int128_v<std::remove_cvref_t<T>>;

/// True when the count is outside [0, 128); negative counts map to large
/// unsigned values, matching the two's-complement modulo semantics below.
template <shift_count T>
constexpr bool shift_count_out_of_range(T const count) noexcept
{
  if constexpr (is_int128_v<T>) {
    if constexpr (std::is_same_v<T, int128_t>) {
      if (count.is_negative()) {
        return true;
      }
    }
    return count.hi() != 0;
  } else {
    using U = std::make_unsigned_t<T>;
    return static_cast<U>(count) >= 128U;
  }
}

template <shift_count T>
constexpr unsigned int shift_count_mod_128(T const count) noexcept
{
  if constexpr (is_int128_v<T>) {
    return static_cast<unsigned int>(count.lo() & 127U);
  } else {
    using U = std::make_unsigned_t<T>;
    return static_cast<unsigned int>(static_cast<U>(count) % 128U);
  }
}

#if STDEX_INT128_HAS_NATIVE
using native_uint128_t = __uint128_t;
using native_int128_t  = __int128_t;
#endif

[[noreturn]] inline void overflow_failure() noexcept { std::abort(); }
[[noreturn]] inline void division_by_zero() noexcept { std::abort(); }

/// Reports a violated function precondition: asserts in debug builds, then
/// aborts in every build type. Reaching this during constant evaluation makes
/// the expression non-constant.
[[noreturn]] inline void precondition_failure() noexcept
{
  assert(false && "stdex::int128_t: result is not representable in common_type_t<T, U>");
  std::abort();
}

template <standard_integral_operand T>
constexpr std::uint64_t integral_high_word(T const input) noexcept
{
  if constexpr (sizeof(T) > 8) {
    using U = std::make_unsigned_t<T>;
    return static_cast<std::uint64_t>(static_cast<U>(input) >> 64U);
  } else {
    return (std::is_signed_v<T> && input < 0) ? ~0ULL : 0ULL;
  }
}

/// \pre value != 0
constexpr int clz64_zero_unchecked_portable(std::uint64_t const value) noexcept
{
  // Binary search: one bit-test per doubling of the candidate count.
  std::uint64_t x = value;
  int count       = 0;
  if ((x & 0xFFFF'FFFF'0000'0000ULL) == 0) { count += 32; x <<= 32U; }
  if ((x & 0xFFFF'0000'0000'0000ULL) == 0) { count += 16; x <<= 16U; }
  if ((x & 0xFF00'0000'0000'0000ULL) == 0) { count += 8;  x <<= 8U;  }
  if ((x & 0xF000'0000'0000'0000ULL) == 0) { count += 4;  x <<= 4U;  }
  if ((x & 0xC000'0000'0000'0000ULL) == 0) { count += 2;  x <<= 2U;  }
  if ((x & 0x8000'0000'0000'0000ULL) == 0) { count += 1; }
  return count;
}

/// \pre value != 0
constexpr int ctz64_zero_unchecked_portable(std::uint64_t const value) noexcept
{
  std::uint64_t x = value;
  int count       = 0;
  if ((x & 0x0000'0000'FFFF'FFFFULL) == 0) { count += 32; x >>= 32U; }
  if ((x & 0x0000'0000'0000'FFFFULL) == 0) { count += 16; x >>= 16U; }
  if ((x & 0x0000'0000'0000'00FFULL) == 0) { count += 8;  x >>= 8U;  }
  if ((x & 0x0000'0000'0000'000FULL) == 0) { count += 4;  x >>= 4U;  }
  if ((x & 0x0000'0000'0000'0003ULL) == 0) { count += 2;  x >>= 2U;  }
  if ((x & 0x0000'0000'0000'0001ULL) == 0) { count += 1; }
  return count;
}

/// \pre value != 0
constexpr int clz64_zero_unchecked(std::uint64_t const value) noexcept
{
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_clzll(value);
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
  if (!std::is_constant_evaluated()) {
    unsigned long index = 0;
    _BitScanReverse64(&index, value);
    return 63 - static_cast<int>(index);
  }
  return clz64_zero_unchecked_portable(value);
#else
  return clz64_zero_unchecked_portable(value);
#endif
}

/// \pre value != 0
constexpr int ctz64_zero_unchecked(std::uint64_t const value) noexcept
{
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_ctzll(value);
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
  if (!std::is_constant_evaluated()) {
    unsigned long index = 0;
    _BitScanForward64(&index, value);
    return static_cast<int>(index);
  }
  return ctz64_zero_unchecked_portable(value);
#else
  return ctz64_zero_unchecked_portable(value);
#endif
}

/// Returns the low 64 bits of lhs * rhs, writes the high 64 bits to high.
constexpr std::uint64_t umul128_portable(std::uint64_t const lhs, std::uint64_t const rhs, std::uint64_t &high) noexcept
{
  // Split into 32-bit half-words: x = x_low + x_high * 2^32
  std::uint64_t const lhs_low  = static_cast<std::uint32_t>(lhs);
  std::uint64_t const lhs_high = lhs >> 32U;
  std::uint64_t const rhs_low  = static_cast<std::uint32_t>(rhs);
  std::uint64_t const rhs_high = rhs >> 32U;

  // Four 32x32 -> 64 partial products
  std::uint64_t const low_product  = lhs_low * rhs_low;   // Contributes to bits [0, 64)
  std::uint64_t const cross1       = lhs_low * rhs_high;  // Contributes to bits [32, 96)
  std::uint64_t const cross2       = lhs_high * rhs_low;  // Contributes to bits [32, 96)
  std::uint64_t const high_product = lhs_high * rhs_high; // Contributes to bits [64, 128)

  // Start the low 64 bits from low_product, and add in the low 32 bits of the cross terms shifted left by 32.
  // The middle carry is carried naturally by a 64-bit temporary:
  // the part of middle above 2^32 is the carry.
  std::uint64_t const middle = (low_product >> 32U) + static_cast<std::uint32_t>(cross1) + static_cast<std::uint32_t>(cross2);

  // Start the high 64 bits from high_product, then accumulate the high 32 bits of the cross terms.
  high = high_product + (cross1 >> 32U) + (cross2 >> 32U) + (middle >> 32U);

  return (middle << 32U) + static_cast<std::uint32_t>(low_product);
}

/// Computes (high * 2^64 + low) / divisor, writes the remainder to remainder.
/// \pre high < divisor, divisor != 0
constexpr std::uint64_t udiv128_portable(std::uint64_t const high, std::uint64_t const low, std::uint64_t const divisor, std::uint64_t &remainder) noexcept
{
  // Long division, one bit at a time. Invariant: rem < divisor at the
  // start of every iteration, so (rem, low_bit) < 2 * divisor.
  std::uint64_t rem = high, quotient = 0;
  for (int index = 63; index >= 0; --index) {
    std::uint64_t const carry { rem >> 63U };
    rem = (rem << 1U) | ((low >> static_cast<unsigned>(index)) & 1U);
    // If carry != 0 then rem * 2 + bit >= 2^64 > divisor, so we must
    // subtract; the subtraction wraps correctly modulo 2^64 and yields a
    // value < divisor.
    if (carry != 0 || rem >= divisor) {
      rem -= divisor;
      quotient |= 1ULL << static_cast<unsigned>(index);
    }
  }
  remainder = rem;
  return quotient;
}

constexpr unsigned char addcarry64(unsigned char const carry, std::uint64_t const lhs, std::uint64_t const rhs, std::uint64_t &result) noexcept
{
#if STDEX_INT128_HAS_MSVC_X64_INTRINSICS
  if (!std::is_constant_evaluated()) {
    return _addcarry_u64(carry, lhs, rhs, &result);
  }
#endif
  std::uint64_t const sum { lhs + rhs + carry };
  result = sum;
  return carry != 0 ? static_cast<unsigned char>(sum <= lhs) : static_cast<unsigned char>(sum < lhs);
}

constexpr unsigned char subborrow64(unsigned char const borrow, std::uint64_t const lhs, std::uint64_t const rhs, std::uint64_t &result) noexcept
{
#if STDEX_INT128_HAS_MSVC_X64_INTRINSICS
  if (!std::is_constant_evaluated()) {
    return _subborrow_u64(borrow, lhs, rhs, &result);
  }
#endif
  std::uint64_t const difference { lhs - rhs - borrow };
  result = difference;
  return borrow != 0 ? static_cast<unsigned char>(difference >= lhs)
                     : static_cast<unsigned char>(difference > lhs);
}
} // namespace int128_detail

constexpr std::uint64_t umul128(std::uint64_t const lhs, std::uint64_t const rhs, std::uint64_t &high) noexcept
{
#if STDEX_INT128_HAS_NATIVE
  using native_type         = int128_detail::native_uint128_t;
  native_type const product { static_cast<native_type>(lhs) * static_cast<native_type>(rhs) };
  high = static_cast<std::uint64_t>(product >> 64U);
  return static_cast<std::uint64_t>(product);
#elif STDEX_INT128_HAS_MSVC_ARM64_INTRINSICS
  if (!std::is_constant_evaluated()) {
    high = __umulh(lhs, rhs);
    return lhs * rhs;
  }
  return int128_detail::umul128_portable(lhs, rhs, high);
#elif STDEX_INT128_HAS_MSVC_X64_INTRINSICS
  if (!std::is_constant_evaluated()) {
    return _umul128(lhs, rhs, &high);
  }
  return int128_detail::umul128_portable(lhs, rhs, high);
#else
  return int128_detail::umul128_portable(lhs, rhs, high);
#endif
}

/// \pre high < divisor, divisor != 0
constexpr std::uint64_t udiv128(std::uint64_t const high, std::uint64_t const low, std::uint64_t const divisor, std::uint64_t &remainder) noexcept
{
#if STDEX_INT128_HAS_NATIVE
  using native_type = int128_detail::native_uint128_t;
  native_type const dividend { (static_cast<native_type>(high) << 64U) | static_cast<native_type>(low) };
  remainder = static_cast<std::uint64_t>(dividend % divisor);
  return static_cast<std::uint64_t>(dividend / divisor);
#elif STDEX_INT128_HAS_MSVC_X64_INTRINSICS
  if (!std::is_constant_evaluated()) {
    return _udiv128(high, low, divisor, &remainder);
  }
  return int128_detail::udiv128_portable(high, low, divisor, remainder);
#else
  return int128_detail::udiv128_portable(high, low, divisor, remainder);
#endif
}

template <class T>
struct divmod_result
{
  T quotient {};
  T remainder {};

  friend constexpr bool operator== (divmod_result const &, divmod_result const &) noexcept = default;
};

namespace int128_detail
{
struct alignas(16) base128
{
#if STDEX_INT128_HAS_NATIVE
  using value_type = native_uint128_t;
#elif defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
  struct value_type
  {
    std::uint64_t hi_;
    std::uint64_t lo_;

    constexpr value_type() noexcept : lo_(0) , hi_(0) {}

    constexpr value_type(std::uint64_t const low, std::uint64_t const high) noexcept
        : hi_(high) , lo_(low) {}
  };
#else
  struct value_type
  {
    std::uint64_t lo_;
    std::uint64_t hi_;

    constexpr value_type() noexcept : lo_(0) , hi_(0) {}

    constexpr value_type(std::uint64_t const low, std::uint64_t const high) noexcept
        : lo_(low) , hi_(high) {}
  };
#endif
  using bytes_type = std::uint8_t[16];
  union { value_type value; bytes_type bytes; };

  constexpr base128() noexcept
#if STDEX_INT128_HAS_NATIVE
      : value(0)
#else
      : value(std::uint64_t { 0 }, std::uint64_t { 0 })
#endif
  {
  }

  constexpr base128(std::uint64_t const low, std::uint64_t const high) noexcept
#if STDEX_INT128_HAS_NATIVE
      : value((static_cast<value_type>(high) << 64U) | static_cast<value_type>(low))
#else
      : value(low, high)
#endif
  {
  }

  template <standard_integral_operand T>
  constexpr base128(T const input) noexcept
#if STDEX_INT128_HAS_NATIVE
      : value(static_cast<value_type>(input))
#else
      : value(static_cast<std::uint64_t>(input), integral_high_word(input))
#endif
  {
  }

  [[nodiscard]] constexpr std::uint64_t lo() const noexcept
  {
#if STDEX_INT128_HAS_NATIVE
    return static_cast<std::uint64_t>(value);
#else
    return value.lo_;
#endif
  }

  [[nodiscard]] constexpr std::uint64_t hi() const noexcept
  {
#if STDEX_INT128_HAS_NATIVE
    return static_cast<std::uint64_t>(value >> 64U);
#else
    return value.hi_;
#endif
  }

  [[nodiscard]] constexpr std::uint8_t *data() noexcept { return bytes; }
  [[nodiscard]] constexpr std::uint8_t const *data() const noexcept { return bytes; }

  constexpr void to_le_bytes(std::uint8_t *const output) const noexcept
  {
    std::uint64_t const low  = lo();
    std::uint64_t const high = hi();
    for (unsigned index = 0; index < 8; ++index) {
      output[index]      = static_cast<std::uint8_t>(low >> (index * 8U));
      output[index + 8U] = static_cast<std::uint8_t>(high >> (index * 8U));
    }
  }

  constexpr void to_be_bytes(std::uint8_t *const output) const noexcept
  {
    std::uint64_t const low  = lo();
    std::uint64_t const high = hi();
    for (unsigned index = 0; index < 8; ++index) {
      output[index]      = static_cast<std::uint8_t>(high >> ((7U - index) * 8U));
      output[index + 8U] = static_cast<std::uint8_t>(low >> ((7U - index) * 8U));
    }
  }

  template <standard_integral_operand T>
  [[nodiscard]] constexpr T to_integral() const noexcept
  {
    if constexpr (sizeof(T) <= 8) {
      return static_cast<T>(lo());
    } else {
      using unsigned_type = std::make_unsigned_t<T>;
      return static_cast<T>((static_cast<unsigned_type>(hi()) << 64U) | static_cast<unsigned_type>(lo()));
    }
  }

  template <standard_integral_operand T>
  [[nodiscard]] constexpr explicit operator T() const noexcept
  {
    return to_integral<T>();
  }

  [[nodiscard]] constexpr bool is_zero() const noexcept { return (lo() | hi()) == 0; }
  [[nodiscard]] constexpr explicit operator bool() const noexcept { return !is_zero(); }

  [[nodiscard]] static constexpr bool equal(base128 const lhs, base128 const rhs) noexcept
  {
    return lhs.lo() == rhs.lo() && lhs.hi() == rhs.hi();
  }

  [[nodiscard]] static constexpr bool less(base128 const lhs, base128 const rhs) noexcept
  {
    return lhs.hi() < rhs.hi() || (lhs.hi() == rhs.hi() && lhs.lo() < rhs.lo());
  }

  [[nodiscard]] friend constexpr bool operator== (base128 const lhs, base128 const rhs) noexcept
  {
    return equal(lhs, rhs);
  }

  [[nodiscard]] friend constexpr bool operator< (base128 const lhs, base128 const rhs) noexcept
  {
    return less(lhs, rhs);
  }

  [[nodiscard]] friend constexpr bool operator> (base128 const lhs, base128 const rhs) noexcept
  {
    return less(rhs, lhs);
  }

  [[nodiscard]] friend constexpr bool operator<= (base128 const lhs, base128 const rhs) noexcept
  {
    return !less(rhs, lhs);
  }

  [[nodiscard]] friend constexpr bool operator>= (base128 const lhs, base128 const rhs) noexcept
  {
    return !less(lhs, rhs);
  }

  [[nodiscard]] friend constexpr bool operator!(base128 const value) noexcept
  {
    return value.is_zero();
  }

  static constexpr void require_nonzero(base128 const rhs) noexcept
  {
    if (rhs.is_zero()) [[unlikely]] {
      division_by_zero();
    }
  }
};

/// Shared facade for the signed and unsigned wrappers. It deliberately adds no
/// data members: storage and layout stay in base128.
template <class Derived>
struct common128 : base128
{
  using base_type  = base128;
  using value_type = base_type::value_type;
  using bytes_type = base_type::bytes_type;

  constexpr common128() noexcept : base_type {} {}
  constexpr common128(std::uint64_t const low, std::uint64_t const high) noexcept : base_type(low, high) {}

  constexpr explicit common128(base_type const &bits) noexcept : base_type(bits) {}

  template <standard_integral_operand T>
  constexpr common128(T const input) noexcept : base_type(input)
  {
  }

  [[nodiscard]] static constexpr Derived from_words(std::uint64_t const low, std::uint64_t const high) noexcept
  {
    return { low, high };
  }

  [[nodiscard]] static constexpr Derived from_u64(std::uint64_t const value) noexcept
  {
    return { value, 0 };
  }

  [[nodiscard]] static constexpr Derived from_i64(std::int64_t const value) noexcept
  {
    return { static_cast<std::uint64_t>(value), value < 0 ? ~0ULL : 0ULL };
  }

  template <standard_integral_operand T>
  [[nodiscard]] static constexpr Derived from_integral(T const value) noexcept
  {
    if constexpr (sizeof(T) > 8) {
      using U = std::make_unsigned_t<T>;
      return { static_cast<std::uint64_t>(static_cast<U>(value)), static_cast<std::uint64_t>(static_cast<U>(value) >> 64U) };
    } else {
      return { static_cast<std::uint64_t>(value), integral_high_word(value) };
    }
  }

  [[nodiscard]] static constexpr Derived from_le_bytes(std::uint8_t const *const input) noexcept
  {
    std::uint64_t low {}, high {};
    for (unsigned index = 0; index < 8; ++index) {
      low |= static_cast<std::uint64_t>(input[index]) << (index * 8U);
      high |= static_cast<std::uint64_t>(input[index + 8U]) << (index * 8U);
    }
    return { low, high };
  }

  [[nodiscard]] static constexpr Derived from_be_bytes(std::uint8_t const *const input) noexcept
  {
    std::uint64_t low {}, high {};
    for (unsigned index = 0; index < 8; ++index) {
      high |= static_cast<std::uint64_t>(input[index]) << ((7U - index) * 8U);
      low |= static_cast<std::uint64_t>(input[index + 8U]) << ((7U - index) * 8U);
    }
    return { low, high };
  }

  /// True when the two's-complement sign bit is set. Unsigned wrappers always
  /// report false; the branch is resolved once per instantiation.
  [[nodiscard]] constexpr bool is_negative() const noexcept
  {
    if constexpr (std::is_same_v<Derived, int128_t>) {
      return (hi() >> 63U) != 0;
    } else {
      return false;
    }
  }

  /// Three-way comparison of two same-type values. Signed wrappers order by
  /// the two's-complement high word; unsigned wrappers compare high then low.
  [[nodiscard]] static constexpr std::strong_ordering compare(Derived const lhs, Derived const rhs) noexcept
  {
    if constexpr (std::is_same_v<Derived, int128_t>) {
      auto const lhs_high = static_cast<std::int64_t>(lhs.hi());
      auto const rhs_high = static_cast<std::int64_t>(rhs.hi());
      if (lhs_high != rhs_high) {
        return lhs_high < rhs_high ? std::strong_ordering::less : std::strong_ordering::greater;
      }
    } else if (lhs.hi() != rhs.hi()) {
      return lhs.hi() < rhs.hi() ? std::strong_ordering::less : std::strong_ordering::greater;
    }
    if (lhs.lo() != rhs.lo()) {
      return lhs.lo() < rhs.lo() ? std::strong_ordering::less : std::strong_ordering::greater;
    }
    return std::strong_ordering::equal;
  }

  /// Two's-complement negation of the raw bits; shared by signed magnitudes
  /// and unsigned subtraction.
  [[nodiscard]] static constexpr Derived negate_bits(Derived const value) noexcept
  {
    std::uint64_t low {};
    unsigned char const borrow = int128_detail::subborrow64(0, 0, value.lo(), low);
    std::uint64_t high {};
    (void) int128_detail::subborrow64(borrow, 0, value.hi(), high);
    return { low, high };
  }

  /// Full 128x128 -> 256-bit product reduced to (low, high).
  [[nodiscard]] static constexpr std::pair<Derived, Derived> mul_full(Derived const lhs, Derived const rhs) noexcept
  {
    std::uint64_t const lhs_low  = lhs.lo();
    std::uint64_t const lhs_high = lhs.hi();
    std::uint64_t const rhs_low  = rhs.lo();
    std::uint64_t const rhs_high = rhs.hi();

    std::uint64_t p00_high {};
    std::uint64_t const p00_low = umul128(lhs_low, rhs_low, p00_high);
    std::uint64_t p01_high {};
    std::uint64_t const p01_low = umul128(lhs_low, rhs_high, p01_high);
    std::uint64_t p10_high {};
    std::uint64_t const p10_low = umul128(lhs_high, rhs_low, p10_high);
    std::uint64_t p11_high {};
    std::uint64_t const p11_low = umul128(lhs_high, rhs_high, p11_high);

    // Carry out of the 2^64 coefficient needs two bits because three 64-bit
    // terms are summed; the carries are accumulated explicitly rather than
    // folded, to keep the intermediate width in check.
    std::uint64_t middle = p00_high;
    unsigned char carry  = int128_detail::addcarry64(0, middle, p01_low, middle);
    carry                = static_cast<unsigned char>(carry + int128_detail::addcarry64(0, middle, p10_low, middle));

    std::uint64_t upper            = p11_low;
    unsigned char upper_carry      = int128_detail::addcarry64(0, upper, p01_high, upper);
    upper_carry                    = static_cast<unsigned char>(upper_carry + int128_detail::addcarry64(0, upper, p10_high, upper));
    upper_carry                    = static_cast<unsigned char>(upper_carry + int128_detail::addcarry64(0, upper, carry, upper));
    std::uint64_t const upper_high = p11_high + upper_carry;

    return {
      { p00_low, middle     },
      { upper,   upper_high }
    };
  }

  /// Unsigned long division (Knuth TAOCP 4.3.1D). Both operands are treated
  /// as non-negative magnitudes; signed wrappers fix the sign outside.
  /// \pre rhs != 0
  [[nodiscard]] static constexpr Derived divide_unsigned(Derived const dividend, Derived const divisor) noexcept
  {
    return divmod_unsigned_impl<true, false>(dividend, divisor).quotient;
  }

  /// \pre rhs != 0
  [[nodiscard]] static constexpr Derived modulo_unsigned(Derived const dividend, Derived const divisor) noexcept
  {
    return divmod_unsigned_impl<false, true>(dividend, divisor).remainder;
  }

  /// \pre rhs != 0
  [[nodiscard]] static constexpr divmod_result<Derived> divmod_unsigned(Derived const dividend, Derived const divisor) noexcept
  {
    return divmod_unsigned_impl<true, true>(dividend, divisor);
  }

  /// Logical (zero-filling) right shift. Used for denormalizing division
  /// results, which must not sign-extend even when the wrapper is signed.
  /// \pre count < 64
  [[nodiscard]] static constexpr Derived logical_shr_bits(Derived const value, unsigned int const count) noexcept
  {
    if (count == 0) {
      return value;
    }
    return { (value.lo() >> count) | (value.hi() << (64U - count)), value.hi() >> count };
  }

  /// Magnitude of a signed value, or the value itself when unsigned.
  [[nodiscard]] constexpr Derived unsigned_abs() const noexcept
  {
    if constexpr (std::is_same_v<Derived, int128_t>) {
      return is_negative() ? negate_bits(self()) : self();
    } else {
      return self();
    }
  }

  [[nodiscard]] constexpr Derived wrapping_add(Derived const rhs) const noexcept
  {
#if STDEX_INT128_HAS_NATIVE
    value_type const result = value + rhs.value;
    return { static_cast<std::uint64_t>(result), static_cast<std::uint64_t>(result >> 64U) };
#else
    std::uint64_t low {};
    unsigned char const carry = int128_detail::addcarry64(0, lo(), rhs.lo(), low);
    std::uint64_t high {};
    (void) int128_detail::addcarry64(carry, hi(), rhs.hi(), high);
    return { low, high };
#endif
  }

  [[nodiscard]] constexpr Derived wrapping_sub(Derived const rhs) const noexcept
  {
#if STDEX_INT128_HAS_NATIVE
    value_type const result = value - rhs.value;
    return { static_cast<std::uint64_t>(result), static_cast<std::uint64_t>(result >> 64U) };
#else
    std::uint64_t low {};
    unsigned char const borrow = int128_detail::subborrow64(0, lo(), rhs.lo(), low);
    std::uint64_t high {};
    (void) int128_detail::subborrow64(borrow, hi(), rhs.hi(), high);
    return { low, high };
#endif
  }

  /// Low 128 bits of the product; sign-independent, so signed and unsigned
  /// share the same reduction.
  [[nodiscard]] constexpr Derived wrapping_mul(Derived const rhs) const noexcept
  {
#if STDEX_INT128_HAS_NATIVE
    value_type const result = value * rhs.value;
    return { static_cast<std::uint64_t>(result), static_cast<std::uint64_t>(result >> 64U) };
#else
    std::uint64_t product_high {};
    std::uint64_t const product_low = umul128(lo(), rhs.lo(), product_high);
    std::uint64_t const cross       = lo() * rhs.hi() + hi() * rhs.lo() + product_high;
    return { product_low, cross };
#endif
  }

  [[nodiscard]] constexpr Derived wrapping_div(Derived const rhs) const noexcept
  {
    base_type::require_nonzero(rhs);
    return self().unchecked_div(rhs);
  }

  [[nodiscard]] constexpr Derived wrapping_rem(Derived const rhs) const noexcept
  {
    base_type::require_nonzero(rhs);
    return self().unchecked_rem(rhs);
  }

  [[nodiscard]] constexpr Derived wrapping_neg() const noexcept
  {
    return negate_bits(self());
  }

  template <int128_detail::shift_count T>
  [[nodiscard]] constexpr Derived wrapping_shl(T const rhs) const noexcept
  {
    return self().unchecked_shl(int128_detail::shift_count_mod_128(rhs));
  }

  template <int128_detail::shift_count T>
  [[nodiscard]] constexpr Derived wrapping_shr(T const rhs) const noexcept
  {
    return self().unchecked_shr(int128_detail::shift_count_mod_128(rhs));
  }

  [[nodiscard]] constexpr std::pair<Derived, bool> overflowing_add(Derived const rhs) const noexcept
  {
    Derived const result = wrapping_add(rhs);
    if constexpr (std::is_same_v<Derived, int128_t>) {
      return { result, is_negative() == rhs.is_negative() && result.is_negative() != is_negative() };
    } else {
      return { result, result < self() };
    }
  }

  [[nodiscard]] constexpr std::pair<Derived, bool> overflowing_sub(Derived const rhs) const noexcept
  {
    Derived const result = wrapping_sub(rhs);
    if constexpr (std::is_same_v<Derived, int128_t>) {
      return { result, is_negative() != rhs.is_negative() && result.is_negative() != is_negative() };
    } else {
      return { result, self() < rhs };
    }
  }

  [[nodiscard]] constexpr std::pair<Derived, bool> overflowing_mul(Derived const rhs) const noexcept
  {
    if constexpr (std::is_same_v<Derived, int128_t>) {
      // Multiply magnitudes so the raw two's-complement bits never leak into
      // the product; the sign is reapplied to the low 128 bits afterwards.
      bool const negative = is_negative() != rhs.is_negative();
      std::pair<Derived, Derived> const product { mul_full(unsigned_abs(), rhs.unsigned_abs()) };

      Derived const limit = negative ? Derived { 0, 0x80'00'00'00'00'00'00'00ULL }
                                     : Derived { ~0ULL, 0x7F'FF'FF'FF'FF'FF'FF'FFULL };

      bool const high_nonzero = (product.second.lo() | product.second.hi()) != 0;
      bool const overflow     = high_nonzero || base_type::less(limit, product.first);
      return { negative ? negate_bits(product.first) : product.first, overflow };
    } else {
      std::pair<Derived, Derived> const product = mul_full(self(), rhs);
      return { product.first, (product.second.lo() | product.second.hi()) != 0 };
    }
  }

  [[nodiscard]] constexpr std::pair<Derived, bool> overflowing_neg() const noexcept
  {
    if constexpr (std::is_same_v<Derived, int128_t>) {
      return { wrapping_neg(), equal_min() };
    } else {
      return { wrapping_neg(), !is_zero() };
    }
  }

  [[nodiscard]] constexpr std::pair<Derived, bool> overflowing_div(Derived const rhs) const noexcept
  {
    base_type::require_nonzero(rhs);
    if constexpr (std::is_same_v<Derived, int128_t>) {
      if (division_overflow(rhs)) {
        return { min_value(), true };
      }
    }
    return { unchecked_div(rhs), false };
  }

  [[nodiscard]] constexpr std::pair<Derived, bool> overflowing_rem(Derived const rhs) const noexcept
  {
    base_type::require_nonzero(rhs);
    if constexpr (std::is_same_v<Derived, int128_t>) {
      if (division_overflow(rhs)) {
        return { Derived {}, true };
      }
    }
    return { unchecked_rem(rhs), false };
  }

  template <int128_detail::shift_count T>
  [[nodiscard]] constexpr std::pair<Derived, bool> overflowing_shl(T const rhs) const noexcept
  {
    return { self().unchecked_shl(int128_detail::shift_count_mod_128(rhs)),
             int128_detail::shift_count_out_of_range(rhs) };
  }

  template <int128_detail::shift_count T>
  [[nodiscard]] constexpr std::pair<Derived, bool> overflowing_shr(T const rhs) const noexcept
  {
    return { self().unchecked_shr(int128_detail::shift_count_mod_128(rhs)),
             int128_detail::shift_count_out_of_range(rhs) };
  }

  /// \pre rhs < 128
  [[nodiscard]] constexpr Derived unchecked_shl(unsigned int const rhs) const noexcept
  {
    if (rhs == 0) {
      return self();
    }
#if STDEX_INT128_HAS_NATIVE
    value_type const shifted = value << rhs;
    return { static_cast<std::uint64_t>(shifted), static_cast<std::uint64_t>(shifted >> 64U) };
#else
#  if STDEX_INT128_HAS_MSVC_X64_INTRINSICS
    if (!std::is_constant_evaluated()) {
      if (rhs < 64U) [[likely]] {
        return { lo() << rhs, __shiftleft128(lo(), hi(), static_cast<unsigned char>(rhs)) };
      }
    }
#  endif
#  if STDEX_INT128_HAS_MSVC_ARM64_INTRINSICS
    if (!std::is_constant_evaluated()) {
      std::uint64_t const lane_data[2] { lo(), hi() };
      uint64x2_t const lanes = vld1q_u64(lane_data);
      if (rhs < 64U) {
        uint64x2_t const left  = vshlq_u64(lanes, vdupq_n_s64(static_cast<std::int64_t>(rhs)));
        uint64x2_t const cross = vshlq_u64(lanes, vdupq_n_s64(-(64 - static_cast<std::int64_t>(rhs))));
        return { vgetq_lane_u64(left, 0), vgetq_lane_u64(left, 1) | vgetq_lane_u64(cross, 0) };
      }
      uint64x2_t const left = vshlq_u64(lanes, vdupq_n_s64(static_cast<std::int64_t>(rhs) - 64));
      return { 0, vgetq_lane_u64(left, 0) };
    }
#  endif
    if (rhs >= 64U) {
      return { 0, lo() << (rhs - 64U) };
    }
    return { lo() << rhs, (hi() << rhs) | (lo() >> (64U - rhs)) };
#endif
  }

  /// \pre rhs < 128
  [[nodiscard]] constexpr Derived unchecked_shr(unsigned int const rhs) const noexcept
  {
    if (rhs == 0) {
      return self();
    }

    if constexpr (std::is_same_v<Derived, int128_t>) {
      // Arithmetic shift: propagate the sign bit instead of a logical zero.
      bool const negative = is_negative();
      if (rhs >= 64U) {
        std::uint64_t low = hi() >> (rhs - 64U);
        if (negative && rhs != 64U) {
          low |= ~0ULL << (128U - rhs);
        }
        std::uint64_t const high = negative ? ~0ULL : 0ULL;
        return { low, high };
      }
      std::uint64_t const high = (hi() >> rhs) | (negative ? (~0ULL << (64U - rhs)) : 0ULL);
      std::uint64_t const low  = (lo() >> rhs) | (hi() << (64U - rhs));
      return { low, high };
    } else {
#if STDEX_INT128_HAS_NATIVE
      value_type const shifted = value >> rhs;
      return { static_cast<std::uint64_t>(shifted), static_cast<std::uint64_t>(shifted >> 64U) };
#else
#  if STDEX_INT128_HAS_MSVC_X64_INTRINSICS
      if (!std::is_constant_evaluated()) {
        if (rhs < 64U) {
          return { __shiftright128(lo(), hi(), static_cast<unsigned char>(rhs)), hi() >> rhs };
        }
      }
#  endif
#  if STDEX_INT128_HAS_MSVC_ARM64_INTRINSICS
      if (!std::is_constant_evaluated()) {
        std::uint64_t const lane_data[2] { lo(), hi() };
        uint64x2_t const lanes = vld1q_u64(lane_data);
        if (rhs < 64U) {
          uint64x2_t const right = vshlq_u64(lanes, vdupq_n_s64(-static_cast<std::int64_t>(rhs)));
          uint64x2_t const cross = vshlq_u64(lanes, vdupq_n_s64(64 - static_cast<std::int64_t>(rhs)));
          return { vgetq_lane_u64(right, 0) | vgetq_lane_u64(cross, 1), vgetq_lane_u64(right, 1) };
        }
        uint64x2_t const right = vshlq_u64(lanes, vdupq_n_s64(-(static_cast<std::int64_t>(rhs) - 64)));
        return { vgetq_lane_u64(right, 1), 0 };
      }
#  endif
      if (rhs >= 64U) {
        return { hi() >> (rhs - 64U), 0 };
      }
      return { (lo() >> rhs) | (hi() << (64U - rhs)), hi() >> rhs };
#endif
    }
  }

  /// \pre rhs != 0
  [[nodiscard]] constexpr Derived unchecked_div(Derived const rhs) const noexcept
  {
    if constexpr (std::is_same_v<Derived, int128_t>) {
      Derived const quotient = divide_unsigned(unsigned_abs(), rhs.unsigned_abs());
      return (is_negative() != rhs.is_negative()) ? negate_bits(quotient) : quotient;
    } else {
      return divide_unsigned(self(), rhs);
    }
  }

  /// \pre rhs != 0
  [[nodiscard]] constexpr Derived unchecked_rem(Derived const rhs) const noexcept
  {
    if constexpr (std::is_same_v<Derived, int128_t>) {
      Derived const remainder = modulo_unsigned(unsigned_abs(), rhs.unsigned_abs());
      return is_negative() ? negate_bits(remainder) : remainder;
    } else {
      return modulo_unsigned(self(), rhs);
    }
  }

  /// \pre rhs != 0
  [[nodiscard]] constexpr divmod_result<Derived> unchecked_divmod(Derived const rhs) const noexcept
  {
    if constexpr (std::is_same_v<Derived, int128_t>) {
      divmod_result<Derived> dm = divmod_unsigned(unsigned_abs(), rhs.unsigned_abs());

      bool const quotient_negative  = is_negative() != rhs.is_negative();
      bool const remainder_negative = is_negative();

      if (quotient_negative) {
        dm.quotient = negate_bits(dm.quotient);
      }
      if (remainder_negative) {
        dm.remainder = negate_bits(dm.remainder);
      }
      return dm;
    } else {
      return divmod_unsigned(self(), rhs);
    }
  }

  [[nodiscard]] constexpr std::optional<Derived> checked_div(Derived const rhs) const noexcept
  {
    if (rhs.is_zero()) {
      return std::nullopt;
    }
    if constexpr (std::is_same_v<Derived, int128_t>) {
      if (division_overflow(rhs)) {
        return std::nullopt;
      }
    }
    return unchecked_div(rhs);
  }

  [[nodiscard]] constexpr std::optional<Derived> checked_rem(Derived const rhs) const noexcept
  {
    if (rhs.is_zero()) {
      return std::nullopt;
    }
    if constexpr (std::is_same_v<Derived, int128_t>) {
      if (division_overflow(rhs)) {
        return std::nullopt;
      }
    }
    return unchecked_rem(rhs);
  }

  [[nodiscard]] constexpr std::optional<divmod_result<Derived>> checked_divmod(Derived const rhs) const noexcept
  {
    if (rhs.is_zero()) {
      return std::nullopt;
    }
    if constexpr (std::is_same_v<Derived, int128_t>) {
      if (division_overflow(rhs)) {
        return std::nullopt;
      }
    }
    return unchecked_divmod(rhs);
  }

  [[nodiscard]] constexpr std::optional<Derived> checked_neg() const noexcept
  {
    if constexpr (std::is_same_v<Derived, int128_t>) {
      if (equal_min()) {
        return std::nullopt;
      }
      return wrapping_neg();
    } else {
      if (!is_zero()) {
        return std::nullopt;
      }
      return Derived {};
    }
  }

  [[nodiscard]] constexpr std::optional<Derived> checked_add(Derived const rhs) const noexcept
  {
    std::pair<Derived, bool> const result = self().overflowing_add(rhs);
    if (result.second) {
      return std::nullopt;
    }
    return result.first;
  }

  [[nodiscard]] constexpr std::optional<Derived> checked_sub(Derived const rhs) const noexcept
  {
    std::pair<Derived, bool> const result = self().overflowing_sub(rhs);
    if (result.second) {
      return std::nullopt;
    }
    return result.first;
  }

  [[nodiscard]] constexpr std::optional<Derived> checked_mul(Derived const rhs) const noexcept
  {
    std::pair<Derived, bool> const result = self().overflowing_mul(rhs);
    if (result.second) {
      return std::nullopt;
    }
    return result.first;
  }

  template <int128_detail::shift_count T>
  [[nodiscard]] constexpr std::optional<Derived> checked_shl(T const rhs) const noexcept
  {
    if (int128_detail::shift_count_out_of_range(rhs)) {
      return std::nullopt;
    }
    return self().unchecked_shl(int128_detail::shift_count_mod_128(rhs));
  }

  template <int128_detail::shift_count T>
  [[nodiscard]] constexpr std::optional<Derived> checked_shr(T const rhs) const noexcept
  {
    if (int128_detail::shift_count_out_of_range(rhs)) {
      return std::nullopt;
    }
    return self().unchecked_shr(int128_detail::shift_count_mod_128(rhs));
  }

  [[nodiscard]] constexpr Derived saturating_add(Derived const rhs) const noexcept
  {
    std::pair<Derived, bool> const result = overflowing_add(rhs);
    if (!result.second) {
      return result.first;
    }
    if constexpr (std::is_same_v<Derived, int128_t>) {
      return rhs.is_negative() ? min_value() : max_value();
    } else {
      return { ~0ULL, ~0ULL };
    }
  }

  [[nodiscard]] constexpr Derived saturating_sub(Derived const rhs) const noexcept
  {
    std::pair<Derived, bool> const result = overflowing_sub(rhs);
    if (!result.second) {
      return result.first;
    }
    if constexpr (std::is_same_v<Derived, int128_t>) {
      return rhs.is_negative() ? max_value() : min_value();
    } else {
      return {};
    }
  }

  [[nodiscard]] constexpr Derived saturating_mul(Derived const rhs) const noexcept
  {
    std::pair<Derived, bool> const result = overflowing_mul(rhs);
    if (!result.second) {
      return result.first;
    }
    if constexpr (std::is_same_v<Derived, int128_t>) {
      return (is_negative() != rhs.is_negative()) ? min_value() : max_value();
    } else {
      return { ~0ULL, ~0ULL };
    }
  }

  [[nodiscard]] constexpr Derived saturating_div(Derived const rhs) const noexcept
  {
    base_type::require_nonzero(rhs);
    if constexpr (std::is_same_v<Derived, int128_t>) {
      if (division_overflow(rhs)) {
        return max_value();
      }
    }
    return unchecked_div(rhs);
  }

  [[nodiscard]] constexpr Derived saturating_neg() const noexcept
  {
    if constexpr (std::is_same_v<Derived, int128_t>) {
      return equal_min() ? max_value() : wrapping_neg();
    } else {
      return {};
    }
  }

  [[nodiscard]] friend constexpr Derived operator& (Derived const lhs, Derived const rhs) noexcept
  {
    return Derived::from_words(lhs.lo() & rhs.lo(), lhs.hi() & rhs.hi());
  }

  [[nodiscard]] friend constexpr Derived operator| (Derived const lhs, Derived const rhs) noexcept
  {
    return Derived::from_words(lhs.lo() | rhs.lo(), lhs.hi() | rhs.hi());
  }

  [[nodiscard]] friend constexpr Derived operator^ (Derived const lhs, Derived const rhs) noexcept
  {
    return Derived::from_words(lhs.lo() ^ rhs.lo(), lhs.hi() ^ rhs.hi());
  }

  [[nodiscard]] friend constexpr Derived operator~ (Derived const value) noexcept
  {
    return Derived::from_words(~value.lo(), ~value.hi());
  }

  template <int128_detail::shift_count T>
  [[nodiscard]] friend constexpr Derived operator<< (Derived const value, T const count) noexcept
  {
    if constexpr (STDEX_INT128_HAS_OVERFLOW_CHECK) {
      if (int128_detail::shift_count_out_of_range(count)) [[unlikely]] {
        int128_detail::overflow_failure();
      }
    }
    return value.wrapping_shl(count);
  }

  template <int128_detail::shift_count T>
  [[nodiscard]] friend constexpr Derived operator>> (Derived const value, T const count) noexcept
  {
    if constexpr (STDEX_INT128_HAS_OVERFLOW_CHECK) {
      if (int128_detail::shift_count_out_of_range(count)) [[unlikely]] {
        int128_detail::overflow_failure();
      }
    }
    return value.wrapping_shr(count);
  }

  [[nodiscard]] constexpr Derived operator+ () const noexcept
  {
    return self();
  }

  constexpr Derived &operator+= (Derived const rhs) noexcept
  {
    self() = self() + rhs;
    return self();
  }

  constexpr Derived &operator-= (Derived const rhs) noexcept
  {
    self() = self() - rhs;
    return self();
  }

  constexpr Derived &operator*= (Derived const rhs) noexcept
  {
    self() = self() * rhs;
    return self();
  }

  constexpr Derived &operator/= (Derived const rhs) noexcept
  {
    self() = self() / rhs;
    return self();
  }

  constexpr Derived &operator%= (Derived const rhs) noexcept
  {
    self() = self() % rhs;
    return self();
  }

  constexpr Derived &operator&= (Derived const rhs) noexcept
  {
    self() = self() & rhs;
    return self();
  }

  constexpr Derived &operator|= (Derived const rhs) noexcept
  {
    self() = self() | rhs;
    return self();
  }

  constexpr Derived &operator^= (Derived const rhs) noexcept
  {
    self() = self() ^ rhs;
    return self();
  }

  template <int128_detail::shift_count T>
  constexpr Derived &operator<<= (T const count) noexcept
  {
    self() = self() << count;
    return self();
  }

  template <int128_detail::shift_count T>
  constexpr Derived &operator>>= (T const count) noexcept
  {
    self() = self() >> count;
    return self();
  }

  constexpr Derived &operator++ () noexcept
  {
    self() += Derived::from_u64(1);
    return self();
  }

  constexpr Derived operator++ (int) noexcept
  {
    Derived const copy = self();
    ++self();
    return copy;
  }

  constexpr Derived &operator-- () noexcept
  {
    self() -= Derived::from_u64(1);
    return self();
  }

  constexpr Derived operator-- (int) noexcept
  {
    Derived const copy = self();
    --self();
    return copy;
  }

  [[nodiscard]] constexpr bool equal_min() const noexcept
  {
    if constexpr (std::is_same_v<Derived, int128_t>) {
      return base_type::equal(self(), min_value());
    } else {
      return is_zero();
    }
  }
  [[nodiscard]] constexpr bool division_overflow(Derived const rhs) const noexcept
  {
    if constexpr (std::is_same_v<Derived, int128_t>) {
      return equal_min() && base_type::equal(rhs, minus_one());
    } else {
      return false;
    }
  }

private:
  [[nodiscard]] static constexpr Derived min_value() noexcept { return { 0, 0x80'00'00'00'00'00'00'00ULL }; }
  [[nodiscard]] static constexpr Derived max_value() noexcept { return { ~0ULL, 0x7F'FF'FF'FF'FF'FF'FF'FFULL }; }
  [[nodiscard]] static constexpr Derived minus_one() noexcept { return { ~0ULL, ~0ULL }; }

  [[nodiscard]] constexpr Derived const &self() const noexcept { return static_cast<Derived const &>(*this); }
  [[nodiscard]] constexpr Derived &self() noexcept { return static_cast<Derived &>(*this); }

  template <bool NeedQ, bool NeedR>
  [[nodiscard]] static constexpr divmod_result<Derived> divmod_unsigned_impl(Derived const dividend, Derived const divisor) noexcept
  {
    static_assert(NeedQ || NeedR);
    divmod_result<Derived> out {};
#if STDEX_INT128_HAS_NATIVE
    if constexpr (NeedQ) {
      value_type const q { dividend.value / divisor.value };
      out.quotient = { static_cast<std::uint64_t>(q), static_cast<std::uint64_t>(q >> 64U) };
    }
    if constexpr (NeedR) {
      value_type const r { dividend.value % divisor.value };
      out.remainder = { static_cast<std::uint64_t>(r), static_cast<std::uint64_t>(r >> 64U) };
    }
    return out;
#else
    if (divisor.hi() == 0) {
      std::uint64_t const den = divisor.lo();
      if (dividend.hi() == 0) {
        if constexpr (NeedQ) out.quotient = Derived::from_u64(dividend.lo() / den);
        if constexpr (NeedR) out.remainder = Derived::from_u64(dividend.lo() % den);
        return out;
      }
      [[maybe_unused]] std::uint64_t remainder {};
      std::uint64_t const quotient_low = udiv128(dividend.hi() % den, dividend.lo(), den, remainder);
      if constexpr (NeedQ) out.quotient = { quotient_low, dividend.hi() / den };
      if constexpr (NeedR) out.remainder = Derived::from_u64(remainder);
      return out;
    }

    if (dividend.hi() < divisor.hi() || (dividend.hi() == divisor.hi() && dividend.lo() < divisor.lo())) {
      if constexpr (NeedR) out.remainder = dividend;
      return out;
    }

    // Normalize so the divisor's top bit is set; the dividend grows a third digit.
    unsigned int const shift         = static_cast<unsigned int>(int128_detail::clz64_zero_unchecked(divisor.hi()));
    Derived const normalized_divisor = divisor.unchecked_shl(shift);
    std::uint64_t high_digit {};
    if (shift != 0) {
      high_digit = dividend.hi() >> (64U - shift);
    }
    Derived const normalized_dividend = dividend.unchecked_shl(shift);

    std::uint64_t const divisor_high    = normalized_divisor.hi();
    std::uint64_t const divisor_low     = normalized_divisor.lo();
    std::uint64_t const normalized_low  = normalized_dividend.lo();
    std::uint64_t const normalized_high = normalized_dividend.hi();

    // high_digit < divisor_high keeps the estimate in [1, 2^64); the normalized
    // dividend is not less than the divisor, so decrementing qhat cannot underflow.
    std::uint64_t remainder_estimate {};
    std::uint64_t qhat = udiv128(high_digit, normalized_high, divisor_high, remainder_estimate);

    std::uint64_t product_high {};
    std::uint64_t product_low {};
    for (;;) {
      product_low = umul128(qhat, divisor_low, product_high);
      bool const qhat_fits =
          product_high < remainder_estimate ||
          (product_high == remainder_estimate && product_low <= normalized_low);
      if (qhat_fits) break;
      --qhat;
      std::uint64_t const sum = remainder_estimate + divisor_high;
      if (sum < remainder_estimate) break;
      remainder_estimate = sum;
    }

    // The overflow break leaves product_high/product_low from the previous
    // candidate, so recompute them for the final qhat.
    product_low = umul128(qhat, divisor_low, product_high);

    std::uint64_t rem_low {};
    unsigned char borrow = int128_detail::subborrow64(0, normalized_low, product_low, rem_low);

    std::uint64_t product1_high {};
    std::uint64_t product1_low = umul128(qhat, divisor_high, product1_high);
    unsigned char const carry  = int128_detail::addcarry64(0, product1_low, product_high, product1_low);
    product1_high += carry;

    std::uint64_t rem_high {};
    borrow = int128_detail::subborrow64(borrow, normalized_high, product1_low, rem_high);

    [[maybe_unused]] std::uint64_t high_scratch {};
    borrow = int128_detail::subborrow64(borrow, high_digit, product1_high, high_scratch);

    // The loop invariant makes qhat admissible, so the borrow should be zero;
    // add the normalized divisor back once to stay correct if it is not.
    if (borrow != 0) [[unlikely]] {
      if constexpr (NeedQ) {
        --qhat;
      }
      if constexpr (NeedR) {
        unsigned char const c = int128_detail::addcarry64(0, rem_low, divisor_low, rem_low);
        (void) int128_detail::addcarry64(c, rem_high, divisor_high, rem_high);
      }
    }
    if constexpr (NeedQ) {
      out.quotient = { qhat, 0 };
    }
    if constexpr (NeedR) {
      Derived const normalized_remainder { rem_low, rem_high };
      out.remainder = logical_shr_bits(normalized_remainder, shift);
    }
    return out;
#endif
  }
};
} // namespace int128_detail

struct alignas(16) uint128_t : int128_detail::common128<uint128_t>
{
  using base_type   = int128_detail::base128;
  using common_type = int128_detail::common128<uint128_t>;
  using value_type  = base_type::value_type;
  using bytes_type  = base_type::bytes_type;

  constexpr uint128_t() noexcept : common_type {} {}
  using common_type::common_type;

  constexpr explicit uint128_t(base_type const &bits) noexcept : common_type(bits) {}

  /// Bit-preserving explicit conversion; the value is not range-checked.
  constexpr explicit uint128_t(int128_t const &bits) noexcept;

  /// --- comparison ----------------------------------------------------------
  [[nodiscard]] friend constexpr std::strong_ordering operator<=> (base_type const lhs, base_type const rhs) noexcept
  {
    if (lhs.hi() != rhs.hi()) {
      return lhs.hi() < rhs.hi() ? std::strong_ordering::less : std::strong_ordering::greater;
    }
    if (lhs.lo() != rhs.lo()) {
      return lhs.lo() < rhs.lo() ? std::strong_ordering::less : std::strong_ordering::greater;
    }
    return std::strong_ordering::equal;
  }

  /// --- operators ----------------------------------------------------------
  [[nodiscard]] friend constexpr uint128_t operator+ (base_type const lhs, base_type const rhs) noexcept
  {
    uint128_t const left { lhs };
    uint128_t const right { rhs };
    uint128_t const result = left.wrapping_add(right);
    if constexpr (STDEX_INT128_HAS_OVERFLOW_CHECK) {
      if (base_type::less(result, left)) [[unlikely]] {
        int128_detail::overflow_failure();
      }
    }
    return result;
  }

  [[nodiscard]] friend constexpr uint128_t operator- (base_type const lhs, base_type const rhs) noexcept
  {
    uint128_t const left { lhs };
    uint128_t const right { rhs };
    uint128_t const result = left.wrapping_sub(right);
    if constexpr (STDEX_INT128_HAS_OVERFLOW_CHECK) {
      if (base_type::less(left, right)) [[unlikely]] {
        int128_detail::overflow_failure();
      }
    }
    return result;
  }

  [[nodiscard]] friend constexpr uint128_t operator* (base_type const lhs, base_type const rhs) noexcept
  {
    uint128_t const left { lhs };
    uint128_t const right { rhs };
    if constexpr (STDEX_INT128_HAS_OVERFLOW_CHECK) {
      std::pair<uint128_t, bool> const result = left.overflowing_mul(right);
      if (result.second) [[unlikely]] {
        int128_detail::overflow_failure();
      }
      return result.first;
    } else {
      return left.wrapping_mul(right);
    }
  }

  [[nodiscard]] friend constexpr uint128_t operator& (base_type const lhs, base_type const rhs) noexcept
  {
    return { lhs.lo() & rhs.lo(), lhs.hi() & rhs.hi() };
  }

  [[nodiscard]] friend constexpr uint128_t operator| (base_type const lhs, base_type const rhs) noexcept
  {
    return { lhs.lo() | rhs.lo(), lhs.hi() | rhs.hi() };
  }

  [[nodiscard]] friend constexpr uint128_t operator^ (base_type const lhs, base_type const rhs) noexcept
  {
    return { lhs.lo() ^ rhs.lo(), lhs.hi() ^ rhs.hi() };
  }

  [[nodiscard]] friend constexpr uint128_t operator/ (base_type const lhs, base_type const rhs) noexcept
  {
    base_type::require_nonzero(rhs);
    return uint128_t { lhs }.unchecked_div(uint128_t { rhs });
  }

  [[nodiscard]] friend constexpr uint128_t operator% (base_type const lhs, base_type const rhs) noexcept
  {
    base_type::require_nonzero(rhs);
    return uint128_t { lhs }.unchecked_rem(uint128_t { rhs });
  }

  [[nodiscard]] constexpr uint128_t operator- () const noexcept
  {
    uint128_t const result = wrapping_neg();
    if constexpr (STDEX_INT128_HAS_OVERFLOW_CHECK) {
      if (!is_zero()) [[unlikely]] {
        int128_detail::overflow_failure();
      }
    }
    return result;
  }
};

static_assert(sizeof(uint128_t) == 16, "stdex::uint128_t must stay 16 bytes");
static_assert(alignof(uint128_t) == 16, "stdex::uint128_t must stay 16-byte aligned");
static_assert(std::is_trivially_copyable_v<uint128_t>);
static_assert(std::is_standard_layout_v<uint128_t>);

struct alignas(16) int128_t : int128_detail::common128<int128_t>
{
  using base_type   = int128_detail::base128;
  using common_type = int128_detail::common128<int128_t>;
  using value_type  = base_type::value_type;
  using bytes_type  = base_type::bytes_type;

  constexpr int128_t() noexcept : common_type {} {}
  using common_type::common_type;

  constexpr explicit int128_t(base_type const &bits) noexcept : common_type(bits) {}

  constexpr explicit int128_t(uint128_t const &bits) noexcept
      : common_type(static_cast<base_type const &>(bits))
  {
  }

  [[nodiscard]] friend constexpr std::strong_ordering operator<=> (int128_t const lhs, int128_t const rhs) noexcept
  {
    return common_type::compare(lhs, rhs);
  }

  [[nodiscard]] friend constexpr bool operator< (int128_t const lhs, int128_t const rhs) noexcept
  {
    return common_type::compare(lhs, rhs) == std::strong_ordering::less;
  }

  [[nodiscard]] friend constexpr bool operator> (int128_t const lhs, int128_t const rhs) noexcept
  {
    return common_type::compare(lhs, rhs) == std::strong_ordering::greater;
  }

  [[nodiscard]] friend constexpr bool operator<= (int128_t const lhs, int128_t const rhs) noexcept
  {
    return common_type::compare(lhs, rhs) != std::strong_ordering::greater;
  }

  [[nodiscard]] friend constexpr bool operator>= (int128_t const lhs, int128_t const rhs) noexcept
  {
    return common_type::compare(lhs, rhs) != std::strong_ordering::less;
  }

  [[nodiscard]] friend constexpr int128_t operator+ (int128_t const lhs, int128_t const rhs) noexcept
  {
    if constexpr (STDEX_INT128_HAS_OVERFLOW_CHECK) {
      std::pair<int128_t, bool> const result = lhs.overflowing_add(rhs);
      if (result.second) [[unlikely]] {
        int128_detail::overflow_failure();
      }
      return result.first;
    } else {
      return lhs.wrapping_add(rhs);
    }
  }

  [[nodiscard]] friend constexpr int128_t operator- (int128_t const lhs, int128_t const rhs) noexcept
  {
    if constexpr (STDEX_INT128_HAS_OVERFLOW_CHECK) {
      std::pair<int128_t, bool> const result = lhs.overflowing_sub(rhs);
      if (result.second) [[unlikely]] {
        int128_detail::overflow_failure();
      }
      return result.first;
    } else {
      return lhs.wrapping_sub(rhs);
    }
  }

  [[nodiscard]] friend constexpr int128_t operator* (int128_t const lhs, int128_t const rhs) noexcept
  {
    if constexpr (STDEX_INT128_HAS_OVERFLOW_CHECK) {
      std::pair<int128_t, bool> const result = lhs.overflowing_mul(rhs);
      if (result.second) [[unlikely]] {
        int128_detail::overflow_failure();
      }
      return result.first;
    } else {
      return lhs.wrapping_mul(rhs);
    }
  }

  [[nodiscard]] friend constexpr int128_t operator/ (int128_t const lhs, int128_t const rhs) noexcept
  {
    base_type::require_nonzero(rhs);
    if (lhs.division_overflow(rhs)) [[unlikely]] {
      int128_detail::overflow_failure();
    }
    return lhs.unchecked_div(rhs);
  }

  [[nodiscard]] friend constexpr int128_t operator% (int128_t const lhs, int128_t const rhs) noexcept
  {
    base_type::require_nonzero(rhs);
    if (lhs.division_overflow(rhs)) [[unlikely]] {
      int128_detail::overflow_failure();
    }
    return lhs.unchecked_rem(rhs);
  }

  [[nodiscard]] constexpr int128_t operator- () const noexcept
  {
    if constexpr (STDEX_INT128_HAS_OVERFLOW_CHECK) {
      if (equal_min()) [[unlikely]] {
        int128_detail::overflow_failure();
      }
    }
    return wrapping_neg();
  }
};

static_assert(sizeof(int128_t) == 16, "stdex::int128_t must stay 16 bytes");
static_assert(alignof(int128_t) == 16, "stdex::int128_t must stay 16-byte aligned");
static_assert(std::is_trivially_copyable_v<int128_t>);
static_assert(std::is_standard_layout_v<int128_t>);

constexpr uint128_t::uint128_t(int128_t const &bits) noexcept
    : common_type(static_cast<int128_detail::base128 const &>(bits))
{
}

[[nodiscard]] constexpr uint128_t uadd128(uint128_t const value, std::uint64_t const addend) noexcept
{
  return value.wrapping_add({ addend, 0 });
}

/// \pre divisor != 0
[[nodiscard]] constexpr uint128_t udiv128(uint128_t const dividend, std::uint64_t const divisor) noexcept
{
  return dividend.unchecked_div({ divisor, 0 });
}

[[nodiscard]] constexpr uint128_t umul128(std::uint64_t const lhs, std::uint64_t const rhs) noexcept
{
  std::uint64_t high {};
  std::uint64_t const low = umul128(lhs, rhs, high);
  return { low, high };
}

[[nodiscard]] constexpr uint128_t umul128_lo(uint128_t const value, std::uint64_t const multiplier) noexcept
{
  return value.wrapping_mul({ multiplier, 0 });
}

[[nodiscard]] constexpr uint128_t umul128_hi(uint128_t const value, std::uint64_t const multiplier) noexcept
{
  // High word of the 192-bit product: the low half's overflow column is added
  // into the high half's low word, and only the resulting carry out matters.
  std::uint64_t bottom_hi {};
  (void) umul128(value.lo(), multiplier, bottom_hi);

  std::uint64_t top_hi {};
  (void) umul128(value.hi(), multiplier, top_hi);
  std::uint64_t const top_lo = value.hi() * multiplier;
  return uadd128({ top_lo, top_hi }, bottom_hi);
}

[[nodiscard]] constexpr std::uint64_t umul128_xor_fold(std::uint64_t const lhs, std::uint64_t const rhs) noexcept
{
  uint128_t const product = umul128(lhs, rhs);
  return product.lo() ^ product.hi();
}

#define STDEX_INT128_DEFINE_MIXED(TYPE)                                                           \
  template <standard_integral_operand T>                                                          \
  [[nodiscard]] constexpr TYPE operator+ (TYPE const lhs, T const rhs) noexcept                   \
  {                                                                                               \
    return lhs + TYPE::from_integral(rhs);                                                        \
  }                                                                                               \
  template <standard_integral_operand T>                                                          \
  [[nodiscard]] constexpr TYPE operator+ (T const lhs, TYPE const rhs) noexcept                   \
  {                                                                                               \
    return TYPE::from_integral(lhs) + rhs;                                                        \
  }                                                                                               \
  template <standard_integral_operand T>                                                          \
  [[nodiscard]] constexpr TYPE operator- (TYPE const lhs, T const rhs) noexcept                   \
  {                                                                                               \
    return lhs - TYPE::from_integral(rhs);                                                        \
  }                                                                                               \
  template <standard_integral_operand T>                                                          \
  [[nodiscard]] constexpr TYPE operator- (T const lhs, TYPE const rhs) noexcept                   \
  {                                                                                               \
    return TYPE::from_integral(lhs) - rhs;                                                        \
  }                                                                                               \
  template <standard_integral_operand T>                                                          \
  [[nodiscard]] constexpr TYPE operator* (TYPE const lhs, T const rhs) noexcept                   \
  {                                                                                               \
    return lhs * TYPE::from_integral(rhs);                                                        \
  }                                                                                               \
  template <standard_integral_operand T>                                                          \
  [[nodiscard]] constexpr TYPE operator* (T const lhs, TYPE const rhs) noexcept                   \
  {                                                                                               \
    return TYPE::from_integral(lhs) * rhs;                                                        \
  }                                                                                               \
  template <standard_integral_operand T>                                                          \
  [[nodiscard]] constexpr TYPE operator/ (TYPE const lhs, T const rhs) noexcept                   \
  {                                                                                               \
    return lhs / TYPE::from_integral(rhs);                                                        \
  }                                                                                               \
  template <standard_integral_operand T>                                                          \
  [[nodiscard]] constexpr TYPE operator/ (T const lhs, TYPE const rhs) noexcept                   \
  {                                                                                               \
    return TYPE::from_integral(lhs) / rhs;                                                        \
  }                                                                                               \
  template <standard_integral_operand T>                                                          \
  [[nodiscard]] constexpr TYPE operator% (TYPE const lhs, T const rhs) noexcept                   \
  {                                                                                               \
    return lhs % TYPE::from_integral(rhs);                                                        \
  }                                                                                               \
  template <standard_integral_operand T>                                                          \
  [[nodiscard]] constexpr TYPE operator% (T const lhs, TYPE const rhs) noexcept                   \
  {                                                                                               \
    return TYPE::from_integral(lhs) % rhs;                                                        \
  }                                                                                               \
  template <standard_integral_operand T>                                                          \
  [[nodiscard]] constexpr bool operator== (TYPE const lhs, T const rhs) noexcept                  \
  {                                                                                               \
    return lhs == TYPE::from_integral(rhs);                                                       \
  }                                                                                               \
  template <standard_integral_operand T>                                                          \
  [[nodiscard]] constexpr bool operator== (T const lhs, TYPE const rhs) noexcept                  \
  {                                                                                               \
    return TYPE::from_integral(lhs) == rhs;                                                       \
  }                                                                                               \
  template <standard_integral_operand T>                                                          \
  [[nodiscard]] constexpr bool operator!= (TYPE const lhs, T const rhs) noexcept                  \
  {                                                                                               \
    return !(lhs == rhs);                                                                         \
  }                                                                                               \
  template <standard_integral_operand T>                                                          \
  [[nodiscard]] constexpr bool operator!= (T const lhs, TYPE const rhs) noexcept                  \
  {                                                                                               \
    return !(lhs == rhs);                                                                         \
  }                                                                                               \
  template <standard_integral_operand T>                                                          \
  [[nodiscard]] constexpr std::strong_ordering operator<=> (TYPE const lhs, T const rhs) noexcept \
  {                                                                                               \
    return lhs <=> TYPE::from_integral(rhs);                                                      \
  }                                                                                               \
  template <standard_integral_operand T>                                                          \
  [[nodiscard]] constexpr std::strong_ordering operator<=> (T const lhs, TYPE const rhs) noexcept \
  {                                                                                               \
    return TYPE::from_integral(lhs) <=> rhs;                                                      \
  }

STDEX_INT128_DEFINE_MIXED(uint128_t)
STDEX_INT128_DEFINE_MIXED(int128_t)

template <class T>
requires std::signed_integral<T> ||
         std::floating_point<T> ||
         std::same_as<std::remove_cvref_t<T>, int128_t>
[[nodiscard]] constexpr auto abs(T const value) noexcept
{
  if constexpr (std::same_as<std::remove_cvref_t<T>, int128_t>) {
    return uint128_t { value.unsigned_abs() };
  } else {
    return std::abs(value);
  }
}

namespace int128_detail
{
[[nodiscard]] constexpr uint128_t gcd_unsigned(uint128_t a, uint128_t b) noexcept
{
  while (!b.is_zero()) {
    uint128_t const remainder { a % b };
    a = b;
    b = remainder;
  }
  return a;
}

template <class T>
[[nodiscard]] constexpr uint128_t unsigned_magnitude(T const value) noexcept
{
  using value_type = std::remove_cvref_t<T>;
  if constexpr (std::same_as<value_type, uint128_t>) {
    return value;
  } else if constexpr (std::same_as<value_type, int128_t>) {
    return uint128_t { value.unsigned_abs() };
  } else {
    using unsigned_type = std::make_unsigned_t<value_type>;
    if constexpr (std::is_signed_v<value_type>) {
      if (value < 0) {
        return uint128_t::from_integral(static_cast<unsigned_type>(unsigned_type { 0 } - static_cast<unsigned_type>(value)));
      }
    }
    return uint128_t::from_integral(static_cast<unsigned_type>(value));
  }
}

template <class T>
[[nodiscard]] constexpr T from_unsigned_magnitude(uint128_t const value) noexcept
{
  if constexpr (std::same_as<T, uint128_t>) {
    return value;
  } else {
    static_assert(std::same_as<T, int128_t>);
    if (value.hi() > 0x7F'FF'FF'FF'FF'FF'FF'FFULL) {
      precondition_failure();
    }
    return T { value };
  }
}

template <class T>
[[nodiscard]] constexpr T from_signed_magnitude(uint128_t const magnitude, bool const negative) noexcept
{
  if constexpr (std::same_as<T, uint128_t>) {
    return magnitude;
  } else {
    static_assert(std::same_as<T, int128_t>);
    if (negative) {
      if (magnitude.hi() > 0x80'00'00'00'00'00'00'00ULL ||
          (magnitude.hi() == 0x80'00'00'00'00'00'00'00ULL && magnitude.lo() != 0)) {
        return T {};
      }
      return T { magnitude }.wrapping_neg();
    }
    if (magnitude.hi() > 0x7F'FF'FF'FF'FF'FF'FF'FFULL) {
      return T {};
    }
    return T { magnitude };
  }
}

template <class T>
[[nodiscard]] constexpr bool exponent_is_zero(T const exponent) noexcept
{
  if constexpr (is_int128_v<std::remove_cvref_t<T>>) {
    return exponent.is_zero();
  } else {
    return exponent == 0;
  }
}

template <class T>
[[nodiscard]] constexpr bool exponent_is_negative(T const exponent) noexcept
{
  using value_type = std::remove_cvref_t<T>;
  if constexpr (std::same_as<value_type, int128_t>) {
    return exponent.is_negative();
  } else if constexpr (std::is_signed_v<value_type>) {
    return exponent < 0;
  } else {
    return false;
  }
}

template <class T>
[[nodiscard]] constexpr bool exponent_is_odd(T const exponent) noexcept
{
  if constexpr (is_int128_v<std::remove_cvref_t<T>>) {
    return (exponent.lo() & 1U) != 0;
  } else {
    using unsigned_type = std::make_unsigned_t<std::remove_cvref_t<T>>;
    return (static_cast<unsigned_type>(exponent) & 1U) != 0;
  }
}
} // namespace int128_detail

/// Greatest common divisor.
/// \pre The mathematical gcd is representable as a value of
///      `std::common_type_t<T, U>`. For an `int128_t` result this excludes
///      2^127, so e.g. `gcd(INT128_MIN, 0)` and `gcd(INT128_MIN, INT128_MIN)`
///      violate the precondition.
/// \note A violated precondition aborts at runtime (after an `assert` in debug
///       builds) and is not a constant expression during constant evaluation.
template <class T, class U>
requires extended_integral_operand<T> && extended_integral_operand<U>
[[nodiscard]] constexpr std::common_type_t<T, U> gcd(T const a, U const b) noexcept
{
  using result_type = std::common_type_t<T, U>;
  if constexpr (is_int128_v<std::remove_cvref_t<T>> || is_int128_v<std::remove_cvref_t<U>>) {
    uint128_t const lhs = int128_detail::unsigned_magnitude(a);
    uint128_t const rhs = int128_detail::unsigned_magnitude(b);
    return int128_detail::from_unsigned_magnitude<result_type>(int128_detail::gcd_unsigned(lhs, rhs));
  } else {
    return std::gcd(a, b);
  }
}

/// Least common multiple.
/// \pre The mathematical lcm is representable as a value of
///      `std::common_type_t<T, U>` without overflowing 128 bits. For an
///      `int128_t` result values above `INT128_MAX` (including 2^127, e.g.
///      `lcm(INT128_MIN, 1)`) violate the precondition.
/// \note A violated precondition aborts at runtime (after an `assert` in debug
///       builds) and is not a constant expression during constant evaluation.
template <class T, class U>
requires extended_integral_operand<T> && extended_integral_operand<U>
[[nodiscard]] constexpr std::common_type_t<T, U> lcm(T const a, U const b) noexcept
{
  using result_type = std::common_type_t<T, U>;
  if constexpr (is_int128_v<std::remove_cvref_t<T>> || is_int128_v<std::remove_cvref_t<U>>) {
    uint128_t const lhs = int128_detail::unsigned_magnitude(a);
    uint128_t const rhs = int128_detail::unsigned_magnitude(b);
    if (lhs.is_zero() || rhs.is_zero()) {
      return result_type {};
    }

    uint128_t const divisor = int128_detail::gcd_unsigned(lhs, rhs);
    uint128_t const quotient = lhs / divisor;
    std::pair<uint128_t, bool> const product = quotient.overflowing_mul(rhs);
    if (product.second) {
      int128_detail::precondition_failure();
    }
    return int128_detail::from_unsigned_magnitude<result_type>(product.first);
  } else {
    return std::lcm(a, b);
  }
}

template <class T>
requires (!std::is_reference_v<T>) &&
         (std::is_arithmetic_v<T> || is_int128_v<std::remove_cvref_t<T>>) &&
         (!std::same_as<std::remove_cv_t<T>, bool>)
[[nodiscard]] constexpr T midpoint(T const a, T const b) noexcept
{
  if constexpr (is_int128_v<std::remove_cvref_t<T>>) {
    uint128_t const lhs = uint128_t { a };
    uint128_t const rhs = uint128_t { b };
    uint128_t const difference = rhs.wrapping_sub(lhs);
    uint128_t const sign_bit = (b < a) ? uint128_t::from_u64(1) : uint128_t {};
    uint128_t half_difference = difference >> 1U;
    half_difference = half_difference.wrapping_add(sign_bit << 127U);
    half_difference = half_difference.wrapping_add(sign_bit & difference);
    return T { lhs.wrapping_add(half_difference) };
  } else {
    return std::midpoint(a, b);
  }
}

template <class T, class U>
requires (!std::is_reference_v<T>) &&
         is_int128_v<std::remove_cvref_t<T>> &&
         extended_integral_operand<U>
[[nodiscard]] constexpr T pow(T const base, U const exponent) noexcept
{
  if (base == T::from_u64(1)) {
    return base;
  }
  if constexpr (std::same_as<std::remove_cvref_t<T>, int128_t>) {
    if (base == int128_t::from_i64(-1)) {
      return int128_detail::exponent_is_odd(exponent) ? base : int128_t::from_u64(1);
    }
  }

  if (int128_detail::exponent_is_negative(exponent)) {
    return T {};
  }
  if (int128_detail::exponent_is_zero(exponent)) {
    return T::from_u64(1);
  }
  if (base.is_zero()) {
    return T {};
  }

  uint128_t result = uint128_t::from_u64(1);
  uint128_t factor = int128_detail::unsigned_magnitude(base);
  uint128_t power  = int128_detail::unsigned_magnitude(exponent);
  while (!power.is_zero()) {
    if ((power.lo() & 1U) != 0) {
      std::pair<uint128_t, bool> const product = result.overflowing_mul(factor);
      if (product.second) {
        return T {};
      }
      result = product.first;
    }
    power = power >> 1U;
    if (!power.is_zero()) {
      std::pair<uint128_t, bool> const square = factor.overflowing_mul(factor);
      if (square.second) {
        return T {};
      }
      factor = square.first;
    }
  }

  bool const negative_result = base.is_negative() && int128_detail::exponent_is_odd(exponent);
  return int128_detail::from_signed_magnitude<T>(result, negative_result);
}

template <class T, class U>
requires std::is_arithmetic_v<T> && std::is_arithmetic_v<U>
[[nodiscard]] constexpr auto pow(T const base, U const exponent) noexcept
{
  return std::pow(base, exponent);
}

namespace int128_detail
{
enum class int128_align : std::uint8_t
{
  none,
  left,
  right,
  center
};

enum class int128_sign : std::uint8_t
{
  none,
  plus,
  minus,
  space
};

template <class CharT>
[[nodiscard]] constexpr bool is_ascii_digit(CharT const character) noexcept
{
  return character >= static_cast<CharT>('0') && character <= static_cast<CharT>('9');
}

template <class CharT>
[[nodiscard]] constexpr int128_align parse_align(CharT const character) noexcept
{
  if (character == static_cast<CharT>('<')) {
    return int128_align::left;
  }
  if (character == static_cast<CharT>('>')) {
    return int128_align::right;
  }
  if (character == static_cast<CharT>('^')) {
    return int128_align::center;
  }
  return int128_align::none;
}

inline char *append_u64(
  char *output, std::uint64_t const value, int const base, bool const uppercase, unsigned const min_digits)
{
  char buffer[65];
  auto const result = std::to_chars(buffer, buffer + sizeof(buffer), value, base);
  assert(result.ec == std::errc {});

  auto const length = static_cast<unsigned>(result.ptr - buffer);
  for (unsigned index = length; index < min_digits; ++index) {
    *output++ = '0';
  }

  for (char const *cursor = buffer; cursor != result.ptr; ++cursor) {
    char digit = *cursor;
    if (uppercase && digit >= 'a' && digit <= 'z') {
      digit = static_cast<char>(digit - 'a' + 'A');
    }
    *output++ = digit;
  }
  return output;
}

inline char *append_decimal(char *output, uint128_t const value)
{
  if (value.hi() == 0) {
    return append_u64(output, value.lo(), 10, false, 0);
  }

  constexpr std::uint64_t chunk_base = 10'000'000'000'000'000'000ULL;
  std::uint64_t chunks[3] {};
  unsigned count      = 0;
  uint128_t remaining = value;
  while (!remaining.is_zero()) {
    auto const part = uint128_t::divmod_unsigned(remaining, uint128_t::from_u64(chunk_base));
    chunks[count]   = static_cast<std::uint64_t>(part.remainder);
    ++count;
    remaining = part.quotient;
  }

  for (unsigned index = count; index-- > 0;) {
    output = append_u64(output, chunks[index], 10, false, index + 1 == count ? 0U : 19U);
  }
  return output;
}

inline char *append_binary(char *output, uint128_t const value)
{
  if (value.hi() != 0) {
    output = append_u64(output, value.hi(), 2, false, 0);
    output = append_u64(output, value.lo(), 2, false, 64);
  } else {
    output = append_u64(output, value.lo(), 2, false, 0);
  }
  return output;
}

inline char *append_hex(char *output, uint128_t const value, bool const uppercase)
{
  if (value.hi() != 0) {
    output = append_u64(output, value.hi(), 16, uppercase, 0);
    output = append_u64(output, value.lo(), 16, uppercase, 16);
  } else {
    output = append_u64(output, value.lo(), 16, uppercase, 0);
  }
  return output;
}

inline char *append_octal(char *output, uint128_t const value)
{
  if (value.hi() == 0) {
    return append_u64(output, value.lo(), 8, false, 0);
  }

  constexpr std::uint64_t mask60 = (1ULL << 60U) - 1U;
  std::uint64_t const chunks[3] {
    value.lo() & mask60,
    ((value.lo() >> 60U) | ((value.hi() & ((1ULL << 56U) - 1U)) << 4U)) & mask60,
    value.hi() >> 56U
  };

  bool started = false;
  for (unsigned index = 3; index-- > 0;) {
    if (!started && chunks[index] == 0 && index != 0) {
      continue;
    }
    output = append_u64(output, chunks[index], 8, false, started ? 20U : 0U);
    started = true;
  }
  return output;
}

inline int grouping_size(std::string const &grouping, std::size_t const index) noexcept
{
  assert(!grouping.empty());

  char const value = grouping[index < grouping.size() ? index : grouping.size() - 1];
  int const size   = static_cast<int>(value);
  if (size <= 0 || size == CHAR_MAX) {
    return 0;
  }
  return size;
}

template <class T>
[[nodiscard]] uint128_t int128_magnitude(T const value) noexcept
{
  if constexpr (std::is_same_v<T, uint128_t>) {
    return value;
  } else {
    return uint128_t { value.unsigned_abs() };
  }
}

template <class T>
[[nodiscard]] bool int128_negative(T const value) noexcept
{
  if constexpr (std::is_same_v<T, int128_t>) {
    return value.is_negative();
  } else {
    return false;
  }
}

template <class T, class CharT>
[[nodiscard]] CharT checked_char(T const value)
{
  constexpr std::uint64_t max_value =
      static_cast<std::uint64_t>(std::numeric_limits<CharT>::max());
  if constexpr (std::is_same_v<T, uint128_t>) {
    if (value.hi() != 0 || value.lo() > max_value) {
      throw std::format_error("integral cannot be stored in char");
    }
    return static_cast<CharT>(value.lo());
  } else {
    if (!value.is_negative()) {
      if (value.hi() != 0 || value.lo() > max_value) {
        throw std::format_error("integral cannot be stored in char");
      }
      return static_cast<CharT>(value.lo());
    }

    uint128_t const magnitude = uint128_t { value.unsigned_abs() };
    constexpr std::uint64_t min_magnitude =
        static_cast<std::uint64_t>(-static_cast<std::int64_t>(std::numeric_limits<CharT>::min()));
    if (magnitude.hi() != 0 || magnitude.lo() > min_magnitude) {
      throw std::format_error("integral cannot be stored in char");
    }
    return static_cast<CharT>(-static_cast<std::int64_t>(magnitude.lo()));
  }
}

template <class T, class CharT>
struct int128_formatter
{
  static constexpr std::size_t max_digits = 128;

  constexpr auto parse(std::basic_format_parse_context<CharT> &context)
  {
    auto iterator  = context.begin();
    auto const end = context.end();

    if (iterator != end && *iterator != static_cast<CharT>('}')) {
      auto next = iterator;
      ++next;
      if (next != end && parse_align(*next) != int128_align::none) {
        if (*iterator == static_cast<CharT>('{') || *iterator == static_cast<CharT>('}')) {
          throw std::format_error("invalid fill character");
        }
        fill_      = *iterator;
        alignment_ = parse_align(*next);
        iterator   = next;
        ++iterator;
      } else {
        int128_align const alignment = parse_align(*iterator);
        if (alignment != int128_align::none) {
          alignment_ = alignment;
          ++iterator;
        }
      }
    }

    if (iterator != end) {
      if (*iterator == static_cast<CharT>('+')) {
        sign_ = int128_sign::plus;
        ++iterator;
      } else if (*iterator == static_cast<CharT>('-')) {
        sign_ = int128_sign::minus;
        ++iterator;
      } else if (*iterator == static_cast<CharT>(' ')) {
        sign_ = int128_sign::space;
        ++iterator;
      }
    }

    if (iterator != end && *iterator == static_cast<CharT>('#')) {
      alternate_ = true;
      ++iterator;
    }

    if (iterator != end && *iterator == static_cast<CharT>('0')) {
      leading_zero_ = true;
      ++iterator;
    }

    if (iterator != end && is_ascii_digit(*iterator)) {
      width_ = parse_digits(iterator, end, "width is too large");
    } else if (iterator != end && *iterator == static_cast<CharT>('{')) {
      ++iterator;
      if (iterator == end) {
        throw std::format_error("invalid dynamic width");
      }

      if (*iterator == static_cast<CharT>('}')) {
        auto const arg_id = context.next_arg_id();
        if (arg_id > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
          throw std::format_error("argument id is too large");
        }
        width_arg_id_ = static_cast<int>(arg_id);
        ++iterator;
      } else {
        if (!is_ascii_digit(*iterator)) {
          throw std::format_error("invalid dynamic width");
        }

        int const arg_id = parse_digits(iterator, end, "argument id is too large");
        if (iterator == end || *iterator != static_cast<CharT>('}')) {
          throw std::format_error("invalid dynamic width");
        }
        context.check_arg_id(static_cast<std::size_t>(arg_id));
        width_arg_id_ = arg_id;
        ++iterator;
      }
    }

    if (iterator != end && *iterator == static_cast<CharT>('.')) {
      throw std::format_error("precision not allowed for integer format");
    }

    if (iterator != end && *iterator == static_cast<CharT>('L')) {
      localized_ = true;
      ++iterator;
    }

    if (iterator != end && *iterator != static_cast<CharT>('}')) {
      CharT const type = *iterator;
      if (type == static_cast<CharT>('b')) {
        type_ = 'b';
      } else if (type == static_cast<CharT>('B')) {
        type_ = 'B';
      } else if (type == static_cast<CharT>('c')) {
        type_ = 'c';
      } else if (type == static_cast<CharT>('d')) {
        type_ = 'd';
      } else if (type == static_cast<CharT>('o')) {
        type_ = 'o';
      } else if (type == static_cast<CharT>('x')) {
        type_ = 'x';
      } else if (type == static_cast<CharT>('X')) {
        type_ = 'X';
      } else {
        throw std::format_error("invalid presentation type specifier");
      }
      ++iterator;
    }

    if (iterator != end && *iterator != static_cast<CharT>('}')) {
      throw std::format_error("invalid format specifier");
    }

    if (type_ == 'c' &&
        (sign_ != int128_sign::none || alternate_ || leading_zero_)) {
      throw std::format_error("invalid format specifier for char");
    }

    return iterator;
  }

  template <class FormatContext>
  auto format(T const &value, FormatContext &context) const -> typename FormatContext::iterator
  {
    static_assert(std::is_same_v<typename FormatContext::char_type, CharT>);

    int const width = width_arg_id_ >= 0 ? width_from_context(context, width_arg_id_) : width_;

    if (type_ == 'c') {
      CharT const character         = checked_char<T, CharT>(value);
      std::size_t const padding     = width > 1 ? static_cast<std::size_t>(width) - 1 : 0;
      int128_align const alignment  = alignment_ == int128_align::none ? int128_align::left : alignment_;
      std::size_t left_padding      = 0;
      if (alignment == int128_align::right) {
        left_padding = padding;
      } else if (alignment == int128_align::center) {
        left_padding = padding / 2;
      }

      auto output = context.out();
      output      = write_fill(output, left_padding, fill_);
      *output++   = character;
      return write_fill(output, padding - left_padding, fill_);
    }

    bool const negative       = int128_negative(value);
    uint128_t const magnitude = int128_magnitude(value);
    char digits[max_digits];
    char *digits_end = digits;
    switch (type_) {
    case 'b' :
    case 'B' :
      digits_end = append_binary(digits, magnitude);
      break;
    case 'o' :
      digits_end = append_octal(digits, magnitude);
      break;
    case 'x' :
    case 'X' :
      digits_end = append_hex(digits, magnitude, type_ == 'X');
      break;
    case 'd' :
    default :
      digits_end = append_decimal(digits, magnitude);
      break;
    }
    std::size_t const digits_size = static_cast<std::size_t>(digits_end - digits);

    char prefix[3];
    std::size_t prefix_size = 0;
    if (negative) {
      prefix[prefix_size++] = '-';
    } else if (sign_ == int128_sign::plus) {
      prefix[prefix_size++] = '+';
    } else if (sign_ == int128_sign::space) {
      prefix[prefix_size++] = ' ';
    }

    if (alternate_) {
      switch (type_) {
      case 'b' :
        prefix[prefix_size++] = '0';
        prefix[prefix_size++] = 'b';
        break;
      case 'B' :
        prefix[prefix_size++] = '0';
        prefix[prefix_size++] = 'B';
        break;
      case 'o' :
        if (!magnitude.is_zero()) {
          prefix[prefix_size++] = '0';
        }
        break;
      case 'x' :
        prefix[prefix_size++] = '0';
        prefix[prefix_size++] = 'x';
        break;
      case 'X' :
        prefix[prefix_size++] = '0';
        prefix[prefix_size++] = 'X';
        break;
      default :
        break;
      }
    }

    unsigned char group_sizes[max_digits];
    std::size_t group_count = 0;
    CharT separator {};
    if (localized_) {
      if constexpr (std::is_same_v<CharT, char> || std::is_same_v<CharT, wchar_t>) {
        std::locale const locale   = context.locale();
        auto const &facet          = std::use_facet<std::numpunct<CharT>>(locale);
        std::string const grouping = facet.grouping();
        if (!grouping.empty()) {
          group_count = make_group_sizes(grouping, digits_size, group_sizes);
          separator   = facet.thousands_sep();
        }
      }
    }

    std::size_t const separator_count = group_count == 0 ? 0 : group_count - 1;
    std::size_t const content_size    = prefix_size + digits_size + separator_count;

    std::size_t zero_padding = 0;
    std::size_t fill_padding = 0;
    if (width > 0) {
      std::size_t const requested = static_cast<std::size_t>(width);
      if (requested > content_size) {
        if (leading_zero_ && alignment_ == int128_align::none) {
          zero_padding = requested - content_size;
        } else {
          fill_padding = requested - content_size;
        }
      }
    }

    int128_align const alignment = alignment_ == int128_align::none ? int128_align::right : alignment_;
    std::size_t left_padding = 0;
    if (alignment == int128_align::right) {
      left_padding = fill_padding;
    } else if (alignment == int128_align::center) {
      left_padding = fill_padding / 2;
    }

    auto output = context.out();
    output      = write_fill(output, left_padding, fill_);
    for (std::size_t index = 0; index < prefix_size; ++index) {
      *output++ = static_cast<CharT>(prefix[index]);
    }
    output = write_fill(output, zero_padding, static_cast<CharT>('0'));
    if (group_count == 0) {
      output = write_digits(output, digits, digits_size);
    } else {
      output = write_grouped(output, digits, group_sizes, group_count, separator);
    }
    return write_fill(output, fill_padding - left_padding, fill_);
  }

private:
  template <class FormatContext>
  [[nodiscard]] static int width_from_context(FormatContext &context, int const arg_id)
  {
    auto arg = context.arg(static_cast<std::size_t>(arg_id));
    if (!arg) {
      throw std::format_error("argument not found");
    }

    int width = 0;
    std::visit_format_arg(
        [&](auto const &value) {
      using value_type = std::remove_cvref_t<decltype(value)>;
      if constexpr (std::is_same_v<value_type, int> ||
                    std::is_same_v<value_type, unsigned int> ||
                    std::is_same_v<value_type, long long> ||
                    std::is_same_v<value_type, unsigned long long>) {
        if constexpr (std::is_signed_v<value_type>) {
          if (value < 0) {
            throw std::format_error("negative width");
          }
        }
        using unsigned_type  = std::make_unsigned_t<value_type>;
        auto const magnitude = static_cast<unsigned_type>(value);
        if (magnitude > static_cast<unsigned_type>(std::numeric_limits<int>::max())) {
          throw std::format_error("width is too large");
        }
        width = static_cast<int>(magnitude);
      } else {
        throw std::format_error("width is not an integer");
      }
    },
        arg);
    return width;
  }

  template <class Output>
  [[nodiscard]] static Output write_fill(Output output, std::size_t const count, CharT const character)
  {
    for (std::size_t index = 0; index < count; ++index) {
      *output++ = character;
    }
    return output;
  }

  template <class Output>
  [[nodiscard]] static Output write_digits(Output output, char const *digits, std::size_t const count)
  {
    for (std::size_t index = 0; index < count; ++index) {
      *output++ = static_cast<CharT>(digits[index]);
    }
    return output;
  }

  template <class Output>
  [[nodiscard]] static Output write_grouped(
    Output output, char const *digits, unsigned char const *group_sizes, std::size_t const group_count, CharT const separator)
  {
    std::size_t position = 0;
    std::size_t index    = group_count;
    while (index != 0) {
      --index;
      std::size_t const size = group_sizes[index];
      output                 = write_digits(output, digits + position, size);
      position += size;
      if (index != 0) {
        *output++ = separator;
      }
    }
    return output;
  }

  [[nodiscard]] static std::size_t make_group_sizes(
    std::string const &grouping, std::size_t remaining, unsigned char *group_sizes) noexcept
  {
    std::size_t count = 0;
    std::size_t index = 0;
    while (remaining != 0) {
      int const size = grouping_size(grouping, index);
      if (size == 0 || static_cast<std::size_t>(size) >= remaining) {
        group_sizes[count++] = static_cast<unsigned char>(remaining);
        break;
      }
      group_sizes[count++] = static_cast<unsigned char>(size);
      remaining -= static_cast<std::size_t>(size);
      ++index;
    }
    return count;
  }

  template <class Iterator>
  static constexpr int parse_digits(Iterator &iterator, Iterator const &end, char const *error_message)
  {
    int value = 0;
    while (iterator != end && is_ascii_digit(*iterator)) {
      int const digit = static_cast<int>(*iterator - static_cast<CharT>('0'));
      if (value > (std::numeric_limits<int>::max() - digit) / 10) {
        throw std::format_error(error_message);
      }
      value = value * 10 + digit;
      ++iterator;
    }
    return value;
  }

  int width_              = 0;
  int width_arg_id_       = -1;
  CharT fill_             = static_cast<CharT>(' ');
  bool alternate_         = false;
  bool leading_zero_      = false;
  bool localized_         = false;
  char type_              = 'd';
  int128_align alignment_ = int128_align::none;
  int128_sign sign_       = int128_sign::none;
};
} // namespace int128_detail

} // namespace stdex

namespace std
{
template <class CharT>
struct formatter<stdex::uint128_t, CharT>
    : stdex::int128_detail::int128_formatter<stdex::uint128_t, CharT>
{
};

template <class CharT>
struct formatter<stdex::int128_t, CharT>
    : stdex::int128_detail::int128_formatter<stdex::int128_t, CharT>
{
};

#if defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable : 4996) // numeric_limits::has_denorm is deprecated in C++23
#endif

template <>
struct numeric_limits<stdex::uint128_t>
{
  static constexpr bool is_specialized           = true;
  static constexpr bool is_signed                = false;
  static constexpr bool is_integer               = true;
  static constexpr bool is_exact                 = true;
  static constexpr bool has_infinity             = false;
  static constexpr bool has_quiet_NaN            = false;
  static constexpr bool has_signaling_NaN        = false;
  static constexpr float_denorm_style has_denorm = denorm_absent;
  static constexpr bool has_denorm_loss          = false;
  static constexpr bool is_iec559                = false;
  static constexpr bool is_bounded               = true;
  static constexpr bool is_modulo                = true;
  static constexpr bool traps                    = false;
  static constexpr bool tinyness_before          = false;
  static constexpr float_round_style round_style = round_toward_zero;
  static constexpr int digits                    = 128;
  static constexpr int digits10                  = 38;
  static constexpr int max_digits10              = 0;
  static constexpr int radix                     = 2;
  static constexpr int min_exponent              = 0;
  static constexpr int min_exponent10            = 0;
  static constexpr int max_exponent              = 0;
  static constexpr int max_exponent10            = 0;

  [[nodiscard]] static constexpr stdex::uint128_t min() noexcept { return {}; }
  [[nodiscard]] static constexpr stdex::uint128_t lowest() noexcept { return {}; }
  [[nodiscard]] static constexpr stdex::uint128_t max() noexcept { return { ~0ULL, ~0ULL }; }
  [[nodiscard]] static constexpr stdex::uint128_t epsilon() noexcept { return {}; }
  [[nodiscard]] static constexpr stdex::uint128_t round_error() noexcept { return {}; }
  [[nodiscard]] static constexpr stdex::uint128_t infinity() noexcept { return {}; }
  [[nodiscard]] static constexpr stdex::uint128_t quiet_NaN() noexcept { return {}; }
  [[nodiscard]] static constexpr stdex::uint128_t signaling_NaN() noexcept { return {}; }
  [[nodiscard]] static constexpr stdex::uint128_t denorm_min() noexcept { return {}; }
};

template <>
struct numeric_limits<stdex::int128_t>
{
  static constexpr bool is_specialized           = true;
  static constexpr bool is_signed                = true;
  static constexpr bool is_integer               = true;
  static constexpr bool is_exact                 = true;
  static constexpr bool has_infinity             = false;
  static constexpr bool has_quiet_NaN            = false;
  static constexpr bool has_signaling_NaN        = false;
  static constexpr float_denorm_style has_denorm = denorm_absent;
  static constexpr bool has_denorm_loss          = false;
  static constexpr bool is_iec559                = false;
  static constexpr bool is_bounded               = true;
  static constexpr bool is_modulo                = false;
  static constexpr bool traps                    = false;
  static constexpr bool tinyness_before          = false;
  static constexpr float_round_style round_style = round_toward_zero;
  static constexpr int digits                    = 127;
  static constexpr int digits10                  = 38;
  static constexpr int max_digits10              = 0;
  static constexpr int radix                     = 2;
  static constexpr int min_exponent              = 0;
  static constexpr int min_exponent10            = 0;
  static constexpr int max_exponent              = 0;
  static constexpr int max_exponent10            = 0;

  [[nodiscard]] static constexpr stdex::int128_t min() noexcept { return { 0, 0x80'00'00'00'00'00'00'00ULL }; }
  [[nodiscard]] static constexpr stdex::int128_t lowest() noexcept { return min(); }
  [[nodiscard]] static constexpr stdex::int128_t max() noexcept { return { ~0ULL, 0x7F'FF'FF'FF'FF'FF'FF'FFULL }; }
  [[nodiscard]] static constexpr stdex::int128_t epsilon() noexcept { return {}; }
  [[nodiscard]] static constexpr stdex::int128_t round_error() noexcept { return {}; }
  [[nodiscard]] static constexpr stdex::int128_t infinity() noexcept { return {}; }
  [[nodiscard]] static constexpr stdex::int128_t quiet_NaN() noexcept { return {}; }
  [[nodiscard]] static constexpr stdex::int128_t signaling_NaN() noexcept { return {}; }
  [[nodiscard]] static constexpr stdex::int128_t denorm_min() noexcept { return {}; }
};

#if defined(_MSC_VER)
#  pragma warning(pop)
#endif
} // namespace std

#undef STDEX_INT128_DEFINE_MIXED
