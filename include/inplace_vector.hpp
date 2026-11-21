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

#include <algorithm>        // std::copy_n, std::equal, std::fill_n, std::move, std::remove,
                            // std::remove_if, std::rotate, std::swap
#include <cassert>          // assert
#include <compare>          // std::lexicographical_compare_three_way
#include <concepts>         // std::constructible_from, std::default_initializable,
                            // std::input_iterator, std::sentinel_for, std::forward_iterator,
                            // std::movable, std::swappable, std::equality_comparable
#include <cstddef>          // std::size_t, std::ptrdiff_t
#include <cstring>          // std::memcpy, std::memmove, std::memset
#include <initializer_list> // std::initializer_list
#include <iterator>         // std::reverse_iterator, std::iter_reference_t
#include <memory>           // std::destroy, std::addressof, std::to_address
#if defined(__cpp_lib_optional) && __cpp_lib_optional >= 202406L
#  include <optional>       // std::optional<T&> for the try_* results
#endif
#include <ranges>           
#include <type_traits>      // std::is_nothrow_move_constructible_v
#include <utility>          // std::forward, std::move, std::swap
#include <version>          // __cpp_lib_containers_ranges

#include "inplace_storage.hpp"
#include "uninitialized_algorithms.hpp"

namespace stdex
{
/**
 * @brief Fixed-capacity, dynamically-resizable contiguous container (P0843).
 * 
 * @details Elements are embedded in the object. @c T need not be
 * default-constructible; non-trivial values are constructed in place and
 * destroyed on removal.
 * 
 * Before C++26 only trivial values are usable in constant expressions; from
 * C++26 on the union storage plus @c std::start_lifetime supports the same for
 * all types the compiler can constant-evaluate.
 * 
 * Exceeding @c capacity() throws @c std::bad_alloc, matching @c std::vector's
 * out-of-memory handling.
 * 
 * @tparam T Element type.
 * @tparam N Maximum number of elements the container can hold.
 */
template <class T, std::size_t N>
class inplace_vector : private inplace_storage<inplace_storage_detail::storage_size_base, T, N>
{
  static_assert(std::is_object_v<T>, "containers require object value types ([container.requirements])");
  using base = inplace_storage<inplace_storage_detail::storage_size_base, T, N>;

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
#if defined(__cpp_lib_optional) && __cpp_lib_optional >= 202406L
  using try_result_type = std::optional<value_type &>;
#else
  using try_result_type = value_type *;
#endif

  constexpr inplace_vector() noexcept
      : base {}
  {
  }

  // ---------- rule of five ----------
  constexpr ~inplace_vector() requires std::is_trivially_destructible_v<T> = default;
  constexpr ~inplace_vector() noexcept
  requires (!std::is_trivially_destructible_v<T>)
  {
    std::destroy_n(data(), size());
  }

  constexpr inplace_vector(inplace_vector const &)             requires std::is_trivially_copy_constructible_v<T> = default;
  constexpr inplace_vector(inplace_vector &&)                  requires std::is_trivially_move_constructible_v<T> = default;
  constexpr inplace_vector &operator= (inplace_vector const &) requires std::is_trivially_copy_assignable_v<T>    = default;
  constexpr inplace_vector &operator= (inplace_vector &&)      requires std::is_trivially_move_assignable_v<T>    = default;

  constexpr inplace_vector(inplace_vector const &other)
      noexcept(std::is_nothrow_copy_constructible_v<value_type>)
  requires (!std::is_trivially_copy_constructible_v<T>)
      : base {}
  {
    (void) ::stdex::uninitialized_copy(other.begin(), other.end(), data(), data() + other.size());
    this->size_ = other.size();
  }

  constexpr inplace_vector(inplace_vector &&other)
      noexcept(std::is_nothrow_move_constructible_v<value_type>)
  requires (!std::is_trivially_move_constructible_v<T>)
      : base {}
  {
    (void) ::stdex::uninitialized_move(other.begin(), other.end(), data(), data() + other.size());
    this->size_ = other.size();
  }

  constexpr inplace_vector &operator= (inplace_vector const &other)
      noexcept(std::is_nothrow_copy_assignable_v<value_type> && std::is_nothrow_copy_constructible_v<value_type>)
  requires (!std::is_trivially_copy_assignable_v<T>)
  {
    if (this != std::addressof(other)) assign(other.begin(), other.end());
    return *this;
  }

