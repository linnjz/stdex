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

#include <algorithm>   // std::copy_n
#include <bit>         // std::countr_zero
#include <cassert>     // assert
#include <concepts>    // std::convertible_to, std::constructible_from
#include <cstddef>     // std::size_t, size_t
#include <cstring>     // std::memcpy
#include <iterator>    // std::next
#include <limits>      // std::numeric_limits
#include <memory>      // std::addressof
#include <ranges>      // std::ranges::input_range, std::ranges::range_reference_t, std::ranges::begin, std::ranges::end
#include <type_traits> // std::conditional_t, std::is_same_v,
                       // std::is_convertible_v,
                       // std::is_trivially_copyable_v,
                       // std::is_standard_layout_v
#include <utility>     // std::pair, std::move, std::forward, std::swap

#include "group15.hpp"
#include "inline_storage.hpp"
#include "iterator.hpp"
#include "try_key_extraction.hpp"

namespace stdex::unordered_flat_detail
{
template <class R, class T>
concept container_compatible_range = std::ranges::input_range<R> && std::convertible_to<std::ranges::range_reference_t<R>, T>;

template <class T>
inline constexpr bool has_is_transparent = requires { typename T::is_transparent; };

template <class Hasher, class KeyEqual>
concept transparent_hash_equal = has_is_transparent<Hasher> && has_is_transparent<KeyEqual>;

template <class KeyType, class Iter, class ConstIter>
concept not_iterator_convertible = !std::is_convertible_v<KeyType, Iter> && !std::is_convertible_v<KeyType, ConstIter>;

template <class KeyType, class Hasher, class KeyEqual, class Iter, class ConstIter>
concept heterogeneous_key = transparent_hash_equal<Hasher, KeyEqual> && not_iterator_convertible<KeyType, Iter, ConstIter>;

template <class Traits>
class hash_table
{
public:
  using hasher           = typename Traits::hasher;
  using key_equal        = typename Traits::key_equal;
  using allocator_type   = typename Traits::allocator_type;
  using allocator_traits = typename Traits::allocator_traits;

  using key_type           = typename Traits::key_type;
  using mutable_value_type = typename Traits::mutable_value_type;
  using value_type         = typename Traits::value_type;
  using size_type          = typename Traits::size_type;
  using difference_type    = typename Traits::difference_type;
  using pointer            = typename Traits::pointer;
  using const_pointer      = typename Traits::const_pointer;
  using raw_pointer        = typename Traits::raw_pointer;
  using raw_const_pointer  = typename Traits::raw_const_pointer;
  using reference          = typename Traits::reference;
  using const_reference    = typename Traits::const_reference;

  using iterator = std::conditional_t<
      std::is_same_v<key_type, value_type>,
      hash_table_iterator<hash_table<Traits>, true>,
      hash_table_iterator<hash_table<Traits>, false>>;
  using const_iterator = iterator;
  using lazy_iterator  = hash_table_lazy_iterator<iterator>;

  template <class, bool>
  friend class hash_table_iterator;

protected:
  using group_type          = group15;
  using raw_group_pointer   = group_type *;
  using inline_storage_type = inline_storage<value_type, group_type, Traits::inline_size(), allocator_type>;
  // clang-format off
  static constexpr size_type group_size = group_type::capacity();
  static constexpr size_type min_group_count = inline_storage_type::min_group_count;
  static constexpr size_t max_load_factor_ratio_numerator   = 8;
  static constexpr size_t max_load_factor_ratio_denominator = 7;
  static constexpr float max_load_factor_ratio = static_cast<float>(max_load_factor_ratio_denominator) /
                                                 static_cast<float>(max_load_factor_ratio_numerator);
  // clang-format on

