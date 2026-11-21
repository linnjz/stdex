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

#include <algorithm>        // std::fill_n
#include <cassert>          // assert
#include <concepts>         // std::constructible_from, std::same_as, std::derived_from
#include <cstddef>          // std::size_t, std::ptrdiff_t
#include <cstdint>          // std::uint8_t
#include <initializer_list> // std::initializer_list
#include <iterator>         // std::forward_iterator, std::sentinel_for, std::iter_reference_t
#include <limits>           // std::numeric_limits
#include <memory>           // std::allocator_traits, std::addressof, std::construct_at, std::destroy_at
#include <ranges>           // std::ranges::forward_range, std::ranges::subrange, std::views::transform
#include <stdexcept>        // std::out_of_range
#include <tuple>            // std::tuple_element_t
#include <type_traits>      // std::remove_cvref_t, std::conditional_t, std::derived_from
#include <utility>          // std::move, std::exchange, std::pair
#include <version>          // __cpp_lib_containers_ranges

#include "../inplace_vector.hpp"
#include "../ptrhash.hpp"
#include "../uninitialized_algorithms.hpp"
#include "../uninitialized_buffer.hpp"
#include "mphmap_traits.hpp"
#include "mphset_traits.hpp"
#include "pointer_wrapper.hpp"
#include "random_placement_buffer.hpp"

namespace stdex::minimal_perfect_hash_detail
{
template <std::input_iterator Iter>
using guide_key_t = std::remove_const_t<std::tuple_element_t<0, typename std::iterator_traits<Iter>::value_type>>;
template <std::input_iterator Iter>
using guide_mapped_t = std::tuple_element_t<1, typename std::iterator_traits<Iter>::value_type>;
template <class Range>
using range_key_t = std::remove_const_t<std::tuple_element_t<0, std::ranges::range_value_t<Range>>>;
template <class Range>
using range_mapped_t = std::tuple_element_t<1, std::ranges::range_value_t<Range>>;

template <class Alloc>
concept is_allocator = requires (Alloc &alloc) {
  typename Alloc::value_type;
  alloc.deallocate(alloc.allocate(std::size_t { 1 }), std::size_t { 1 });
};

template <class Hasher, class Key>
concept is_hasher_for = std::regular_invocable<Hasher const &, Key const &> &&
                        std::convertible_to<std::invoke_result_t<Hasher const &, Key const &>, std::uint64_t>;

template <class T>
inline constexpr bool has_is_transparent = requires { typename T::is_transparent; };

template <class Hasher, class KeyEqual>
concept transparent_hash_equal = has_is_transparent<Hasher> && has_is_transparent<KeyEqual>;

template <class KeyType, class Iter, class ConstIter>
concept not_iterator_convertible = !std::is_convertible_v<KeyType, Iter> && !std::is_convertible_v<KeyType, ConstIter>;

template <class KeyType, class Hasher, class KeyEqual, class Iter, class ConstIter, class K>
concept heterogeneous_key =
    transparent_hash_equal<Hasher, KeyEqual> &&
    not_iterator_convertible<KeyType, Iter, ConstIter> &&
    is_hasher_for<Hasher, K> &&
    std::predicate<KeyEqual const &, KeyType const &, K const &>;

/// Immutable associative table backed by a minimal-perfect hash index.
///
/// The index stores no keys: @c ptr_hash::index() is a bijection over the built
/// key set onto `[0, size())`, so the value buffer holds exactly `size()` elements
/// in index order and every lookup verifies the candidate with @c key_equal.
/// Misses are rejected by the fingerprint that @c ptr_hash keeps inside its own
/// index blob, so no separate fingerprint buffer is needed.
///
/// Construction is a single pass: the index is built first (which also validates
/// the stack capacity and fixes the key->index mapping), then each value is
/// constructed directly at the index its key maps to. The range constructors
/// therefore require forward iterators -- the range is walked once to build the
/// index and once to construct the values; single-pass input iterators are
/// intentionally not supported (that would need a second value buffer).
///
/// heap: the value buffer is a bare pointer handle allocated from the single
/// allocator kept inside @c ptr_hash_storage_. stack: an inline raw buffer
/// (@c random_placement_buffer) with no allocation.
template <class Traits, class Hasher, class KeyEqual, auto Options>
requires ptr_hash_detail::is_ptr_hash_options_v<std::remove_cvref_t<decltype(Options)>>
class hash_table
{
  static_assert(Options.hash_scheme == ptr_hash_scheme::minimal_perfect,
                "minimal_perfect_hash containers require ptr_hash_scheme::minimal_perfect");

public:
  using options_type = std::remove_cvref_t<decltype(Options)>;
  static constexpr options_type options_value { Options };
  static constexpr ptr_hash_storage_policy storage_policy { options_type::storage_traits::storage_policy };
  /// The option set owns the allocator; rebinding it to @c value_type gives the
  /// container's value allocator. Stack storage is inline and never allocates.
  using allocator_type = typename std::allocator_traits<typename options_type::storage_traits::allocator_type>
                                     ::template rebind_alloc<typename Traits::value_type>;

