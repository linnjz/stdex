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

#include <memory> // std::allocator_traits

namespace stdex::unordered_flat_detail
{
template <class KeyType, size_t N, class Hasher, class KeyEqual, class Allocator>
struct ufset
{
  using hasher         = Hasher;
  using key_equal      = KeyEqual;
  using allocator_type = Allocator;
  using allocator_traits = std::allocator_traits<allocator_type>;

  using key_type           = KeyType;
  using mutable_value_type = KeyType;
  using value_type         = KeyType;
  using size_type          = typename allocator_traits::size_type;
  using difference_type    = typename allocator_traits::difference_type;
  using pointer            = typename allocator_traits::pointer;
  using const_pointer      = typename allocator_traits::const_pointer;
  using raw_pointer        = value_type *;
  using raw_const_pointer  = value_type const *;
  using reference          = value_type &;
  using const_reference    = value_type const &;

  static constexpr key_type const &key_fn(value_type const &val) noexcept { return val; }

  static constexpr size_type inline_size() noexcept { return N; }
};
} // namespace stdex::unordered_flat_detail