  /// Boost's transfer choice (unchecked_rehash, core.hpp:2336): move when the
  /// movable form is nothrow-movable, or when it differs from the stored value
  /// (a map moves pair<const K, T>, i.e. copies the const key and moves the
  /// mapped value), or when the value cannot be copied at all; otherwise copy,
  /// so a throwing copy leaves the sources intact and yields the strong
  /// guarantee.
  static constexpr bool transfer_by_move =
      std::is_nothrow_move_constructible_v<mutable_value_type> ||
      !std::is_same_v<mutable_value_type, value_type> ||
      !std::is_copy_constructible_v<value_type>;

  static constexpr void pocca_(allocator_type &left, allocator_type const &right) noexcept
  {
    if constexpr (allocator_traits::propagate_on_container_copy_assignment::value) {
      left = right;
    }
  }

  static constexpr void pocma_(allocator_type &left, allocator_type &right) noexcept
  {
    if constexpr (allocator_traits::propagate_on_container_move_assignment::value) {
      left = std::move(right);
    }
  }

  static constexpr void pocs_(allocator_type &left, allocator_type &right) noexcept
  {
    if constexpr (allocator_traits::propagate_on_container_swap::value) {
      std::swap(left, right);
    } else {
      assert(left == right && "containers incompatible for swap");
    }
  }

  static constexpr bool allocators_equal_(allocator_type &left, allocator_type &right) noexcept
  {
    if constexpr (allocator_traits::is_always_equal::value) {
      return true;
    } else {
      return left == right;
    }
  }

  /// Steals other's storage and counters and leaves other a valid empty table.
  /// Valid only where the allocators permit sharing the arrays (equal or
  /// propagating); moving a storage disposes whatever it replaced, so *this is
  /// released here as well.
  constexpr void adopt_moved_(hash_table &other) noexcept
  {
    is_             = std::move(other.is_);
    size_           = other.size_;
    max_load_       = other.max_load_;
    other.size_     = 0;
    other.max_load_ = floor_max_load_(0);
  }

  ///< Given the capacity, figure out how much it can hold.
  static constexpr size_type floor_max_load_(size_type capacity) noexcept
  {
    if (capacity <= 2 * group_size - 1) {
      return capacity; // <= 29, 100%
    } else {
      return static_cast<size_type>(max_load_factor_ratio * static_cast<float>(capacity)); // > 29, 87.5%
    }
  }

  ///< Given the number of elements, calculate the actual required capacity based on the max load factor
  static constexpr size_type ceil_capacity_for_load_(size_type elem_count) noexcept
  {
    return (elem_count * max_load_factor_ratio_numerator + max_load_factor_ratio_denominator - 1) / max_load_factor_ratio_denominator;
  }

  ///< The actual number of groups, aligned to a power of two, is calculated based on
  ///< the capacity calculated from the load factor and serves as the actual capacity.
  constexpr size_type group_aligned_capacity_(size_type capacity) noexcept
  {
    auto const shift       = is_.group_index_shift(capacity);
    auto const group_count = is_.group_count(shift);

    return group_count * group_size - 1;
  }

public:
  constexpr hash_table(hasher const &hash_arg, key_equal const &key_eq, allocator_type const &alloc)
      : is_ {}
      , size_ {}
      , max_load_ { floor_max_load_(0) }
      , hasher_ { hash_arg }
      , key_eq_ { key_eq }
      , alloc_ { alloc }
  {
  }

protected:
  // clang-format off
  struct triangular_probing
  {
    constexpr triangular_probing(size_t pos) : pos_ { pos } , step_ { } {}
    constexpr size_t position() const { return pos_; }
    constexpr bool next(size_t group_count_mask)
    {
      ++step_;
      pos_ = (pos_ + step_) & group_count_mask;
      return step_ <= group_count_mask;
    }

  private:
    size_t pos_, step_;
  };
  using prober_type = triangular_probing;
  // clang-format on

  template <typename... Args>
  requires std::constructible_from<value_type, Args...> &&
           requires (allocator_type &a, raw_pointer p, Args &&...args) {
             allocator_traits::construct(a, p, std::forward<Args>(args)...);
           }
  constexpr void construct_element_(raw_pointer element_ptr, Args &&...args)
  {
    allocator_traits::construct(alloc_, element_ptr, std::forward<Args>(args)...);
  }

