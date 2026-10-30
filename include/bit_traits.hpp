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

#include <array>       // std::array
#include <bit>         // std::bit_cast, std::byteswap, std::endian
#include <concepts>    // std::same_as, std::unsigned_integral, std::integral, std::floating_point
#include <climits>     // CHAR_BIT
#include <cstddef>     // std::size_t, std::byte, std::to_integer
#include <cstdint>     // std::uint64_t, std::uint32_t, std::uint16_t, std::uint8_t
#include <span>        // std::span
#include <type_traits> // std::remove_cvref_t, std::is_enum_v, std::make_unsigned_t, std::underlying_type_t, std::conditional_t
#include <utility>     // std::make_index_sequence
#include <version>

#if defined(_MSC_VER)
#  include <intrin.h> // _byteswap_ushort, _byteswap_ulong, _byteswap_uint64
#endif

#if defined(__SIZEOF_INT128__)
#  define STDEX_BIT_DETAIL_NATIVE_INT128 1
#else
#  define STDEX_BIT_DETAIL_NATIVE_INT128 0
#endif

#if defined(__has_builtin)
#  if __has_builtin(__builtin_clzg) && __has_builtin(__builtin_ctzg) && __has_builtin(__builtin_popcountg)
#    define STDEX_BIT_DETAIL_HAS_GENERIC_COUNT_BUILTINS 1
#  endif
#  if __has_builtin(__builtin_bswap128)
#    define STDEX_BIT_DETAIL_HAS_BSWAP128 1
#  endif
#  if __has_builtin(__builtin_bswapg)
#    define STDEX_BIT_DETAIL_HAS_BSWAPG 1
#  endif
#  if __has_builtin(__builtin_bitreverse8)
#    define STDEX_BIT_DETAIL_HAS_BITREVERSE8 1
#  endif
#  if __has_builtin(__builtin_bitreverse16)
#    define STDEX_BIT_DETAIL_HAS_BITREVERSE16 1
#  endif
#  if __has_builtin(__builtin_bitreverse32)
#    define STDEX_BIT_DETAIL_HAS_BITREVERSE32 1
#  endif
#  if __has_builtin(__builtin_bitreverse64)
#    define STDEX_BIT_DETAIL_HAS_BITREVERSE64 1
#  endif
#  if __has_builtin(__builtin_bitreverse128)
#    define STDEX_BIT_DETAIL_HAS_BITREVERSE128 1
#  endif
#  if __has_builtin(__builtin_bitreverseg)
#    define STDEX_BIT_DETAIL_HAS_BITREVERSEG 1
#  endif
#endif

#ifndef STDEX_BIT_DETAIL_HAS_BITREVERSE8
#  define STDEX_BIT_DETAIL_HAS_BITREVERSE8 0
#endif
#ifndef STDEX_BIT_DETAIL_HAS_BITREVERSE16
#  define STDEX_BIT_DETAIL_HAS_BITREVERSE16 0
#endif
#ifndef STDEX_BIT_DETAIL_HAS_BITREVERSE32
#  define STDEX_BIT_DETAIL_HAS_BITREVERSE32 0
#endif
#ifndef STDEX_BIT_DETAIL_HAS_BITREVERSE64
#  define STDEX_BIT_DETAIL_HAS_BITREVERSE64 0
#endif

#ifndef STDEX_BIT_DETAIL_HAS_GENERIC_COUNT_BUILTINS
#  define STDEX_BIT_DETAIL_HAS_GENERIC_COUNT_BUILTINS 0
#endif
#ifndef STDEX_BIT_DETAIL_HAS_BSWAP128
#  define STDEX_BIT_DETAIL_HAS_BSWAP128 0
#endif
#ifndef STDEX_BIT_DETAIL_HAS_BSWAPG
#  define STDEX_BIT_DETAIL_HAS_BSWAPG 0
#endif
#ifndef STDEX_BIT_DETAIL_HAS_BITREVERSE128
#  define STDEX_BIT_DETAIL_HAS_BITREVERSE128 0
#endif
#ifndef STDEX_BIT_DETAIL_HAS_BITREVERSEG
#  define STDEX_BIT_DETAIL_HAS_BITREVERSEG 0
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
struct uint128_t;
struct int128_t;

