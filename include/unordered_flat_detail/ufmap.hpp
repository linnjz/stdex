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

#include <memory>  // std::allocator_traits
#include <utility> // std::pair

namespace stdex::unordered_flat_detail
{
template <class KeyType, class ValueType, size_t N, class Hasher, class KeyEqual, class Allocator>
struct ufmap
{
  using hasher           = Hasher;
  using key_equal        = KeyEqual;
  using allocator_type   = Allocator;
  using allocator_traits = std::allocator_traits<allocator_type>;

  using key_type           = KeyType;
  using mutable_value_type = std::pair<KeyType, ValueType>;
  using value_type         = std::pair<KeyType const, ValueType>;
  using size_type          = typename allocator_traits::size_type;
  using difference_type    = typename allocator_traits::difference_type;
  using pointer            = typename allocator_traits::pointer;
  using const_pointer      = typename allocator_traits::const_pointer;
  using raw_pointer        = value_type *;
  using raw_const_pointer  = value_type const *;
  using reference          = value_type &;
  using const_reference    = value_type const &;

  template <class T1, class T2>
  static constexpr key_type const &key_fn(std::pair<T1, T2> const &val) noexcept
  {
    return val.first;
  }

  static constexpr size_type inline_size() noexcept { return N; }
};
} // namespace stdex::unordered_flat_detail