  template <class... Args>
  constexpr iterator emplace_at_available_slot_(
      inline_storage_type const &storage, size_type hash, size_type group_index, Args &&...args)
  {
    for (prober_type prober(group_index);; prober.next(storage.group_count_mask_)) {
      auto pos        = prober.position();
      auto curr_group = storage.groups_address() + pos;
      if (auto avail_mask = curr_group->available_mask()) [[likely]] {
        unsigned slot_index = std::countr_zero(static_cast<unsigned>(avail_mask));
        auto element_ptr    = storage.elements_address() + pos * group_size + slot_index;
        construct_element_(element_ptr, std::forward<Args>(args)...);
        curr_group->set_fingerprint_hash(slot_index, hash);
        return iterator { element_ptr, curr_group, slot_index };
      } else {
        curr_group->set_overflow_mark(hash);
      }
    }
  }

  /// Only occupied slots hold live objects; a bulk copy would read the
  /// uninitialized slots, which is UB at runtime and impossible in consteval.
  /// Each slot is marked occupied as soon as its object exists, so the storage's
  /// own destructor can undo a partial copy from its occupancy bits.  The mark is
  /// transient: copy_from_ replaces the whole group array from other right after
  /// this returns, before the storage is ever read, so no made-up fingerprint
  /// survives into the committed table.
  constexpr void copy_occupied_into_(hash_table const &other, inline_storage_type &storage)
  {
    inline_storage_type::for_each_occupied(other.is_, [&](raw_pointer occupied_ptr, raw_group_pointer src_group, unsigned slot_index) {
      construct_element_(storage.elements_address() + (occupied_ptr - other.is_.elements_address()), *occupied_ptr);
      /// set_occupied_mask is occupancy only; the following std::copy_n of other's
      /// groups in copy_from_ overwrites this slot with the real fingerprint.
      group_type::attorney::set_occupied_mask(*(storage.groups_address() + (src_group - other.is_.groups_address())), slot_index);
      return true;
    });
  }

  constexpr void copy_from_(hash_table const &other)
  {
    auto storage_alloc = static_cast<typename inline_storage_type::allocator_type>(alloc_);
    auto storage       = inline_storage_type::create(storage_alloc, ceil_capacity_for_load_(other.size_));

    bool const is_group_count_equal = storage.group_count_mask_ == other.is_.group_count_mask_;
    if (is_group_count_equal) {
      if constexpr (std::is_trivially_copyable_v<value_type> && std::is_standard_layout_v<value_type>) {
        if consteval {
          copy_occupied_into_(other, storage);
        } else {
          std::memcpy(
              reinterpret_cast<unsigned char *>(storage.elements_address()),
              reinterpret_cast<unsigned char *>(other.is_.elements_address()),
              other.capacity() * sizeof(value_type));
        }
      } else {
        copy_occupied_into_(other, storage);
      }
      std::copy_n(other.is_.groups_address(), other.is_.group_count(), storage.groups_address());
#ifndef NDEBUG
      /// The group copy must have replaced every transient occupancy mark with
      /// real data; a divergence here means a slot kept a made-up fingerprint
      /// and would silently make that element unfindable.
      for (size_t i = 0; i < storage.group_count(); ++i)
        assert(group_type::attorney::equals(storage.groups_address()[i], other.is_.groups_address()[i]));
#endif
    } else {
      transfer_occupied_<false>(other.is_, storage);
    }

    /// If anything above threw, storage's own destructor has already destroyed
    /// the partial elements and freed its memory, and is_ is untouched.  Only
    /// now is the result committed.
    is_       = std::move(storage);
    size_     = other.size_;
    max_load_ = is_group_count_equal ? other.max_load_ : floor_max_load_(capacity()); ///< is_ -> is_.group_count_mask_ -> capacity -> max_load_
  }

public:
  template <class AnyAllocator>
  constexpr hash_table(hash_table const &other, AnyAllocator const &alloc)
      : is_ {}
      , size_ {}
      , max_load_ {}
      , hasher_ { other.hasher_ }
      , key_eq_ { other.key_eq_ }
      , alloc_ { static_cast<allocator_type>(alloc) }
  {
    if (other.empty()) [[unlikely]]
      return;
    copy_from_(other);
  }