  constexpr inplace_vector &operator= (inplace_vector &&other)
      noexcept(std::is_nothrow_move_assignable_v<value_type> && std::is_nothrow_move_constructible_v<value_type>)
  requires (!std::is_trivially_move_assignable_v<T>)
  {
    if (this != std::addressof(other)) assign(std::make_move_iterator(other.begin()), std::make_move_iterator(other.end()));
    return *this;
  }

  constexpr explicit inplace_vector(size_type const count)
      : inplace_vector {}
  {
    if (count > capacity()) [[unlikely]] inplace_storage_detail::throw_bad_alloc();
    (void) ::stdex::uninitialized_value_construct_n(data(), static_cast<difference_type>(count));
    this->size_ = count;
  }

  constexpr inplace_vector(size_type const count, const_reference value)
      : inplace_vector {}
  {
    if (count > capacity()) [[unlikely]] inplace_storage_detail::throw_bad_alloc();
    (void) ::stdex::uninitialized_fill_n(data(), static_cast<difference_type>(count), value);
    this->size_ = count;
  }

  template <std::input_iterator InputIt, std::sentinel_for<InputIt> Sentinel>
  constexpr inplace_vector(InputIt first, Sentinel last)
      : inplace_vector {}
  {
    append_range(std::ranges::subrange { std::move(first), std::move(last) });
  }

  constexpr inplace_vector(std::initializer_list<value_type> ilist)
      : inplace_vector { ilist.begin(), ilist.end() }
  {
  }

#ifdef __cpp_lib_containers_ranges
  template <inplace_storage_detail::container_compatible_range<value_type> R>
  constexpr inplace_vector(std::from_range_t, R &&rg)
      : inplace_vector {}
  {
    append_range(std::forward<R>(rg));
  }
#endif

  constexpr inplace_vector &operator= (std::initializer_list<value_type> ilist)
  {
    assign(ilist.begin(), ilist.end());
    return *this;
  }

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
  [[nodiscard]] static constexpr size_type capacity() noexcept { return N; }
  static constexpr void shrink_to_fit() noexcept { }
  static constexpr void reserve(size_type const count)
  {
    if (count > N) [[unlikely]] inplace_storage_detail::throw_bad_alloc();
  }

  // ---------- iterators ----------
  using base::begin;
  using base::end;
  using base::rbegin;
  using base::rend;
  using base::cbegin;
  using base::cend;
  using base::crbegin;
  using base::crend;

private:
  /// @brief Destroys the element tail [new_size, size()) and sets size() to @p new_size.
  ///
  /// @details
  /// The `if constexpr` guard is not merely an optimization.
  /// For trivially destructible T, elements are sub-objects of the backing `std::array<T, N>`.
  /// The array’s trivial destructor already ends element lifetimes, so explicit destruction is redundant.
  ///
  /// Critically, MSVC’s constant evaluator (cl.exe) considers `std::destroy`/`std::destroy_at` on
  /// these sub-objects to terminate their lifetimes. Subsequent destruction of the parent `std::array`
  /// then produces C2131: "destroying an object that has already been destroyed".
  ///
  /// This guard makes constexpr usage (clear, erase, shrink resize, shrink assign, swap) compile on MSVC.
  /// GCC and Clang accept redundant destroy calls, but the guard is kept for MSVC compatibility;
  /// it also avoids unnecessary destructor loops for POD types.
  ///
  /// @param new_size The size after destruction; must be <= size().

  constexpr void destroy_from_(size_type const new_size) noexcept
  {
    assert(new_size <= size());
    if constexpr (!std::is_trivially_destructible_v<value_type>) {
      std::destroy(data() + static_cast<difference_type>(new_size), data() + static_cast<difference_type>(size()));
    }
    this->size_ = new_size;
  }