template <class T>
concept byte_like = std::same_as<std::remove_cvref_t<T>, std::byte> ||
                    std::same_as<std::remove_cvref_t<T>, char> ||
                    std::same_as<std::remove_cvref_t<T>, signed char> ||
                    std::same_as<std::remove_cvref_t<T>, unsigned char> ||
                    std::same_as<std::remove_cvref_t<T>, char8_t>;

template <std::size_t>
struct unsigned_of_bits;

template <> struct unsigned_of_bits<8> { using type = std::uint8_t;  };
template <> struct unsigned_of_bits<16> { using type = std::uint16_t; };
template <> struct unsigned_of_bits<32> { using type = std::uint32_t; };
template <> struct unsigned_of_bits<64> { using type = std::uint64_t; };
template <> struct unsigned_of_bits<128> { using type = uint128_t; };

template <std::size_t Bits>
using unsigned_of_bits_t = typename unsigned_of_bits<Bits>::type;

template <std::size_t>
struct signed_of_bits;

template <> struct signed_of_bits<8> { using type = std::int8_t;  };
template <> struct signed_of_bits<16> { using type = std::int16_t; };
template <> struct signed_of_bits<32> { using type = std::int32_t; };
template <> struct signed_of_bits<64> { using type = std::int64_t; };
template <> struct signed_of_bits<128> { using type = int128_t; };

template <std::size_t Bits>
using signed_of_bits_t = typename signed_of_bits<Bits>::type;

template <class T>
struct unsigned_equivalent;

template <>
struct unsigned_equivalent<bool>
{
  using type = std::uint8_t;
};

template <std::integral T>
struct unsigned_equivalent<T>
{
  using type = std::make_unsigned_t<T>;
};

template <class T>
requires std::is_enum_v<T>
struct unsigned_equivalent<T>
{
  using type = std::conditional_t<
      std::same_as<std::underlying_type_t<T>, bool>,
      std::uint8_t,
      std::make_unsigned_t<std::underlying_type_t<T>>>;
};

template <std::floating_point T>
struct unsigned_equivalent<T>
{
  static_assert(std::has_single_bit(static_cast<unsigned>(sizeof(T) * CHAR_BIT)), 
                "floating bit width is not a power of two; no integer can carry it");
  using type = unsigned_of_bits_t<sizeof(T) * CHAR_BIT>;
};

template <>
struct unsigned_equivalent<uint128_t>
{
  using type = uint128_t;
};

template <>
struct unsigned_equivalent<int128_t>
{
  using type = uint128_t;
};

template <class T>
using unsigned_equivalent_t = typename unsigned_equivalent<std::remove_cvref_t<T>>::type;

