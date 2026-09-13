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

#include <bit>         // std::bit_width, std::countr_zero
#include <climits>     // CHAR_BIT
#include <cstddef>     // std::size_t, size_t
#include <cstdint>     // std::uintptr_t / uintptr_t
#include <cstring>     // std::memset
#include <iterator>    // std::forward_iterator_tag
#include <memory>      // std::allocator, std::allocator_traits,
                       // std::pointer_traits, std::to_address,
                       // std::construct_at
#include <type_traits> // std::conditional_t, std::is_same_v, std::add_pointer_t

namespace stdex::unordered_flat_detail
{
template <class ValueTypePointer, class GroupTypePointer>
struct alignas(16) heap_block
{
  ValueTypePointer elements_ptr_ = nullptr;
  GroupTypePointer groups_ptr_   = nullptr;
};

template <class ValueType, class GroupType, size_t N, class Allocator>
struct inline_storage
{
  using value_type = ValueType;
  using group_type = GroupType;

  using allocator_type         = typename std::allocator_traits<Allocator>::template rebind_alloc<value_type>;
  using allocator_traits       = std::allocator_traits<allocator_type>;
  using group_allocator_type   = typename std::allocator_traits<Allocator>::template rebind_alloc<group_type>;
  using group_allocator_traits = std::allocator_traits<group_allocator_type>;
  using pointer                = typename allocator_traits::pointer;
  using pointer_traits         = std::pointer_traits<pointer>;
  using group_pointer          = typename pointer_traits::template rebind<group_type>;
  using group_pointer_traits   = std::pointer_traits<group_pointer>;

  static constexpr auto group_size      = group_type::capacity();
  static constexpr auto min_group_count = 2;

  static constexpr group_type dummy_groups[min_group_count] = {
    group_type::make_dummy_group(), group_type::make_dummy_group()
  };

  /// An inline_storage is always probe-able: a default-constructed one stands
  /// for an empty table (capacity 0) by pointing at the static dummy group,
  /// so callers never need to remember to call create(alloc, 0).
  constexpr inline_storage() noexcept { dissociate(); }

  constexpr inline_storage(inline_storage &&other) noexcept
      : us_ { std::move(other.us_) }
      , group_index_shift_ { other.group_index_shift_ }
      , group_count_mask_ { other.group_count_mask_ }
      , alloc_ { other.alloc_ }
  {
    other.dissociate();
  }

  /// Move assignment disposes the current contents first -- with the allocator
  /// that actually created them -- then adopts the source and empties it.  This
  /// is what makes `is_ = std::move(storage)` safe without a companion release():
  /// ownership, not discipline, decides who frees.
  constexpr inline_storage &operator= (inline_storage &&other) noexcept
  {
    if (this != std::addressof(other)) {
      dispose();
      us_                = std::move(other.us_);
      group_index_shift_ = other.group_index_shift_;
      group_count_mask_  = other.group_count_mask_;
      alloc_             = other.alloc_;
      other.dissociate();
    }
    return *this;
  }

  inline_storage(inline_storage const &)             = delete;
  inline_storage &operator= (inline_storage const &) = delete;

  /// The storage is the sole owner of its elements and memory, so its
  /// destructor is the backout: a storage that never gets adopted (because a
  /// caller threw while filling it) still tears down what it holds.
  constexpr ~inline_storage() noexcept { dispose(); }

  /// Leaves the storage representing an empty table.  Only the low-level
  /// elements/groups pointers are reset here; the caller decides whether the
  /// memory is freed (dispose) or kept (clear).
  constexpr void dissociate() noexcept
  {
    us_.heap_.elements_ptr_ = pointer {};
    if constexpr (std::is_same_v<std::add_pointer_t<group_type>, group_pointer>) {
      us_.heap_.groups_ptr_ = const_cast<group_pointer>(dummy_groups);
    }
    group_index_shift_ = group_index_shift(0);
    group_count_mask_  = min_group_count - 1;
  }

  /// Frees every element and the memory behind them, then becomes empty.
  constexpr void dispose() noexcept
  {
    destroy_elements(alloc_, *this);
    destroy(alloc_, *this);
    dissociate();
  }

