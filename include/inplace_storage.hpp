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

#include <algorithm>   // std::equal, std::lexicographical_compare_three_way
#include <array>       // std::array
#include <cassert>     // assert
#include <compare>     // std::weak_ordering, std::three_way_comparable_with
#include <concepts>    // std::convertible_to, std::equality_comparable, std::same_as
#include <cstddef>     // std::size_t
#include <cstdlib>     // std::abort
#include <iterator>    // std::reverse_iterator
#include <memory>      // std::construct_at, std::destroy_at, std::start_lifetime
#include <new>         // std::bad_alloc
#include <ranges>      // std::ranges::input_range, std::ranges::range_reference_t
#include <stdexcept>   // std::out_of_range, std::length_error
#include <type_traits> // std::is_trivially_destructible_v, std::is_constant_evaluated
#include <utility>     // std::forward, std::move_if_noexcept
#include <version>     // __cpp_lib_start_lifetime

namespace stdex
{
/// Tag requesting that the elements' storage is left uninitialized.
///
/// It is a deliberately explicit, high-risk opt-in: the caller takes
/// over lifetime management (construction of every element before
/// the container is destroyed). Any exception or early return that
/// leaves fewer than N elements constructed makes the container's
/// destruction undefined behavior.
struct uninitialized_t
{
  explicit constexpr uninitialized_t() = default;
};

inline constexpr uninitialized_t uninitialized {};

namespace inplace_storage_detail
{
template <class R, class T>
concept container_compatible_range = std::ranges::input_range<R> && std::convertible_to<std::ranges::range_reference_t<R>, T>;

struct synth_three_way_fn
{
  template <class T, class U>
  constexpr auto operator() (T const &lhs, U const &rhs) const
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

[[noreturn]] static constexpr void throw_out_of_range()
{
#if defined(_CPPUNWIND) || defined(__EXCEPTIONS)
  throw std::out_of_range("index out of range");
#else
  std::abort();
#endif
}

[[noreturn]] constexpr void throw_bad_alloc()
{
#if defined(_CPPUNWIND) || defined(__EXCEPTIONS)
  throw std::bad_alloc();
#else
  std::abort();
#endif
}

[[noreturn]] constexpr void throw_length_error(char const *what)
{
#if defined(_CPPUNWIND) || defined(__EXCEPTIONS)
  throw std::length_error(what);
#else
  (void) what;
  std::abort();
#endif
}

/// Size policy: carriers the logical element count used by dynamically-sized
/// inplace_storage consumers (inplace_vector).
struct storage_size_base
{
  std::size_t size_ = 0;
};

/// Size policy: nothing. Used by fixed-size consumers (inplace_array) where the
/// element count is always exactly `N` and must not cost an extra word.
struct storage_no_size_base
{
};
#if defined(__cpp_lib_start_lifetime) && __cpp_lib_start_lifetime >= 202603L
/// C++26: std::start_lifetime() brings the array member into lifetime, so a
/// union (which never constructs elements on its own) serves every element type.
template <class T, std::size_t N>
struct inplace_storage_holder
{
  union { std::array<T, N> elements_; };
  constexpr inplace_storage_holder() noexcept {}
  constexpr inplace_storage_holder(inplace_storage_holder const &) = default;
  constexpr inplace_storage_holder(inplace_storage_holder &&)      = default;
  constexpr inplace_storage_holder &operator=(inplace_storage_holder const &) = default;
  constexpr inplace_storage_holder &operator=(inplace_storage_holder &&)      = default;
  constexpr ~inplace_storage_holder() requires std::is_trivially_destructible_v<T> = default;
  constexpr ~inplace_storage_holder() noexcept requires (!std::is_trivially_destructible_v<T>) {}
};
#else
/// Pre-C++26 has no std::start_lifetime(); a strict compiler rejects constant
/// evaluation of a union member whose lifetime was never started. Trivial
/// elements therefore use a plain array member (byte-identical to the union of a
/// single array, and constexpr-usable), while non-trivial elements keep the union
/// so nothing is constructed before uninitialized_* runs.
/// 
// std::is_trivial() is deprecated in C++26 (P3247R2, "Deprecating the notion of
// trivial types"); spell out its exact definition (trivially default constructible
// && trivially copyable) so this branch stays warning-free if ever compiled as C++26.
template <class T, std::size_t N,
          bool = std::is_trivially_default_constructible_v<T> && std::is_trivially_copyable_v<T>>
struct inplace_storage_holder
{
  union { std::array<T, N> elements_; };
  constexpr inplace_storage_holder() noexcept {}
  constexpr inplace_storage_holder(inplace_storage_holder const &) = default;
  constexpr inplace_storage_holder(inplace_storage_holder &&)      = default;
  constexpr inplace_storage_holder &operator=(inplace_storage_holder const &) = default;
  constexpr inplace_storage_holder &operator=(inplace_storage_holder &&)      = default;
  constexpr ~inplace_storage_holder() requires std::is_trivially_destructible_v<T> = default;
  constexpr ~inplace_storage_holder() noexcept requires (!std::is_trivially_destructible_v<T>) {}
};
template <class T, std::size_t N>
struct inplace_storage_holder<T, N, true>
{
  std::array<T, N> elements_;
};
#endif
} // namespace inplace_storage_detail

/// Element buffer for union-backed storages. The bound is exactly N (N == 0 stays zero-capacity).
template <class SizePolicy, class T, std::size_t N>
struct inplace_storage;

template <class SizePolicy, class T, std::size_t N>
requires (N != 0)
struct inplace_storage<SizePolicy, T, N> : SizePolicy, inplace_storage_detail::inplace_storage_holder<T, N>
{
private:
  /// Borrow std::array's iterators so debug builds get the standard library's
  /// iterator checking on begin()/end(), which raw T* cannot provide.
  /// data() still yields a raw T*, as std::array does.
  using array_type = std::array<T, N>;

public:
  using value_type             = typename array_type::value_type;
  using pointer                = typename array_type::pointer;
  using const_pointer          = typename array_type::const_pointer;
  using reference              = typename array_type::reference;
  using const_reference        = typename array_type::const_reference;
  using size_type              = typename array_type::size_type;
  using difference_type        = typename array_type::difference_type;
  using iterator               = typename array_type::iterator;
  using const_iterator         = typename array_type::const_iterator;
  using reverse_iterator       = typename array_type::reverse_iterator;
  using const_reverse_iterator = typename array_type::const_reverse_iterator;

