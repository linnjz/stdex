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
#include <array>            // std::array
#include <cassert>          // assert
#include <compare>          // std::weak_ordering, std::strong_ordering, std::lexicographical_compare_three_way
#include <concepts>         // std::constructible_from, std::convertible_to, std::default_initializable,
                            // std::input_iterator, std::sentinel_for, std::forward_iterator,
                            // std::movable, std::swappable, std::equality_comparable,
                            // std::is_nothrow_constructible_v
#include <cstddef>          // std::size_t, std::ptrdiff_t
#include <cstdlib>          // std::abort
#include <cstring>          // std::memcpy, std::memmove, std::memset
#include <initializer_list> // std::initializer_list
#include <iterator>         // std::reverse_iterator, std::iter_reference_t
#include <memory>           // std::construct_at, std::destroy_at, std::destroy, std::addressof,
                            // std::to_address
#include <new>              // std::bad_alloc
#if __cplusplus >= 202603L
#  include <optional>       // std::optional<T&> for the try_* results
#endif
#include <ranges>           // std::ranges::input_range, std::ranges::distance, std::ranges::begin,
                            // std::ranges::end, std::ranges::range_reference_t, std::ranges::sized_range,
                            // std::ranges::forward_range
#include <stdexcept>        // std::out_of_range
#include <type_traits>      // std::is_nothrow_move_constructible_v
#include <utility>          // std::forward, std::move, std::swap
#include <version>          // __cpp_lib_containers_ranges, __cpp_lib_start_lifetime

#include "uninitialized_algorithms.hpp"

namespace stdex
{
namespace inplace_vector_detail
{
/// Trivially-copyable, trivially default-constructible, trivially destructible.
/// Only such values may be used in constant expressions before C++26.
template <class T>
concept trivial_value = std::is_trivially_copyable_v<T> &&
                        std::is_trivially_default_constructible_v<T> &&
                        std::is_trivially_destructible_v<T>;

template <class R, class T>
concept container_compatible_range = std::ranges::input_range<R> && std::convertible_to<std::ranges::range_reference_t<R>, T>;

/// `synth-three-way`: use `<=>` when available, otherwise synthesize a
/// `weak_ordering` from `<` (as the standard algorithms do).
struct synth_three_way_fn
{
  template <class T, class U>
  constexpr auto operator()(T const &lhs, U const &rhs) const
  {
    if constexpr (std::three_way_comparable_with<T, U>) {
      return lhs <=> rhs;
    } else {
      if (lhs < rhs) return std::weak_ordering::less;
      if (rhs < lhs) return std::weak_ordering::greater;
      return std::weak_ordering::equivalent;
    }
  }
};

inline constexpr synth_three_way_fn synth_three_way {};

[[noreturn]] constexpr void throw_bad_alloc_()
{
#if defined(_CPPUNWIND) || defined(__EXCEPTIONS)
  throw std::bad_alloc();
#else
  std::abort();
#endif
}

[[noreturn]] constexpr void throw_out_of_range_()
{
#if defined(_CPPUNWIND) || defined(__EXCEPTIONS)
  throw std::out_of_range("inplace_vector::at: index out of range");
#else
  std::abort();
#endif
}

constexpr void start_storage_lifetime_(auto &storage) noexcept
{
  if (std::is_constant_evaluated()) {
#if defined(__cpp_lib_start_lifetime) && __cpp_lib_start_lifetime >= 202603L
    std::start_lifetime(storage);
#else
    (void) storage;
#endif
  }
}

/// Shared size bookkeeping for the storage variants.
struct storage_size_base
{
  std::size_t size_ = 0;

  [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }
  constexpr void set_size(std::size_t const count) noexcept { size_ = count; }
};

/// Storage for trivial values before C++26. Elements live for the whole object
/// lifetime; the logical size only selects the active prefix.
template <class T, std::size_t N>
struct array_storage : storage_size_base
{
  std::array<T, N> elements_ {};

  [[nodiscard]] constexpr T *data() noexcept { return elements_.data(); }
  [[nodiscard]] constexpr T const *data() const noexcept { return elements_.data(); }

  // The array value-initializes all slots, so placement-new would end an
  // already-live element's lifetime (rejected by constant evaluation); assign
  // instead, which is a transparent replacement for a trivial value.
  template <class... Args>
  constexpr T &emplace_at(std::size_t const index, Args &&...args)
  {
    elements_[index] = T(std::forward<Args>(args)...);
    return elements_[index];
  }

  template <class U>
  constexpr void uninitialized_fill_n_at(std::size_t const index, std::size_t const count, U const &value)
  {
    (void) std::fill_n(elements_.data() + index, count, value);
  }

  constexpr void uninitialized_value_construct_n(std::size_t count)
  {
    (void) std::fill_n(elements_.data(), count, T {});
  }

  constexpr void destroy_from(std::size_t const from) noexcept { size_ = from; }
};

/// Union storage for trivial values under C++26 (start_lifetime path). The
/// implicit copy/move/destructor stay trivial, so the container is trivially
/// copyable exactly when `T` is.
template <class T, std::size_t N>
struct trivial_union_storage : storage_size_base
{
  union { T elements_[N]; };

  constexpr trivial_union_storage() noexcept { start_storage_lifetime_(elements_); }

  [[nodiscard]] constexpr T *data() noexcept { return elements_; }
  [[nodiscard]] constexpr T const *data() const noexcept { return elements_; }

  template <class... Args>
  constexpr T &emplace_at(std::size_t const index, Args &&...args)
  {
    return *std::construct_at(elements_ + index, std::forward<Args>(args)...);
  }

  template <class U>
  constexpr void uninitialized_fill_n_at(std::size_t const index, std::size_t const count, U const &value)
  {
    (void) ::stdex::uninitialized_fill_n(elements_ + index, static_cast<std::ptrdiff_t>(count), value);
  }

  constexpr void uninitialized_value_construct_n(std::size_t count)
  {
    (void) ::stdex::uninitialized_value_construct_n(elements_, static_cast<std::ptrdiff_t>(count));
  }

  constexpr void destroy_from(std::size_t const from) noexcept { size_ = from; }
};

/// Union storage for non-trivial values. The union keeps elements unconstructed
/// until placed; element lifetime is managed explicitly, so `T` need not be
/// default-constructible. No allocation is ever performed.
template <class T, std::size_t N>
struct nontrivial_storage : storage_size_base
{
  union { T elements_[N]; };

  constexpr nontrivial_storage() noexcept { start_storage_lifetime_(elements_); }

  constexpr nontrivial_storage(nontrivial_storage const &other)
  {
    start_storage_lifetime_(elements_);
    (void) ::stdex::uninitialized_copy_n(other.elements_, static_cast<std::ptrdiff_t>(other.size_),
                                         elements_, elements_ + other.size_);
    size_ = other.size_;
  }

  constexpr nontrivial_storage(nontrivial_storage &&other) noexcept(std::is_nothrow_move_constructible_v<T>)
  {
    start_storage_lifetime_(elements_);
    (void) ::stdex::uninitialized_move_n(other.elements_, static_cast<std::ptrdiff_t>(other.size_),
                                         elements_, elements_ + other.size_);
    size_ = other.size_;
  }

  constexpr nontrivial_storage &operator=(nontrivial_storage const &other)
  {
    if (this == std::addressof(other)) return *this;

    if constexpr (std::is_copy_assignable_v<T>) {
      size_t const old_n  = size_;
      size_t const new_n  = other.size_;
      size_t const common = new_n < old_n ? new_n : old_n;

      if (common != 0) [[likely]] std::copy_n(other.elements_, common, elements_);

      if (new_n > old_n) {
        (void) ::stdex::uninitialized_copy_n(other.elements_ + common, static_cast<std::ptrdiff_t>(new_n - common),
                                             elements_ + common, elements_ + new_n);
      } else if (old_n > new_n) {
        std::destroy(elements_ + new_n, elements_ + old_n);
      }
      size_ = new_n;
    } else {
      destroy_all_();
      size_ = 0;
      (void) ::stdex::uninitialized_copy_n(other.elements_, static_cast<std::ptrdiff_t>(other.size_),
                                           elements_, elements_ + other.size_);
      size_ = other.size_;
    }
    return *this;
  }

  constexpr nontrivial_storage &operator= (nontrivial_storage &&other) noexcept(
    std::is_nothrow_move_constructible_v<T> && (!std::is_move_assignable_v<T> || std::is_nothrow_move_assignable_v<T>) )
  {
    if (this == std::addressof(other)) [[unlikely]] return *this;

    if constexpr (std::is_move_assignable_v<T>) {
      size_t const old_n  = size_;
      size_t const new_n  = other.size_;
      size_t const common = new_n < old_n ? new_n : old_n;

      if (common != 0) [[likely]] std::move(other.elements_, other.elements_ + common, elements_);

      if (new_n > old_n) {
        (void) ::stdex::uninitialized_move_n(other.elements_ + common, static_cast<std::ptrdiff_t>(new_n - common),
                                             elements_ + common, elements_ + new_n);
      } else if (old_n > new_n) {
        std::destroy(elements_ + new_n, elements_ + old_n);
      }
      size_ = new_n;
    } else {
      destroy_all_();
      size_ = 0;
      (void) ::stdex::uninitialized_move_n(other.elements_, static_cast<std::ptrdiff_t>(other.size_),
                                           elements_, elements_ + other.size_);
      size_ = other.size_;
    }

    return *this;
  }

  constexpr ~nontrivial_storage() { destroy_all_(); }

