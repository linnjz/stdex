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
#include <array>     // std::array, std::bit_cast (via <bit>)
#include <bit>       // std::endian, std::byteswap, std::bit_cast
#include <climits>   // CHAR_BIT
#include <concepts>  // std::same_as, std::unsigned_integral
#include <cstddef>   // std::size_t, std::byte, std::to_integer
#include <cstdint>   // uint64_t, uint32_t, uint8_t
#include <span>      // std::span
#include <type_traits> // std::remove_cv_t
#include <utility>     // std::unreachable

#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wattributes"
#elif defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wattributes"
#endif

namespace stdex
{
static_assert(CHAR_BIT == 8, "stdexhash requires 8-bit bytes");
static_assert(sizeof(uint32_t) == 4);
static_assert(sizeof(uint64_t) == 8);

template <typename T>
concept byte_like = std::same_as<std::remove_cv_t<T>, std::byte> ||
                    std::same_as<std::remove_cv_t<T>, char> ||
                    std::same_as<std::remove_cv_t<T>, signed char> ||
                    std::same_as<std::remove_cv_t<T>, unsigned char> ||
                    std::same_as<std::remove_cv_t<T>, uint8_t>;

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
[[nodiscard, msvc::forceinline, gnu::always_inline]] constexpr uint64_t read64_le(std::span<Byte const> const input, size_t const offset) noexcept
{
  std::array<Byte, 8> buf;
  std::copy_n(input.begin() + offset, buf.size(), buf.begin());
  if constexpr (std::endian::native == std::endian::big) {
    return std::byteswap(std::bit_cast<uint64_t>(buf));
  } else if constexpr (std::endian::native == std::endian::little) {
    return std::bit_cast<uint64_t>(buf);
  } else {
    std::unreachable();
  }
}

template <byte_like Byte>
[[nodiscard, msvc::forceinline, gnu::always_inline]] constexpr uint32_t read32_le(std::span<Byte const> const input, size_t const offset) noexcept
{
  std::array<Byte, 4> buf;
  std::copy_n(input.begin() + offset, buf.size(), buf.begin());

  if constexpr (std::endian::native == std::endian::big) {
    return std::byteswap(std::bit_cast<uint32_t>(buf));
  } else if constexpr (std::endian::native == std::endian::little) {
    return std::bit_cast<uint32_t>(buf);
  } else {
    std::unreachable();
  }
}

template <std::unsigned_integral UInt>
[[nodiscard]] constexpr auto little_endian_array(UInt const value) noexcept
{
  if constexpr (std::endian::native == std::endian::big) {
    return std::bit_cast<std::array<std::byte, sizeof(UInt)>>(std::byteswap(value));
  } else if constexpr (std::endian::native == std::endian::little) {
    return std::bit_cast<std::array<std::byte, sizeof(UInt)>>(value);
  } else {
    std::unreachable();
  }
}
} // namespace stdex

#if defined(__clang__)
#  pragma clang diagnostic pop
#elif defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif
