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

#include <algorithm>        // std::copy, std::equal, std::fill_n, std::move,
                            // std::lexicographical_compare_three_way, std::swap_ranges
#include <compare>
#include <concepts>         // std::constructible_from, std::default_initializable,
                            // std::equality_comparable, std::swappable
#include <cstddef>          // std::size_t
#include <initializer_list> // std::initializer_list
#include <iterator>         // std::input_iterator, std::sentinel_for, std::iter_reference_t
#include <memory>           // std::addressof, std::destroy_n
#include <ranges>           // std::from_range_t, std::ranges::begin/end
#include <tuple>            // std::tuple_size, std::tuple_element (structured bindings)
#include <type_traits>      // std::is_object_v, std::is_nothrow_*, std::is_assignable_v,
                            // std::is_trivially_*, std::integral_constant
#include <utility>          // std::in_place_t
#include <version>          // __cpp_lib_containers_ranges

#include "inplace_storage.hpp"
#include "uninitialized_algorithms.hpp"

namespace stdex
{
template <class T, std::size_t N>
class inplace_array : private inplace_storage<inplace_storage_detail::storage_no_size_base, T, N>
{
  static_assert(std::is_object_v<T>, "containers require object value types ([container.requirements])");
  using base = inplace_storage<inplace_storage_detail::storage_no_size_base, T, N>;

public:
  using value_type             = base::value_type;
  using pointer                = base::pointer;
  using const_pointer          = base::const_pointer;
  using reference              = base::reference;
  using const_reference        = base::const_reference;
  using size_type              = base::size_type;
  using difference_type        = base::difference_type;
  using iterator               = base::iterator;
  using const_iterator         = base::const_iterator;
  using reverse_iterator       = base::reverse_iterator;
  using const_reverse_iterator = base::const_reverse_iterator;

  constexpr inplace_array() noexcept(std::is_nothrow_default_constructible_v<value_type>)
  requires std::default_initializable<value_type>
      : base {}
  {
    ::stdex::uninitialized_value_construct_n(data(), static_cast<difference_type>(size()));
  }

  /// Leaves all N slots uninitialized. Precondition: the caller constructs
  /// every element before this object is destroyed; until then the container's
  /// destructor (which destroys N elements) would run over dead storage.
  constexpr inplace_array(uninitialized_t) noexcept
      : base {}
  {
  }

  // ---------- rule of five ----------
  // Destruction lives here (not in the storage base): see the note in
  // inplace_storage's destructor: the base subobject is destroyed during a
  // throwing constructor, when fewer than N elements are alive.
  constexpr ~inplace_array() requires std::is_trivially_destructible_v<T> = default;
  constexpr ~inplace_array() noexcept
  requires (!std::is_trivially_destructible_v<T>)
  {
    std::destroy_n(data(), size());
  }

  constexpr inplace_array(inplace_array const &)             requires std::is_trivially_copy_constructible_v<T> = default;
  constexpr inplace_array(inplace_array &&)                  requires std::is_trivially_move_constructible_v<T> = default;
  constexpr inplace_array &operator= (inplace_array const &) requires std::is_trivially_copy_assignable_v<T>    = default;
  constexpr inplace_array &operator= (inplace_array &&)      requires std::is_trivially_move_assignable_v<T>    = default;

  constexpr inplace_array(inplace_array const &other)
      noexcept(std::is_nothrow_copy_constructible_v<value_type>)
  requires (!std::is_trivially_copy_constructible_v<T> && std::is_copy_constructible_v<value_type>)
      : base {}
  {
    (void) ::stdex::uninitialized_copy(other.begin(), other.end(), data(), data() + size());
  }

  constexpr inplace_array(inplace_array &&other)
      noexcept(std::is_nothrow_move_constructible_v<value_type>)
  requires (!std::is_trivially_move_constructible_v<T> && std::is_move_constructible_v<value_type>)
      : base {}
  {
    (void) ::stdex::uninitialized_move(other.begin(), other.end(), data(), data() + size());
  }

  constexpr inplace_array &operator= (inplace_array const &other)
      noexcept(std::is_nothrow_copy_assignable_v<value_type>)
  requires (!std::is_trivially_copy_assignable_v<T> && std::is_copy_assignable_v<value_type>)
  {
    if (this != std::addressof(other)) std::copy(other.begin(), other.end(), data());
    return *this;
  }

  constexpr inplace_array &operator= (inplace_array &&other)
      noexcept(std::is_nothrow_move_assignable_v<value_type>)
  requires (!std::is_trivially_move_assignable_v<T> && std::is_move_assignable_v<value_type>)
  {
    if (this != std::addressof(other)) std::move(other.begin(), other.end(), data());
    return *this;
  }