  using key_type        = typename Traits::key_type;
  using mapped_type     = typename Traits::mapped_type;
  using value_type      = typename Traits::value_type;
  using hasher          = Hasher;
  using key_equal       = KeyEqual;
  using size_type       = std::size_t;
  using difference_type = std::ptrdiff_t;
  using const_reference = value_type const &;
  using reference       = const_reference;

  using ptr_hash_storage_type = ptr_hash<key_type, Hasher, options_value>;
  using value_storage_type    = std::conditional_t<
      storage_policy == ptr_hash_storage_policy::heap,
      pointer_wrapper<typename std::allocator_traits<allocator_type>::pointer>,
      random_placement_buffer<value_type, static_cast<size_type>(options_type::storage_traits::max_key_count)>>;

  /// Per-slot "constructed yet?" marker used by the construction pass so a
  /// throwing element construction rolls back exactly the slots it filled.
  using build_marker_type = std::conditional_t<
      storage_policy == ptr_hash_storage_policy::heap,
      uninitialized_buffer<std::uint8_t, typename std::allocator_traits<allocator_type>::template rebind_alloc<std::uint8_t>>,
      inplace_vector<std::uint8_t, static_cast<size_type>(options_type::storage_traits::max_key_count)>>;

  using const_pointer  = value_type const *;
  using pointer        = const_pointer;
  using const_iterator = value_type const *;
  using iterator       = const_iterator;

private:
  /// Deallocate the heap value buffer (destroying the `size()` live elements).
  constexpr void release_storage_() noexcept
  {
    if constexpr (storage_policy == ptr_hash_storage_policy::heap) {
      if (!value_storage_) [[unlikely]] return;
      allocator_type value_alloc { ptr_hash_storage_.get_allocator() };
      value_type *const raw = value_storage_.data();
      size_type const count = size();
      for (size_type i = 0; i < count; ++i) {
        std::allocator_traits<allocator_type>::destroy(value_alloc, raw + i);
      }
      std::allocator_traits<allocator_type>::deallocate(value_alloc, value_storage_.get(), count);
      value_storage_.reset();
    }
  }

public:
  constexpr hash_table() = default;
  constexpr ~hash_table()
  {
    if constexpr (storage_policy == ptr_hash_storage_policy::heap) {
      release_storage_();
    }
  }

private:
  /// Rolls a partially built value buffer back on unwind: destroys exactly the
  /// slots marked as constructed (they sit at arbitrary final indices, not a
  /// contiguous prefix). A null @c marker_ means the caller already rolled back
  /// its constructed prefix (e.g. @c uninitialized_copy_n), so only the raw heap
  /// allocation is released. @c alloc_ is null for stack storage.
  struct value_build_guard
  {
    value_storage_type &storage_;
    build_marker_type *marker_;
    size_type count_;
    allocator_type *alloc_ = nullptr;
    bool dismissed_        = false;

    value_build_guard &operator= (value_build_guard const &) = delete;
    value_build_guard &operator= (value_build_guard &&)      = delete;

    constexpr ~value_build_guard() noexcept
    {
      if (dismissed_) [[likely]] return;
      if constexpr (storage_policy == ptr_hash_storage_policy::heap) {
        if (marker_ != nullptr) {
          for (size_type i = 0; i < count_; ++i) {
            if ((*marker_)[i]) std::allocator_traits<allocator_type>::destroy(*alloc_, storage_.data() + i);
          }
        }
        std::allocator_traits<allocator_type>::deallocate(*alloc_, storage_.get(), count_);
      } else if (marker_ != nullptr) {
        for (size_type i = 0; i < count_; ++i) {
          if ((*marker_)[i]) std::destroy_at(storage_.data() + i);
        }
      }
    }