  constexpr hash_table(hash_table const &other)
      : hash_table(other, allocator_traits::select_on_container_copy_construction(other.alloc_))
  {
  }

  constexpr hash_table &operator= (hash_table const &other)
  {
    if (this == std::addressof(other)) [[unlikely]]
      return *this;

    /// Copy the policies first; if that throws, *this is untouched.
    auto hasher = other.hasher_;
    auto key_eq = other.key_eq_;

    if constexpr (allocator_traits::propagate_on_container_copy_assignment::value) {
      if (!(alloc_ == other.alloc_)) {
        /// The current arrays belong to the old allocator, so they must be
        /// released before the new allocator is adopted.
        is_.dispose();
        size_     = 0;
        max_load_ = floor_max_load_(0);
      }
      alloc_ = other.alloc_;
    }

    using std::swap;
    swap(hasher, hasher_);
    swap(key_eq, key_eq_);

    /// Destroy this's elements but keep the arrays, then grow only when they
    /// cannot hold other (boost's clear + noshrink_reserve).  Copying in place
    /// favors allocator reuse over the old strong guarantee, matching boost: a
    /// throwing copy leaves a valid, partially filled table.
    is_.clear();
    size_     = 0;
    max_load_ = floor_max_load_(capacity());

    if (other.empty()) return *this;

    noshrink_reserve_(other.size_);

    if (is_.group_count_mask_ == other.is_.group_count_mask_) {
      if constexpr (std::is_trivially_copyable_v<value_type> && std::is_standard_layout_v<value_type>) {
        if consteval {
          copy_insert_occupied_(other); ///< memcpy is not a constant expression
        } else {
          std::memcpy(
              reinterpret_cast<unsigned char *>(is_.elements_address()),
              reinterpret_cast<unsigned char *>(other.is_.elements_address()),
              other.capacity() * sizeof(value_type));
          std::copy_n(other.is_.groups_address(), other.is_.group_count(), is_.groups_address());
          size_ = other.size_;
        }
      } else {
        copy_insert_occupied_(other);
      }
    } else {
      copy_insert_occupied_(other);
    }
    max_load_ = floor_max_load_(capacity());
    return *this;
  }

  constexpr hash_table(hash_table &&other, allocator_type const &alloc)
      : is_ {}
      , size_ {}
      , max_load_ {}
      , hasher_ { std::move(other.hasher_) }
      , key_eq_ { std::move(other.key_eq_) }
      , alloc_ { alloc }
  {
    if (allocators_equal_(alloc_, other.alloc_)) {
      adopt_moved_(other);
    } else if (!other.empty()) {
      /// Different allocators cannot share the arrays, so the elements are moved
      /// across one by one and the source is emptied.
      other.is_.for_each_occupied(other.is_, [&](raw_pointer element_ptr, raw_group_pointer, unsigned) {
        emplace(std::move(*element_ptr));
        return true;
      });
      other.clear();
    }
  }

  constexpr hash_table(hash_table &&other) noexcept(
      std::is_nothrow_move_constructible_v<hasher> &&
      std::is_nothrow_move_constructible_v<key_equal> &&
      std::is_nothrow_move_constructible_v<allocator_type>)
      : is_ { std::move(other.is_) }
      , size_ { other.size_ }
      , max_load_ { other.max_load_ }
      , hasher_ { std::move(other.hasher_) }
      , key_eq_ { std::move(other.key_eq_) }
      , alloc_ { std::move(other.alloc_) }
  {
    other.size_     = 0;
    other.max_load_ = floor_max_load_(0);
  }

