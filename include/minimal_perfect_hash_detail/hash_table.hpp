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

#include <cassert>          // assert
#include <concepts>         // std::constructible_from, std::same_as, std::derived_from
#include <cstddef>          // std::size_t, std::ptrdiff_t
#include <cstdint>          // std::uint8_t, std::uint32_t
#include <initializer_list> // std::initializer_list
#include <iterator>         // std::input_iterator, std::sentinel_for, std::iter_reference_t
#include <limits>           // std::numeric_limits
#include <memory>           // std::allocator_traits, std::addressof
#include <ranges>           // std::ranges::forward_range, std::ranges::sized_range,
                            // std::ranges::range_reference_t, std::ranges::begin, std::ranges::end,
                            // std::ranges::size, std::views::transform
#include <stdexcept>        // std::out_of_range
#include <tuple>            // std::tuple_element_t
#include <type_traits>      // std::remove_cvref_t, std::remove_cv_t, std::is_move_assignable_v, std::is_trivially_copyable_v
#include <utility>          // std::move
#include <vector>           // std::vector
#include <version>          // __cpp_lib_containers_ranges

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#  include <intrin.h> // _mm_prefetch
#endif

#include "../inplace_vector.hpp"
#include "../ptrhash.hpp"
#include "../uninitialized_buffer.hpp"
#include "mphmap_traits.hpp"
#include "mphset_traits.hpp"

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

/// Maps the storage policy carried by the option set onto the concrete value
/// container: an allocator-backed @c std::vector for @c heap, and a
/// fixed-capacity @c stdex::inplace_vector for @c stack. This mirrors
/// @c ptr_hash_detail::ptr_hash_storage_selector so the value payload and the
/// index share the same storage mode.
template <::stdex::ptr_hash_storage_policy Policy, class Value, class Allocator, std::size_t Capacity>
struct value_storage_selector;

template <class Value, class Allocator, std::size_t Capacity>
struct value_storage_selector<::stdex::ptr_hash_storage_policy::heap, Value, Allocator, Capacity>
{
  using allocator_type = typename std::allocator_traits<Allocator>::template rebind_alloc<Value>;
  using type           = std::vector<Value, allocator_type>;
};

template <class Value, class Allocator, std::size_t Capacity>
struct value_storage_selector<::stdex::ptr_hash_storage_policy::stack, Value, Allocator, Capacity>
{
  using type = ::stdex::inplace_vector<Value, Capacity>;
};

/// Auxiliary buffers (fingerprints and the permutation scratch) follow the same
/// policy: allocator-backed @c uninitialized_buffer on the heap, inline
/// @c inplace_vector on the stack. Both expose @c size() and @c operator[] over
/// exactly the requested element count; every entry is written before it is read.
template <::stdex::ptr_hash_storage_policy Policy, class T, class Allocator, std::size_t Capacity>
struct buffer_storage_selector;

template <class T, class Allocator, std::size_t Capacity>
struct buffer_storage_selector<::stdex::ptr_hash_storage_policy::heap, T, Allocator, Capacity>
{
  using allocator_type = typename std::allocator_traits<Allocator>::template rebind_alloc<T>;
  using type           = ::stdex::uninitialized_buffer<T, allocator_type>;
};

template <class T, class Allocator, std::size_t Capacity>
struct buffer_storage_selector<::stdex::ptr_hash_storage_policy::stack, T, Allocator, Capacity>
{
  using type = ::stdex::inplace_vector<T, Capacity>;
};

/**
 * @brief Immutable associative table backed by a minimal-perfect hash index.
 *
 * @details The @c ptr_hash index stores no keys, and its @c index()
 * result is only unique over the keys that were used to build it; an arbitrary
 * query still lands on some slot in @c [0, size()). @c storage_ therefore
 * stores the elements in index order, and every lookup verifies the candidate
 * with @c key_equal. A parallel one-byte fingerprint table rejects most misses
 * before @c storage_ is touched; only a fingerprint match pays for the
 * value-line fetch and the equality check. This keeps the final table free of
 * an occupancy bitmap or duplicate key storage.
 *
 * Only @c ptr_hash_scheme::minimal_perfect is supported: it is the scheme that
 * maps every key onto a distinct index in @c [0, key_count), so a value
 * container of exactly @c key_count elements is sufficient and all its slots are
 * populated. The @c perfect scheme leaves holes in @c [0, capacity) and would
 * need a separate slot-to-value mapping.
 *
 * Lookups also accept heterogeneous key types when both @c Hasher and
 * @c KeyEqual expose @c is_transparent. The alternate key is hashed through the
 * same seeded PtrHash path, so the resulting index stays consistent with the
 * index built from @c key_type.
 */