  template <class... Args>
  requires std::constructible_from<value_type, Args const &...>
  constexpr inplace_array(std::in_place_t, Args const &...args)
      noexcept(std::is_nothrow_constructible_v<value_type, Args const &...>)
      : base {}
  {
    if constexpr (sizeof...(Args) == 1) {
      (void) ::stdex::uninitialized_fill_n(data(), static_cast<difference_type>(size()), (args, ...));
    } else {
      ::stdex::uninitialized_backout<value_type *> backout { data() };
      for (size_type i = 0; i < size(); ++i) backout.emplace_back(args...);
      backout.release();
    }
  }

  constexpr inplace_array(std::initializer_list<value_type> ilist)
      : base {}
  {
    if (ilist.size() != size()) [[unlikely]] {
      inplace_storage_detail::throw_length_error("inplace_array: initializer_list size must equal N");
    }
    // uninitialized_copy takes the memcpy fast path for bit-copyable elements
    // and destroys any already-built prefix if a copy throws.
    (void) ::stdex::uninitialized_copy(ilist.begin(), ilist.end(), data(), data() + size());
  }

  // ---------- iterator-pair construction ----------
  template <std::forward_iterator It, std::sentinel_for<It> Sent>
  requires std::constructible_from<value_type, std::iter_reference_t<It>>
  constexpr inplace_array(It first, Sent last)
      : base {}
  {
    if (static_cast<size_type>(std::ranges::distance(first, last)) != size()) [[unlikely]] {
      inplace_storage_detail::throw_length_error("inplace_array: range size must equal N");
    }
    (void) ::stdex::uninitialized_copy(first, last, data(), data() + size());
  }

  template <std::input_iterator It, std::sentinel_for<It> Sent>
  requires (!std::forward_iterator<It>) &&
           std::constructible_from<value_type, std::iter_reference_t<It>>
  constexpr inplace_array(It first, Sent last)
      : base {}
  {
    ::stdex::uninitialized_backout<value_type *> backout { data() };
    for (; first != last; ++first) {
      if (static_cast<size_type>(backout.last() - data()) == size()) [[unlikely]] {
        inplace_storage_detail::throw_length_error("inplace_array: range size must equal N");
      }
      backout.emplace_back(*first);
    }
    if (static_cast<size_type>(backout.last() - data()) != size()) [[unlikely]] {
      inplace_storage_detail::throw_length_error("inplace_array: range size must equal N");
    }
    backout.release();
  }

#ifdef __cpp_lib_containers_ranges
  template <inplace_storage_detail::container_compatible_range<value_type> R>
  constexpr inplace_array(std::from_range_t, R &&rg)
      : inplace_array(std::ranges::begin(rg), std::ranges::end(rg))
  {
  }
#endif

  // ---------- element access ----------
  using base::operator[];
  using base::at;
  using base::front;
  using base::back;
  using base::data;

  // ---------- capacity ----------
  using base::empty;
  using base::max_size;
  using base::size;

  // ---------- iterators ----------
  using base::begin;
  using base::end;
  using base::rbegin;
  using base::rend;
  using base::cbegin;
  using base::cend;
  using base::crbegin;
  using base::crend;

  constexpr void fill(const_reference value)
  requires std::is_assignable_v<value_type &, const_reference>
  {
    // Every element is already alive, so this is assignment, not construction.
    std::fill_n(data(), size(), value);
  }

  // Matches std::array: swapping an object with itself is undefined behavior
  // (the element ranges alias), so no self-check is performed.
  constexpr void swap(inplace_array &other) noexcept(std::is_nothrow_swappable_v<value_type>)
  requires std::swappable<value_type>
  {
    std::swap_ranges(data(), data() + size(), other.data());
  }

  friend constexpr void swap(inplace_array &lhs, inplace_array &rhs) noexcept(noexcept(lhs.swap(rhs)))
  {
    lhs.swap(rhs);
  }

  // ---------- comparison ----------
  friend constexpr bool operator== (inplace_array const &lhs, inplace_array const &rhs)
  requires std::equality_comparable<value_type>
  {
    return std::equal(lhs.begin(), lhs.end(), rhs.begin());
  }

  friend constexpr auto operator<=> (inplace_array const &lhs, inplace_array const &rhs)
  requires requires (value_type const &lhs_value, value_type const &rhs_value) {
    inplace_storage_detail::synth_three_way(lhs_value, rhs_value);
  }
  {
    return std::lexicographical_compare_three_way(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
                                                  inplace_storage_detail::synth_three_way);
  }
};

template <class T>
class inplace_array<T, 0> : private inplace_storage<inplace_storage_detail::storage_no_size_base, T, 0>
{
  static_assert(std::is_object_v<T>, "containers require object value types ([container.requirements])");
  using base = inplace_storage<inplace_storage_detail::storage_no_size_base, T, 0>;

public:
  using typename base::value_type;
  using typename base::pointer;
  using typename base::const_pointer;
  using typename base::reference;
  using typename base::const_reference;
  using typename base::size_type;
  using typename base::difference_type;
  using typename base::iterator;
  using typename base::const_iterator;
  using typename base::reverse_iterator;
  using typename base::const_reverse_iterator;

