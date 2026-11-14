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

namespace stdex::minimal_perfect_hash_detail
{
template <class KeyType>
struct mphset_traits
{
  using key_type    = KeyType;
  using mapped_type = KeyType;
  using value_type  = KeyType;

  [[nodiscard]] static constexpr key_type const &key_of(value_type const &value) noexcept { return value; }

  [[nodiscard]] static constexpr mapped_type const &mapped_of(value_type const &value) noexcept { return value; }
};
} // namespace stdex::minimal_perfect_hash_detail