  constexpr inplace_storage() noexcept
  {
#if defined(__cpp_lib_start_lifetime) && __cpp_lib_start_lifetime >= 202603L
    if (std::is_constant_evaluated()) std::start_lifetime(this->elements_);
#endif
  }

  constexpr inplace_storage(inplace_storage const &) = default;
  constexpr inplace_storage(inplace_storage &&)      = default;
  constexpr inplace_storage &operator=(inplace_storage const &) = default;
  constexpr inplace_storage &operator=(inplace_storage &&)      = default;

  /// Lifetime is owned by the container, not by this storage.
  ///
  /// A storage subobject is destroyed while a derived constructor unwinds, at which point
  /// only the prefix built so far is alive (and any backout has already destroyed it),
  /// so destroying `size()` elements here would over-destroy.
  /// Non-trivially destructible T therefore only needs a destructor that exists (the union's
  /// implicit one is deleted) and does nothing; the container destroys elements.
  constexpr ~inplace_storage() requires std::is_trivially_destructible_v<T> = default;
  constexpr ~inplace_storage() noexcept requires (!std::is_trivially_destructible_v<T>) {} // don't call std::destory_n

  // ---------- element access ----------
  [[nodiscard]] constexpr reference operator[] (size_type const index) noexcept
  {
    assert(index < size());
    return this->elements_[index]; // std::array keeps its debug bounds check
  }

  [[nodiscard]] constexpr const_reference operator[] (size_type const index) const noexcept
  {
    assert(index < size());
    return this->elements_[index];
  }