  [[nodiscard]] constexpr T *data() noexcept { return elements_; }
  [[nodiscard]] constexpr T const *data() const noexcept { return elements_; }

  template <class... Args>
  constexpr T &emplace_at(std::size_t const index, Args &&...args)
      noexcept(std::is_nothrow_constructible_v<T, Args...>)
  {
    return *std::construct_at(elements_ + index, std::forward<Args>(args)...);
  }

  template <class U>
  constexpr void uninitialized_fill_n_at(std::size_t const index, std::size_t const count, U const &value)
  {
    (void) ::stdex::uninitialized_fill_n(elements_ + index, static_cast<std::ptrdiff_t>(count), value);
  }

  constexpr void uninitialized_value_construct_n(std::size_t count)
  {
    (void) ::stdex::uninitialized_value_construct_n(elements_, static_cast<std::ptrdiff_t>(count));
  }

  constexpr void destroy_from(std::size_t const from) noexcept
  {
    std::destroy(elements_ + from, elements_ + size_);
    size_ = from;
  }

private:
  constexpr void destroy_all_() noexcept { std::destroy(elements_, elements_ + size_); }
};

template <class T, std::size_t N>
using union_storage = std::conditional_t<trivial_value<T>, trivial_union_storage<T, N>, nontrivial_storage<T, N>>;

#if defined(__cpp_lib_start_lifetime) && __cpp_lib_start_lifetime >= 202603L
template <class T, std::size_t N>
using storage_for_t = union_storage<T, N>;
#else
template <class T, std::size_t N>
using storage_for_t = std::conditional_t<trivial_value<T>, array_storage<T, N>, union_storage<T, N>>;
#endif
} // namespace inplace_vector_detail

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
class inplace_vector
{
  static_assert(N > 0, "inplace_vector<T, 0> is handled by the empty specialization");
  using storage_type = inplace_vector_detail::storage_for_t<T, N>;
public:
  /// C++26 returns `optional<T&>`; earlier modes use a null pointer to spell
  /// "no element" (an inplace vector lives in an array, so &value is available).
#if __cplusplus >= 202603L
  using try_result_type = std::optional<T &>;
#else
  using try_result_type = T *;
#endif
  using value_type             = T;
  using pointer                = value_type *;
  using const_pointer          = value_type const *;
  using reference              = value_type &;
  using const_reference        = value_type const &;
  using size_type              = std::size_t;
  using difference_type        = std::ptrdiff_t;
  using iterator               = value_type *;
  using const_iterator         = value_type const *;
  using reverse_iterator       = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  // clang-format off
  // ---------- construct / copy / destroy ----------
  constexpr inplace_vector() noexcept = default;
  constexpr inplace_vector(inplace_vector const &) = default;
  constexpr inplace_vector(inplace_vector &&) noexcept(std::is_nothrow_move_constructible_v<storage_type>) = default;
  constexpr ~inplace_vector() = default;
  constexpr inplace_vector &operator= (inplace_vector const &) = default;
  constexpr inplace_vector &operator= (inplace_vector &&) noexcept(std::is_nothrow_move_assignable_v<storage_type>) = default;
  // clang-format on

  constexpr explicit inplace_vector(size_type const count)
      : inplace_vector {}
  {
    if (count > capacity()) [[unlikely]] inplace_vector_detail::throw_bad_alloc_();
    storage_.uninitialized_value_construct_n(count);
    storage_.set_size(count);
  }