  constexpr hash_table &operator= (hash_table &&other) noexcept(
      allocator_traits::propagate_on_container_move_assignment::value ||
      allocator_traits::is_always_equal::value)
  {
    if (this == std::addressof(other)) [[unlikely]]
      return *this;

    using std::swap;
    swap(hasher_, other.hasher_);
    swap(key_eq_, other.key_eq_);

    if constexpr (allocator_traits::propagate_on_container_move_assignment::value) {
      alloc_ = std::move(other.alloc_);
      adopt_moved_(other);
    } else if (allocators_equal_(alloc_, other.alloc_)) {
      adopt_moved_(other);
    } else {
      /// Unequal allocators cannot share arrays, so the elements are moved one by
      /// one.  *this is emptied first whether or not other has elements, or a
      /// move from an empty other would silently keep the old contents.
      is_.clear();
      size_     = 0;
      max_load_ = floor_max_load_(capacity());
      other.is_.for_each_occupied(other.is_, [&](raw_pointer element_ptr, raw_group_pointer, unsigned) {
        emplace(std::move(*element_ptr));
        return true;
      });
      other.clear();
    }
    return *this;
  }

  constexpr void swap(hash_table &other) noexcept;

public:
  constexpr ~hash_table() noexcept { }

public:
  // clang-format off

  [[nodiscard]] constexpr allocator_type get_allocator() const noexcept { return alloc_; }
  [[nodiscard]] constexpr hasher hash_function() const noexcept { return hasher_; }
  [[nodiscard]] constexpr key_equal key_eq() const noexcept { return key_eq_; }

  [[nodiscard]] constexpr float max_load_factor() const noexcept { return max_load_factor_ratio; }
  constexpr void max_load_factor([[maybe_unused]] float new_value) const noexcept { }
  [[nodiscard]] constexpr size_type max_size() const noexcept { return std::numeric_limits<size_type>::max(); }
  [[nodiscard]] constexpr size_type size() const noexcept { return size_; }
  [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] constexpr size_type capacity() const noexcept
  {
    return is_.elements_address() ? (is_.group_count()) * group_size - 1 : 0;
  }

  /// Drops every element but keeps the allocation, so the table can be refilled
  /// without going back to the allocator (mirrors boost's clear()).
  constexpr void clear() noexcept
  {
    is_.clear();
    size_     = 0;
    max_load_ = floor_max_load_(capacity());
  }

  /// Rehashes into a capacity that is at least count and at least large enough
  /// for the current size, if that differs from the current capacity (boost's
  /// rehash, L1824).
  constexpr void rehash(size_type count)
  {
    auto const min_needed = ceil_capacity_for_load_(size_);
    if (count < min_needed) count = min_needed;
    if (count) count = group_aligned_capacity_(count);
    if (count != capacity()) {
      auto storage_alloc = static_cast<typename inline_storage_type::allocator_type>(alloc_);
      auto storage       = inline_storage_type::create(storage_alloc, count);
      force_rehash_(storage);
    }
  }


  /// Grows the storage to hold elem_count elements, never shrinking, and only
  /// allocating when the current capacity is too small (boost's noshrink_reserve,
  /// L2004).  The storage must already be empty, so growing only frees the old
  /// arrays without touching elements.
  constexpr void noshrink_reserve_(size_type elem_count)
  {
    if (elem_count == 0) return;
    auto const needed = group_aligned_capacity_(ceil_capacity_for_load_(elem_count));
    if (needed <= capacity()) return;

    auto storage_alloc = static_cast<typename inline_storage_type::allocator_type>(alloc_);
    is_                = inline_storage_type::create(storage_alloc, needed);
    max_load_          = floor_max_load_(capacity());
  }

