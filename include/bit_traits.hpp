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

#include <bit>        // std::bit_cast
#include <concepts>   // std::integral, std::floating_point
#include <cstdint>    // uint64_t, uint32_t, uint8_t
#include <type_traits> // std::remove_cv_t, std::is_enum_v, std::make_unsigned_t,
                       // std::underlying_type_t, std::conditional_t
#include <utility>     // std::to_underlying

namespace stdex
{
template <typename T>
concept scalar_hashable = std::integral<std::remove_cv_t<T>> ||
                          std::is_enum_v<std::remove_cv_t<T>> ||
                          std::floating_point<std::remove_cv_t<T>>;

template <typename T>
struct unsigned_equivalent;

template <>
struct unsigned_equivalent<bool>
{
  using type = uint8_t;
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
  using type = std::make_unsigned_t<std::underlying_type_t<T>>;
};

template <std::floating_point T>
struct unsigned_equivalent<T>
{
  static_assert(sizeof(T) == 4 || sizeof(T) == 8, "stdexhash supports IEEE-width 32-bit and 64-bit floating types");
  using type = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;
};

template <typename T>
using unsigned_equivalent_t = typename unsigned_equivalent<std::remove_cv_t<T>>::type;

template <scalar_hashable T>
[[nodiscard]] constexpr unsigned_equivalent_t<T> to_unsigned_bits(T const value) noexcept
{
  if constexpr (std::floating_point<std::remove_cv_t<T>>) {
    return std::bit_cast<unsigned_equivalent_t<T>>(value);
  } else if constexpr (std::is_enum_v<std::remove_cv_t<T>>) {
    return static_cast<unsigned_equivalent_t<T>>(std::to_underlying(value));
  } else {
    return static_cast<unsigned_equivalent_t<T>>(value);
  }
}
} // namespace stdex