  constexpr inplace_vector(size_type const count, const_reference value)
      : inplace_vector {}
  {
    if (count > capacity()) [[unlikely]] inplace_vector_detail::throw_bad_alloc_();
    storage_.uninitialized_fill_n_at(size_type { 0 }, count, value);
    storage_.set_size(count);
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
  template <inplace_vector_detail::container_compatible_range<value_type> R>
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

private:
  constexpr bool try_memset_fill_n_from_(size_type index, size_type count, const_reference value)
  {
    constexpr bool memset_ok = uninitialized_detail::fill_memset_is_safe<decltype(data()), value_type>;
    constexpr bool zero_ok   = uninitialized_detail::fill_zero_memset_is_safe<decltype(data()), value_type>;

    if constexpr (memset_ok) {
      if (!std::is_constant_evaluated()) {
        uninitialized_detail::invoke_memset(data() + index, value, count);
        return true;
      }
    } else if constexpr (zero_ok) {
      if (!std::is_constant_evaluated() && ::stdex::is_all_bits_zero(value)) {
        uninitialized_detail::invoke_zero_memset(data() + index, count);
        return true;
      }
    }
    return false;
  }

  template <class... Args>
  constexpr void emplace_at_unchecked_(size_type i, Args &&...args)
      noexcept(noexcept(storage_.emplace_at(i, std::forward<Args>(args)...)))
  {
    (void) storage_.emplace_at(i, std::forward<Args>(args)...);
    storage_.set_size(i + 1);
  }

  template <std::forward_iterator InputIt>
  requires std::assignable_from<value_type &, std::iter_reference_t<InputIt>> &&
           std::constructible_from<value_type, std::iter_reference_t<InputIt>>
  constexpr void assign_forward_(InputIt first, size_type const count)
  {
    size_type const old    = size();
    size_type const common = count < old ? count : old;

    (void) std::copy_n(first, static_cast<std::iter_difference_t<InputIt>>(common), data());
    if (count > old) {
      std::ranges::advance(first, static_cast<std::iter_difference_t<InputIt>>(common));

      if constexpr (uninitialized_detail::iter_copy_cat<InputIt, value_type *>::bitcopy_constructible) {
        if (!std::is_constant_evaluated()) {
          (void) uninitialized_detail::invoke_memcpy(
              first, first + static_cast<std::iter_difference_t<InputIt>>(count - old),
              data() + old, data() + count);
          storage_.set_size(count);
          return;
        }
      }

      for (size_type i = old; i < count; ++i, ++first) {
        emplace_at_unchecked_(i, *first);
      }
    } else if (count < old) {
      storage_.destroy_from(count);
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
      storage_.destroy_from(i);
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
        if (count > capacity()) [[unlikely]] inplace_vector_detail::throw_bad_alloc_();
        assign_forward_(first, count);
      } else {
        assign_input_(first, last);
      }
    } else {
      if constexpr (std::forward_iterator<InputIt>) {
        size_type const count = static_cast<size_type>(std::ranges::distance(first, last));
        if (count > capacity()) [[unlikely]] inplace_vector_detail::throw_bad_alloc_();
        clear();
        for (; first != last; ++first) (void) unchecked_emplace_back(*first); // capacity pre-checked
      } else {
        clear();
        for (; first != last; ++first) (void) emplace_back(*first); // count unknown: keep per-element check
      }
    }
  }

  template <inplace_vector_detail::container_compatible_range<value_type> R>
  constexpr void assign_range(R &&rg)
  {
    assign(std::ranges::begin(rg), std::ranges::end(rg));
  }

  constexpr void assign(size_type const count, const_reference value)
  {
    if (count > capacity()) [[unlikely]] inplace_vector_detail::throw_bad_alloc_();

    value_type const copy(value);
    if constexpr (std::is_assignable_v<value_type &, const_reference>) {
      size_type const old    = size();
      size_type const common = count < old ? count : old;

      (void) std::fill(data(), data() + common, copy);

      if (count > old) {
        if (try_memset_fill_n_from_(old, count, copy)) {
          storage_.set_size(count);
        } else {
          for (size_type i = old; i < count; ++i) emplace_at_unchecked_(i, copy);
        }
      }
      if (count < old) storage_.destroy_from(count);
    } else {
      clear();
      for (size_type i = 0; i < count; ++i) emplace_at_unchecked_(i, copy);
    }
  }

  constexpr void assign(std::initializer_list<value_type> ilist) { assign(ilist.begin(), ilist.end()); }