template <class Traits, class Hasher, class KeyEqual, auto Options>
requires ::stdex::ptr_hash_detail::is_ptr_hash_options_v<std::remove_cvref_t<decltype(Options)>>
class hash_table
{
public:
  using options_type = std::remove_cvref_t<decltype(Options)>;
  static constexpr options_type options_value { Options };
  /// Storage mode requested by the option set; selects the value container and auxiliary buffers.
  static constexpr ::stdex::ptr_hash_storage_policy storage_policy { options_type::storage_traits::storage_policy };
  /// The option set owns the allocator; rebinding it to @c value_type gives the
  /// container's value allocator. Stack storage is inline and never allocates,
  /// so there the rebound allocator only backs @c get_allocator().
  using allocator_type = typename std::allocator_traits<
      typename options_type::storage_traits::allocator_type>::template rebind_alloc<typename Traits::value_type>;

  using key_type        = typename Traits::key_type;
  using mapped_type     = typename Traits::mapped_type;
  using value_type      = typename Traits::value_type;
  using hasher          = Hasher;
  using key_equal       = KeyEqual;
  using size_type       = std::size_t;
  using difference_type = std::ptrdiff_t;
  using const_reference = value_type const &;
  using const_pointer   = typename std::allocator_traits<allocator_type>::const_pointer;
  using reference       = const_reference;
  using pointer         = const_pointer;

private:
  template <class T>
  using rebind_alloc_t = typename std::allocator_traits<allocator_type>::template rebind_alloc<T>;

  using fingerprint_type = std::uint8_t;
  using order_index_type = std::uint32_t;
  
  using ptr_hash_storage = ptr_hash<key_type, Hasher, Options>;
  using value_storage    = typename value_storage_selector<
      storage_policy,
      value_type,
      allocator_type,
      options_type::storage_traits::max_key_count>::type;
  using fingerprint_storage = typename buffer_storage_selector<
      storage_policy,
      fingerprint_type,
      allocator_type,
      options_type::storage_traits::max_key_count>::type;
  using order_storage = typename buffer_storage_selector<
      storage_policy,
      order_index_type,
      allocator_type,
      options_type::storage_traits::max_key_count>::type;

  /// The cycle path avoids the second value buffer. Probe data shows that
  /// sequential out-of-place writes win for small values, while the cycle path
  /// wins once the value is large or its nontrivial copy/move cost dominates.
  static constexpr size_type cycle_reorder_trivial_threshold    = 32;
  static constexpr size_type cycle_reorder_nontrivial_threshold = 16;
  static constexpr bool use_cycle_reorder =
      std::is_move_assignable_v<value_type> &&
      (sizeof(value_type) >= cycle_reorder_trivial_threshold ||
       (!std::is_trivially_copyable_v<value_type> && sizeof(value_type) >= cycle_reorder_nontrivial_threshold));

public:
  using const_iterator = typename value_storage::const_iterator;
  using iterator       = const_iterator;

  static_assert(options_value.hash_scheme == ptr_hash_scheme::minimal_perfect,
                "minimal_perfect_hash containers require ptr_hash_scheme::minimal_perfect");

  constexpr hash_table() = default;

  // ---------- iterator-range constructors ----------
  template <std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
  requires std::constructible_from<value_type, std::iter_reference_t<Iterator>>
  constexpr hash_table(Iterator first, Sentinel last, hasher const &hash, key_equal const &equal, allocator_type const &alloc)
      : storage_ { make_storage_(std::ranges::subrange { std::move(first), std::move(last) }, alloc) }
      , ptr_hash_storage_ { make_ptr_hash_storage_(storage_, hash, alloc) }
      , fingerprints_ { make_buffer_<fingerprint_storage>(storage_.size()) }
      , key_eq_ { equal }
  {
    reorder_storage_and_fingerprints_();
  }