  /// Copies other's live elements into this table, which must be empty.  size_ is
  /// advanced per element so a throwing copy leaves a valid, partially filled
  /// table instead of one whose count lies about its occupancy.
  constexpr void copy_insert_occupied_(hash_table const &other)
  {
    inline_storage_type::for_each_occupied(other.is_, [&](raw_pointer element_ptr, raw_group_pointer, unsigned) {
      auto hash = hasher_(Traits::key_fn(*element_ptr));
      emplace_at_available_slot_(is_, hash, is_.group_index(hash), static_cast<value_type const &>(*element_ptr));
      ++size_;
      return true;
    });
  }
  /// Reserves room for count elements: rehash(ceil(count / max_load_factor)).
  constexpr void reserve(size_type count) { rehash(ceil_capacity_for_load_(count)); }

  [[nodiscard]] constexpr iterator begin() noexcept
  {
    auto elems_raw_ptr = is_.elements_address();
    auto groups_raw_ptr = is_.groups_address();
    iterator it{ elems_raw_ptr, groups_raw_ptr, 0 };
    if (elems_raw_ptr && !(groups_raw_ptr[0].occupied_mask() & 0x1)) ++it;
    return it;
  }
  [[nodiscard]] constexpr iterator end() noexcept { return {}; }
  [[nodiscard]] constexpr const_iterator begin() const noexcept { return const_cast<hash_table*>(this)->begin(); }
  [[nodiscard]] constexpr const_iterator end() const noexcept { return {}; }
  [[nodiscard]] constexpr const_iterator cbegin() const noexcept { return begin(); }
  [[nodiscard]] constexpr const_iterator cend() const noexcept { return {}; }

protected:
  template <class K>
  constexpr const_iterator find_impl_(K const &key_val, size_type hash, size_type group_index) const
  {
    prober_type prober(group_index);
    do [[likely]] {
      auto pos        = prober.position();
      auto curr_group = is_.groups_address() + pos;
      if (auto fp_mask = curr_group->fingerprint_mask(hash)) {
        auto elements_ptr = is_.elements_address();
        [[assume(elements_ptr != nullptr)]];
        auto group_element_ptr = elements_ptr + pos * group_size;
        do {
          unsigned slot_index = std::countr_zero(static_cast<unsigned>(fp_mask));
          if (key_eq_(key_val, Traits::key_fn(group_element_ptr[slot_index]))) [[likely]] {
            return const_iterator { group_element_ptr + slot_index, curr_group, slot_index };
          }
          fp_mask &= fp_mask - 1;
        } while (fp_mask);
      }
      if (!curr_group->is_overflow(hash)) [[likely]] {
        return {};
      }
    } while (prober.next(is_.group_count_mask_));

    return {};
  }

public:
  constexpr iterator find(key_type const& key_val)
  {
    auto hash = hasher_(key_val);
    auto index = is_.group_index(hash);
    return iterator { const_iterator_cast_tag {}, find_impl_(key_val, hash, index) };
  }

  constexpr const_iterator find(key_type const& key_val) const
  {
    auto hash  = hasher_(key_val);
    auto index = is_.group_index(hash);
    return find_impl_(key_val, hash, index);
  }

  template <class K>
  requires heterogeneous_key<K const &, hasher, key_equal, iterator, const_iterator>
  constexpr iterator find(K const &key_val)
  {
    auto hash  = hasher_(key_val);
    auto index = is_.group_index(hash);
    return iterator { const_iterator_cast_tag {}, find_impl_(key_val, hash, index) };
  }

  template <class K>
  requires heterogeneous_key<K const &, hasher, key_equal, iterator, const_iterator>
  constexpr const_iterator find(K const &key_val) const
  {
    auto hash  = hasher_(key_val);
    auto index = is_.group_index(hash);
    return find_impl_(key_val, hash, index);
  }

  [[nodiscard]] constexpr size_type count(key_type const &key_val) const { return find(key_val) != end() ? 1 : 0; }
  [[nodiscard]] constexpr bool contains(key_type const &key_val) const { return find(key_val) != end(); }
  [[nodiscard]] constexpr auto equal_range(this auto &&self, key_type const &key_val)
  {
    auto it = self.find(key_val);
    return std::pair { it, it != self.end() ? std::next(it) : it };
  }