  /// Drops every element but keeps the allocation, so a cleared table can be
  /// refilled without a round trip to the allocator (mirrors boost's clear()).
  constexpr void clear() noexcept
  {
    if (elements_address() == nullptr) return;

    destroy_elements(alloc_, *this);

    auto *groups_ptr = groups_address();
    auto const count = group_count();
    for (size_t i = 0; i < count; ++i) groups_ptr[i].reset();
    groups_ptr[count - 1].set_sentinel_mark();
  }

  constexpr value_type *elements_address() const noexcept { return std::to_address(us_.heap_.elements_ptr_); }

  constexpr group_type *groups_address() const noexcept { return std::to_address(us_.heap_.groups_ptr_); }

  constexpr size_t group_count() const noexcept { return group_count_mask_ + 1; }

  constexpr size_t group_index(size_t hash) const noexcept { return hash >> group_index_shift_; }

  /// Visits the occupied slots in layout order; func(element, group, slot) may
  /// return false to stop early.  This is the single traversal shared by
  /// copying, rehashing and element destruction.
  template <class F>
  static constexpr bool for_each_occupied(inline_storage const &s, F func)
  {
    value_type *elements_ptr = s.elements_address();
    if (!elements_ptr) return true;

    group_type *groups_ptr     = s.groups_address();
    group_type *sentinel_group = groups_ptr + s.group_count_mask_;

    /// Indexed rather than pointer-walked: the last group is the sentinel, so
    /// advancing a group-sized stride after it would form an out-of-range
    /// pointer (valid as runtime UB, but rejected in constant evaluation).
    for (size_t i = 0, gc = s.group_count(); i < gc; ++i) {
      value_type *group_elements = elements_ptr + i * group_size;
      auto occupied_mask         = group_type::occupied_mask_excluding_sentinel(groups_ptr + i, sentinel_group);
      while (occupied_mask) {
        unsigned slot_index = std::countr_zero(static_cast<unsigned>(occupied_mask));
        if (!func(group_elements + slot_index, groups_ptr + i, slot_index)) return false;
        occupied_mask &= occupied_mask - 1;
      }
    }
    return true;
  }

  static constexpr void destroy_element(allocator_type alloc, value_type *element_ptr) noexcept
  {
    allocator_traits::destroy(alloc, element_ptr);
  }

  /// Destroys every live element; releasing the memory itself stays destroy()'s
  /// job.  This is the undo primitive shared by dispose() and clear().
  static constexpr void destroy_elements(allocator_type alloc, inline_storage &s) noexcept
  {
    for_each_occupied(s, [&](value_type *element_ptr, group_type *, unsigned) {
      destroy_element(alloc, element_ptr);
      return true;
    });
  }

  static constexpr size_t group_index_shift(size_t elem_count) noexcept
  {
    size_t const group_count = elem_count / group_size + 1; ///< extra +1 for the sentinel
    return sizeof(size_t) * CHAR_BIT -
           (group_count <= min_group_count ? 1 : static_cast<size_t>(std::bit_width(group_count - 1)));
  }

  static constexpr size_t group_count(size_t index_shift) noexcept
  {
    return size_t(1) << (sizeof(size_t) * CHAR_BIT - index_shift);
  }

  static constexpr size_t align_up(size_t value, size_t alignment) noexcept
  {
    return (value + alignment - 1) & ~(alignment - 1);
  }

  static constexpr size_t groups_offset(size_t group_count) noexcept
  {
    return align_up((group_count * group_size - 1) * sizeof(value_type), alignof(group_type));
  }

  static constexpr size_t calculate_total_bytes(size_t group_count) noexcept
  {
    return groups_offset(group_count) + group_count * sizeof(group_type);
  }

  static constexpr size_t calculate_storage_size(size_t group_count) noexcept
  {
    return (calculate_total_bytes(group_count) + sizeof(value_type) - 1) / sizeof(value_type);
  }

