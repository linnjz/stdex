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

#include <algorithm>        // std::fill, std::fill_n, std::lexicographical_compare_three_way, std::rotate
#include <cassert>          // assert
#include <compare>          // std::three_way_comparable, std::three_way_comparable_with, std::weak_ordering
#include <concepts>         // std::constructible_from, std::convertible_to
#include <cstddef>          // std::size_t, std::ptrdiff_t
#include <cstdlib>          // std::abort
#include <initializer_list> // std::initializer_list
#include <iterator>         // std::input_iterator, std::sentinel_for, std::iter_reference_t
#include <memory>           // std::allocator, std::allocator_traits
#include <ranges>           // std::ranges::input_range, std::from_range_t, std::ranges::begin/end
#include <stdexcept>        // std::out_of_range
#include <type_traits>      // std::is_nothrow_move_constructible_v, std::remove_cv_t
#include <utility>          // std::forward, std::move
#include <version>          // __cpp_lib_containers_ranges

#include "inline_storage.hpp"

namespace stdex
{
/**
 * @brief Contiguous sequence with up to @p N inline elements and a heap
 *        fallback, built directly on @c inline_storage.
 *
 * @details This is @c inlined_bitset with the bit packing removed: the same
 * five-rule wiring, iterator/observer/proxy-free surface, assign/insert/erase
 * family and comparisons, over one @c inline_storage instance. That base owns
 * exactly one allocator and one inline-or-heap block, so a container built on
 * it never carries a second allocator the way composing std::vector members
 * would.
 */
template <class T, std::size_t N, class Allocator = std::allocator<T>>
class inlined_vector : private inline_storage<T, N, Allocator>
{
  using base = inline_storage<T, N, Allocator>;

public:
  using typename base::value_type;
  using typename base::allocator_type;
  using typename base::allocator_traits;
  using typename base::size_type;
  using typename base::difference_type;
  using typename base::reference;
  using typename base::const_reference;
  using typename base::pointer;
  using typename base::const_pointer;
  using typename base::iterator;
  using typename base::const_iterator;
  using typename base::reverse_iterator;
  using typename base::const_reverse_iterator;

  using base::inline_capacity;

private:
  using base::copy_assignment_from_;
  using base::copy_construct_from_;
  using base::assign_range_;
  using base::destroy_element_unchecked_;
  using base::destroy_elements_unchecked_;
  using base::emplace_one_at_back_;
  using base::get_flag_;
  using base::insert_range_;
  using base::move_assignment_from_;
  using base::move_construct_from_;
  using base::construct_elements_unchecked_;
  using base::set_size_;
  using base::storage_compare_;
  using base::storage_equal_;
  using base::throw_length_error_;

public:
  // Re-expose base observers that private inheritance would otherwise hide.
  using base::begin;
  using base::capacity;
  using base::cbegin;
  using base::cend;
  using base::clear;
  using base::crbegin;
  using base::crend;
  using base::data;
  using base::empty;
  using base::end;
  using base::get_allocator;
  using base::is_heap;
  using base::is_stack;
  using base::max_size;
  using base::rbegin;
  using base::rend;
  using base::reserve;
  using base::resize;
  using base::shrink_to_fit;
  using base::size;
  using base::swap;

  // ---------- construction ----------
  constexpr inlined_vector() noexcept
      : base { allocator_type {} }
  {
  }

  using base::base; // (n), (n, value), (first, last), (allocator) constructors

  constexpr inlined_vector(inlined_vector const &other)
      : base { allocator_traits::select_on_container_copy_construction(other.get_allocator()) }
  {
    copy_construct_from_(other);
  }

  constexpr inlined_vector(inlined_vector const &other, allocator_type const &alloc)
      : base { alloc }
  {
    copy_construct_from_(other);
  }

  constexpr inlined_vector(inlined_vector &&other) noexcept(noexcept(move_construct_from_(std::move(other))))
      : base { std::move(other.get_allocator()) }
  {
    (void) move_construct_from_(std::move(other));
  }

  constexpr inlined_vector(inlined_vector &&other, allocator_type const &alloc)
      : base { alloc }
  {
    (void) move_construct_from_(std::move(other), alloc);
  }

  template <class InputIt, std::sentinel_for<InputIt> Sentinel>
  requires std::constructible_from<value_type, std::iter_reference_t<InputIt>>
  constexpr inlined_vector(InputIt first, Sentinel last, allocator_type const &alloc = allocator_type {})
      : base { first, last, alloc }
  {
  }

  constexpr inlined_vector(std::initializer_list<value_type> ilist, allocator_type const &alloc = allocator_type {})
      : base { ilist.begin(), ilist.end(), alloc }
  {
  }

#ifdef __cpp_lib_containers_ranges
  template <std::ranges::input_range Range>
  requires std::constructible_from<value_type, std::ranges::range_reference_t<Range>>
  constexpr inlined_vector(std::from_range_t, Range &&range, allocator_type const &alloc = allocator_type {})
      : base { std::ranges::begin(range), std::ranges::end(range), alloc }
  {
  }
#endif