  [[nodiscard]] constexpr reference at(size_type const index)
  {
    if (index >= size()) [[unlikely]] inplace_storage_detail::throw_out_of_range();
    return this->elements_[index];
  }

  [[nodiscard]] constexpr const_reference at(size_type const index) const
  {
    if (index >= size()) [[unlikely]] inplace_storage_detail::throw_out_of_range();
    return this->elements_[index];
  }

  [[nodiscard]] constexpr reference front() noexcept
  {
    assert(size() != 0 && "front on empty array");
    return data()[0];
  }

  [[nodiscard]] constexpr const_reference front() const noexcept
  {
    assert(size() != 0 && "front on empty array");
    return data()[0];
  }

  [[nodiscard]] constexpr reference back() noexcept
  {
    assert(size() != 0 && "back on empty array");
    return data()[size() - 1];
  }

  [[nodiscard]] constexpr const_reference back() const noexcept
  {
    assert(size() != 0 && "back on empty array");
    return data()[size() - 1];
  }

  [[nodiscard]] constexpr value_type *data() noexcept { return this->elements_.data(); }
  [[nodiscard]] constexpr value_type const *data() const noexcept { return this->elements_.data(); }


  // ---------- capacity ----------
  constexpr bool empty() const noexcept { return size() == 0; }
  [[nodiscard]] static constexpr size_type max_size() noexcept { return N; }
  [[nodiscard]] constexpr size_type size() const noexcept
  {
    if constexpr (std::same_as<SizePolicy, inplace_storage_detail::storage_size_base>) {
      return this->size_;
    } else {
      return N;
    }
  }