    constexpr void dismiss() noexcept { dismissed_ = true; }
  };

public:
  constexpr hash_table(hash_table const& other)
      : ptr_hash_storage_ { other.ptr_hash_storage_ }
      , key_eq_ { other.key_eq_ }
  {
    if constexpr (storage_policy == ptr_hash_storage_policy::heap) {
      size_type const count = other.size();
      if (count == 0) [[unlikely]] return;
      allocator_type value_alloc { ptr_hash_storage_.get_allocator() };
      value_storage_ = value_storage_type { std::allocator_traits<allocator_type>::allocate(value_alloc, count) };
      value_build_guard guard { value_storage_, nullptr, count, &value_alloc };
      (void) ::stdex::uninitialized_copy_n(other.value_storage_.data(), static_cast<difference_type>(count),
                                           value_storage_.data(), value_storage_.data() + count, value_alloc);
      guard.dismiss();
    } else {
      value_storage_ = other.value_storage_;
    }
  }

  constexpr hash_table(hash_table &&other) noexcept
      : ptr_hash_storage_ { std::move(other.ptr_hash_storage_) }
      , key_eq_ { std::move(other.key_eq_) }
  {
    if constexpr (storage_policy == ptr_hash_storage_policy::heap) {
      value_storage_ = other.value_storage_;
      other.value_storage_.reset();
    } else {
      value_storage_ = std::move(other.value_storage_);
    }
  }

  constexpr hash_table &operator= (hash_table const &other)
  {
    if (this == std::addressof(other)) [[unlikely]]  return *this;
    hash_table temporary { other };
    *this = std::move(temporary);
    return *this;
  }

  constexpr hash_table &operator= (hash_table &&other) noexcept
  {
    if (this == std::addressof(other)) [[unlikely]]  return *this;
    // Destroy and rebuild in place through the move constructor so a stateful
    // allocator is adopted (a plain member move would keep this object's old
    // allocator and mis-own the stolen buffers).
    this->~hash_table();
    std::construct_at(this, std::move(other));
    return *this;
  }

private:
  /// Builds the index from the value range by projecting each value onto its key,
  /// so the hasher only ever sees @c key_type (never @c value_type).
  template <class Iterator, class Sentinel>
  [[nodiscard]] static constexpr ptr_hash_storage_type make_ptr_hash_storage_(
      Iterator first, Sentinel last, hasher const &hash, allocator_type const &alloc)
  {
    auto keys = std::views::transform(
      std::ranges::subrange { std::move(first), std::move(last) },
      [] (value_type const &value) -> key_type const & { return Traits::key_of(value); });

    if constexpr (storage_policy == ptr_hash_storage_policy::heap) {
      return ptr_hash_storage_type { std::ranges::begin(keys), std::ranges::end(keys), hash, alloc };
    } else {
      (void) alloc;
      return ptr_hash_storage_type { std::ranges::begin(keys), std::ranges::end(keys), hash };
    }
  }

  [[nodiscard]] constexpr build_marker_type make_marker_(size_type const count, allocator_type const &alloc) const
  {
    if constexpr (storage_policy == ptr_hash_storage_policy::heap) {
      build_marker_type marker { count, typename std::allocator_traits<allocator_type>::template rebind_alloc<std::uint8_t> { alloc } };
      std::fill_n(marker.data(), count, std::uint8_t { 0 });
      return marker;
    } else {
      (void) alloc;
      return build_marker_type(count, std::uint8_t { 0 });
    }
  }

  template <std::forward_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
  constexpr value_storage_type construct_values_(Iterator first, Sentinel last)
  {
    size_type const count = ptr_hash_storage_.size();

    if constexpr (storage_policy == ptr_hash_storage_policy::heap) {
      if (count == 0) [[unlikely]] return {};

      allocator_type value_alloc { ptr_hash_storage_.get_allocator() };
      build_marker_type marker = make_marker_(count, value_alloc);
      value_storage_type storage { std::allocator_traits<allocator_type>::allocate(value_alloc, count) };
      value_build_guard guard { storage, &marker, count, &value_alloc };
      // Single pass: hash each key once and construct the value directly at its
      // final minimal-perfect index. No permutation pass and no move churn.
      for (Iterator it = first; it != last; ++it) {
        size_type const index = ptr_hash_storage_.index_unchecked(Traits::key_of(*it));
        assert(index < count);
        std::allocator_traits<allocator_type>::construct(value_alloc, storage.data() + index, *it);
        marker[index] = 1;
      }
      guard.dismiss();
      return storage;
    } else {
      if (count == 0) [[unlikely]]  return {};

      value_storage_type storage {};
      build_marker_type marker = make_marker_(count, allocator_type {});
      value_build_guard guard { storage, &marker, count };
      // Single pass: construct each value directly at its final minimal-perfect
      // index, then publish the element count.
      for (Iterator it = first; it != last; ++it) {
        size_type const index = ptr_hash_storage_.index_unchecked(Traits::key_of(*it));
        assert(index < count);
        storage.emplace_at(index, *it);
        marker[index] = 1;
      }
      storage.set_size(count);
      guard.dismiss();
      return storage;
    }
  }

public:
  // ---------- iterator-range constructors ----------
  template <std::forward_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
  requires std::constructible_from<value_type, std::iter_reference_t<Iterator>> &&
               (storage_policy == ptr_hash_storage_policy::heap)
  constexpr hash_table(Iterator first, Sentinel last, hasher const &hasher_arg, key_equal const &eq, allocator_type const &alloc)
      : ptr_hash_storage_ { make_ptr_hash_storage_(first, last, hasher_arg, alloc) }
      , value_storage_ { construct_values_(std::move(first), std::move(last)) }
      , key_eq_ { eq }
  {
  }

