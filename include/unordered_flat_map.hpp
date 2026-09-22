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

#include <stdexcept> // std::out_of_range

#include "ufmap_traits.hpp"
#include "hash_table.hpp"
#include "hash.hpp"

namespace stdex
{
template <class KeyType, class ValueType, size_t N = 0,
          class Hasher = stdex::hash<KeyType>,
          class KeyEqual = std::equal_to<KeyType>,
          class Allocator = std::allocator<std::pair<KeyType, ValueType>>>
class unordered_flat_map : public unordered_flat_detail::hash_table<unordered_flat_detail::ufmap_traits<KeyType, ValueType, N>, 
                                                                    Hasher, 
                                                                    KeyEqual, 
                                                                    Allocator>
{
  using traits = unordered_flat_detail::ufmap_traits<KeyType, ValueType, N>;
  using base = unordered_flat_detail::hash_table<traits, Hasher, KeyEqual, Allocator>;

public:
  // ---------- exported member types ----------
  using typename base::hasher;
  using typename base::key_equal;
  using typename base::allocator_type;


  using mapped_type = typename traits::mapped_type;
  using typename base::key_type;
  using typename base::value_type;
  using typename base::size_type;
  using typename base::difference_type;
  using typename base::pointer;
  using typename base::const_pointer;
  using typename base::reference;
  using typename base::const_reference;

  using typename base::const_iterator;
  using typename base::iterator;
  using typename base::lazy_iterator;

  using typename base::node_type;
  using typename base::insert_return_type;

  using base::inline_capacity;
  using base::is_heap;
  using base::is_stack;

  // ---------- exported observers ----------
  using base::get_allocator;
  using base::hash_function;
  using base::key_eq;

  // ---------- exported capacity ----------
  using base::bucket_count;
  using base::capacity;
  using base::empty;
  using base::load_factor;
  using base::max_load_factor;
  using base::max_size;
  using base::size;

  // ---------- exported iterators ----------
  using base::begin;
  using base::cbegin;
  using base::cend;
  using base::end;

  // ---------- exported swap ----------
  using base::swap;

  // ---------- exported modifiers ----------
  using base::rehash;
  using base::reserve;
  using base::clear;
  using base::reset;

  using base::emplace;
  using base::emplace_hint;
  using base::insert;
  using base::insert_range;
  using base::erase;
  using base::extract;
  using base::merge;

  // ---------- exported lookup ----------
  using base::find;
  using base::count;
  using base::contains;
  using base::equal_range;

public:
  // ---------- five rules ----------
  constexpr unordered_flat_map()
      : base { hasher {}, key_equal {}, allocator_type {} } { };

  constexpr explicit unordered_flat_map(allocator_type const &alloc)
      : base { hasher {}, key_equal {}, alloc }
  {
  }

  constexpr unordered_flat_map(unordered_flat_map const &other)
      : base { other, std::allocator_traits<allocator_type>::select_on_container_copy_construction(other.get_allocator()) }
  {
  }

  constexpr unordered_flat_map(unordered_flat_map const &other, allocator_type const &alloc)
      : base { other, alloc }
  {
  }

  constexpr unordered_flat_map(unordered_flat_map &&other)
      : base { std::move(other) }
  {
  }

  constexpr unordered_flat_map(unordered_flat_map &&other, allocator_type const &alloc)
      : base { std::move(other), alloc }
  {
  }

  constexpr ~unordered_flat_map() = default;

  constexpr unordered_flat_map &operator= (unordered_flat_map const &other)
  {
    base::operator= (other);
    return *this;
  }

  constexpr unordered_flat_map &operator= (unordered_flat_map &&other) noexcept(std::is_nothrow_move_assignable_v<base>)
  {
    base::operator= (std::move(other));
    return *this;
  }

  // ---------- bucket / hash / key_equal constructor ----------
  constexpr explicit unordered_flat_map(size_type bucket_count)
      : base { hasher {}, key_equal {}, allocator_type {} }
  {
    if (bucket_count > 0) this->reserve(bucket_count);
  }

  constexpr unordered_flat_map(size_type bucket_count, allocator_type const &alloc)
      : base { hasher {}, key_equal {}, alloc }
  {
    if (bucket_count > 0) this->reserve(bucket_count);
  }

  constexpr unordered_flat_map(size_type bucket_count, hasher const &hash)
      : base { hash, key_equal {}, allocator_type {} }
  {
    if (bucket_count > 0) this->reserve(bucket_count);
  }

  constexpr unordered_flat_map(size_type bucket_count, hasher const &hash, allocator_type const &alloc)
      : base { hash, key_equal {}, alloc }
  {
    if (bucket_count > 0) this->reserve(bucket_count);
  }

  constexpr unordered_flat_map(size_type bucket_count, hasher const &hash, key_equal const &eq)
      : base { hash, eq, allocator_type {} }
  {
    if (bucket_count > 0) this->reserve(bucket_count);
  }

  constexpr unordered_flat_map(size_type bucket_count, hasher const &hash, key_equal const &eq, allocator_type const &alloc)
      : base { hash, eq, alloc }
  {
    if (bucket_count > 0) this->reserve(bucket_count);
  }

  // ---------- iterator-range constructor ----------
  template <class Iter>
  constexpr unordered_flat_map(Iter first, Iter last)
      : base { hasher {}, key_equal {}, allocator_type {} }
  {
    this->insert(first, last);
  }

  template <class Iter>
  constexpr unordered_flat_map(Iter first, Iter last, allocator_type const &alloc)
      : base { hasher {}, key_equal {}, alloc }
  {
    this->insert(first, last);
  }

  template <class Iter>
  constexpr unordered_flat_map(Iter first, Iter last, size_type bucket_count)
      : base { hasher {}, key_equal {}, allocator_type {} }
  {
    if (bucket_count > 0) this->reserve(bucket_count);
    this->insert(first, last);
  }

  template <class Iter>
  constexpr unordered_flat_map(Iter first, Iter last, size_type bucket_count, allocator_type const &alloc)
      : base { hasher {}, key_equal {}, alloc }
  {
    if (bucket_count > 0) this->reserve(bucket_count);
    this->insert(first, last);
  }

  template <class Iter>
  constexpr unordered_flat_map(Iter first, Iter last, size_type bucket_count, hasher const &hash)
      : base { hash, key_equal {}, allocator_type {} }
  {
    if (bucket_count > 0) this->reserve(bucket_count);
    this->insert(first, last);
  }

  template <class Iter>
  constexpr unordered_flat_map(Iter first, Iter last, size_type bucket_count, hasher const &hash, allocator_type const &alloc)
      : base { hash, key_equal {}, alloc }
  {
    if (bucket_count > 0) this->reserve(bucket_count);
    this->insert(first, last);
  }

  template <class Iter>
  constexpr unordered_flat_map(Iter first, Iter last, size_type bucket_count, hasher const &hash, key_equal const &eq)
      : base { hash, eq, allocator_type {} }
  {
    if (bucket_count > 0) this->reserve(bucket_count);
    this->insert(first, last);
  }

  template <class Iter>
  constexpr unordered_flat_map(Iter first, Iter last, size_type bucket_count, hasher const &hash, key_equal const &eq, allocator_type const &alloc)
      : base { hash, eq, alloc }
  {
    if (bucket_count > 0) this->reserve(bucket_count);
    this->insert(first, last);
  }

  // ---------- from_range constructor ----------
  template <unordered_flat_detail::container_compatible_range<value_type> Rng>
  constexpr unordered_flat_map(std::from_range_t, Rng &&rng)
      : base { hasher {}, key_equal {}, allocator_type {} }
  {
    this->insert_range(std::forward<Rng>(rng));
  }

  template <unordered_flat_detail::container_compatible_range<value_type> Rng>
  constexpr unordered_flat_map(std::from_range_t, Rng &&rng, allocator_type const &alloc)
      : base { hasher {}, key_equal {}, alloc }
  {
    this->insert_range(std::forward<Rng>(rng));
  }

  template <unordered_flat_detail::container_compatible_range<value_type> Rng>
  constexpr unordered_flat_map(std::from_range_t, Rng &&rng, size_type bucket_count)
      : base { hasher {}, key_equal {}, allocator_type {} }
  {
    if (bucket_count > 0) this->reserve(bucket_count);
    this->insert_range(std::forward<Rng>(rng));
  }

  template <unordered_flat_detail::container_compatible_range<value_type> Rng>
  constexpr unordered_flat_map(std::from_range_t, Rng &&rng, size_type bucket_count, allocator_type const &alloc)
      : base { hasher {}, key_equal {}, alloc }
  {
    if (bucket_count > 0) this->reserve(bucket_count);
    this->insert_range(std::forward<Rng>(rng));
  }

  template <unordered_flat_detail::container_compatible_range<value_type> Rng>
  constexpr unordered_flat_map(std::from_range_t, Rng &&rng, size_type bucket_count, hasher const &hash)
      : base { hash, key_equal {}, allocator_type {} }
  {
    if (bucket_count > 0) this->reserve(bucket_count);
    this->insert_range(std::forward<Rng>(rng));
  }

  template <unordered_flat_detail::container_compatible_range<value_type> Rng>
  constexpr unordered_flat_map(std::from_range_t, Rng &&rng, size_type bucket_count, hasher const &hash, allocator_type const &alloc)
      : base { hash, key_equal {}, alloc }
  {
    if (bucket_count > 0) this->reserve(bucket_count);
    this->insert_range(std::forward<Rng>(rng));
  }

  template <unordered_flat_detail::container_compatible_range<value_type> Rng>
  constexpr unordered_flat_map(std::from_range_t, Rng &&rng, size_type bucket_count, hasher const &hash, key_equal const &eq)
      : base { hash, eq, allocator_type {} }
  {
    if (bucket_count > 0) this->reserve(bucket_count);
    this->insert_range(std::forward<Rng>(rng));
  }

  template <unordered_flat_detail::container_compatible_range<value_type> Rng>
  constexpr unordered_flat_map(std::from_range_t, Rng &&rng, size_type bucket_count, hasher const &hash, key_equal const &eq, allocator_type const &alloc)
      : base { hash, eq, alloc }
  {
    if (bucket_count > 0) this->reserve(bucket_count);
    this->insert_range(std::forward<Rng>(rng));
  }

  // ---------- initializer_list constructor ----------
  constexpr unordered_flat_map(std::initializer_list<value_type> ilist)
      : base { hasher {}, key_equal {}, allocator_type {} }
  {
    this->insert(ilist.begin(), ilist.end());
  }

  constexpr unordered_flat_map(std::initializer_list<value_type> ilist, allocator_type const &alloc)
      : base { hasher {}, key_equal {}, alloc }
  {
    this->insert(ilist.begin(), ilist.end());
  }

  constexpr unordered_flat_map(std::initializer_list<value_type> ilist, size_type bucket_count)
      : base { hasher {}, key_equal {}, allocator_type {} }
  {
    if (bucket_count > 0) this->reserve(bucket_count);
    this->insert(ilist.begin(), ilist.end());
  }

  constexpr unordered_flat_map(std::initializer_list<value_type> ilist, size_type bucket_count, allocator_type const &alloc)
      : base { hasher {}, key_equal {}, alloc }
  {
    if (bucket_count > 0) this->reserve(bucket_count);
    this->insert(ilist.begin(), ilist.end());
  }

  constexpr unordered_flat_map(std::initializer_list<value_type> ilist, size_type bucket_count, hasher const &hash)
      : base { hash, key_equal {}, allocator_type {} }
  {
    if (bucket_count > 0) this->reserve(bucket_count);
    this->insert(ilist.begin(), ilist.end());
  }

  constexpr unordered_flat_map(std::initializer_list<value_type> ilist, size_type bucket_count, hasher const &hash, allocator_type const &alloc)
      : base { hash, key_equal {}, alloc }
  {
    if (bucket_count > 0) this->reserve(bucket_count);
    this->insert(ilist.begin(), ilist.end());
  }

  constexpr unordered_flat_map(std::initializer_list<value_type> ilist, size_type bucket_count, hasher const &hash, key_equal const &eq)
      : base { hash, eq, allocator_type {} }
  {
    if (bucket_count > 0) this->reserve(bucket_count);
    this->insert(ilist.begin(), ilist.end());
  }

  constexpr unordered_flat_map(std::initializer_list<value_type> ilist, size_type bucket_count, hasher const &hash, key_equal const &eq, allocator_type const &alloc)
      : base { hash, eq, alloc }
  {
    if (bucket_count > 0) this->reserve(bucket_count);
    this->insert(ilist.begin(), ilist.end());
  }

  // ---------- initializer_list assignment ----------
  constexpr unordered_flat_map &operator= (std::initializer_list<value_type> ilist)
  {
    this->clear();
    this->insert(ilist.begin(), ilist.end());
    return *this;
  }

  // ---------- element access ----------
  constexpr mapped_type &operator[] (key_type const &key_val)
  {
    return this->emplace(std::piecewise_construct, std::forward_as_tuple(key_val), std::forward_as_tuple()).first->second;
  }

  constexpr mapped_type &operator[] (key_type &&key_val)
  {
    return this->emplace(std::piecewise_construct, std::forward_as_tuple(std::move(key_val)), std::forward_as_tuple()).first->second;
  }

  constexpr mapped_type &at(key_type const &key_val)
  {
    auto it = this->find(key_val);
    if (it == this->end()) throw std::out_of_range("unordered_flat_map::at: key not found");
    return it->second;
  }

  constexpr mapped_type const &at(key_type const &key_val) const
  {
    auto it = this->find(key_val);
    if (it == this->end()) throw std::out_of_range("unordered_flat_map::at: key not found");
    return it->second;
  }

  template <class K> requires unordered_flat_detail::heterogeneous_key<K const &, hasher, key_equal, iterator, const_iterator>
  constexpr mapped_type &at(K const &key_val)
  {
    auto it = this->find(key_val);
    if (it == this->end()) throw std::out_of_range("unordered_flat_map::at: key not found");
    return it->second;
  }

  template <class K> requires unordered_flat_detail::heterogeneous_key<K const &, hasher, key_equal, iterator, const_iterator>
  constexpr mapped_type const &at(K const &key_val) const
  {
    auto it = this->find(key_val);
    if (it == this->end()) throw std::out_of_range("unordered_flat_map::at: key not found");
    return it->second;
  }

  // ---------- try_emplace ----------
  template <class... Mappedty>
  constexpr std::pair<iterator, bool> try_emplace(key_type const &key_val, Mappedty &&...args)
  {
    return this->emplace(std::piecewise_construct, 
                         std::forward_as_tuple(key_val), 
                         std::forward_as_tuple(std::forward<Mappedty>(args)...));
  }

  template <class... Mappedty>
  constexpr std::pair<iterator, bool> try_emplace(key_type &&key_val, Mappedty &&...args)
  {
    return this->emplace(std::piecewise_construct, 
                         std::forward_as_tuple(std::move(key_val)), 
                         std::forward_as_tuple(std::forward<Mappedty>(args)...));
  }

  template <class... Mappedty>
  constexpr iterator try_emplace(const_iterator /*hint*/, key_type const &key_val, Mappedty &&...args)
  {
    return try_emplace(key_val, std::forward<Mappedty>(args)...).first;
  }

  template <class... Mappedty>
  constexpr iterator try_emplace(const_iterator /*hint*/, key_type &&key_val, Mappedty &&...args)
  {
    return try_emplace(std::move(key_val), std::forward<Mappedty>(args)...).first;
  }

  // ---------- insert_or_assign ----------
  template <class Mappedty>
  constexpr std::pair<iterator, bool> insert_or_assign(key_type const &key_val, Mappedty &&obj)
  {
    auto it = this->find(key_val);
    if (it != this->end()) {
      it->second = std::forward<Mappedty>(obj);
      return { it, false };
    }
    return this->emplace(std::piecewise_construct, 
                         std::forward_as_tuple(key_val), 
                         std::forward_as_tuple(std::forward<Mappedty>(obj)));
  }

  template <class Mappedty>
  constexpr std::pair<iterator, bool> insert_or_assign(key_type &&key_val, Mappedty &&obj)
  {
    auto it = this->find(key_val);
    if (it != this->end()) {
      it->second = std::forward<Mappedty>(obj);
      return { it, false };
    }
    return this->emplace(std::piecewise_construct, 
                         std::forward_as_tuple(std::move(key_val)), 
                         std::forward_as_tuple(std::forward<Mappedty>(obj)));
  }

  template <class Mappedty>
  constexpr iterator insert_or_assign(const_iterator /*hint*/, key_type const &key_val, Mappedty &&obj)
  {
    return insert_or_assign(key_val, std::forward<Mappedty>(obj)).first;
  }

  template <class Mappedty>
  constexpr iterator insert_or_assign(const_iterator /*hint*/, key_type &&key_val, Mappedty &&obj)
  {
    return insert_or_assign(std::move(key_val), std::forward<Mappedty>(obj)).first;
  }
};

// ---------- CTAD iterator-range ----------
template <class Iter,
          class Hash  = stdex::hash<unordered_flat_detail::guide_key_t<Iter>>,
          class KeyEq = std::equal_to<unordered_flat_detail::guide_key_t<Iter>>,
          class Alloc = std::allocator<std::iter_value_t<Iter>>>
requires std::input_iterator<Iter>
      && unordered_flat_detail::is_hasher_for<Hash, unordered_flat_detail::guide_key_t<Iter>>
      && (!unordered_flat_detail::is_allocator<KeyEq>)
      && unordered_flat_detail::is_allocator<Alloc>
unordered_flat_map(Iter, Iter, unordered_flat_detail::guide_size_type_t<Alloc> = 0, Hash = Hash(), KeyEq = KeyEq(), Alloc = Alloc()) -> 
unordered_flat_map<unordered_flat_detail::guide_key_t<Iter>, unordered_flat_detail::guide_mapped_t<Iter>, 0, Hash, KeyEq, Alloc>;

template <class Iter, class Alloc>
requires std::input_iterator<Iter> && unordered_flat_detail::is_allocator<Alloc>
unordered_flat_map(Iter, Iter, Alloc) -> 
unordered_flat_map<unordered_flat_detail::guide_key_t<Iter>, unordered_flat_detail::guide_mapped_t<Iter>, 0,
                   stdex::hash<unordered_flat_detail::guide_key_t<Iter>>, std::equal_to<unordered_flat_detail::guide_key_t<Iter>>, Alloc>;

template <class Iter, class Alloc>
requires std::input_iterator<Iter> && unordered_flat_detail::is_allocator<Alloc>
unordered_flat_map(Iter, Iter, unordered_flat_detail::guide_size_type_t<Alloc>, Alloc) -> 
unordered_flat_map<unordered_flat_detail::guide_key_t<Iter>, unordered_flat_detail::guide_mapped_t<Iter>, 0,
                   stdex::hash<unordered_flat_detail::guide_key_t<Iter>>, std::equal_to<unordered_flat_detail::guide_key_t<Iter>>, Alloc>;

template <class Iter, class Hash, class Alloc>
requires std::input_iterator<Iter>
      && unordered_flat_detail::is_hasher_for<Hash, unordered_flat_detail::guide_key_t<Iter>>
      && unordered_flat_detail::is_allocator<Alloc>
unordered_flat_map(Iter, Iter, unordered_flat_detail::guide_size_type_t<Alloc>, Hash, Alloc) -> 
unordered_flat_map<unordered_flat_detail::guide_key_t<Iter>, unordered_flat_detail::guide_mapped_t<Iter>, 0, 
                   Hash, std::equal_to<unordered_flat_detail::guide_key_t<Iter>>, Alloc>;

// ---------- CTAD initializer_list ----------
template <class Key, class T,
          class Hash  = stdex::hash<Key>,
          class KeyEq = std::equal_to<Key>,
          class Alloc = std::allocator<std::pair<Key, T>>>
requires unordered_flat_detail::is_hasher_for<Hash, Key>
      && (!unordered_flat_detail::is_allocator<KeyEq>)
      && unordered_flat_detail::is_allocator<Alloc>
unordered_flat_map(std::initializer_list<std::pair<Key, T>>, unordered_flat_detail::guide_size_type_t<Alloc> = 0, Hash = Hash(), KeyEq = KeyEq(), Alloc = Alloc()) -> 
unordered_flat_map<Key, T, 0, Hash, KeyEq, Alloc>;

template <class Key, class T, class Alloc>
requires unordered_flat_detail::is_allocator<Alloc>
unordered_flat_map(std::initializer_list<std::pair<Key, T>>, Alloc) -> 
unordered_flat_map<Key, T, 0, stdex::hash<Key>, std::equal_to<Key>, Alloc>;

template <class Key, class T, class Alloc>
requires unordered_flat_detail::is_allocator<Alloc>
unordered_flat_map(std::initializer_list<std::pair<Key, T>>, unordered_flat_detail::guide_size_type_t<Alloc>, Alloc) -> 
unordered_flat_map<Key, T, 0, stdex::hash<Key>, std::equal_to<Key>, Alloc>;

template <class Key, class T, class Hash, class Alloc>
requires unordered_flat_detail::is_hasher_for<Hash, Key>
      && unordered_flat_detail::is_allocator<Alloc>
unordered_flat_map(std::initializer_list<std::pair<Key, T>>, unordered_flat_detail::guide_size_type_t<Alloc>, Hash, Alloc) -> 
unordered_flat_map<Key, T, 0, Hash, std::equal_to<Key>, Alloc>;

// ---------- CTAD from_range ----------
template <std::ranges::input_range Rng,
          class Hash  = stdex::hash<unordered_flat_detail::range_key_t<Rng>>,
          class KeyEq = std::equal_to<unordered_flat_detail::range_key_t<Rng>>,
          class Alloc = std::allocator<std::ranges::range_value_t<Rng>>>
requires unordered_flat_detail::is_hasher_for<Hash, unordered_flat_detail::range_key_t<Rng>>
      && (!unordered_flat_detail::is_allocator<KeyEq>)
      && unordered_flat_detail::is_allocator<Alloc>
unordered_flat_map(std::from_range_t, Rng &&, unordered_flat_detail::guide_size_type_t<Alloc> = 0, Hash = Hash(), KeyEq = KeyEq(), Alloc = Alloc()) -> 
unordered_flat_map<unordered_flat_detail::range_key_t<Rng>, unordered_flat_detail::range_mapped_t<Rng>, 0, Hash, KeyEq, Alloc>;

template <std::ranges::input_range Rng, class Alloc>
requires unordered_flat_detail::is_allocator<Alloc>
unordered_flat_map(std::from_range_t, Rng &&, Alloc) -> 
unordered_flat_map<unordered_flat_detail::range_key_t<Rng>, unordered_flat_detail::range_mapped_t<Rng>, 0,
                   stdex::hash<unordered_flat_detail::range_key_t<Rng>>, std::equal_to<unordered_flat_detail::range_key_t<Rng>>, Alloc>;

template <std::ranges::input_range Rng, class Alloc>
requires unordered_flat_detail::is_allocator<Alloc>
unordered_flat_map(std::from_range_t, Rng &&, unordered_flat_detail::guide_size_type_t<Alloc>, Alloc) -> 
unordered_flat_map<unordered_flat_detail::range_key_t<Rng>, unordered_flat_detail::range_mapped_t<Rng>, 0,
                   stdex::hash<unordered_flat_detail::range_key_t<Rng>>, std::equal_to<unordered_flat_detail::range_key_t<Rng>>, Alloc>;

template <std::ranges::input_range Rng, class Hash, class Alloc>
requires unordered_flat_detail::is_hasher_for<Hash, unordered_flat_detail::range_key_t<Rng>>
      && unordered_flat_detail::is_allocator<Alloc>
unordered_flat_map(std::from_range_t, Rng &&, unordered_flat_detail::guide_size_type_t<Alloc>, Hash, Alloc) -> 
unordered_flat_map<unordered_flat_detail::range_key_t<Rng>, unordered_flat_detail::range_mapped_t<Rng>, 0, 
                   Hash, std::equal_to<unordered_flat_detail::range_key_t<Rng>>, Alloc>;

// ---------- non-member swap ----------
template <class KeyType, class ValueType, size_t N, class Hasher, class KeyEqual, class Allocator>
constexpr void swap(unordered_flat_map<KeyType, ValueType, N, Hasher, KeyEqual, Allocator> &a,
                    unordered_flat_map<KeyType, ValueType, N, Hasher, KeyEqual, Allocator> &b)
    noexcept(noexcept(a.swap(b)))
{
  a.swap(b);
}

// ---------- erase_if ----------
template <class KeyType, class ValueType, size_t N, class Hasher, class KeyEqual, class Allocator, class Pred>
typename unordered_flat_map<KeyType, ValueType, N, Hasher, KeyEqual, Allocator>::size_type
erase_if(unordered_flat_map<KeyType, ValueType, N, Hasher, KeyEqual, Allocator> &c, Pred pred)
{
  using map_t = unordered_flat_map<KeyType, ValueType, N, Hasher, KeyEqual, Allocator>;
  auto const old_size = c.size();
  for (auto it = c.begin(); it != c.end(); ) {
    if (pred(*it)) it = c.erase(it);   // lazy_iterator → iterator
    else           ++it;
  }
  return static_cast<typename map_t::size_type>(old_size - c.size());
}
} // namespace stdex

// ---------- pmr alias ----------
#include <memory_resource>
namespace stdex::pmr
{
template <class KeyType, class ValueType, size_t N = 0, class Hasher = stdex::hash<KeyType>, class KeyEqual = std::equal_to<KeyType>>
using unordered_flat_map = stdex::unordered_flat_map<
    KeyType,
    ValueType,
    N,
    Hasher,
    KeyEqual,
    std::pmr::polymorphic_allocator<std::pair<KeyType, ValueType>>>;

} // namespace stdex::pmr