namespace bit_detail
{
template <class UInt, std::size_t... I>
constexpr UInt byteswap_impl(UInt value, std::index_sequence<I...>)
{
  UInt result {};
  ((result = static_cast<UInt>((result << 8) | ((value >> (I * 8)) & UInt(0xFF)))), ...);
  return result;
}

template <std::unsigned_integral UInt, std::size_t I>
constexpr UInt swap_adjacent_bit_groups(UInt value) noexcept
{
  constexpr std::size_t shift = std::size_t(1) << I;
  constexpr UInt mask         = UInt(UInt(-1) / ((UInt(1) << shift) + 1));
  return UInt(((value >> shift) & mask) | ((value & mask) << shift));
}

template <std::unsigned_integral UInt, std::size_t... I>
[[nodiscard]] constexpr UInt bit_reverse_impl(UInt value, std::index_sequence<I...>) noexcept
{
  ((value = swap_adjacent_bit_groups<UInt, I>(value)), ...);
  return value;
}

template <class UInt>
[[nodiscard]] constexpr int countl_zero_128(UInt const bits) noexcept
{
#if STDEX_BIT_DETAIL_NATIVE_INT128 && STDEX_BIT_DETAIL_HAS_GENERIC_COUNT_BUILTINS
  return __builtin_clzg(bits.value, 128);
#else
  if (bits.hi() != 0) {
    return std::countl_zero(bits.hi());
  }
  if (bits.lo() != 0) {
    return 64 + std::countl_zero(bits.lo());
  }
  return 128;
#endif
}

template <class UInt>
[[nodiscard]] constexpr int countr_zero_128(UInt const bits) noexcept
{
#if STDEX_BIT_DETAIL_NATIVE_INT128 && STDEX_BIT_DETAIL_HAS_GENERIC_COUNT_BUILTINS
  return __builtin_ctzg(bits.value, 128);
#else
  if (bits.lo() != 0) {
    return std::countr_zero(bits.lo());
  }
  if (bits.hi() != 0) {
    return 64 + std::countr_zero(bits.hi());
  }
  return 128;
#endif
}

template <class UInt>
[[nodiscard]] constexpr int popcount_128(UInt const bits) noexcept
{
#if STDEX_BIT_DETAIL_NATIVE_INT128 && STDEX_BIT_DETAIL_HAS_GENERIC_COUNT_BUILTINS
  return __builtin_popcountg(bits.value);
#else
  return std::popcount(bits.lo()) + std::popcount(bits.hi());
#endif
}

template <class T>
[[nodiscard]] constexpr T from_unsigned(unsigned_equivalent_t<T> const bits) noexcept
{
  if constexpr (std::same_as<std::remove_cvref_t<T>, uint128_t> || std::same_as<std::remove_cvref_t<T>, int128_t>) {
    return T::from_words(bits.lo(), bits.hi());
  } else {
    return std::bit_cast<T>(bits);
  }
}

template <class T, class Native>
[[nodiscard]] constexpr T from_native_128(Native const bits) noexcept
{
  return from_unsigned<T>(unsigned_equivalent_t<T>::from_words(
      static_cast<std::uint64_t>(bits), static_cast<std::uint64_t>(bits >> 64U)));
}

template <class T>
[[nodiscard]] constexpr bool is_negative_value(T const value) noexcept
{
  if constexpr (std::same_as<T, int128_t>) {
    return (value.hi() & (std::uint64_t { 1 } << 63U)) != 0;
  } else if constexpr (std::same_as<T, uint128_t>) {
    return false;
  } else if constexpr (std::is_enum_v<T>) {
    using U = std::underlying_type_t<T>;
    if constexpr (std::signed_integral<U>) {
      return static_cast<U>(value) < 0;
    } else {
      return false;
    }
  } else {
    if constexpr (std::signed_integral<T>) {
      return value < 0;
    } else {
      return false;
    }
  }
}

template <class S>
[[nodiscard]] constexpr bool is_negative_count(S const count) noexcept
{
  if constexpr (std::signed_integral<S>) {
    return count < 0;
  } else {
    return false;
  }
}

template <class S>
[[nodiscard]] constexpr std::uint64_t count_magnitude(S const count) noexcept
{
  using U = std::make_unsigned_t<S>;
  if constexpr (std::signed_integral<S>) {
    return count < 0
             ? static_cast<std::uint64_t>(static_cast<U>(U { 0 } - static_cast<U>(count)))
             : static_cast<std::uint64_t>(static_cast<U>(count));
  } else {
    return static_cast<std::uint64_t>(count);
  }
}

template <class T>
concept bit_shift_count = std::integral<std::remove_cvref_t<T>> &&
                          !std::same_as<std::remove_cvref_t<T>, bool> &&
                          sizeof(std::remove_cvref_t<T>) <= sizeof(std::uint64_t) &&
                          !(std::same_as<std::remove_cvref_t<T>, uint128_t> || std::same_as<std::remove_cvref_t<T>, int128_t>);
} // namespace bit_detail

template <class T>
inline constexpr int bit_size = static_cast<int>(sizeof(T) * CHAR_BIT);

template <class T>
concept bit_operand = (std::integral<T> || std::is_enum_v<T> ||
                       std::same_as<T, uint128_t> || std::same_as<T, int128_t>) &&
                      !std::same_as<std::remove_cv_t<T>, bool>;

template <class T>
requires (std::is_arithmetic_v<T> || std::is_enum_v<T> || std::same_as<T, uint128_t> || std::same_as<T, int128_t>)
[[nodiscard]] constexpr unsigned_equivalent_t<T> to_unsigned(T const value) noexcept
{
  if constexpr (std::floating_point<T>) {
    return std::bit_cast<unsigned_equivalent_t<T>>(value);
  } else if constexpr (std::is_enum_v<T>) {
    using U = std::underlying_type_t<T>;
    return static_cast<unsigned_equivalent_t<T>>(static_cast<U>(value));
  } else {
    return static_cast<unsigned_equivalent_t<T>>(value);
  }
}