  constexpr bool try_memset_fill_n_from_(size_type index, size_type count, const_reference value)
  {
    constexpr bool memset_ok = uninitialized_detail::fill_memset_is_safe<decltype(data()), value_type>;
    constexpr bool zero_ok   = uninitialized_detail::fill_zero_memset_is_safe<decltype(data()), value_type>;

    if constexpr (memset_ok) {
      if (!std::is_constant_evaluated()) {
        uninitialized_detail::invoke_memset(data() + static_cast<difference_type>(index), value, count);
        return true;
      }
    } else if constexpr (zero_ok) {
      if (!std::is_constant_evaluated() && ::stdex::is_all_bits_zero(value)) {
        uninitialized_detail::invoke_zero_memset(data() + static_cast<difference_type>(index), count);
        return true;
      }
    }
    return false;
  }

  template <class... Args>
  constexpr void emplace_at_unchecked_(size_type i, Args &&...args) noexcept(std::is_nothrow_constructible_v<value_type, Args...>)
  {
    std::construct_at(data() + static_cast<difference_type>(i), std::forward<Args>(args)...);
    this->size_ = i + 1;
  }

  template <std::forward_iterator InputIt>
  requires std::assignable_from<value_type &, std::iter_reference_t<InputIt>> &&
           std::constructible_from<value_type, std::iter_reference_t<InputIt>>
  constexpr void assign_forward_(InputIt first, size_type const count)
  {
    size_type const old    = size();
    size_type const common = count < old ? count : old;

    (void) std::copy_n(first, static_cast<difference_type>(common), data());
    if (count > old) {
      std::ranges::advance(first, static_cast<difference_type>(common));

      if constexpr (uninitialized_detail::iter_copy_cat<InputIt, value_type *>::bitcopy_constructible) {
        if (!std::is_constant_evaluated()) {
          (void) uninitialized_detail::invoke_memcpy(
              first, first + static_cast<difference_type>(count - old),
              data() + static_cast<difference_type>(old), data() + static_cast<difference_type>(count));
          this->size_ = count;
          return;
        }
      }

      for (size_type i = old; i < count; ++i, ++first) {
        emplace_at_unchecked_(i, *first);
      }
    } else if (count < old) {
      destroy_from_(count);
    }
  }

  template <std::input_iterator InputIt, std::sentinel_for<InputIt> Sentinel>
  requires std::assignable_from<value_type &, std::iter_reference_t<InputIt>> &&
           std::constructible_from<value_type, std::iter_reference_t<InputIt>>
  constexpr void assign_input_(InputIt first, Sentinel last)
  {
    size_type i { 0 };
    size_type const old = size();
    for (; i < old && first != last; ++i, ++first) data()[i] = *first;
    if (i == old) {
      for (; first != last; ++first) emplace_back(*first); // capacity-checked, size tracked
    } else {
      destroy_from_(i);
    }
  }

public:
  // ---------- assign ----------
  template <std::input_iterator InputIt, std::sentinel_for<InputIt> Sentinel>
  requires std::constructible_from<value_type, std::iter_reference_t<InputIt>>
  constexpr void assign(InputIt first, Sentinel last)
  {
    if constexpr (std::assignable_from<value_type &, std::iter_reference_t<InputIt>>) {
      if constexpr (std::forward_iterator<InputIt>) {
        size_type const count = static_cast<size_type>(std::ranges::distance(first, last));
        if (count > capacity()) [[unlikely]] inplace_storage_detail::throw_bad_alloc();
        assign_forward_(first, count);
      } else {
        assign_input_(first, last);
      }
    } else {
      if constexpr (std::forward_iterator<InputIt>) {
        size_type const count = static_cast<size_type>(std::ranges::distance(first, last));
        if (count > capacity()) [[unlikely]] inplace_storage_detail::throw_bad_alloc();
        clear();
        for (; first != last; ++first) (void) unchecked_emplace_back(*first); // capacity pre-checked
      } else {
        clear();
        for (; first != last; ++first) (void) emplace_back(*first); // count unknown: keep per-element check
      }
    }
  }

  template <inplace_storage_detail::container_compatible_range<value_type> R>
  constexpr void assign_range(R &&rg)
  {
    assign(std::ranges::begin(rg), std::ranges::end(rg));
  }