  template <std::forward_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
  requires std::constructible_from<value_type, std::iter_reference_t<Iterator>> &&
           (storage_policy == ptr_hash_storage_policy::heap)
  constexpr hash_table(Iterator first, Sentinel last, hasher const &hasher_arg, allocator_type const &alloc)
      : hash_table { std::move(first), std::move(last), hasher_arg, key_equal {}, alloc }
  {
  }

  template <std::forward_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
  requires std::constructible_from<value_type, std::iter_reference_t<Iterator>> &&
           (storage_policy == ptr_hash_storage_policy::heap)
  constexpr hash_table(Iterator first, Sentinel last, allocator_type const &alloc)
      : hash_table { std::move(first), std::move(last), hasher {}, key_equal {}, alloc }
  {
  }

  template <std::forward_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
  requires std::constructible_from<value_type, std::iter_reference_t<Iterator>>
  constexpr hash_table(Iterator first, Sentinel last, hasher const &hasher_arg, key_equal const &eq)
      : ptr_hash_storage_ { make_ptr_hash_storage_(first, last, hasher_arg, allocator_type {}) }
      , value_storage_ { construct_values_(std::move(first), std::move(last)) }
      , key_eq_ { eq }
  {
  }

  template <std::forward_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
  requires std::constructible_from<value_type, std::iter_reference_t<Iterator>>
  constexpr hash_table(Iterator first, Sentinel last, hasher const &hasher_arg)
      : hash_table { std::move(first), std::move(last), hasher_arg, key_equal {} }
  {
  }

  template <std::forward_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
  requires std::constructible_from<value_type, std::iter_reference_t<Iterator>>
  constexpr hash_table(Iterator first, Sentinel last)
      : hash_table { std::move(first), std::move(last), hasher {}, key_equal {} }
  {
  }

  // ---------- initializer_list constructors ----------
  constexpr hash_table(std::initializer_list<value_type> ilist, hasher const &hasher_arg, key_equal const &eq, allocator_type const &alloc)
  requires (storage_policy == ptr_hash_storage_policy::heap)
      : hash_table { ilist.begin(), ilist.end(), hasher_arg, eq, alloc }
  {
  }

  constexpr hash_table(std::initializer_list<value_type> ilist, hasher const &hasher_arg, allocator_type const &alloc)
  requires (storage_policy == ptr_hash_storage_policy::heap)
      : hash_table { ilist.begin(), ilist.end(), hasher_arg, key_equal {}, alloc }
  {
  }

  constexpr hash_table(std::initializer_list<value_type> ilist, allocator_type const &alloc)
  requires (storage_policy == ptr_hash_storage_policy::heap)
      : hash_table { ilist.begin(), ilist.end(), hasher {}, key_equal {}, alloc }
  {
  }

  constexpr hash_table(std::initializer_list<value_type> ilist, hasher const &hasher_arg, key_equal const &eq)
      : hash_table { ilist.begin(), ilist.end(), hasher_arg, eq }
  {
  }

  constexpr hash_table(std::initializer_list<value_type> ilist, hasher const &hasher_arg)
      : hash_table { ilist.begin(), ilist.end(), hasher_arg, key_equal {} }
  {
  }

  constexpr hash_table(std::initializer_list<value_type> ilist)
      : hash_table { ilist.begin(), ilist.end(), hasher {}, key_equal {} }
  {
  }