template <bit_operand T>
[[nodiscard]] constexpr int countl_zero(T const value) noexcept
{
  using uint_t = unsigned_equivalent_t<T>;
  uint_t const bits { to_unsigned(value) };

  if constexpr (std::same_as<T, uint128_t> || std::same_as<T, int128_t>) {
    return bit_detail::countl_zero_128(bits);
  } else {
    return std::countl_zero(bits);
  }
}

template <bit_operand T>
[[nodiscard]] constexpr int countl_one(T const value) noexcept
{
  using uint_t = unsigned_equivalent_t<T>;
  return countl_zero(static_cast<uint_t>(~to_unsigned(value)));
}

template <bit_operand T>
[[nodiscard]] constexpr int countr_zero(T const value) noexcept
{
  using uint_t = unsigned_equivalent_t<T>;
  uint_t const bits { to_unsigned(value) };

  if constexpr (std::same_as<T, uint128_t> || std::same_as<T, int128_t>) {
    return bit_detail::countr_zero_128(bits);
  } else {
    return std::countr_zero(bits);
  }
}

template <bit_operand T>
[[nodiscard]] constexpr int countr_one(T const value) noexcept
{
  using uint_t = unsigned_equivalent_t<T>;
  return countr_zero(static_cast<uint_t>(~to_unsigned(value)));
}

template <bit_operand T>
[[nodiscard]] constexpr int popcount(T const value) noexcept
{
  using uint_t = unsigned_equivalent_t<T>;
  uint_t const bits { to_unsigned(value) };

  if constexpr (std::same_as<T, uint128_t> || std::same_as<T, int128_t>) {
    return bit_detail::popcount_128(bits);
  } else {
    return std::popcount(bits);
  }
}

template <bit_operand T>
[[nodiscard]] constexpr bool has_single_bit(T const value) noexcept
{
  return popcount(value) == 1;
}

template <bit_operand T>
[[nodiscard]] constexpr int bit_width(T const value) noexcept
{
  return bit_size<T> - countl_zero(value);
}

template <bit_operand T>
[[nodiscard]] constexpr T bit_floor(T const value) noexcept
{
  using uint_t = unsigned_equivalent_t<T>;
  uint_t const bits { to_unsigned(value) };
  if (bits == uint_t {}) {
    return T {};
  }

  int const width = bit_width(value);
  return bit_detail::from_unsigned<T>(static_cast<uint_t>(uint_t { 1 } << (width - 1)));
}

template <bit_operand T>
[[nodiscard]] constexpr T bit_ceil(T const value) noexcept
{
  using uint_t = unsigned_equivalent_t<T>;
  uint_t const bits { to_unsigned(value) };
  if (bits <= uint_t { 1 }) {
    return bit_detail::from_unsigned<T>(uint_t { 1 });
  }

  int const width = bit_width(value);
  if (width >= bit_size<T>) {
    uint_t const top = static_cast<uint_t>(uint_t { 1 } << (bit_size<T> - 1));
    return bits == top ? bit_detail::from_unsigned<T>(top) : T {};
  }
  return bit_detail::from_unsigned<T>(static_cast<uint_t>(uint_t { 1 } << width));
}

template <bit_operand T, bit_detail::bit_shift_count S>
[[nodiscard]] constexpr T shl(T const value, S const count) noexcept
{
  using uint_t = unsigned_equivalent_t<T>;

  bool const negative_count     = bit_detail::is_negative_count(count);
  std::uint64_t const magnitude = bit_detail::count_magnitude(count);

  if (negative_count) {
    if (magnitude >= static_cast<std::uint64_t>(bit_size<T>)) {
      return bit_detail::is_negative_value(value) ? bit_detail::from_unsigned<T>(static_cast<uint_t>(~uint_t {}))
                                                  : T {};
    }
    unsigned const shift = static_cast<unsigned>(magnitude);
    uint_t bits          = static_cast<uint_t>(to_unsigned(value) >> shift);
    if (bit_detail::is_negative_value(value)) {
      bits = static_cast<uint_t>(bits | static_cast<uint_t>(~uint_t {} << (bit_size<T> - shift)));
    }
    return bit_detail::from_unsigned<T>(bits);
  }
  if (magnitude >= static_cast<std::uint64_t>(bit_size<T>)) {
    return T {};
  }
  return bit_detail::from_unsigned<T>(static_cast<uint_t>(to_unsigned(value) << static_cast<unsigned>(magnitude)));
}