  constexpr void assign(size_type const count, const_reference value)
  {
    if (count > capacity()) [[unlikely]] inplace_storage_detail::throw_bad_alloc();

    value_type const copy(value);
    if constexpr (std::is_assignable_v<value_type &, const_reference>) {
      size_type const old    = size();
      size_type const common = count < old ? count : old;

      (void) std::fill_n(data(), static_cast<difference_type>(common), copy);

      if (count > old) {
        if (try_memset_fill_n_from_(old, count - old, copy)) {
          this->size_ = count;
        } else {
          for (size_type i = old; i < count; ++i) emplace_at_unchecked_(i, copy);
        }
      }
      if (count < old) {
        destroy_from_(count);
      }
    } else {
      clear();
      for (size_type i = 0; i < count; ++i) emplace_at_unchecked_(i, copy);
    }
  }

  constexpr void assign(std::initializer_list<value_type> ilist) { assign(ilist.begin(), ilist.end()); }

private:
  constexpr size_type resize_prepare_(size_type const count)
  {
    if (count > capacity()) [[unlikely]] inplace_storage_detail::throw_bad_alloc();
    size_type const old = size();
    if (count < old) {
      destroy_from_(count);
    }
    return old;
  }

public:
  constexpr void resize(size_type const count)
  requires std::default_initializable<value_type>
  {
    size_type const old = resize_prepare_(count);
    if (count <= old) return;
    // Strong guarantee: uninitialized_value_construct_n rolls back (backout) on throw,
    // and still takes the zero-memset fast path for scalars internally.
    (void) ::stdex::uninitialized_value_construct_n(data() + static_cast<difference_type>(old),
                                                    static_cast<difference_type>(count - old));
    this->size_ = count;
  }

  constexpr void resize(size_type const count, const_reference value)
  {
    size_type const old = resize_prepare_(count);
    if (count <= old) return;
    (void) ::stdex::uninitialized_fill_n(data() + static_cast<difference_type>(old),
                                         static_cast<difference_type>(count - old), value);
    this->size_ = count;
  }

  // ---------- modifiers ----------
  template <class... Args>
  requires std::constructible_from<value_type, Args...>
  constexpr reference unchecked_emplace_back(Args &&...args) noexcept(std::is_nothrow_constructible_v<value_type, Args...>)
  {
    size_type const index = size();
    assert(index < capacity());
    value_type &element = *std::construct_at(data() + static_cast<difference_type>(index), std::forward<Args>(args)...);
    this->size_ = index + 1;
    return element;
  }

  constexpr reference unchecked_push_back(const_reference value) noexcept(noexcept(unchecked_emplace_back(value)))
  {
    assert(size() < capacity());
    return unchecked_emplace_back(value);
  }

  constexpr reference unchecked_push_back(value_type &&value) noexcept(noexcept(unchecked_emplace_back(std::move(value))))
  {
    assert(size() < capacity());
    return unchecked_emplace_back(std::move(value));
  }

  template <class... Args>
  requires std::constructible_from<value_type, Args...>
  constexpr reference emplace_back(Args &&...args)
  {
    if (size() == capacity()) [[unlikely]] inplace_storage_detail::throw_bad_alloc();
    return unchecked_emplace_back(std::forward<Args>(args)...);
  }

  constexpr reference push_back(const_reference value) { return emplace_back(value); }

  constexpr reference push_back(value_type &&value) { return emplace_back(std::move(value)); }

  template <class... Args>
  requires std::constructible_from<value_type, Args...>
  [[nodiscard]] constexpr try_result_type try_emplace_back(Args &&...args) noexcept(noexcept(unchecked_emplace_back(std::forward<Args>(args)...)))
  {
    if (size() == capacity()) {
#if defined(__cpp_lib_optional) && __cpp_lib_optional >= 202406L
      return std::nullopt;
#else
      return nullptr;
#endif
    }
    reference element = unchecked_emplace_back(std::forward<Args>(args)...);
#if defined(__cpp_lib_optional) && __cpp_lib_optional >= 202406L
    return element;
#else
    return std::addressof(element);
#endif
  }

  [[nodiscard]] constexpr try_result_type try_push_back(const_reference value) noexcept(noexcept(try_emplace_back(value)))
  {
    return try_emplace_back(value);
  }

  [[nodiscard]] constexpr try_result_type try_push_back(value_type &&value) noexcept(noexcept(try_emplace_back(std::move(value))))
  {
    return try_emplace_back(std::move(value));
  }

