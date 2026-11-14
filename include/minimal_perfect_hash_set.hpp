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

#include <functional> // std::equal_to
#include <memory>     // std::allocator

#include "hash.hpp"
#include "minimal_perfect_hash_detail/mphset_traits.hpp"
#include "minimal_perfect_hash_detail/hash_table.hpp"

namespace stdex
{
/// Immutable hash set with O(1) lookup and no insertion or erase operations.
///
/// The key set is fixed at construction. Keys must be unique; construction
/// throws if the minimal-perfect hash index cannot be built from them.
template <class KeyType, class Hasher = stdex::hash<KeyType>, class KeyEqual = std::equal_to<KeyType>, class Allocator = std::allocator<KeyType>, class Options = ptr_hash_option_default>
class minimal_perfect_hash_set
    : public minimal_perfect_hash_detail::hash_table<minimal_perfect_hash_detail::mphset_traits<KeyType>, Hasher, KeyEqual, Allocator, Options>
{
  using traits = minimal_perfect_hash_detail::mphset_traits<KeyType>;
  using base   = minimal_perfect_hash_detail::hash_table<traits, Hasher, KeyEqual, Allocator, Options>;

public:
  using typename base::allocator_type;
  using typename base::const_iterator;
  using typename base::const_pointer;
  using typename base::const_reference;
  using typename base::difference_type;
  using typename base::hasher;
  using typename base::iterator;
  using typename base::key_equal;
  using typename base::key_type;
  using typename base::mapped_type;
  using typename base::size_type;
  using typename base::value_type;

  using base::base;

  constexpr minimal_perfect_hash_set() = default;
};
} // namespace stdex