  template <std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
  constexpr hash_table(Iterator first, Sentinel last)
      : hash_table { std::move(first), std::move(last), hasher {}, key_equal {}, allocator_type {} }
  {
  }

  template <std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
  constexpr hash_table(Iterator first, Sentinel last, hasher const &hash)
      : hash_table { std::move(first), std::move(last), hash, key_equal {}, allocator_type {} }
  {
  }

  template <std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
  constexpr hash_table(Iterator first, Sentinel last, hasher const &hash, key_equal const &equal)
      : hash_table { std::move(first), std::move(last), hash, equal, allocator_type {} }
  {
  }

  template <std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
  constexpr hash_table(Iterator first, Sentinel last, allocator_type const &alloc)
      : hash_table { std::move(first), std::move(last), hasher {}, key_equal {}, alloc }
  {
  }

  template <std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
  constexpr hash_table(Iterator first, Sentinel last, hasher const &hash, allocator_type const &alloc)
      : hash_table { std::move(first), std::move(last), hash, key_equal {}, alloc }
  {
  }

  // ---------- initializer_list constructors ----------
  constexpr hash_table(std::initializer_list<value_type> ilist, hasher const &hash, key_equal const &equal, allocator_type const &alloc)
      : hash_table { ilist.begin(), ilist.end(), hash, equal, alloc }
  {
  }

  constexpr hash_table(std::initializer_list<value_type> ilist)
      : hash_table { ilist, hasher {}, key_equal {}, allocator_type {} }
  {
  }

  constexpr hash_table(std::initializer_list<value_type> ilist, hasher const &hash)
      : hash_table { ilist, hash, key_equal {}, allocator_type {} }
  {
  }

  constexpr hash_table(std::initializer_list<value_type> ilist, hasher const &hash, key_equal const &equal)
      : hash_table { ilist, hash, equal, allocator_type {} }
  {
  }

  constexpr hash_table(std::initializer_list<value_type> ilist, allocator_type const &alloc)
      : hash_table { ilist, hasher {}, key_equal {}, alloc }
  {
  }

  constexpr hash_table(std::initializer_list<value_type> ilist, hasher const &hash, allocator_type const &alloc)
      : hash_table { ilist, hash, key_equal {}, alloc }
  {
  }

  // ---------- from_range constructors ----------
#ifdef __cpp_lib_containers_ranges
  template <std::ranges::input_range Range>
  requires (!std::derived_from<std::remove_cvref_t<Range>, hash_table>) &&
               std::constructible_from<value_type, std::ranges::range_reference_t<Range>>
  constexpr hash_table(std::from_range_t, Range &&range, hasher const &hash, key_equal const &equal, allocator_type const &alloc)
      : hash_table { std::ranges::begin(range), std::ranges::end(range), hash, equal, alloc }
  {
  }

  template <std::ranges::input_range Range>
  requires (!std::derived_from<std::remove_cvref_t<Range>, hash_table>) &&
               std::constructible_from<value_type, std::ranges::range_reference_t<Range>>
  constexpr hash_table(std::from_range_t, Range &&range)
      : hash_table { std::ranges::begin(range), std::ranges::end(range), hasher {}, key_equal {}, allocator_type {} }
  {
  }

  template <std::ranges::input_range Range>
  requires (!std::derived_from<std::remove_cvref_t<Range>, hash_table>) &&
               std::constructible_from<value_type, std::ranges::range_reference_t<Range>>
  constexpr hash_table(std::from_range_t, Range &&range, hasher const &hash)
      : hash_table { std::ranges::begin(range), std::ranges::end(range), hash, key_equal {}, allocator_type {} }
  {
  }

  template <std::ranges::input_range Range>
  requires (!std::derived_from<std::remove_cvref_t<Range>, hash_table>) &&
               std::constructible_from<value_type, std::ranges::range_reference_t<Range>>
  constexpr hash_table(std::from_range_t, Range &&range, hasher const &hash, key_equal const &equal)
      : hash_table { std::ranges::begin(range), std::ranges::end(range), hash, equal, allocator_type {} }
  {
  }

