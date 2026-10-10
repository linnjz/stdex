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

namespace stdex::unordered_flat_detail
{
template <class KeyType, std::size_t N>
struct ufset_traits
{
  using key_type        = KeyType;
  using value_type      = KeyType;
  using reference       = value_type &;
  using const_reference = value_type const &;

  static constexpr std::size_t inline_capacity = N;

  static constexpr reference to_reference(value_type &val) noexcept { return val; }

  static constexpr const_reference to_const_reference(value_type const &val) noexcept { return val; }

  static constexpr key_type const &key_fn(value_type const &val) noexcept { return val; }
};
} // namespace stdex::unordered_flat_detail