  constexpr ~inlined_vector() = default;

  constexpr inlined_vector &operator= (inlined_vector const &other)
  {
    copy_assignment_from_(other);
    return *this;
  }

  constexpr inlined_vector &operator= (inlined_vector &&other)
      noexcept((allocator_traits::propagate_on_container_move_assignment::value ||
                allocator_traits::is_always_equal::value) &&
               std::is_nothrow_move_constructible_v<value_type> &&
               std::is_nothrow_move_assignable_v<value_type>)
  {
    (void) move_assignment_from_(std::move(other));
    return *this;
  }

  constexpr inlined_vector &operator= (std::initializer_list<value_type> ilist)
  {
    assign(ilist.begin(), ilist.end());
    return *this;
  }

  // ---------- element access ----------
  [[nodiscard]] constexpr reference operator[] (size_type const index) noexcept
  {
    assert(index < size());
    return data()[index];
  }

  [[nodiscard]] constexpr const_reference operator[] (size_type const index) const noexcept
  {
    assert(index < size());
    return data()[index];
  }

  [[nodiscard]] constexpr reference at(size_type const index)
  {
    if (index >= size()) [[unlikely]] throw_out_of_range_();
    return data()[index];
  }

  [[nodiscard]] constexpr const_reference at(size_type const index) const
  {
    if (index >= size()) [[unlikely]] throw_out_of_range_();
    return data()[index];
  }

  [[nodiscard]] constexpr reference front() noexcept
  {
    assert(!empty());
    return data()[0];
  }

  [[nodiscard]] constexpr const_reference front() const noexcept
  {
    assert(!empty());
    return data()[0];
  }

  [[nodiscard]] constexpr reference back() noexcept
  {
    assert(!empty());
    return data()[size() - 1];
  }

  [[nodiscard]] constexpr const_reference back() const noexcept
  {
    assert(!empty());
    return data()[size() - 1];
  }

  // ---------- modifiers ----------
  template <class... Args>
  requires std::constructible_from<value_type, Args...>
  constexpr reference emplace_back(Args &&...args)
  {
    return emplace_one_at_back_(std::forward<Args>(args)...);
  }

  constexpr reference push_back(const_reference value) { return emplace_back(value); }
  constexpr reference push_back(value_type &&value) { return emplace_back(std::move(value)); }

  constexpr void pop_back() noexcept
  {
    assert(!empty());
    destroy_element_unchecked_(data() + size() - 1);
    set_size_(size() - 1, get_flag_());
  }

  template <class... Args>
  requires std::constructible_from<value_type, Args...> && std::movable<value_type>
  constexpr iterator emplace(const_iterator position, Args &&...args)
  {
    size_type const pos = static_cast<size_type>(position - cbegin());
    assert(pos <= size());
    size_type const old = size();
    (void) emplace_one_at_back_(std::forward<Args>(args)...);
    value_type *const storage = data();
    std::rotate(storage + pos, storage + old, storage + old + 1);
    return begin() + pos;
  }

  constexpr iterator insert(const_iterator position, const_reference value)
  requires std::movable<value_type>
  {
    size_type const pos = static_cast<size_type>(position - cbegin());
    assert(pos <= size());
    value_type tmp(value); // aliasing-safe: value may reference an element moved by rotate
    size_type const old = size();
    (void) emplace_one_at_back_(std::move(tmp));
    value_type *const storage = data();
    std::rotate(storage + pos, storage + old, storage + old + 1);
    return begin() + pos;
  }

  constexpr iterator insert(const_iterator position, value_type &&value)
  requires std::movable<value_type>
  {
    size_type const pos = static_cast<size_type>(position - cbegin());
    assert(pos <= size());
    value_type tmp(std::move(value));
    size_type const old = size();
    (void) emplace_one_at_back_(std::move(tmp));
    value_type *const storage = data();
    std::rotate(storage + pos, storage + old, storage + old + 1);
    return begin() + pos;
  }

  constexpr iterator insert(const_iterator position, size_type const count, const_reference value)
  requires std::movable<value_type>
  {
    size_type const pos = static_cast<size_type>(position - cbegin());
    assert(pos <= size());
    if (count == 0) [[unlikely]] return begin() + pos;
    if (count > max_size() - size()) [[unlikely]] throw_length_error_();
    value_type const tmp(value); // aliasing-safe: value may reference an element moved by rotate
    size_type const old = size();
    // Grow once, fill the appended block in bulk, then rotate it into place.
    // Avoids a per-element emplace loop (each of which re-checks capacity and
    // flags); the bulk fill is a single vectorizable pass for trivial values.
    reserve(old + count);
    construct_elements_unchecked_(data() + old, count, tmp);
    set_size_(old + count, get_flag_());
    value_type *const storage = data();
    std::rotate(storage + pos, storage + old, storage + old + count);
    return begin() + pos;
  }