  // ---------- from_range constructors ----------
#ifdef __cpp_lib_containers_ranges
  template <std::ranges::forward_range Range>
  requires (!std::derived_from<std::remove_cvref_t<Range>, hash_table>) &&
           std::constructible_from<value_type, std::ranges::range_reference_t<Range>> &&
           (storage_policy == ptr_hash_storage_policy::heap)
  constexpr hash_table(std::from_range_t, Range &&range, hasher const &hasher_arg, key_equal const &eq, allocator_type const &alloc)
      : hash_table { std::ranges::begin(range), std::ranges::end(range), hasher_arg, eq, alloc }
  {
  }

  template <std::ranges::forward_range Range>
  requires (!std::derived_from<std::remove_cvref_t<Range>, hash_table>) &&
           std::constructible_from<value_type, std::ranges::range_reference_t<Range>> &&
           (storage_policy == ptr_hash_storage_policy::heap)
  constexpr hash_table(std::from_range_t, Range &&range, hasher const &hasher_arg, allocator_type const &alloc)
      : hash_table { std::ranges::begin(range), std::ranges::end(range), hasher_arg, key_equal {}, alloc }
  {
  }

  template <std::ranges::forward_range Range>
  requires (!std::derived_from<std::remove_cvref_t<Range>, hash_table>) &&
           std::constructible_from<value_type, std::ranges::range_reference_t<Range>> &&
           (storage_policy == ptr_hash_storage_policy::heap)
  constexpr hash_table(std::from_range_t, Range &&range, allocator_type const &alloc)
      : hash_table { std::ranges::begin(range), std::ranges::end(range), hasher {}, key_equal {}, alloc }
  {
  }

  template <std::ranges::forward_range Range>
  requires (!std::derived_from<std::remove_cvref_t<Range>, hash_table>) &&
           std::constructible_from<value_type, std::ranges::range_reference_t<Range>>
  constexpr hash_table(std::from_range_t, Range &&range, hasher const &hasher_arg, key_equal const &eq)
      : hash_table { std::ranges::begin(range), std::ranges::end(range), hasher_arg, eq }
  {
  }

  template <std::ranges::forward_range Range>
  requires (!std::derived_from<std::remove_cvref_t<Range>, hash_table>) &&
           std::constructible_from<value_type, std::ranges::range_reference_t<Range>>
  constexpr hash_table(std::from_range_t, Range &&range, hasher const &hasher_arg)
      : hash_table { std::ranges::begin(range), std::ranges::end(range), hasher_arg, key_equal {} }
  {
  }

  template <std::ranges::forward_range Range>
  requires (!std::derived_from<std::remove_cvref_t<Range>, hash_table>) &&
           std::constructible_from<value_type, std::ranges::range_reference_t<Range>>
  constexpr hash_table(std::from_range_t, Range &&range)
      : hash_table { std::ranges::begin(range), std::ranges::end(range), hasher {}, key_equal {} }
  {
  }
#endif // __cpp_lib_containers_ranges

public:
  // ---------- observers ----------
  [[nodiscard]] constexpr size_type bucket_count() const noexcept { return ptr_hash_storage_.bucket_count(); }
  /// The bucket array is fixed at construction, so the current count is also the maximum.
  [[nodiscard]] constexpr size_type max_bucket_count() const noexcept { return ptr_hash_storage_.bucket_count(); }
  [[nodiscard]] constexpr bool empty() const noexcept { return size() == 0; }
  [[nodiscard]] constexpr size_type size() const noexcept { return ptr_hash_storage_.size(); }
  [[nodiscard]] constexpr size_type max_size() const noexcept
  {
    if constexpr (storage_policy == ptr_hash_storage_policy::heap) {
      return (std::numeric_limits<size_type>::max)();
    } else {
      return static_cast<size_type>(options_type::storage_traits::max_key_count);
    }
  }

  [[nodiscard]] constexpr const_iterator cbegin() const noexcept { return begin(); }
  [[nodiscard]] constexpr const_iterator cend() const noexcept { return end(); }
  [[nodiscard]] constexpr const_iterator begin() const noexcept { return static_cast<const_iterator>(value_storage_.data()); }
  [[nodiscard]] constexpr const_iterator end() const noexcept
  {
    const_iterator const first = begin();
    return first == nullptr ? first : first + static_cast<difference_type>(size());
  }