  template <inplace_storage_detail::container_compatible_range<value_type> R>
  constexpr void append_range(R &&rg)
  {
    if constexpr (std::ranges::sized_range<R> || std::ranges::forward_range<R>) {
      size_type const count = static_cast<size_type>(std::ranges::distance(rg));
      size_type const old   = size();
      if (count > capacity() - old) [[unlikely]] inplace_storage_detail::throw_bad_alloc();

      if constexpr (uninitialized_detail::iter_copy_cat<std::ranges::iterator_t<R>, value_type *>::bitcopy_constructible) {
        if (!std::is_constant_evaluated()) {
          if (count != 0) {
            auto const src = std::ranges::begin(rg);
            (void) uninitialized_detail::invoke_memcpy(
                src, src + static_cast<difference_type>(count),
                data() + static_cast<difference_type>(old), data() + static_cast<difference_type>(old + count));
          }
          this->size_ = old + count;
          return;
        }
      }
      for (auto &&value : rg) (void) unchecked_emplace_back(std::forward<decltype(value)>(value));
    } else {
      for (auto &&value : rg) (void) emplace_back(std::forward<decltype(value)>(value));
    }
  }

  constexpr void pop_back() noexcept
  {
    assert(!empty());
    destroy_from_(size() - 1);
  }

  template <class... Args>
  requires std::constructible_from<value_type, Args...> && std::movable<value_type>
  constexpr iterator emplace(const_iterator position, Args &&...args)
  {
    size_type const old = size();
    if (old == capacity()) [[unlikely]] inplace_storage_detail::throw_bad_alloc();
    size_type const index = static_cast<size_type>(position - cbegin());

    if constexpr (uninitialized_detail::iter_move_cat<value_type *, value_type *>::bitcopy_constructible && std::is_trivially_destructible_v<value_type>) {
      if (!std::is_constant_evaluated()) {
        value_type temp(std::forward<Args>(args)...);
        std::memmove(static_cast<void *>(data() + index + 1),
                     static_cast<void const *>(data() + index), (old - index) * sizeof(value_type));
        (void) std::construct_at(data() + static_cast<difference_type>(index), std::move(temp));
        this->size_ = old + 1;
        return begin() + index;
      }
    }

    (void) unchecked_emplace_back(std::forward<Args>(args)...);
    iterator const inserted = begin() + static_cast<difference_type>(index);
    std::rotate(inserted, end() - 1, end());
    return inserted;
  }

  constexpr iterator insert(const_iterator position, const_reference value)
  {
    return emplace(position, value);
  }

  constexpr iterator insert(const_iterator position, value_type &&value)
  {
    return emplace(position, std::move(value));
  }

  constexpr iterator insert(const_iterator position, size_type const count, const_reference value)
  requires std::movable<value_type>
  {
    size_type const old = size();
    if (count > capacity() - old) [[unlikely]] inplace_storage_detail::throw_bad_alloc();
    size_type const index = static_cast<size_type>(position - cbegin());
    if (count == 0) [[unlikely]] return begin() + static_cast<difference_type>(index);

    if constexpr (uninitialized_detail::iter_move_cat<value_type *, value_type *>::bitcopy_constructible && std::is_trivially_destructible_v<value_type>) {
      if (!std::is_constant_evaluated()) {
        value_type const copy(value); // value may alias an element shifted by memmove
        std::memmove(static_cast<void *>(data() + index + count),
                     static_cast<void const *>(data() + index), (old - index) * sizeof(value_type));
        if (!try_memset_fill_n_from_(index, count, copy)) {
          (void) ::stdex::uninitialized_fill_n(data() + static_cast<difference_type>(index),
                                               static_cast<difference_type>(count), copy);
        }
        this->size_ = old + count;
        return begin() + index;
      }
    }

    for (size_type i = 0; i < count; ++i) (void) unchecked_emplace_back(value);
    std::rotate(begin() + static_cast<difference_type>(index), end() - static_cast<difference_type>(count), end());
    return begin() + static_cast<difference_type>(index);
  }