  template <std::ranges::input_range Range>
  requires (!std::derived_from<std::remove_cvref_t<Range>, hash_table>) &&
               std::constructible_from<value_type, std::ranges::range_reference_t<Range>>
  constexpr hash_table(std::from_range_t, Range &&range, allocator_type const &alloc)
      : hash_table { std::ranges::begin(range), std::ranges::end(range), hasher {}, key_equal {}, alloc }
  {
  }

  template <std::ranges::input_range Range>
  requires (!std::derived_from<std::remove_cvref_t<Range>, hash_table>) &&
               std::constructible_from<value_type, std::ranges::range_reference_t<Range>>
  constexpr hash_table(std::from_range_t, Range &&range, hasher const &hash, allocator_type const &alloc)
      : hash_table { std::ranges::begin(range), std::ranges::end(range), hash, key_equal {}, alloc }
  {
  }
#endif // __cpp_lib_containers_ranges

  [[nodiscard]] constexpr bool empty() const noexcept { return storage_.empty(); }
  [[nodiscard]] constexpr size_type size() const noexcept { return storage_.size(); }
  [[nodiscard]] constexpr size_type max_size() const noexcept { return storage_.max_size(); }
  [[nodiscard]] constexpr size_type bucket_count() const noexcept { return ptr_hash_storage_.bucket_count(); }
  /// The bucket array is fixed at construction, so the current count is also the maximum.
  [[nodiscard]] constexpr size_type max_bucket_count() const noexcept { return ptr_hash_storage_.bucket_count(); }
  [[nodiscard]] constexpr const_iterator begin() const noexcept { return storage_.begin(); }
  [[nodiscard]] constexpr const_iterator end() const noexcept { return storage_.end(); }
  [[nodiscard]] constexpr const_iterator cbegin() const noexcept { return storage_.cbegin(); }
  [[nodiscard]] constexpr const_iterator cend() const noexcept { return storage_.cend(); }

  [[nodiscard]] constexpr const_iterator find(key_type const &key) const { return find_impl_(key); }
  template <class K>
  requires heterogeneous_key<key_type, hasher, key_equal, iterator, const_iterator, K>
  [[nodiscard]] constexpr const_iterator find(K const &key) const { return find_impl_(key); }

  [[nodiscard]] constexpr bool contains(key_type const &key) const { return contains_impl_(key); }
  template <class K>
  requires heterogeneous_key<key_type, hasher, key_equal, iterator, const_iterator, K>
  [[nodiscard]] constexpr bool contains(K const &key) const { return contains_impl_(key); }

  [[nodiscard]] constexpr size_type count(key_type const &key) const { return contains(key) ? 1U : 0U; }
  template <class K>
  requires heterogeneous_key<key_type, hasher, key_equal, iterator, const_iterator, K>
  [[nodiscard]] constexpr size_type count(K const &key) const { return contains(key) ? 1U : 0U; }

  [[nodiscard]] constexpr std::pair<const_iterator, const_iterator> equal_range(key_type const &key) const
  {
    const_iterator const it = find(key);
    if (it == end()) return { end(), end() };
    return { it, it + 1 };
  }

  template <class K>
  requires heterogeneous_key<key_type, hasher, key_equal, iterator, const_iterator, K>
  [[nodiscard]] constexpr std::pair<const_iterator, const_iterator> equal_range(K const &key) const
  {
    const_iterator const it = find(key);
    if (it == end()) return { end(), end() };
    return { it, it + 1 };
  }

  [[nodiscard]] constexpr mapped_type const &at(key_type const &key) const { return at_impl_(key); }
  template <class K>
  requires heterogeneous_key<key_type, hasher, key_equal, iterator, const_iterator, K>
  [[nodiscard]] constexpr mapped_type const &at(K const &key) const { return at_impl_(key); }

  [[nodiscard]] constexpr mapped_type const &at_unchecked(key_type const &key) const
  {
    size_type const index = ptr_hash_storage_.index_unchecked(key);
    assert(index < storage_.size());
    assert(key_eq_(Traits::key_of(storage_[index]), key));
    return Traits::mapped_of(storage_[index]);
  }