  [[nodiscard]] constexpr hasher hash_function() const noexcept { return ptr_hash_storage_.hash_function(); }
  [[nodiscard]] constexpr key_equal key_eq() const noexcept { return key_eq_; }
  [[nodiscard]] constexpr allocator_type get_allocator() const noexcept
  requires (storage_policy == ptr_hash_storage_policy::heap)
  {
    return ptr_hash_storage_.get_allocator();
  }

private:
  template <class K>
  [[nodiscard]] constexpr const_iterator find_impl_(K const &key) const
  {
    size_type const index = ptr_hash_storage_.index(key);
    if (index == ptr_hash_storage_type::npos) return end();
    assert(index < size());
    return key_eq_(Traits::key_of(value_storage_[index]), key) ? begin() + static_cast<difference_type>(index) : end();
  }

  template <class K>
  [[nodiscard]] constexpr bool contains_impl_(K const &key) const
  {
    size_type const index = ptr_hash_storage_.index(key);
    if (index == ptr_hash_storage_type::npos) return false;
    assert(index < size());
    return key_eq_(Traits::key_of(value_storage_[index]), key);
  }

  template <class K>
  [[nodiscard]] constexpr mapped_type const &at_impl_(K const &key) const
  {
    if (empty()) [[unlikely]] {
      throw std::out_of_range("minimal_perfect_hash::at: key not found");
    }
    size_type const index = ptr_hash_storage_.index_unchecked(key);
    if (index == ptr_hash_storage_type::npos || !key_eq_(Traits::key_of(value_storage_[index]), key)) [[unlikely]] {
      throw std::out_of_range("minimal_perfect_hash::at: key not found");
    }
    assert(index < size());
    return Traits::mapped_of(value_storage_[index]);
  }

public:
  // ---------- lookup ----------
  [[nodiscard]] constexpr mapped_type const &at(key_type const &key) const
  {
    return at_impl_(key);
  }

  template <class K>
  requires heterogeneous_key<key_type, hasher, key_equal, iterator, const_iterator, K>
  [[nodiscard]] constexpr mapped_type const &at(K const &key) const
  {
    return at_impl_(key);
  }

  [[nodiscard]] constexpr mapped_type const &at_unchecked(key_type const &key) const
  {
    size_type const index = ptr_hash_storage_.index_unchecked(key);
    assert(index < size());
    assert(key_eq_(Traits::key_of(value_storage_[index]), key));
    return Traits::mapped_of(value_storage_[index]);
  }

  template <class K>
  requires heterogeneous_key<key_type, hasher, key_equal, iterator, const_iterator, K>
  [[nodiscard]] constexpr mapped_type const &at_unchecked(K const &key) const
  {
    size_type const index = ptr_hash_storage_.index_unchecked(key);
    assert(index < size());
    assert(key_eq_(Traits::key_of(value_storage_[index]), key));
    return Traits::mapped_of(value_storage_[index]);
  }

  [[nodiscard]] constexpr const_iterator find(key_type const &key) const { return find_impl_(key); }
  [[nodiscard]] constexpr bool contains(key_type const &key) const { return contains_impl_(key); }
  [[nodiscard]] constexpr size_type count(key_type const &key) const { return contains(key) ? 1U : 0U; }
  [[nodiscard]] constexpr std::pair<const_iterator, const_iterator> equal_range(key_type const &key) const
  {
    const_iterator const it = find(key);
    if (it == end()) return { end(), end() };
    return { it, it + 1 };
  }

  template <class K>
  requires heterogeneous_key<key_type, hasher, key_equal, iterator, const_iterator, K>
  [[nodiscard]] constexpr const_iterator find(K const &key) const
  {
    return find_impl_(key);
  }

  template <class K>
  requires heterogeneous_key<key_type, hasher, key_equal, iterator, const_iterator, K>
  [[nodiscard]] constexpr bool contains(K const &key) const
  {
    return contains_impl_(key);
  }

  template <class K>
  requires heterogeneous_key<key_type, hasher, key_equal, iterator, const_iterator, K>
  [[nodiscard]] constexpr size_type count(K const &key) const
  {
    return contains(key) ? 1U : 0U;
  }

  template <class K>
  requires heterogeneous_key<key_type, hasher, key_equal, iterator, const_iterator, K>
  [[nodiscard]] constexpr std::pair<const_iterator, const_iterator> equal_range(K const &key) const
  {
    const_iterator const it = find(key);
    if (it == end()) return { end(), end() };
    return { it, it + 1 };
  }

private:
  ptr_hash_storage_type ptr_hash_storage_;
  value_storage_type value_storage_; // heap: pointer handle; stack: inline raw buffer
  [[no_unique_address]] key_equal key_eq_ {};
};
} // namespace stdex::minimal_perfect_hash_detail