  template <std::input_iterator InputIt, std::sentinel_for<InputIt> Sentinel>
  requires std::constructible_from<value_type, std::iter_reference_t<InputIt>> && std::movable<value_type>
  constexpr iterator insert(const_iterator position, InputIt first, Sentinel last)
  {
    size_type const old   = size();
    size_type const index = static_cast<size_type>(position - cbegin());

    if constexpr (std::forward_iterator<InputIt>) {
      size_type const count = static_cast<size_type>(std::ranges::distance(first, last));
      if (count > capacity() - old) [[unlikely]] inplace_storage_detail::throw_bad_alloc();
      if (count == 0) [[unlikely]] return begin() + static_cast<difference_type>(index);

      constexpr bool can_relocate = uninitialized_detail::iter_move_cat<value_type *, value_type *>::bitcopy_constructible && std::is_trivially_destructible_v<value_type>;
      constexpr bool can_copy_in  = uninitialized_detail::iter_copy_cat<InputIt, value_type *>::bitcopy_constructible;

      if constexpr (can_relocate && can_copy_in) {
        if (!std::is_constant_evaluated()) {
          std::memmove(static_cast<void *>(data() + index + count),
                       static_cast<void const *>(data() + index), (old - index) * sizeof(value_type));
          std::memcpy(static_cast<void *>(data() + index), std::to_address(first), count * sizeof(value_type));
          this->size_ = old + count;
          return begin() + static_cast<difference_type>(index);
        }
      }
      for (; first != last; ++first) (void) unchecked_emplace_back(*first);
    } else {
      for (; first != last; ++first) (void) emplace_back(*first);
    }
    std::rotate(begin() + static_cast<difference_type>(index), begin() + static_cast<difference_type>(old), end());
    return begin() + static_cast<difference_type>(index);
  }

  template <inplace_storage_detail::container_compatible_range<value_type> R>
  requires std::movable<value_type>
  constexpr iterator insert_range(const_iterator position, R &&rg)
  {
    return insert(position, std::ranges::begin(rg), std::ranges::end(rg));
  }

  constexpr iterator insert(const_iterator position, std::initializer_list<value_type> ilist)
  requires std::movable<value_type>
  {
    return insert(position, ilist.begin(), ilist.end());
  }

  constexpr iterator erase(const_iterator position) noexcept(std::is_nothrow_move_assignable_v<value_type>)
  requires std::movable<value_type>
  {
    return erase(position, position + 1);
  }

  constexpr iterator erase(const_iterator first, const_iterator last) noexcept(std::is_nothrow_move_assignable_v<value_type>)
  requires std::movable<value_type>
  {
    size_type const old   = size();
    size_type const index = static_cast<size_type>(first - cbegin());
    size_type const count = static_cast<size_type>(last - first);
    if (count == 0) [[unlikely]] return begin() + static_cast<difference_type>(index);
    std::move(data() + static_cast<difference_type>(index + count),
              data() + static_cast<difference_type>(old), data() + static_cast<difference_type>(index));
    destroy_from_(old - count);
    return begin() + static_cast<difference_type>(index);
  }

  constexpr void clear() noexcept { destroy_from_(0); }

  constexpr void swap(inplace_vector &other) noexcept(std::is_nothrow_swappable_v<value_type> && std::is_nothrow_move_constructible_v<value_type>)
  requires std::swappable<value_type>
  {
    if (this == std::addressof(other)) return;

    inplace_vector *const smaller = other.size() < size() ? std::addressof(other) : this;
    inplace_vector *const bigger  = other.size() < size() ? this : std::addressof(other);
    size_type const common        = smaller->size();
    size_type const extra         = bigger->size();

    std::swap_ranges(smaller->data(), smaller->data() + common, bigger->data());

    if constexpr (uninitialized_detail::iter_move_cat<value_type *, value_type *>::bitcopy_constructible && std::is_trivially_destructible_v<value_type>) {
      if (!std::is_constant_evaluated()) {
        (void) uninitialized_detail::invoke_memcpy(bigger->data() + common, bigger->data() + extra,
                                                   smaller->data() + common, smaller->data() + extra);
        smaller->size_ = extra;
        bigger->destroy_from_(common);
        return;
      }
    }

    for (size_type i = common; i < extra; ++i) {
      (void) std::construct_at(smaller->data() + i, std::move((*bigger)[i]));
      smaller->size_ = i + 1;
    }
    bigger->destroy_from_(common);
  }

  friend constexpr void swap(inplace_vector &lhs, inplace_vector &rhs) noexcept(noexcept(lhs.swap(rhs)))
  {
    lhs.swap(rhs);
  }

