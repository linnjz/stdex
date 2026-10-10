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

#include <utility> // std::pair

namespace stdex::unordered_flat_detail
{
template <class KeyType, class ValueType, std::size_t N>
struct ufmap_traits
{
  using key_type        = KeyType;
  using mapped_type     = ValueType;
  using value_type      = std::pair<key_type, mapped_type>;
  using reference       = std::pair<key_type const &, mapped_type &>;
  using const_reference = std::pair<key_type const &, mapped_type const &>;

  static constexpr std::size_t inline_capacity = N;

  static constexpr reference to_reference(value_type &val) noexcept
  {
    return { val.first, val.second };
  }

  static constexpr const_reference to_const_reference(value_type const &val) noexcept
  {
    return { val.first, val.second };
  }

  template <class T1, class T2>
  static constexpr key_type const &key_fn(std::pair<T1, T2> const &val) noexcept
  {
    return val.first;
  }
};
} // namespace stdex::unordered_flat_detail