  static constexpr inline_storage create(allocator_type alloc, size_t capacity)
  {
    auto const shift       = group_index_shift(capacity);
    auto const group_count = inline_storage::group_count(shift); ///< Power of 2 alignment
    inline_storage s {};
    s.group_index_shift_ = shift;
    s.group_count_mask_  = group_count - 1;
    s.alloc_             = alloc; ///< remembered so the destructor can deallocate

    if constexpr (std::is_same_v<std::add_pointer_t<group_type>, group_pointer>) {
      if (capacity == 0) [[unlikely]] {
        return s; // default-constructed storage already points at dummy_groups
      }
    }

    ///< The actual number of elements that can be stored
    auto const actual_capacity = group_count * group_size - 1;

    if consteval {
      auto groups_alloc = group_allocator_type { alloc };

      auto elements_ptr = allocator_traits::allocate(alloc, actual_capacity);
      auto groups_ptr   = group_allocator_traits::allocate(groups_alloc, group_count);

      /// reset() after construct_at zeroes every byte through a member write;
      /// MSVC's constant evaluator needs that write (not just value-init) to
      /// consider the union member live before available_mask reads it.
      for (size_t i = 0; i < group_count; ++i) {
        std::construct_at(groups_ptr + i);
        groups_ptr[i].reset();
      }
      groups_ptr[group_count - 1].set_sentinel_mark();

      s.us_.heap_ = { elements_ptr, groups_ptr };
      return s;
    } else {
      auto elements_ptr = allocator_traits::allocate(alloc, calculate_storage_size(group_count));
      auto byte_ptr     = reinterpret_cast<unsigned char *>(std::to_address(elements_ptr) + actual_capacity);

      ///< The group must be 16-byte aligned
      byte_ptr += (uintptr_t(sizeof(group_type)) - reinterpret_cast<uintptr_t>(byte_ptr)) % sizeof(group_type);

      auto groups_ptr = reinterpret_cast<group_type *>(byte_ptr);
      std::memset(groups_ptr, 0, sizeof(group_type) * group_count);
      groups_ptr[group_count - 1].set_sentinel_mark();

      s.us_.heap_ = { elements_ptr, group_pointer_traits::pointer_to(*groups_ptr) };
      return s;
    }
  }

  static constexpr void destroy(allocator_type alloc, inline_storage &s) noexcept(
      noexcept(allocator_traits::deallocate(
          alloc,
          s.us_.heap_.elements_ptr_,
          calculate_storage_size(s.group_count()))))
  {
    if (s.elements_address() != nullptr) {
      if consteval {
        auto groups_alloc = group_allocator_type { alloc };
        allocator_traits::deallocate(alloc, s.us_.heap_.elements_ptr_, s.group_count() * group_size - 1);
        group_allocator_traits::deallocate(groups_alloc, s.us_.heap_.groups_ptr_, s.group_count());
      } else {
        allocator_traits::deallocate(alloc, s.us_.heap_.elements_ptr_, calculate_storage_size(s.group_count()));
      }
    }
  }

  union union_storage
  {
    heap_block<pointer, group_pointer> heap_;

    /*reserve stack_block<value_type, ?> stack_*/

    constexpr union_storage()
    {
      std::construct_at(&heap_);
    }

    constexpr ~union_storage() noexcept
    {
      // stack: if it holds a non-trivial type (e.g. std::string), it needs destruction
    }

    /// Storage owns its allocation, so copying would alias and double-free;
    /// ownership is transferred by move only.
    union_storage(union_storage const &)                           = delete;
    union_storage &operator= (union_storage const &)               = delete;
    constexpr union_storage(union_storage &&) noexcept             = default;
    constexpr union_storage &operator= (union_storage &&) noexcept = default;
  } us_;

  size_t group_index_shift_;
  size_t group_count_mask_;

  /// The rebind that actually owns the element memory; kept so dispose() can
  /// deallocate with the same allocator state that allocated (the empty,
  /// never-created state never deallocates, so a default-constructed storage is
  /// still fine as long as the allocator is default-constructible).
  ///
  /// Moves copy it rather than move it: allocators are CopyConstructible, and a
  /// steal happens only when the container's allocator is already equal (so the
  /// copy is the same value), which keeps this storage's allocator interchangeable
  /// with hash_table::alloc_ without depending on a moved-from allocator -- the
  /// container must not propagate its allocator when propagation is off.
  [[no_unique_address]] allocator_type alloc_;
};
} // namespace stdex::unordered_flat_detail