  // ---------- comparison ----------
  friend constexpr bool operator== (inplace_vector const &lhs, inplace_vector const &rhs)
  requires std::equality_comparable<value_type>
  {
    return lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin());
  }

  friend constexpr auto operator<=> (inplace_vector const &lhs, inplace_vector const &rhs)
  requires requires (value_type const &lhs_value, value_type const &rhs_value) {
    inplace_storage_detail::synth_three_way(lhs_value, rhs_value);
  }
  {
    return std::lexicographical_compare_three_way(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), inplace_storage_detail::synth_three_way);
  }
};

template <class T>
class inplace_vector<T, 0> : private inplace_storage<inplace_storage_detail::storage_no_size_base, T, 0>
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

#if defined(__cpp_lib_optional) && __cpp_lib_optional >= 202406L
  using try_result_type = std::optional<value_type &>;
#else
  using try_result_type = value_type *;
#endif

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

  constexpr inplace_vector() noexcept                          = default;
  constexpr ~inplace_vector()                                  = default;
  constexpr inplace_vector(inplace_vector const &)             = default;
  constexpr inplace_vector(inplace_vector &&)                  = default;
  constexpr inplace_vector &operator= (inplace_vector const &) = default;
  constexpr inplace_vector &operator= (inplace_vector &&)      = default;

  constexpr explicit inplace_vector(size_type const count)
  {
    if (count != 0) [[unlikely]] inplace_storage_detail::throw_bad_alloc();
  }

  constexpr inplace_vector(size_type const count, const_reference)
  {
    if (count != 0) [[unlikely]] inplace_storage_detail::throw_bad_alloc();
  }

  template <std::input_iterator InputIt, std::sentinel_for<InputIt> Sentinel>
  constexpr inplace_vector(InputIt first, Sentinel last)
  {
    if (first != last) [[unlikely]] inplace_storage_detail::throw_bad_alloc();
  }

  constexpr inplace_vector(std::initializer_list<value_type> ilist)
  {
    if (ilist.size() != 0) [[unlikely]] inplace_storage_detail::throw_bad_alloc();
  }

#ifdef __cpp_lib_containers_ranges
  template <inplace_storage_detail::container_compatible_range<value_type> R>
  constexpr inplace_vector(std::from_range_t, R &&rg)
  {
    if (std::ranges::begin(rg) != std::ranges::end(rg)) [[unlikely]] inplace_storage_detail::throw_bad_alloc();
  }
