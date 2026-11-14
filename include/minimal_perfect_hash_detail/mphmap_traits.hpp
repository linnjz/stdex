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

namespace stdex::minimal_perfect_hash_detail
{
template <class KeyType, class MappedType>
struct mphmap_traits
{
  using key_type    = KeyType;
  using mapped_type = MappedType;
  using value_type  = std::pair<KeyType, MappedType>;

  [[nodiscard]] static constexpr key_type const &key_of(value_type const &value) noexcept { return value.first; }

  [[nodiscard]] static constexpr mapped_type const &mapped_of(value_type const &value) noexcept { return value.second; }
};
} // namespace stdex::minimal_perfect_hash_detail