  template <class K> requires heterogeneous_key<K const &, hasher, key_equal, iterator, const_iterator>
  [[nodiscard]] constexpr size_type count(K const &key_val) const { return find(key_val) != end() ? 1 : 0; }
  template <class K> requires heterogeneous_key<K const &, hasher, key_equal, iterator, const_iterator>
  [[nodiscard]] constexpr bool contains(K const &key_val) const { return find(key_val) != end(); }
  template <class K> requires heterogeneous_key<K const &, hasher, key_equal, iterator, const_iterator>
  [[nodiscard]] constexpr auto equal_range(this auto &&self, K const &key_val)
  {
    auto it = self.find(key_val);
    return std::pair { it, it != self.end() ? std::next(it) : it };
  }

protected:
  constexpr size_type calculate_growth_() noexcept
  {
    auto n = size_ + size_ / 61 + 1;
    return (n * max_load_factor_ratio_numerator + 6) / max_load_factor_ratio_denominator;
  }

  /// Rehashes every live element of src into dst, recomputing each hash because
  /// dst lays its groups out differently.  This is the single "source to target
  /// rehash" primitive shared by growth (force_rehash_), the unequal-group copy
  /// path and future rehash/reserve.
  ///
  /// Following boost (unchecked_rehash, core.hpp:2287), a Move transfer destroys
  /// each source element and clears its slot as soon as it has been moved, so
  /// src stays a faithful record of what remains: if a later hash or move throws,
  /// size_ has already been decremented for the consumed slots, leaving exactly
  /// the untransferred elements (boost's recover_slot state, without a separate
  /// counter).  A copy transfer leaves src untouched until the caller commits,
  /// giving the strong guarantee.
  ///
  /// Precondition for Move: &src == &is_.  size_ counts this table's elements and
  /// only describes src when src is this table's own storage.
  template <bool Move>
  constexpr void transfer_occupied_(inline_storage_type const &src, inline_storage_type &dst)
  {
    auto const old_size = size_;
    inline_storage_type::for_each_occupied(src, [&](raw_pointer element_ptr, raw_group_pointer group, unsigned slot) {
      auto hash = hasher_(Traits::key_fn(*element_ptr));
      if constexpr (Move) {
        emplace_at_available_slot_(dst, hash, dst.group_index(hash), std::move(*element_ptr));
        inline_storage_type::destroy_element(alloc_, element_ptr);
        group->reset(slot);
        --size_;
      } else {
        emplace_at_available_slot_(dst, hash, dst.group_index(hash), static_cast<value_type const &>(*element_ptr));
      }
      return true;
    });
    if constexpr (Move) size_ = old_size; ///< every source was consumed
  }

  /// Rehashes is_ into the freshly allocated storage that already holds the
  /// inserted element, then adopts storage.  The source-side backout is
  /// transfer_occupied_'s; the destination storage owns its own rollback, and
  /// moving it into is_ (which disposes the old arrays) is the commit.
  constexpr void force_rehash_(inline_storage_type &storage)
  {
    assert(&storage != &is_ && "force_rehash_ needs a distinct destination storage");
    transfer_occupied_<transfer_by_move>(is_, storage);
    is_       = std::move(storage);
    max_load_ = floor_max_load_(capacity());
  }

  template <class... Args>
  constexpr iterator emplace_with_growth_(size_type hash, Args &&...args)
  {
    auto storage_alloc = static_cast<typename inline_storage_type::allocator_type>(alloc_);
    auto storage       = inline_storage_type::create(storage_alloc, calculate_growth_());
    auto it            = emplace_at_available_slot_(storage, hash, storage.group_index(hash), std::forward<Args>(args)...);
    force_rehash_(storage);
    ++size_;
    return it;
  }