  template <std::input_iterator InputIt, std::sentinel_for<InputIt> Sentinel>
  requires std::constructible_from<value_type, std::iter_reference_t<InputIt>>
  constexpr iterator insert(const_iterator position, InputIt first, Sentinel last)
  {
    size_type const pos = static_cast<size_type>(position - cbegin());
    assert(pos <= size());
    insert_range_(pos, first, last);
    return begin() + pos;
  }

  constexpr iterator insert(const_iterator position, std::initializer_list<value_type> ilist)
  {
    return insert(position, ilist.begin(), ilist.end());
  }

#ifdef __cpp_lib_containers_ranges
  template <std::ranges::input_range Range>
  requires std::constructible_from<value_type, std::ranges::range_reference_t<Range>>
  constexpr iterator insert_range(const_iterator position, Range &&range)
  {
    return insert(position, std::ranges::begin(range), std::ranges::end(range));
  }
#endif

  constexpr iterator erase(const_iterator position) noexcept(std::is_nothrow_move_assignable_v<value_type>)
  requires std::movable<value_type>
  {
    return erase(position, position + 1);
  }

  constexpr iterator erase(const_iterator first, const_iterator last) noexcept(std::is_nothrow_move_assignable_v<value_type>)
  requires std::movable<value_type>
  {
    size_type const index = static_cast<size_type>(first - cbegin());
    size_type const count = static_cast<size_type>(last - first);
    if (count == 0) [[unlikely]] return begin() + index;
    value_type *const storage = data();
    size_type const old       = size();
    std::move(storage + index + count, storage + old, storage + index);
    destroy_elements_unchecked_(storage + (old - count), count);
    set_size_(old - count, get_flag_());
    return begin() + index;
  }

  constexpr void assign(size_type const count, const_reference value)
  requires std::movable<value_type>
  {
    if (count > max_size()) [[unlikely]] throw_length_error_();
    value_type const tmp(value); // value may alias an element overwritten/destroyed below
    if constexpr (std::is_copy_assignable_v<value_type>) {
      size_type const old    = size();
      size_type const common = count < old ? count : old;
      if (common != 0) (void) std::fill_n(data(), common, tmp);
      if (count > old) {
        reserve(count);
        construct_elements_unchecked_(data() + old, count - old, tmp);
        set_size_(count, get_flag_());
      } else {
        destroy_elements_unchecked_(data() + count, old - count);
        set_size_(count, get_flag_());
      }
    } else {
      // Copy-constructible but not copy-assignable: rebuild from scratch.
      clear();
      reserve(count);
      if (count != 0) construct_elements_unchecked_(data(), count, tmp);
      set_size_(count, get_flag_());
    }
  }

  constexpr void assign(std::initializer_list<value_type> ilist) { assign(ilist.begin(), ilist.end()); }

  template <std::input_iterator InputIt, std::sentinel_for<InputIt> Sentinel>
  requires std::constructible_from<value_type, std::iter_reference_t<InputIt>>
  constexpr void assign(InputIt first, Sentinel last)
  {
    assign_range_(first, last);
  }

#ifdef __cpp_lib_containers_ranges
  template <std::ranges::input_range Range>
  requires std::constructible_from<value_type, std::ranges::range_reference_t<Range>>
  constexpr void assign_range(Range &&range)
  {
    assign(std::ranges::begin(range), std::ranges::end(range));
  }
#endif

  template <class InputIt, std::sentinel_for<InputIt> Sentinel>
  requires std::constructible_from<value_type, std::iter_reference_t<InputIt>>
  constexpr void append(InputIt first, Sentinel last)
  {
    (void) insert(end(), first, last);
  }

  constexpr void append(std::initializer_list<value_type> ilist) { (void) insert(end(), ilist); }

#ifdef __cpp_lib_containers_ranges
  template <std::ranges::input_range Range>
  requires std::constructible_from<value_type, std::ranges::range_reference_t<Range>>
  constexpr void append_range(Range &&range)
  {
    (void) insert(end(), std::ranges::begin(range), std::ranges::end(range));
  }
#endif

  // ---------- comparison ----------
  [[nodiscard]] constexpr bool operator== (inlined_vector const &other) const
  {
    if (size() != other.size()) return false;
    return storage_equal_(static_cast<base const &>(other));
  }

  [[nodiscard]] constexpr auto operator<=> (inlined_vector const &other) const
  requires requires (value_type const &a, value_type const &b) { base::synth_three_way(a, b); }
  {
    return std::lexicographical_compare_three_way(begin(), end(), other.begin(), other.end(),
                                                  base::synth_three_way);
  }

  constexpr void swap(inlined_vector &other) noexcept(noexcept(base::swap(other)))
  {
    base::swap(other);
  }

  friend constexpr void swap(inlined_vector &lhs, inlined_vector &rhs) noexcept(noexcept(lhs.swap(rhs)))
  {
    lhs.swap(rhs);
  }

private:
  [[noreturn]] static void throw_out_of_range_()
  {
#if defined(_CPPUNWIND) || defined(__EXCEPTIONS)
    throw std::out_of_range("inlined_vector::at: index out of range");
#else
    std::abort();
#endif
  }
};
} // namespace stdex
