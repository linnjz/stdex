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

#include <algorithm> // std::copy_n
#include <array>     // std::array
#include <bit>       // std::endian, std::byteswap, std::bit_cast
#include <climits>   // CHAR_BIT
#include <concepts>  // std::same_as, std::unsigned_integral
#include <cstddef>   // std::size_t, std::byte, std::to_integer
#include <cstdint>   // uint64_t, uint32_t, uint8_t
#include <span>      // std::span
#include <type_traits> // std::remove_cv_t
#include <utility>   // std::make_index_sequence
#include <version>

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
template <std::unsigned_integral UInt>
[[nodiscard]] constexpr UInt byteswap(UInt value) noexcept
{
#ifdef __cpp_lib_byteswap
  return std::byteswap(value);
#else
  return [&]<std::size_t... I>(std::index_sequence<I...>) {
    UInt result {};
    ((result = static_cast<UInt>((result << 8) | ((value >> (I * 8)) & UInt(0xFF)))), ...);
    return result;
  }(std::make_index_sequence<sizeof(UInt)> {});
#endif // __cpp_lib_byteswap
}

template <typename T>
concept byte_like = std::same_as<std::remove_cv_t<T>, std::byte> ||
                    std::same_as<std::remove_cv_t<T>, char> ||
                    std::same_as<std::remove_cv_t<T>, signed char> ||
                    std::same_as<std::remove_cv_t<T>, unsigned char> ||
                    std::same_as<std::remove_cv_t<T>, char8_t> ||
                    std::same_as<std::remove_cv_t<T>, bool>;

template <byte_like T>
[[nodiscard]] constexpr uint8_t byte_value(T const value) noexcept
{
  if constexpr (std::same_as<std::remove_cv_t<T>, std::byte>) {
    return std::to_integer<uint8_t>(value);
  } else {
    return static_cast<uint8_t>(value);
  }
}

template <byte_like Byte>
[[nodiscard]]
#if !defined(DEBUG) && !defined(_DEBUG)
[[msvc::forceinline, gnu::always_inline]]
#endif
constexpr uint64_t read64_le(std::span<Byte const> const input, size_t const offset) noexcept
{
  static_assert(std::endian::native == std::endian::little || 
                std::endian::native == std::endian::big, 
                "Unsupported native endianness: expected big or little.");

  std::array<Byte, 8> buf;
  std::copy_n(input.begin() + offset, buf.size(), buf.begin());
  if constexpr (std::endian::native == std::endian::big) {
    return byteswap(std::bit_cast<uint64_t>(buf));
  } else /*if constexpr (std::endian::native == std::endian::little)*/ {
    return std::bit_cast<uint64_t>(buf);
  }
}

template <byte_like Byte>
[[nodiscard]]
#if !defined(DEBUG) && !defined(_DEBUG)
[[msvc::forceinline, gnu::always_inline]]
#endif
constexpr uint32_t read32_le(std::span<Byte const> const input, size_t const offset) noexcept
{
  static_assert(std::endian::native == std::endian::little || 
                std::endian::native == std::endian::big, 
                "Unsupported native endianness: expected big or little.");

  std::array<Byte, 4> buf;
  std::copy_n(input.begin() + offset, buf.size(), buf.begin());

  if constexpr (std::endian::native == std::endian::big) {
    return byteswap(std::bit_cast<uint32_t>(buf));
  } else /*if constexpr (std::endian::native == std::endian::little)*/ {
    return std::bit_cast<uint32_t>(buf);
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