  template <class... Args>
  constexpr std::pair<iterator, bool> emplace_with_key_(key_type const &key, Args &&...args)
  {
    auto hash = hasher_(key);
    auto index = is_.group_index(hash);

    if (auto it = find_impl_(key, hash, index); it != end()) {
      return { iterator { const_iterator_cast_tag{}, it }, false };
    }
    if (size_ < max_load_) {
      auto it = emplace_at_available_slot_(is_, hash, index, std::forward<Args>(args)...);
      ++size_;
      return { it, true };
    }
    return { emplace_with_growth_(hash, std::forward<Args>(args)...), true };
  }

  template <class... Args>
  constexpr std::pair<iterator, bool> emplace_impl_(Args &&...args)
  {
    value_type tmp(std::forward<Args>(args)...);
    return emplace_with_key_(Traits::key_fn(tmp), std::move(tmp));
  }

public:
  template <class... Args>
  constexpr std::pair<iterator, bool> emplace(Args &&...args)
  {
    return try_key_extraction<key_type>([this](key_type const &key, Args &&...args2)
    {
      return emplace_with_key_(key, std::forward<Args>(args2)...);
    }, [this](Args &&...args2)
    {
      return emplace_impl_(std::forward<Args>(args2)...);
    }, std::forward<Args>(args)...);
  }

  template <class... Args>
  constexpr iterator emplace_hint(const_iterator, Args &&...args) { return emplace(std::forward<Args>(args)...).first; }

  constexpr std::pair<iterator, bool> insert(value_type &&obj) { return emplace(std::move(obj)); }
  template <std::convertible_to<value_type> U>
  constexpr std::pair<iterator, bool> insert(U &&obj) { return emplace(std::forward<U>(obj)); }

  constexpr iterator insert(const_iterator, value_type &&obj) { return emplace(std::move(obj)).first; }
  template <std::convertible_to<value_type> U>
  constexpr iterator insert(const_iterator, U &&obj) { return emplace(std::forward<U>(obj)).first; }

  template <class InputIter>
  constexpr void insert(InputIter first, InputIter last) { for (auto pos = first; pos != last; ++pos) emplace(*pos); }
  template <container_compatible_range<value_type> R>
  constexpr void insert_range(R &&range) { insert(std::ranges::begin(range), std::ranges::end(range)); }

  //clang-format on

  // constexpr insert_return_type insert(node_type &&mode);
  // constexpr iterator insert(const_iterator hint, node_type &&node);
  // constexpr node_type extract(const_iterator position);
  // constexpr node_type extract(key_type const &key_val);
  // template <class K>
  // requires heterogeneous_key<K, hasher, key_equal, iterator, const_iterator>
  // constexpr node_type extract(K &&key_val);
  // template <OtherTraits>
  // constexpr void merge(hash_table<OtherTraits> &other);
  // template <OtherTraits>
  // constexpr void merge(hash_table<OtherTraits> &&other);

  // constexpr iterator erase(iterator position);
  // constexpr iterator erase(const_iterator position);
  // constexpr iterator erase(const_iterator first, const_iterator last);
  // constexpr size_type erase(key_type const &key_val);
  // template <class K>
  // requires heterogeneous_key<K, hasher, key_equal, iterator, const_iterator>
  // constexpr size_type erase(K &&key_val);

  // constexpr size_type bucket_count() const noexcept;
  // constexpr size_type max_bucket_count() const noexcept;
  // constexpr size_type bucket_size(size_type bucket_count) const;
  // constexpr size_type bucket(key_type const &key_val) const;

  // constexpr float load_factor() const noexcept;
  // constexpr void reset() noexcept;

  // consteval auto freeze();

protected:
  inline_storage_type is_;
  size_type size_;
  size_type max_load_;

  [[no_unique_address]] hasher hasher_;
  [[no_unique_address]] key_equal key_eq_;
  [[no_unique_address]] allocator_type alloc_;
};
} // namespace stdex::unordered_flat_detail