  template <class K>
  requires heterogeneous_key<key_type, hasher, key_equal, iterator, const_iterator, K>
  [[nodiscard]] constexpr mapped_type const &at_unchecked(K const &key) const
  {
    size_type const index = ptr_hash_storage_.index_unchecked(key);
    assert(index < storage_.size());
    assert(key_eq_(Traits::key_of(storage_[index]), key));
    return Traits::mapped_of(storage_[index]);
  }

  [[nodiscard]] constexpr hasher hash_function() const noexcept { return ptr_hash_storage_.hash_function(); }
  [[nodiscard]] constexpr key_equal key_eq() const noexcept { return key_eq_; }
  /// Heap storage keeps the allocator inside its value vector; stack storage is
  /// inline and its allocator is always the stateless @c std::allocator, so no
  /// separate allocator copy is stored for it.
  [[nodiscard]] constexpr allocator_type get_allocator() const noexcept
  {
    if constexpr (storage_policy == ::stdex::ptr_hash_storage_policy::heap) {
      return storage_.get_allocator();
    } else {
      return allocator_type {};
    }
  }

private:
  template <class K>
  [[nodiscard]] constexpr const_iterator find_impl_(K const &key) const
  {
    if (empty()) [[unlikely]]
      return end();

    std::uint64_t const hash = ptr_hash_storage_.hash_value(key);
    size_type const index    = ptr_hash_storage_.index_from_hash(hash);
    assert(index < storage_.size());
    if (fingerprints_[index] != fingerprint_of_(hash))
      return end();

#if defined(__GNUC__) || defined(__clang__)
    void const *const address = std::addressof(storage_[index]);
    __builtin_prefetch(address, 0, 3);
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    void const *const address = std::addressof(storage_[index]);
    _mm_prefetch(static_cast<char const *>(address), _MM_HINT_T0);
#endif

    return key_eq_(Traits::key_of(storage_[index]), key) ? storage_.begin() + static_cast<difference_type>(index)
                                                        : storage_.end();
  }

  template <class K>
  [[nodiscard]] constexpr bool contains_impl_(K const &key) const
  {
    if (empty()) [[unlikely]]
      return false;

    std::uint64_t const hash = ptr_hash_storage_.hash_value(key);
    size_type const index    = ptr_hash_storage_.index_from_hash(hash);
    assert(index < storage_.size());
    return fingerprints_[index] == fingerprint_of_(hash) && key_eq_(Traits::key_of(storage_[index]), key);
  }

  template <class K>
  [[nodiscard]] constexpr mapped_type const &at_impl_(K const &key) const
  {
    if (empty()) [[unlikely]] {
      throw std::out_of_range("minimal_perfect_hash::at: key not found");
    }

    std::uint64_t const hash = ptr_hash_storage_.hash_value(key);
    size_type const index    = ptr_hash_storage_.index_from_hash(hash);
    assert(index < storage_.size());
    if (fingerprints_[index] != fingerprint_of_(hash) || !key_eq_(Traits::key_of(storage_[index]), key)) [[unlikely]] {
      throw std::out_of_range("minimal_perfect_hash::at: key not found");
    }
    return Traits::mapped_of(storage_[index]);
  }

  template <std::ranges::input_range Range>
  [[nodiscard]] static constexpr value_storage make_storage_(Range &&values, allocator_type const &alloc)
  {
    if constexpr (storage_policy == ::stdex::ptr_hash_storage_policy::heap) {
      value_storage result { rebind_alloc_t<value_type> { alloc } };
      if constexpr (std::ranges::sized_range<Range>) {
        result.reserve(static_cast<size_type>(std::ranges::size(values)));
      }
      for (auto &&value : values) result.emplace_back(std::forward<decltype(value)>(value));
      return result;
    } else {
      (void) alloc;
      value_storage result;
      if constexpr (std::ranges::sized_range<Range>) {
        result.reserve(static_cast<size_type>(std::ranges::size(values)));
      }
      for (auto &&value : values) result.emplace_back(std::forward<decltype(value)>(value));
      return result;
    }
  }