  // element access / capacity / iterators: reuse the empty versions from the base
  using base::operator[];
  using base::at;
  using base::front;
  using base::back;
  using base::data;
  using base::empty;
  using base::max_size;
  using base::size;
  using base::begin;
  using base::end;
  using base::rbegin;
  using base::rend;
  using base::cbegin;
  using base::cend;
  using base::crbegin;
  using base::crend;

  constexpr inplace_array() noexcept = default;
  constexpr inplace_array(uninitialized_t) noexcept {}
  constexpr inplace_array(inplace_array const &) = default;
  constexpr inplace_array(inplace_array &&) = default;
  constexpr inplace_array &operator= (inplace_array const &) = default;
  constexpr inplace_array &operator= (inplace_array &&) = default;
  constexpr ~inplace_array() = default;

  template <class... Args>
  constexpr inplace_array(std::in_place_t, Args const &...) noexcept {}

  constexpr inplace_array(std::initializer_list<value_type> ilist)
  {
    if (ilist.size() != 0) [[unlikely]]
      inplace_storage_detail::throw_length_error("inplace_array: initializer_list size must equal N");
  }

  template <std::input_iterator It, std::sentinel_for<It> Sent>
  constexpr inplace_array(It first, Sent last)
  {
    if (first != last) [[unlikely]]
      inplace_storage_detail::throw_length_error("inplace_array: range size must equal N");
  }

#ifdef __cpp_lib_containers_ranges
  template <inplace_storage_detail::container_compatible_range<value_type> R>
  constexpr inplace_array(std::from_range_t, R &&rg)
  {
    if (std::ranges::begin(rg) != std::ranges::end(rg)) [[unlikely]]
      inplace_storage_detail::throw_length_error("inplace_array: range size must equal N");
  }
#endif

  constexpr void fill(const_reference) noexcept {}
  constexpr void swap(inplace_array &) noexcept {}
  friend constexpr void swap(inplace_array &, inplace_array &) noexcept {}

  friend constexpr bool operator== (inplace_array const &, inplace_array const &)
  requires std::equality_comparable<value_type>
  {
    return true;
  }

  friend constexpr auto operator<=> (inplace_array const &, inplace_array const &)
  requires requires (value_type const &lhs_value, value_type const &rhs_value) {
    inplace_storage_detail::synth_three_way(lhs_value, rhs_value);
  }
  {
    return std::strong_ordering::equal;
  }
};

/// Non-member interface mirroring std::array: get<I>() and to_inplace_array()
template <std::size_t I, class T, std::size_t N>
[[nodiscard]] constexpr T &get(inplace_array<T, N> &arr) noexcept
{
  static_assert(I < N, "array index out of bounds");
  return arr[I];
}

template <std::size_t I, class T, std::size_t N>
[[nodiscard]] constexpr T const &get(inplace_array<T, N> const &arr) noexcept
{
  static_assert(I < N, "array index out of bounds");
  return arr[I];
}

template <std::size_t I, class T, std::size_t N>
[[nodiscard]] constexpr T &&get(inplace_array<T, N> &&arr) noexcept
{
  static_assert(I < N, "array index out of bounds");
  return std::move(arr[I]);
}

template <std::size_t I, class T, std::size_t N>
[[nodiscard]] constexpr T const &&get(inplace_array<T, N> const &&arr) noexcept
{
  static_assert(I < N, "array index out of bounds");
  return std::move(arr[I]);
}

template <class T, std::size_t N>
[[nodiscard]] constexpr inplace_array<std::remove_cv_t<T>, N> to_inplace_array(T (&a)[N])
{
  static_assert(!std::is_array_v<T>, "to_inplace_array does not accept multidimensional arrays");
  static_assert(std::is_constructible_v<std::remove_cv_t<T>, T &>,
                "to_inplace_array requires copy constructible elements");
  return inplace_array<std::remove_cv_t<T>, N>(a, a + N);
}

template <class T, std::size_t N>
[[nodiscard]] constexpr inplace_array<std::remove_cv_t<T>, N> to_inplace_array(T (&&a)[N])
{
  static_assert(!std::is_array_v<T>, "to_inplace_array does not accept multidimensional arrays");
  static_assert(std::is_move_constructible_v<std::remove_cv_t<T>>,
                "to_inplace_array requires move constructible elements");
  return inplace_array<std::remove_cv_t<T>, N>(std::make_move_iterator(a), std::make_move_iterator(a + N));
}
} // namespace stdex

/// Structured-binding support: std::tuple_size / std::tuple_element for
/// inplace_array (the element lookup itself goes through stdex::get via ADL).
namespace std
{
template <class T, std::size_t N>
struct tuple_size<stdex::inplace_array<T, N>> : integral_constant<std::size_t, N>
{
};

template <std::size_t I, class T, std::size_t N>
struct tuple_element<I, stdex::inplace_array<T, N>>
{
  using type = T;
};
} // namespace std