#endif

  constexpr inplace_vector &operator=(std::initializer_list<value_type> ilist)
  {
    if (ilist.size() != 0) [[unlikely]] inplace_storage_detail::throw_bad_alloc();
    return *this;
  }

  template <std::input_iterator InputIt, std::sentinel_for<InputIt> Sentinel>
  constexpr void assign(InputIt first, Sentinel last)
  {
    if (first != last) [[unlikely]] inplace_storage_detail::throw_bad_alloc();
  }

  template <inplace_storage_detail::container_compatible_range<value_type> R>
  constexpr void assign_range(R &&rg)
  {
    if (std::ranges::begin(rg) != std::ranges::end(rg)) [[unlikely]] inplace_storage_detail::throw_bad_alloc();
  }

  constexpr void assign(size_type const count, const_reference)
  {
    if (count != 0) [[unlikely]] inplace_storage_detail::throw_bad_alloc();
  }

  constexpr void assign(std::initializer_list<value_type> ilist)
  {
    if (ilist.size() != 0) [[unlikely]] inplace_storage_detail::throw_bad_alloc();
  }

  [[nodiscard]] static constexpr size_type capacity() noexcept { return 0; }
  static constexpr void shrink_to_fit() noexcept {}

  static constexpr void reserve(size_type const count)
  {
    if (count != 0) [[unlikely]] inplace_storage_detail::throw_bad_alloc();
  }

  constexpr void resize(size_type const count)
  {
    if (count != 0) [[unlikely]] inplace_storage_detail::throw_bad_alloc();
  }

  constexpr void resize(size_type const count, const_reference)
  {
    if (count != 0) [[unlikely]] inplace_storage_detail::throw_bad_alloc();
  }

  template <class... Args>
  constexpr reference emplace_back(Args &&...) { inplace_storage_detail::throw_bad_alloc(); }
  constexpr reference push_back(const_reference) { inplace_storage_detail::throw_bad_alloc(); }

  constexpr reference push_back(value_type &&) { inplace_storage_detail::throw_bad_alloc(); }

  template <class... Args>
  [[nodiscard]] constexpr try_result_type try_emplace_back(Args &&...) noexcept
  {
#if defined(__cpp_lib_optional) && __cpp_lib_optional >= 202406L
    return std::nullopt;
#else
    return nullptr;
#endif
  }

  [[nodiscard]] constexpr try_result_type try_push_back(const_reference) noexcept
  {
    return try_emplace_back();
  }

  [[nodiscard]] constexpr try_result_type try_push_back(value_type &&) noexcept
  {
    return try_emplace_back();
  }

  template <inplace_storage_detail::container_compatible_range<value_type> R>
  constexpr void append_range(R &&rg)
  {
    if (std::ranges::begin(rg) != std::ranges::end(rg)) [[unlikely]] inplace_storage_detail::throw_bad_alloc();
  }

  constexpr void pop_back() noexcept { assert(false && "inplace_vector<T, 0>::pop_back on empty"); }

  template <class... Args>
  constexpr iterator emplace(const_iterator, Args &&...) { inplace_storage_detail::throw_bad_alloc(); }
  constexpr iterator insert(const_iterator, const_reference) { inplace_storage_detail::throw_bad_alloc(); }
  constexpr iterator insert(const_iterator, value_type &&) { inplace_storage_detail::throw_bad_alloc(); }
  constexpr iterator insert(const_iterator, size_type const count, const_reference)
  {
    if (count != 0) [[unlikely]] inplace_storage_detail::throw_bad_alloc();
    return nullptr;
  }
  template <std::input_iterator InputIt, std::sentinel_for<InputIt> Sentinel>
  constexpr iterator insert(const_iterator, InputIt first, Sentinel last)
  {
    if (first != last) [[unlikely]] inplace_storage_detail::throw_bad_alloc();
    return nullptr;
  }
  template <inplace_storage_detail::container_compatible_range<value_type> R>
  constexpr iterator insert_range(const_iterator, R &&rg)
  {
    if (std::ranges::begin(rg) != std::ranges::end(rg)) [[unlikely]] inplace_storage_detail::throw_bad_alloc();
    return nullptr;
  }
  constexpr iterator insert(const_iterator, std::initializer_list<value_type> ilist)
  {
    if (ilist.size() != 0) [[unlikely]] inplace_storage_detail::throw_bad_alloc();
    return nullptr;
  }

  constexpr iterator erase(const_iterator) noexcept { assert(false && "erase on empty inplace_vector"); return nullptr; }
  constexpr iterator erase(const_iterator first, const_iterator last) noexcept
  {
    assert(first == last && "erase range on empty inplace_vector");
    return nullptr;
  }

  constexpr void clear() noexcept {}
  constexpr void swap(inplace_vector &) noexcept {}
  friend constexpr void swap(inplace_vector &, inplace_vector &) noexcept {}

  friend constexpr bool operator==(inplace_vector const &, inplace_vector const &) noexcept { return true; }
  friend constexpr auto operator<=>(inplace_vector const &, inplace_vector const &) noexcept
  {
    return std::strong_ordering::equal;
  }
};

// ---------- erasure ----------
template <class T, std::size_t N, class U = T>
constexpr typename inplace_vector<T, N>::size_type erase(inplace_vector<T, N> &container, U const &value)
{
  if (container.empty()) return 0;
  auto const removed = std::remove(container.begin(), container.end(), value);
  auto const count   = static_cast<typename inplace_vector<T, N>::size_type>(container.end() - removed);
  container.erase(removed, container.end());
  return count;
}

template <class T, std::size_t N, class Predicate>
constexpr typename inplace_vector<T, N>::size_type erase_if(inplace_vector<T, N> &container, Predicate predicate)
{
  if (container.empty()) return 0;
  auto const removed = std::remove_if(container.begin(), container.end(), predicate);
  auto const count   = static_cast<typename inplace_vector<T, N>::size_type>(container.end() - removed);
  container.erase(removed, container.end());
  return count;
}
} // namespace stdex