  [[nodiscard]] static constexpr ptr_hash_storage make_ptr_hash_storage_(value_storage const &values, hasher const &hash, allocator_type const &alloc)
  {
    auto keys = values | std::views::transform([](value_type const &value) -> key_type const & { return Traits::key_of(value); });
    if constexpr (storage_policy == ::stdex::ptr_hash_storage_policy::heap) {
      return ptr_hash_storage { std::ranges::begin(keys), std::ranges::end(keys), hash, alloc };
    } else {
      return ptr_hash_storage { std::ranges::begin(keys), std::ranges::end(keys), hash };
    }
  }

  template <class Buffer>
  [[nodiscard]] constexpr Buffer make_buffer_(size_type const count) const
  {
    if constexpr (storage_policy == ::stdex::ptr_hash_storage_policy::heap) {
      return Buffer { count, rebind_alloc_t<typename Buffer::value_type> { storage_.get_allocator() } };
    } else {
      return Buffer(count);
    }
  }

  [[nodiscard]] constexpr value_storage make_empty_storage_() const
  {
    if constexpr (storage_policy == ::stdex::ptr_hash_storage_policy::heap) {
      return value_storage { rebind_alloc_t<value_type> { storage_.get_allocator() } };
    } else {
      return value_storage {};
    }
  }

  [[nodiscard]] static constexpr fingerprint_type fingerprint_of_(std::uint64_t const hash) noexcept
  {
    return static_cast<fingerprint_type>(hash >> 56U);
  }

  /**
   * @brief Permutes the input order into minimal-perfect-index order and builds
   *        the fingerprint table in the same pass.
   *
   * @details The index is a bijection from the key set onto [0, size()), so every
   * destination slot is written exactly once; no occupancy check is needed during
   * the permutation. The key hash computed for each input element is also the
   * fingerprint source, so hashing and `key_of` are each done only once.
   *
   * The cycle variant avoids the second value buffer used by the fallback when
   * the value is large enough to make that buffer expensive; it requires
   * assignable values and marks consumed permutation entries with `count`,
   * which is outside the valid index range. Small values keep the sequential
   * out-of-place write used by the fallback.
   */
  constexpr void reorder_storage_and_fingerprints_()
  {
    size_type const count = storage_.size();
    assert(fingerprints_.size() == count);
    if (count == 0) return;
    assert(count <= static_cast<size_type>((std::numeric_limits<order_index_type>::max)()));

    order_storage order = make_buffer_<order_storage>(count);
    for (size_type input_index = 0; input_index < count; ++input_index) {
      key_type const &key          = Traits::key_of(storage_[input_index]);
      std::uint64_t const hash     = ptr_hash_storage_.hash_value(key);
      size_type const output_index = ptr_hash_storage_.index_from_hash(hash);
      assert(output_index < count);
      order[output_index]         = static_cast<order_index_type>(input_index);
      fingerprints_[output_index] = fingerprint_of_(hash);
    }

    if constexpr (use_cycle_reorder) {
      order_index_type const visited = static_cast<order_index_type>(count);
      for (size_type start = 0; start < count; ++start) {
        if (order[start] == visited) continue;
        if (order[start] == static_cast<order_index_type>(start)) {
          order[start] = visited;
          continue;
        }

        value_type pending    = std::move(storage_[start]);
        size_type destination = start;
        size_type source      = static_cast<size_type>(order[start]);
        while (source != start) {
          storage_[destination] = std::move(storage_[source]);
          order[destination]   = visited;
          destination          = source;
          source               = static_cast<size_type>(order[source]);
        }
        storage_[destination] = std::move(pending);
        order[destination]   = visited;
      }
    } else {
      value_storage ordered = make_empty_storage_();
      ordered.reserve(count);
      for (order_index_type input_index : order) {
        ordered.emplace_back(std::move(storage_[input_index]));
      }
      storage_.swap(ordered);
    }
  }

  value_storage storage_;
  ptr_hash_storage ptr_hash_storage_;
  fingerprint_storage fingerprints_;
  [[no_unique_address]] key_equal key_eq_ {};
};
} // namespace stdex::minimal_perfect_hash_detail