  // ---------- iterators ----------
  // The live range is [0, size()): inplace_array has size() == N, inplace_vector
  // size() <= N, so both consumers reuse these (end() stops at size(), never N).
  [[nodiscard]] constexpr iterator begin() noexcept { return this->elements_.begin(); }
  [[nodiscard]] constexpr const_iterator begin() const noexcept { return this->elements_.begin(); }
  [[nodiscard]] constexpr iterator end() noexcept { return this->elements_.begin() + static_cast<difference_type>(size()); }
  [[nodiscard]] constexpr const_iterator end() const noexcept { return this->elements_.begin() + static_cast<difference_type>(size()); }
  [[nodiscard]] constexpr reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
  [[nodiscard]] constexpr const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
  [[nodiscard]] constexpr reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
  [[nodiscard]] constexpr const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
  [[nodiscard]] constexpr const_iterator cbegin() const noexcept { return begin(); }
  [[nodiscard]] constexpr const_iterator cend() const noexcept { return end(); }
  [[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept { return rbegin(); }
  [[nodiscard]] constexpr const_reverse_iterator crend() const noexcept { return rend(); }

  // ---------- comparison ----------
  friend constexpr bool operator== (inplace_storage const &lhs, inplace_storage const &rhs)
  requires std::equality_comparable<value_type>
  {
    return std::equal(lhs.begin(), lhs.end(), rhs.begin());
  }

  friend constexpr auto operator<=> (inplace_storage const &lhs, inplace_storage const &rhs)
  requires requires (value_type const &lhs_value, value_type const &rhs_value) { inplace_storage_detail::synth_three_way(lhs_value, rhs_value); }
  {
    return std::lexicographical_compare_three_way(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
                                                  inplace_storage_detail::synth_three_way);
  }

};

template <class SizePolicy, class T>
struct inplace_storage<SizePolicy, T, 0> : SizePolicy
{
public:
  /// No storage is retained, so iterators degrade to raw pointers over nothing.
  /// std::array<T, 0>'s iterator is a class type on some standard libraries and
  /// cannot be built from nullptr, hence these aliases are spelled directly.
  using value_type             = T;
  using pointer                = T *;
  using const_pointer          = T const *;
  using reference              = T &;
  using const_reference        = T const &;
  using size_type              = std::size_t;
  using difference_type        = std::ptrdiff_t;
  using iterator               = T *;
  using const_iterator         = T const *;
  using reverse_iterator       = std::reverse_iterator<T *>;
  using const_reverse_iterator = std::reverse_iterator<T const *>;

  constexpr inplace_storage() noexcept  = default;
  constexpr ~inplace_storage() noexcept = default;

  // ---------- element access ----------
  [[nodiscard]] constexpr reference operator[] (size_type) noexcept
  {
    assert(false && "operator[] on zero-capacity storage");
    return *static_cast<pointer>(nullptr);
  }

  [[nodiscard]] constexpr const_reference operator[] (size_type) const noexcept
  {
    assert(false && "operator[] on zero-capacity storage");
    return *static_cast<const_pointer>(nullptr);
  }

  [[nodiscard]] constexpr reference at(size_type)
  {
    inplace_storage_detail::throw_out_of_range();
  }

  [[nodiscard]] constexpr const_reference at(size_type) const
  {
    inplace_storage_detail::throw_out_of_range();
  }

  [[nodiscard]] constexpr reference front() noexcept
  {
    assert(false && "front on empty storage");
    return *static_cast<pointer>(nullptr);
  }

  [[nodiscard]] constexpr const_reference front() const noexcept
  {
    assert(false && "front on empty storage");
    return *static_cast<const_pointer>(nullptr);
  }

  [[nodiscard]] constexpr reference back() noexcept
  {
    assert(false && "back on empty storage");
    return *static_cast<pointer>(nullptr);
  }

  [[nodiscard]] constexpr const_reference back() const noexcept
  {
    assert(false && "back on empty storage");
    return *static_cast<const_pointer>(nullptr);
  }

  [[nodiscard]] constexpr value_type *data() noexcept { return nullptr; }
  [[nodiscard]] constexpr value_type const *data() const noexcept { return nullptr; }

  // ---------- capacity ----------
  constexpr bool empty() const noexcept { return true; }
  [[nodiscard]] static constexpr size_type max_size() noexcept { return 0; }
  [[nodiscard]] constexpr size_type size() const noexcept
  {
    if constexpr (std::same_as<SizePolicy, inplace_storage_detail::storage_size_base>) {
      return this->size_;
    } else {
      return 0;
    }
  }

  // ---------- iterators ----------
  [[nodiscard]] constexpr iterator begin() noexcept { return nullptr; }
  [[nodiscard]] constexpr const_iterator begin() const noexcept { return nullptr; }
  [[nodiscard]] constexpr iterator end() noexcept { return nullptr; }
  [[nodiscard]] constexpr const_iterator end() const noexcept { return nullptr; }
  [[nodiscard]] constexpr reverse_iterator rbegin() noexcept { return reverse_iterator(nullptr); }
  [[nodiscard]] constexpr const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(nullptr); }
  [[nodiscard]] constexpr reverse_iterator rend() noexcept { return reverse_iterator(nullptr); }
  [[nodiscard]] constexpr const_reverse_iterator rend() const noexcept { return const_reverse_iterator(nullptr); }
  [[nodiscard]] constexpr const_iterator cbegin() const noexcept { return nullptr; }
  [[nodiscard]] constexpr const_iterator cend() const noexcept { return nullptr; }
  [[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept { return rbegin(); }
  [[nodiscard]] constexpr const_reverse_iterator crend() const noexcept { return rend(); }

  // ---------- comparison ----------
  friend constexpr bool operator== (inplace_storage const &lhs, inplace_storage const &rhs)
  requires std::equality_comparable<value_type>
  {
    return std::equal(lhs.begin(), lhs.end(), rhs.begin());
  }

  friend constexpr auto operator<=> (inplace_storage const &lhs, inplace_storage const &rhs)
  requires requires (value_type const &lhs_value, value_type const &rhs_value) { inplace_storage_detail::synth_three_way(lhs_value, rhs_value); }
  {
    return std::lexicographical_compare_three_way(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
                                                  inplace_storage_detail::synth_three_way);
  }
};
} // namespace stdex
