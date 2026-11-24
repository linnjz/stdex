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
#include <iterator>   // std::input_iterator
#include <memory>     // std::allocator
#include <version>    // __cpp_lib_containers_ranges

#include "hash.hpp"
#include "minimal_perfect_hash_detail/mphset_traits.hpp"
#include "minimal_perfect_hash_detail/hash_table.hpp"

namespace stdex
{
/// Immutable hash set with O(1) lookup and no insertion or erase operations.
///
/// The key set is fixed at construction. Keys must be unique; construction
/// throws if the minimal-perfect hash index cannot be built from them.
template <class KeyType, class Hasher = stdex::hash<KeyType>, class KeyEqual = std::equal_to<KeyType>, auto Options = ptr_hash_heap_options<std::allocator<KeyType>>::profile_default()>
class minimal_perfect_hash_set
    : public minimal_perfect_hash_detail::hash_table<minimal_perfect_hash_detail::mphset_traits<KeyType>, Hasher, KeyEqual, Options>
{
  using traits = minimal_perfect_hash_detail::mphset_traits<KeyType>;
  using base   = minimal_perfect_hash_detail::hash_table<traits, Hasher, KeyEqual, Options>;

  static_assert(std::is_object_v<KeyType>, "containers require object key types ([container.requirements])");
  // Only the heap build allocates through the option set's allocator; stack options
  // carry a placeholder allocator and never allocate. base::allocator_type is already
  // rebound to value_type, so validate the option set's allocator instead -- that is
  // what actually catches a user allocator whose value_type differs from value_type.
  static_assert(base::storage_policy == ptr_hash_storage_policy::stack ||
                std::is_same_v<
                    typename std::allocator_traits<typename base::options_type::storage_traits::allocator_type>::value_type,
                    typename base::value_type>,
                "allocator value_type must match the container value_type");

public:
  using typename base::allocator_type;
  using typename base::const_iterator;
  using typename base::const_pointer;
  using typename base::const_reference;
  using typename base::pointer;
  using typename base::reference;
  using typename base::difference_type;
  using typename base::hasher;
  using typename base::iterator;
  using typename base::key_equal;
  using typename base::key_type;
  using typename base::mapped_type;
  using typename base::size_type;
  using typename base::value_type;

  using typename base::const_view;

  using base::base;

  constexpr minimal_perfect_hash_set() = default;
};

// ---------- CTAD iterator-range ----------
template <class Iter,
          class Hash  = stdex::hash<std::iter_value_t<Iter>>,
          class KeyEq = std::equal_to<std::iter_value_t<Iter>>,
          class Alloc = std::allocator<std::iter_value_t<Iter>>>
requires std::input_iterator<Iter>
      && minimal_perfect_hash_detail::is_hasher_for<Hash, std::iter_value_t<Iter>>
      && (!minimal_perfect_hash_detail::is_allocator<KeyEq>)
      && minimal_perfect_hash_detail::is_allocator<Alloc>
minimal_perfect_hash_set(Iter, Iter, Hash = Hash(), KeyEq = KeyEq(), Alloc = Alloc()) ->
minimal_perfect_hash_set<std::iter_value_t<Iter>, Hash, KeyEq,
                         stdex::ptr_hash_heap_options<Alloc>::profile_default()>;

template <class Iter, class Alloc>
requires std::input_iterator<Iter> && minimal_perfect_hash_detail::is_allocator<Alloc>
minimal_perfect_hash_set(Iter, Iter, Alloc) ->
minimal_perfect_hash_set<std::iter_value_t<Iter>, stdex::hash<std::iter_value_t<Iter>>,
                         std::equal_to<std::iter_value_t<Iter>>,
                         stdex::ptr_hash_heap_options<Alloc>::profile_default()>;

template <class Iter, class Hash, class Alloc>
requires std::input_iterator<Iter>
      && minimal_perfect_hash_detail::is_hasher_for<Hash, std::iter_value_t<Iter>>
      && minimal_perfect_hash_detail::is_allocator<Alloc>
minimal_perfect_hash_set(Iter, Iter, Hash, Alloc) ->
minimal_perfect_hash_set<std::iter_value_t<Iter>, Hash, std::equal_to<std::iter_value_t<Iter>>,
                         stdex::ptr_hash_heap_options<Alloc>::profile_default()>;

// ---------- CTAD initializer_list ----------
template <class Key,
          class Hash  = stdex::hash<Key>,
          class KeyEq = std::equal_to<Key>,
          class Alloc = std::allocator<Key>>
requires minimal_perfect_hash_detail::is_hasher_for<Hash, Key>
      && (!minimal_perfect_hash_detail::is_allocator<KeyEq>)
      && minimal_perfect_hash_detail::is_allocator<Alloc>
minimal_perfect_hash_set(std::initializer_list<Key>, Hash = Hash(), KeyEq = KeyEq(), Alloc = Alloc()) ->
minimal_perfect_hash_set<Key, Hash, KeyEq, stdex::ptr_hash_heap_options<Alloc>::profile_default()>;

template <class Key, class Alloc>
requires minimal_perfect_hash_detail::is_allocator<Alloc>
minimal_perfect_hash_set(std::initializer_list<Key>, Alloc) ->
minimal_perfect_hash_set<Key, stdex::hash<Key>, std::equal_to<Key>,
                         stdex::ptr_hash_heap_options<Alloc>::profile_default()>;

template <class Key, class Hash, class Alloc>
requires minimal_perfect_hash_detail::is_hasher_for<Hash, Key>
      && minimal_perfect_hash_detail::is_allocator<Alloc>
minimal_perfect_hash_set(std::initializer_list<Key>, Hash, Alloc) ->
minimal_perfect_hash_set<Key, Hash, std::equal_to<Key>,
                         stdex::ptr_hash_heap_options<Alloc>::profile_default()>;

#ifdef __cpp_lib_containers_ranges
// ---------- CTAD from_range ----------
template <std::ranges::input_range Rng,
          class Hash  = stdex::hash<std::ranges::range_value_t<Rng>>,
          class KeyEq = std::equal_to<std::ranges::range_value_t<Rng>>,
          class Alloc = std::allocator<std::ranges::range_value_t<Rng>>>
requires minimal_perfect_hash_detail::is_hasher_for<Hash, std::ranges::range_value_t<Rng>>
      && (!minimal_perfect_hash_detail::is_allocator<KeyEq>)
      && minimal_perfect_hash_detail::is_allocator<Alloc>
minimal_perfect_hash_set(std::from_range_t, Rng &&, Hash = Hash(), KeyEq = KeyEq(), Alloc = Alloc()) ->
minimal_perfect_hash_set<std::ranges::range_value_t<Rng>, Hash, KeyEq,
                         stdex::ptr_hash_heap_options<Alloc>::profile_default()>;

template <std::ranges::input_range Rng, class Alloc>
requires minimal_perfect_hash_detail::is_allocator<Alloc>
minimal_perfect_hash_set(std::from_range_t, Rng &&, Alloc) ->
minimal_perfect_hash_set<std::ranges::range_value_t<Rng>,
                         stdex::hash<std::ranges::range_value_t<Rng>>,
                         std::equal_to<std::ranges::range_value_t<Rng>>,
                         stdex::ptr_hash_heap_options<Alloc>::profile_default()>;

template <std::ranges::input_range Rng, class Hash, class Alloc>
requires minimal_perfect_hash_detail::is_hasher_for<Hash, std::ranges::range_value_t<Rng>>
      && minimal_perfect_hash_detail::is_allocator<Alloc>
minimal_perfect_hash_set(std::from_range_t, Rng &&, Hash, Alloc) ->
minimal_perfect_hash_set<std::ranges::range_value_t<Rng>, Hash,
                         std::equal_to<std::ranges::range_value_t<Rng>>,
                         stdex::ptr_hash_heap_options<Alloc>::profile_default()>;
#endif // __cpp_lib_containers_ranges
} // namespace stdex