  // ---------- iterators ----------
  [[nodiscard]] constexpr iterator begin() noexcept { return data(); }
  [[nodiscard]] constexpr const_iterator begin() const noexcept { return data(); }
  [[nodiscard]] constexpr iterator end() noexcept { return data() + size(); }
  [[nodiscard]] constexpr const_iterator end() const noexcept { return data() + size(); }
  [[nodiscard]] constexpr reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
  [[nodiscard]] constexpr const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
  [[nodiscard]] constexpr reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
  [[nodiscard]] constexpr const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
  [[nodiscard]] constexpr const_iterator cbegin() const noexcept { return begin(); }
  [[nodiscard]] constexpr const_iterator cend() const noexcept { return end(); }
  [[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept { return rbegin(); }
  [[nodiscard]] constexpr const_reverse_iterator crend() const noexcept { return rend(); }

  // ---------- capacity ----------
  constexpr bool empty() const noexcept { return size() == 0; }
  [[nodiscard]] constexpr size_type size() const noexcept { return storage_.size(); }
  [[nodiscard]] static constexpr size_type max_size() noexcept { return N; }
  [[nodiscard]] static constexpr size_type capacity() noexcept { return N; }
  static constexpr void shrink_to_fit() noexcept {}

  static constexpr void reserve(size_type const count)
  {
    if (count > N) [[unlikely]] inplace_vector_detail::throw_bad_alloc_();
  }

private:
  constexpr size_type resize_prepare_(size_type const count)
  {
    if (count > capacity()) [[unlikely]] inplace_vector_detail::throw_bad_alloc_();
    size_type const old = size();
    if (count < old) storage_.destroy_from(count);
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
    (void) ::stdex::uninitialized_value_construct_n(data() + old, static_cast<std::ptrdiff_t>(count - old));
    storage_.set_size(count);
  }

  constexpr void resize(size_type const count, const_reference value)
  {
    size_type const old = resize_prepare_(count);
    if (count <= old) return;
    (void) ::stdex::uninitialized_fill_n(data() + old, static_cast<std::ptrdiff_t>(count - old), value);
    storage_.set_size(count);
  }

  // ---------- element access ----------
  [[nodiscard]] constexpr reference operator[](size_type const index) noexcept
  {
    assert(index < size());
    return data()[index];
  }

  [[nodiscard]] constexpr const_reference operator[](size_type const index) const noexcept
  {
    assert(index < size());
    return data()[index];
  }

  [[nodiscard]] constexpr reference at(size_type const index)
  {
    if (index >= size()) [[unlikely]] inplace_vector_detail::throw_out_of_range_();
    return data()[index];
  }

  [[nodiscard]] constexpr const_reference at(size_type const index) const
  {
    if (index >= size()) [[unlikely]] inplace_vector_detail::throw_out_of_range_();
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

  [[nodiscard]] constexpr value_type *data() noexcept { return storage_.data(); }
  [[nodiscard]] constexpr value_type const *data() const noexcept { return storage_.data(); }

  // ---------- modifiers ----------
  template <class... Args>
  requires std::constructible_from<value_type, Args...>
  constexpr reference unchecked_emplace_back(Args &&...args)
      noexcept(noexcept(storage_.emplace_at(size(), std::forward<Args>(args)...)))
  {
    size_type const index = size();
    assert(index < capacity());
    value_type &element = storage_.emplace_at(index, std::forward<Args>(args)...);
    storage_.set_size(index + 1);
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
    if (size() == capacity()) [[unlikely]] inplace_vector_detail::throw_bad_alloc_();
    return unchecked_emplace_back(std::forward<Args>(args)...);
  }

  constexpr reference push_back(const_reference value) { return emplace_back(value); }
  constexpr reference push_back(value_type &&value) { return emplace_back(std::move(value)); }

  template <class... Args>
  requires std::constructible_from<value_type, Args...>
  [[nodiscard]] constexpr try_result_type try_emplace_back(Args &&...args)
      noexcept(noexcept(unchecked_emplace_back(std::forward<Args>(args)...)))
  {
    if (size() == capacity()) {
#if __cplusplus >= 202603L
      return std::nullopt;
#else
      return nullptr;
#endif
    }
    reference element = unchecked_emplace_back(std::forward<Args>(args)...);
#if __cplusplus >= 202603L
    return element;
#else
    return std::addressof(element);
#endif
  }

  [[nodiscard]] constexpr try_result_type try_push_back(const_reference value)
      noexcept(noexcept(try_emplace_back(value)))
  {
    return try_emplace_back(value);
  }

  [[nodiscard]] constexpr try_result_type try_push_back(value_type &&value)
      noexcept(noexcept(try_emplace_back(std::move(value))))
  {
    return try_emplace_back(std::move(value));
  }

  template <inplace_vector_detail::container_compatible_range<value_type> R>
  constexpr void append_range(R &&rg)
  {
    if constexpr (std::ranges::sized_range<R> || std::ranges::forward_range<R>) {
      size_type const count = static_cast<size_type>(std::ranges::distance(rg));
      size_type const old   = size();
      if (count > capacity() - old) [[unlikely]] inplace_vector_detail::throw_bad_alloc_();

      if constexpr (uninitialized_detail::iter_copy_cat<std::ranges::iterator_t<R>, value_type *>::bitcopy_constructible) {
        if (!std::is_constant_evaluated()) {
          if (count != 0) {
            auto const src = std::ranges::begin(rg);
            (void) uninitialized_detail::invoke_memcpy(
                src, src + static_cast<std::ranges::range_difference_t<R>>(count),
                data() + old, data() + old + count);
          }
          storage_.set_size(old + count);
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
    storage_.destroy_from(size() - 1);
  }

  template <class... Args>
  requires std::constructible_from<value_type, Args...> && std::movable<value_type>
  constexpr iterator emplace(const_iterator position, Args &&...args)
  {
    size_type const old = size();
    if (old == capacity()) [[unlikely]] inplace_vector_detail::throw_bad_alloc_();
    size_type const index = static_cast<size_type>(position - cbegin());

    if constexpr (uninitialized_detail::iter_move_cat<value_type *, value_type *>::bitcopy_constructible) {
      if (!std::is_constant_evaluated()) {
        value_type temp(std::forward<Args>(args)...);
        std::memmove(static_cast<void *>(data() + index + 1),
                     static_cast<void const *>(data() + index),
                     (old - index) * sizeof(value_type));
        storage_.emplace_at(index, std::move(temp));
        storage_.set_size(old + 1);
        return begin() + index;
      }
    }

    (void) unchecked_emplace_back(std::forward<Args>(args)...);
    iterator const inserted = begin() + index;
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
    if (count > capacity() - old) [[unlikely]] inplace_vector_detail::throw_bad_alloc_();
    size_type const index = static_cast<size_type>(position - cbegin());
    if (count == 0) [[unlikely]] return begin() + index;

    if constexpr (uninitialized_detail::iter_move_cat<value_type *, value_type *>::bitcopy_constructible) {
      if (!std::is_constant_evaluated()) {
        value_type const copy(value); // value may alias an element shifted by memmove
        std::memmove(static_cast<void *>(data() + index + count),
                     static_cast<void const *>(data() + index),
                     (old - index) * sizeof(value_type));
        if (!try_memset_fill_n_from_(index, count, copy)) {
          storage_.uninitialized_fill_n_at(index, count, copy);
        }
        storage_.set_size(old + count);
        return begin() + index;
      }
    }

    for (size_type i = 0; i < count; ++i) (void) unchecked_emplace_back(value);
    std::rotate(begin() + index, end() - count, end());
    return begin() + index;
  }

  template <std::input_iterator InputIt, std::sentinel_for<InputIt> Sentinel>
  requires std::constructible_from<value_type, std::iter_reference_t<InputIt>> && std::movable<value_type>
  constexpr iterator insert(const_iterator position, InputIt first, Sentinel last)
  {
    size_type const old   = size();
    size_type const index = static_cast<size_type>(position - cbegin());

    if constexpr (std::forward_iterator<InputIt>) {
      size_type const count = static_cast<size_type>(std::ranges::distance(first, last));
      if (count > capacity() - old) [[unlikely]] inplace_vector_detail::throw_bad_alloc_();
      if (count == 0) [[unlikely]] return begin() + index;

      constexpr bool can_relocate = uninitialized_detail::iter_move_cat<value_type *, value_type *>::bitcopy_constructible;
      constexpr bool can_copy_in  = uninitialized_detail::iter_copy_cat<InputIt, value_type *>::bitcopy_constructible;

      if constexpr (can_relocate && can_copy_in) {
        if (!std::is_constant_evaluated()) {
          std::memmove(static_cast<void *>(data() + index + count),
                       static_cast<void const *>(data() + index),
                       (old - index) * sizeof(value_type));
          std::memcpy(static_cast<void *>(data() + index), std::to_address(first), count * sizeof(value_type));
          storage_.set_size(old + count);
          return begin() + index;
        }
      }
      for (; first != last; ++first) (void) unchecked_emplace_back(*first);
    } else {
      for (; first != last; ++first) (void) emplace_back(*first);
    }
    std::rotate(begin() + index, begin() + old, end());
    return begin() + index;
  }

  template <inplace_vector_detail::container_compatible_range<value_type> R>
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
              data() + static_cast<difference_type>(old),
              data() + static_cast<difference_type>(index));
    storage_.destroy_from(old - count);
    return begin() + static_cast<difference_type>(index);
  }

  constexpr void clear() noexcept { storage_.destroy_from(0); }

  constexpr void swap(inplace_vector &other) noexcept(std::is_nothrow_swappable_v<value_type> && std::is_nothrow_move_constructible_v<value_type>)
  requires std::swappable<value_type>
  {
    if (this == std::addressof(other)) return;

    inplace_vector *const smaller = other.size() < size() ? std::addressof(other) : this;
    inplace_vector *const bigger  = other.size() < size() ? this : std::addressof(other);
    size_type const common = smaller->size();
    size_type const extra  = bigger->size();

    std::swap_ranges(smaller->data(), smaller->data() + common, bigger->data());

    if constexpr (uninitialized_detail::iter_move_cat<value_type *, value_type *>::bitcopy_constructible) {
      if (!std::is_constant_evaluated()) {
        (void) uninitialized_detail::invoke_memcpy(bigger->data() + common, bigger->data() + extra,
                                                   smaller->data() + common, smaller->data() + extra);
        smaller->storage_.set_size(extra);
        bigger->storage_.destroy_from(common);
        return;
      }
    }

    for (size_type i = common; i < extra; ++i) {
      smaller->storage_.emplace_at(i, std::move((*bigger)[i]));
      smaller->storage_.set_size(i + 1);
    }
    bigger->storage_.destroy_from(common);
  }

  friend constexpr void swap(inplace_vector &lhs, inplace_vector &rhs) noexcept(noexcept(lhs.swap(rhs)))
  {
    lhs.swap(rhs);
  }

  // ---------- comparison ----------
  friend constexpr bool operator==(inplace_vector const &lhs, inplace_vector const &rhs)
  requires std::equality_comparable<value_type>
  {
    return lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin());
  }

  friend constexpr auto operator<=>(inplace_vector const &lhs, inplace_vector const &rhs)
  requires requires(value_type const &lhs_value, value_type const &rhs_value) {
    inplace_vector_detail::synth_three_way(lhs_value, rhs_value);
  }
  {
    return std::lexicographical_compare_three_way(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
                                                  inplace_vector_detail::synth_three_way);
  }

private:
  inplace_vector_detail::storage_for_t<value_type, N> storage_ {};
};

/// Zero-capacity specialization: empty for every `value_type`, trivially copyable, and
/// every operation that would add an element throws `std::bad_alloc`.
template <class T>
class inplace_vector<T, 0>
{
public:
  using value_type             = T;
  using pointer                = value_type *;
  using const_pointer          = value_type const *;
  using reference              = value_type &;
  using const_reference        = value_type const &;
  using size_type              = std::size_t;
  using difference_type        = std::ptrdiff_t;
  using iterator               = value_type *;
  using const_iterator         = value_type const *;
  using reverse_iterator       = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;
#if __cplusplus >= 202603L
  using try_result_type = std::optional<value_type &>;
#else
  using try_result_type = value_type *;
#endif

  constexpr inplace_vector() noexcept = default;
  constexpr inplace_vector(inplace_vector const &) = default;
  constexpr inplace_vector(inplace_vector &&) = default;
  constexpr ~inplace_vector() = default;
  constexpr inplace_vector &operator=(inplace_vector const &) = default;
  constexpr inplace_vector &operator=(inplace_vector &&) = default;

  constexpr explicit inplace_vector(size_type const count)
  {
    if (count != 0) [[unlikely]] inplace_vector_detail::throw_bad_alloc_();
  }

  constexpr inplace_vector(size_type const count, const_reference)
  {
    if (count != 0) [[unlikely]] inplace_vector_detail::throw_bad_alloc_();
  }

  template <std::input_iterator InputIt, std::sentinel_for<InputIt> Sentinel>
  constexpr inplace_vector(InputIt first, Sentinel last)
  {
    if (first != last) [[unlikely]] inplace_vector_detail::throw_bad_alloc_();
  }

  constexpr inplace_vector(std::initializer_list<value_type> ilist)
  {
    if (ilist.size() != 0) [[unlikely]] inplace_vector_detail::throw_bad_alloc_();
  }

#ifdef __cpp_lib_containers_ranges
  template <inplace_vector_detail::container_compatible_range<value_type> R>
  constexpr inplace_vector(std::from_range_t, R &&rg)
  {
    if (std::ranges::begin(rg) != std::ranges::end(rg)) [[unlikely]] inplace_vector_detail::throw_bad_alloc_();
  }
#endif

  constexpr inplace_vector &operator=(std::initializer_list<value_type> ilist)
  {
    if (ilist.size() != 0) [[unlikely]] inplace_vector_detail::throw_bad_alloc_();
    return *this;
  }

  template <std::input_iterator InputIt, std::sentinel_for<InputIt> Sentinel>
  constexpr void assign(InputIt first, Sentinel last)
  {
    if (first != last) [[unlikely]] inplace_vector_detail::throw_bad_alloc_();
  }

  template <inplace_vector_detail::container_compatible_range<value_type> R>
  constexpr void assign_range(R &&rg)
  {
    if (std::ranges::begin(rg) != std::ranges::end(rg)) [[unlikely]] inplace_vector_detail::throw_bad_alloc_();
  }

  constexpr void assign(size_type const count, const_reference)
  {
    if (count != 0) [[unlikely]] inplace_vector_detail::throw_bad_alloc_();
  }

  constexpr void assign(std::initializer_list<value_type> ilist)
  {
    if (ilist.size() != 0) [[unlikely]] inplace_vector_detail::throw_bad_alloc_();
  }

  [[nodiscard]] constexpr iterator begin() noexcept { return nullptr; }
  [[nodiscard]] constexpr const_iterator begin() const noexcept { return nullptr; }
  [[nodiscard]] constexpr iterator end() noexcept { return nullptr; }
  [[nodiscard]] constexpr const_iterator end() const noexcept { return nullptr; }
  [[nodiscard]] constexpr reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
  [[nodiscard]] constexpr const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
  [[nodiscard]] constexpr reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
  [[nodiscard]] constexpr const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
  [[nodiscard]] constexpr const_iterator cbegin() const noexcept { return nullptr; }
  [[nodiscard]] constexpr const_iterator cend() const noexcept { return nullptr; }
  [[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept { return rbegin(); }
  [[nodiscard]] constexpr const_reverse_iterator crend() const noexcept { return rend(); }

  constexpr bool empty() const noexcept { return true; }
  [[nodiscard]] constexpr size_type size() const noexcept { return 0; }
  [[nodiscard]] static constexpr size_type max_size() noexcept { return 0; }
  [[nodiscard]] static constexpr size_type capacity() noexcept { return 0; }
  static constexpr void shrink_to_fit() noexcept {}

  static constexpr void reserve(size_type const count)
  {
    if (count != 0) [[unlikely]] inplace_vector_detail::throw_bad_alloc_();
  }

  constexpr void resize(size_type const count)
  {
    if (count != 0) [[unlikely]] inplace_vector_detail::throw_bad_alloc_();
  }

  constexpr void resize(size_type const count, const_reference)
  {
    if (count != 0) [[unlikely]] inplace_vector_detail::throw_bad_alloc_();
  }

  [[nodiscard]] constexpr reference operator[](size_type) noexcept
  {
    assert(false && "inplace_vector<T, 0>::operator[] is out of range");
    return *static_cast<value_type *>(nullptr);
  }

  [[nodiscard]] constexpr const_reference operator[](size_type) const noexcept
  {
    assert(false && "inplace_vector<T, 0>::operator[] is out of range");
    return *static_cast<value_type const *>(nullptr);
  }

  [[nodiscard]] constexpr reference at(size_type) { inplace_vector_detail::throw_out_of_range_(); }
  [[nodiscard]] constexpr const_reference at(size_type) const { inplace_vector_detail::throw_out_of_range_(); }
  [[nodiscard]] constexpr reference front() noexcept
  {
    assert(false && "inplace_vector<T, 0>::front on empty");
    return *static_cast<value_type *>(nullptr);
  }
  [[nodiscard]] constexpr const_reference front() const noexcept
  {
    assert(false && "inplace_vector<T, 0>::front on empty");
    return *static_cast<value_type const *>(nullptr);
  }
  [[nodiscard]] constexpr reference back() noexcept
  {
    assert(false && "inplace_vector<T, 0>::back on empty");
    return *static_cast<value_type *>(nullptr);
  }
  [[nodiscard]] constexpr const_reference back() const noexcept
  {
    assert(false && "inplace_vector<T, 0>::back on empty");
    return *static_cast<value_type const *>(nullptr);
  }

  [[nodiscard]] constexpr value_type *data() noexcept { return nullptr; }
  [[nodiscard]] constexpr value_type const *data() const noexcept { return nullptr; }

  template <class... Args>
  constexpr reference emplace_back(Args &&...) { inplace_vector_detail::throw_bad_alloc_(); }
  constexpr reference push_back(const_reference) { inplace_vector_detail::throw_bad_alloc_(); }
  constexpr reference push_back(value_type &&) { inplace_vector_detail::throw_bad_alloc_(); }
  template <class... Args>
  [[nodiscard]] constexpr try_result_type try_emplace_back(Args &&...)
  {
#if __cplusplus >= 202603L
    return std::nullopt;
#else
    return nullptr;
#endif
  }
  [[nodiscard]] constexpr try_result_type try_push_back(const_reference)
  {
#if __cplusplus >= 202603L
    return std::nullopt;
#else
    return nullptr;
#endif
  }
  [[nodiscard]] constexpr try_result_type try_push_back(value_type &&)
  {
#if __cplusplus >= 202603L
    return std::nullopt;
#else
    return nullptr;
#endif
  }

  template <inplace_vector_detail::container_compatible_range<value_type> R>
  constexpr void append_range(R &&rg)
  {
    if (std::ranges::begin(rg) != std::ranges::end(rg)) [[unlikely]] inplace_vector_detail::throw_bad_alloc_();
  }

  constexpr void pop_back() noexcept { assert(false && "inplace_vector<T, 0>::pop_back on empty"); }

  template <class... Args>
  constexpr iterator emplace(const_iterator, Args &&...) { inplace_vector_detail::throw_bad_alloc_(); }
  constexpr iterator insert(const_iterator, const_reference) { inplace_vector_detail::throw_bad_alloc_(); }
  constexpr iterator insert(const_iterator, value_type &&) { inplace_vector_detail::throw_bad_alloc_(); }
  constexpr iterator insert(const_iterator, size_type const count, const_reference)
  {
    if (count != 0) [[unlikely]] inplace_vector_detail::throw_bad_alloc_();
    return nullptr;
  }
  template <std::input_iterator InputIt, std::sentinel_for<InputIt> Sentinel>
  constexpr iterator insert(const_iterator, InputIt first, Sentinel last)
  {
    if (first != last) [[unlikely]] inplace_vector_detail::throw_bad_alloc_();
    return nullptr;
  }
  template <inplace_vector_detail::container_compatible_range<value_type> R>
  constexpr iterator insert_range(const_iterator, R &&rg)
  {
    if (std::ranges::begin(rg) != std::ranges::end(rg)) [[unlikely]] inplace_vector_detail::throw_bad_alloc_();
    return nullptr;
  }
  constexpr iterator insert(const_iterator, std::initializer_list<value_type> ilist)
  {
    if (ilist.size() != 0) [[unlikely]] inplace_vector_detail::throw_bad_alloc_();
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