template <bit_operand T, bit_detail::bit_shift_count S>
[[nodiscard]] constexpr T shr(T const value, S const count) noexcept
{
  using uint_t = unsigned_equivalent_t<T>;

  bool const negative_count     = bit_detail::is_negative_count(count);
  std::uint64_t const magnitude = bit_detail::count_magnitude(count);

  if (negative_count) {
    if (magnitude >= static_cast<std::uint64_t>(bit_size<T>)) {
      return T {};
    }
    return bit_detail::from_unsigned<T>(static_cast<uint_t>(to_unsigned(value) << static_cast<unsigned>(magnitude)));
  }
  if (magnitude >= static_cast<std::uint64_t>(bit_size<T>)) {
    return bit_detail::is_negative_value(value) ? bit_detail::from_unsigned<T>(static_cast<uint_t>(~uint_t {}))
                                                : T {};
  }
  if (magnitude == 0) {
    return value;
  }
  unsigned const shift = static_cast<unsigned>(magnitude);
  uint_t bits          = static_cast<uint_t>(to_unsigned(value) >> shift);
  if (bit_detail::is_negative_value(value)) {
    bits = static_cast<uint_t>(bits | static_cast<uint_t>(~uint_t {} << (bit_size<T> - shift)));
  }
  return bit_detail::from_unsigned<T>(bits);
}

template <bit_operand T, bit_detail::bit_shift_count S>
[[nodiscard]] constexpr T rotl(T const value, S const count) noexcept
{
  using uint_t = unsigned_equivalent_t<T>;

  std::uint64_t amount = bit_detail::count_magnitude(count) % static_cast<std::uint64_t>(bit_size<T>);
  if (bit_detail::is_negative_count(count) && amount != 0) {
    amount = static_cast<std::uint64_t>(bit_size<T>) - amount;
  }
  if (amount == 0) {
    return value;
  }
  if constexpr (std::same_as<T, uint128_t> || std::same_as<T, int128_t>) {
    uint_t const bits = to_unsigned(value);
    return bit_detail::from_unsigned<T>(
        static_cast<uint_t>((bits << amount) | (bits >> (static_cast<std::uint64_t>(bit_size<T>) - amount))));
  } else {
    return bit_detail::from_unsigned<T>(std::rotl(static_cast<uint_t>(to_unsigned(value)), static_cast<int>(amount)));
  }
}

template <bit_operand T, bit_detail::bit_shift_count S>
[[nodiscard]] constexpr T rotr(T const value, S const count) noexcept
{
  using uint_t = unsigned_equivalent_t<T>;

  std::uint64_t amount = bit_detail::count_magnitude(count) % static_cast<std::uint64_t>(bit_size<T>);
  if (bit_detail::is_negative_count(count) && amount != 0) {
    amount = static_cast<std::uint64_t>(bit_size<T>) - amount;
  }
  if (amount == 0) {
    return value;
  }
  if constexpr (std::same_as<T, uint128_t> || std::same_as<T, int128_t>) {
    uint_t const bits = to_unsigned(value);
    return bit_detail::from_unsigned<T>(static_cast<uint_t>((bits >> amount) | (bits << (static_cast<std::uint64_t>(bit_size<T>) - amount))));
  } else {
    return bit_detail::from_unsigned<T>(std::rotr(static_cast<uint_t>(to_unsigned(value)), static_cast<int>(amount)));
  }
}

template <bit_operand T>
[[nodiscard]] constexpr T bit_repeat(T const pattern, int const length) noexcept
{
  using uint_t = unsigned_equivalent_t<T>;
  if (length <= 0) {
    return T {};
  }
  if (length >= bit_size<T>) {
    return pattern;
  }

  unsigned const len = static_cast<unsigned>(length);
  uint_t const mask  = static_cast<uint_t>(static_cast<uint_t>(uint_t { 1 } << len) - uint_t { 1 });
  uint_t result      = static_cast<uint_t>(to_unsigned(pattern) & mask);
  for (unsigned filled = len; filled < static_cast<unsigned>(bit_size<T>); filled <<= 1) {
    result = static_cast<uint_t>(result | static_cast<uint_t>(result << filled));
  }
  return bit_detail::from_unsigned<T>(result);
}

