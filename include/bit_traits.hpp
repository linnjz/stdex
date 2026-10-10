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

#include <bit>         // std::bit_cast
#include <concepts>    // std::integral, std::floating_point
#include <climits>     // CHAR_BIT
#include <cstdint>     // std::uint64_t, std::uint32_t, std::uint16_t, std::uint8_t
#include <type_traits> // std::remove_cv_t, std::is_enum_v, std::make_unsigned_t, std::underlying_type_t, std::conditional_t

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
template <std::size_t>
struct unsigned_of_bits;

template <> struct unsigned_of_bits<8> { using type = std::uint8_t;  };
template <> struct unsigned_of_bits<16> { using type = std::uint16_t; };
template <> struct unsigned_of_bits<32> { using type = std::uint32_t; };
template <> struct unsigned_of_bits<64> { using type = std::uint64_t; };
#if defined(__SIZEOF_INT128__)
template <> struct unsigned_of_bits<128> { using type = __uint128_t; };
#endif // __SIZEOF_INT128__

template <std::size_t Bits>
using unsigned_of_bits_t = typename unsigned_of_bits<Bits>::type;

template <std::size_t>
struct signed_of_bits;

template <> struct signed_of_bits<8> { using type = std::int8_t;  };
template <> struct signed_of_bits<16> { using type = std::int16_t; };
template <> struct signed_of_bits<32> { using type = std::int32_t; };
template <> struct signed_of_bits<64> { using type = std::int64_t; };
#if defined(__SIZEOF_INT128__)
template <> struct signed_of_bits<128> { using type = __int128_t; };
#endif // __SIZEOF_INT128__

template <std::size_t Bits>
using signed_of_bits_t = typename signed_of_bits<Bits>::type;

template <typename T>
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

template <typename T>
requires std::is_enum_v<T>
struct unsigned_equivalent<T>
{
  using type = std::conditional_t<
      std::is_same_v<std::underlying_type_t<T>, bool>,
      std::uint8_t,
      std::make_unsigned_t<std::underlying_type_t<T>>>;
};

template <std::floating_point T>
struct unsigned_equivalent<T>
{
  using type = unsigned_of_bits_t<sizeof(T) * CHAR_BIT>;
};

template <typename T>
using unsigned_equivalent_t = typename unsigned_equivalent<std::remove_cv_t<T>>::type;

template <class T>
requires (std::is_arithmetic_v<T> || std::is_enum_v<T>)
[[nodiscard]] constexpr unsigned_equivalent_t<T> to_unsigned(T const value) noexcept
{
  if constexpr (std::floating_point<std::remove_cv_t<T>>) {
    return std::bit_cast<unsigned_equivalent_t<T>>(value);
  } else if constexpr (std::is_enum_v<std::remove_cv_t<T>>) {
    using U = std::underlying_type_t<std::remove_cv_t<T>>;
    return static_cast<unsigned_equivalent_t<T>>(static_cast<U>(value));
  } else {
    return static_cast<unsigned_equivalent_t<T>>(value);
  }
}

template <class T>
requires (std::is_arithmetic_v<T> || std::is_enum_v<T>)
[[nodiscard]]
#if !defined(DEBUG) && !defined(_DEBUG)
[[msvc::forceinline, gnu::always_inline]]
#endif
constexpr std::size_t highest_one_bit_index(T const value) noexcept
{
  using uint_t = unsigned_equivalent_t<T>;
  return std::bit_width(static_cast<uint_t>(to_unsigned(value) | uint_t(1))) - 1;
}

template <class T>
[[nodiscard]] constexpr bool is_all_bits_zero(T const &val) noexcept
{
  static_assert(std::is_scalar_v<T> && !std::is_member_pointer_v<T>);
  if constexpr (std::is_null_pointer_v<T>) {
    return true;
  } else if constexpr (std::is_integral_v<T> || std::is_pointer_v<T>) {
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
} // namespace stdex

#if defined(__clang__)
#  pragma clang diagnostic pop
#elif defined(__GNUC__)
#  pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#  pragma warning(pop)
#endif