template <bit_operand T>
[[nodiscard]] constexpr T bit_compress(T const value, T const mask) noexcept
{
  using uint_t           = unsigned_equivalent_t<T>;
  uint_t const source    = to_unsigned(value);
  uint_t const mask_bits = to_unsigned(mask);

  uint_t result {};
  unsigned out = 0;
  for (unsigned i = 0; i < static_cast<unsigned>(bit_size<T>); ++i) {
    if (static_cast<uint_t>(static_cast<uint_t>(mask_bits >> i) & uint_t { 1 }) != uint_t {}) {
      uint_t const bit = static_cast<uint_t>(static_cast<uint_t>(source >> i) & uint_t { 1 });
      result           = static_cast<uint_t>(result | static_cast<uint_t>(bit << out));
      ++out;
    }
  }
  return bit_detail::from_unsigned<T>(result);
}

template <bit_operand T>
[[nodiscard]] constexpr T bit_expand(T const value, T const mask) noexcept
{
  using uint_t = unsigned_equivalent_t<T>;

  uint_t const source    = to_unsigned(value);
  uint_t const mask_bits = to_unsigned(mask);
  uint_t result {};
  unsigned in = 0;
  for (unsigned i = 0; i < static_cast<unsigned>(bit_size<T>); ++i) {
    if (static_cast<uint_t>(static_cast<uint_t>(mask_bits >> i) & uint_t { 1 }) != uint_t {}) {
      uint_t const bit = static_cast<uint_t>(static_cast<uint_t>(source >> in) & uint_t { 1 });
      result           = static_cast<uint_t>(result | static_cast<uint_t>(bit << i));
      ++in;
    }
  }
  return bit_detail::from_unsigned<T>(result);
}

template <class T>
requires (std::is_arithmetic_v<T> || std::is_enum_v<T> || std::same_as<T, uint128_t> || std::same_as<T, int128_t>)
[[nodiscard]] constexpr std::size_t highest_one_bit_index(T const value) noexcept
{
  int const width = bit_width(to_unsigned(value));
  return width == 0 ? 0u : static_cast<std::size_t>(width - 1);
}

template <bit_operand T>
[[nodiscard]] constexpr std::size_t lowest_one_bit_index(T const value) noexcept
{
  int const n = countr_zero(value);
  return n == bit_size<T> ? 0u : static_cast<std::size_t>(n);
}

template <class T>
requires (std::is_trivially_copyable_v<T> && !std::is_member_pointer_v<T> && !std::is_array_v<T>)
[[nodiscard]] constexpr bool is_all_bits_zero(T const &val) noexcept
{
  if constexpr (std::is_null_pointer_v<T>) {
    return true;
  } else if constexpr (std::integral<T> || std::is_pointer_v<T> || std::same_as<T, uint128_t> || std::same_as<T, int128_t>) {
    return val == T {};
  } else if constexpr (sizeof(T) == 1) {
    return std::bit_cast<std::uint8_t>(val) == 0;
  } else if constexpr (sizeof(T) == 2) {
    return std::bit_cast<std::uint16_t>(val) == 0;
  } else if constexpr (sizeof(T) == 4) {
    return std::bit_cast<std::uint32_t>(val) == 0;
  } else if constexpr (sizeof(T) == 8) {
    return std::bit_cast<std::uint64_t>(val) == 0;
  } else {
    return std::bit_cast<std::array<unsigned char, sizeof(T)>>(val) ==
           std::bit_cast<std::array<unsigned char, sizeof(T)>>(T {});
  }
}

template <bit_operand T>
[[nodiscard]] constexpr T byteswap(T value) noexcept
{
  using uint_t = unsigned_equivalent_t<T>;
  if constexpr (std::same_as<T, uint128_t> || std::same_as<T, int128_t>) {
    uint_t const bits = to_unsigned(value);
#if STDEX_BIT_DETAIL_NATIVE_INT128 && STDEX_BIT_DETAIL_HAS_BSWAP128
    return bit_detail::from_native_128<T>(__builtin_bswap128(bits.value));
#elif STDEX_BIT_DETAIL_NATIVE_INT128 && STDEX_BIT_DETAIL_HAS_BSWAPG
    return bit_detail::from_native_128<T>(__builtin_bswapg(bits.value));
#else
    return bit_detail::from_unsigned<T>(uint_t { byteswap(bits.hi()), byteswap(bits.lo()) });
#endif
  } else if constexpr (sizeof(uint_t) == 1) {
    return value;
  } else {
    uint_t bits { to_unsigned(value) };
#if defined(__cpp_lib_byteswap) && __cpp_lib_byteswap >= 202110L
    bits = std::byteswap(bits);
#elif defined(__GNUC__) || defined(__clang__)
    if constexpr (sizeof(uint_t) == 2) {
      bits = static_cast<uint_t>(__builtin_bswap16(static_cast<std::uint16_t>(bits)));
    } else if constexpr (sizeof(uint_t) == 4) {
      bits = static_cast<uint_t>(__builtin_bswap32(static_cast<std::uint32_t>(bits)));
    } else {
      static_assert(sizeof(uint_t) == 8, "byteswap: unsupported integer width");
      bits = static_cast<uint_t>(__builtin_bswap64(static_cast<std::uint64_t>(bits)));
    }
#elif defined(_MSC_VER)
    if (std::is_constant_evaluated()) {
      bits = bit_detail::byteswap_impl(bits, std::make_index_sequence<sizeof(uint_t)> {});
    } else if constexpr (sizeof(uint_t) == 2) {
      bits = static_cast<uint_t>(_byteswap_ushort(static_cast<unsigned short>(bits)));
    } else if constexpr (sizeof(uint_t) == 4) {
      bits = static_cast<uint_t>(_byteswap_ulong(static_cast<unsigned long>(bits)));
    } else {
      static_assert(sizeof(uint_t) == 8, "byteswap: unsupported integer width");
      bits = static_cast<uint_t>(_byteswap_uint64(static_cast<unsigned long long>(bits)));
    }
#else
    bits = bit_detail::byteswap_impl(bits, std::make_index_sequence<sizeof(uint_t)> {});
#endif // __cpp_lib_byteswap
    return bit_detail::from_unsigned<T>(bits);
  }
}

template <class T>
requires ((std::integral<T> || std::is_enum_v<T> || std::same_as<T, uint128_t> || std::same_as<T, int128_t>) && !std::same_as<std::remove_cv_t<T>, bool>)
[[nodiscard]] constexpr T bit_reverse(T const value) noexcept
{
  using uint_t = unsigned_equivalent_t<T>;
  uint_t const bits { to_unsigned(value) };

  if constexpr (std::same_as<T, uint128_t> || std::same_as<T, int128_t>) {
#if STDEX_BIT_DETAIL_NATIVE_INT128 && STDEX_BIT_DETAIL_HAS_BITREVERSE128
    return bit_detail::from_native_128<T>(__builtin_bitreverse128(bits.value));
#elif STDEX_BIT_DETAIL_NATIVE_INT128 && STDEX_BIT_DETAIL_HAS_BITREVERSEG
    return bit_detail::from_native_128<T>(__builtin_bitreverseg(bits.value));
#else
    return bit_detail::from_unsigned<T>(uint_t { bit_reverse(bits.hi()), bit_reverse(bits.lo()) });
#endif
  } else {
#if STDEX_BIT_DETAIL_HAS_BITREVERSE8
    if constexpr (sizeof(uint_t) == 1) {
      return bit_detail::from_unsigned<T>(static_cast<uint_t>(__builtin_bitreverse8(static_cast<unsigned char>(bits))));
    }
#endif
#if STDEX_BIT_DETAIL_HAS_BITREVERSE16
    if constexpr (sizeof(uint_t) == 2) {
      return bit_detail::from_unsigned<T>(static_cast<uint_t>(__builtin_bitreverse16(static_cast<std::uint16_t>(bits))));
    }
#endif
#if STDEX_BIT_DETAIL_HAS_BITREVERSE32
    if constexpr (sizeof(uint_t) == 4) {
      return bit_detail::from_unsigned<T>(static_cast<uint_t>(__builtin_bitreverse32(static_cast<std::uint32_t>(bits))));
    }
#endif
#if STDEX_BIT_DETAIL_HAS_BITREVERSE64
    if constexpr (sizeof(uint_t) == 8) {
      return bit_detail::from_unsigned<T>(static_cast<uint_t>(__builtin_bitreverse64(static_cast<std::uint64_t>(bits))));
    }
#endif
    constexpr unsigned stages =
        static_cast<unsigned>(std::countr_zero(static_cast<unsigned>(bit_size<uint_t>)));
    constexpr auto index_sequence = std::make_index_sequence<stages> {};
    return bit_detail::from_unsigned<T>(bit_detail::bit_reverse_impl(bits, index_sequence));
  }
}

template <byte_like T>
[[nodiscard]] constexpr std::uint8_t byte_value(T const value) noexcept
{
  if constexpr (std::same_as<std::remove_cvref_t<T>, std::byte>) {
    return std::to_integer<std::uint8_t>(value);
  } else {
    return static_cast<std::uint8_t>(value);
  }
}

template <byte_like Byte>
[[nodiscard]]
#if !defined(DEBUG) && !defined(_DEBUG)
[[msvc::forceinline, gnu::always_inline]]
#endif
constexpr std::uint64_t read64_le_unchecked(std::span<Byte const> const input, std::size_t const offset) noexcept
{
  static_assert(std::endian::native == std::endian::little ||
                std::endian::native == std::endian::big, 
                "Unsupported native endianness: expected big or little.");

  std::array buf {
    input[offset], input[offset + 1], input[offset + 2], input[offset + 3], 
    input[offset + 4], input[offset + 5], input[offset + 6], input[offset + 7]
  };

  if constexpr (std::endian::native == std::endian::big) {
    return byteswap(std::bit_cast<std::uint64_t>(buf));
  } else /*if constexpr (std::endian::native == std::endian::little)*/ {
    return std::bit_cast<std::uint64_t>(buf);
  }
}

template <byte_like Byte>
[[nodiscard]]
#if !defined(DEBUG) && !defined(_DEBUG)
[[msvc::forceinline, gnu::always_inline]]
#endif
constexpr std::uint32_t read32_le_unchecked(std::span<Byte const> const input, std::size_t const offset) noexcept
{
  static_assert(std::endian::native == std::endian::little ||
                std::endian::native == std::endian::big, 
                "Unsupported native endianness: expected big or little.");

  std::array buf { input[offset], input[offset + 1], input[offset + 2], input[offset + 3] };

  if constexpr (std::endian::native == std::endian::big) {
    return byteswap(std::bit_cast<std::uint32_t>(buf));
  } else /*if constexpr (std::endian::native == std::endian::little)*/ {
    return std::bit_cast<std::uint32_t>(buf);
  }
}

template <std::unsigned_integral UInt>
[[nodiscard]] constexpr auto little_endian_array(UInt const value) noexcept
{  
  static_assert(std::endian::native == std::endian::little || 
                std::endian::native == std::endian::big, 
                "Unsupported native endianness: expected big or little.");

  if constexpr (std::endian::native == std::endian::big) {
    return std::bit_cast<std::array<std::byte, sizeof(UInt)>>(byteswap(value));
  } else /*if constexpr (std::endian::native == std::endian::little) */ {
    return std::bit_cast<std::array<std::byte, sizeof(UInt)>>(value);
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

#undef STDEX_BIT_DETAIL_NATIVE_INT128
#undef STDEX_BIT_DETAIL_HAS_GENERIC_COUNT_BUILTINS
#undef STDEX_BIT_DETAIL_HAS_BSWAP128
#undef STDEX_BIT_DETAIL_HAS_BSWAPG
#undef STDEX_BIT_DETAIL_HAS_BITREVERSE8
#undef STDEX_BIT_DETAIL_HAS_BITREVERSE16
#undef STDEX_BIT_DETAIL_HAS_BITREVERSE32
#undef STDEX_BIT_DETAIL_HAS_BITREVERSE64
#undef STDEX_BIT_DETAIL_HAS_BITREVERSE128
#undef STDEX_BIT_DETAIL_HAS_BITREVERSEG
