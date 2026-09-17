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
#include <cassert>     // assert
#include <concepts>    // std::convertible_to, std::constructible_from
#include <cstddef>     // std::size_t
#include <cstring>     // std::memcpy
#include <ranges>      // std::ranges::input_range, std::ranges::range_reference_t, std::ranges::begin, std::ranges::end
#include <type_traits> // std::conditional_t, std::is_same_v,
                       // std::is_convertible_v,
                       // std::is_trivially_copyable_v,
                       // std::is_standard_layout_v

#include "hash_table_iterator.hpp"
#include "inline_storage.hpp"
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

template <class Traits, class Hasher, class KeyEqual, class Allocator>
class hash_table : private inline_storage<Traits, Hasher, KeyEqual, Allocator>
{
  // clang-format off
  using base = inline_storage<Traits, Hasher, KeyEqual, Allocator>;

public:
  using typename base::allocator_traits;
  using typename base::allocator_type;
  using typename base::hasher;
  using typename base::key_equal;
  
  using typename base::key_type;
  using typename base::value_type;
  using typename base::size_type;
  using typename base::difference_type;
  using typename base::pointer;
  using typename base::const_pointer;
  using typename base::reference;
  using typename base::const_reference;

  using typename base::iterator;
  using typename base::const_iterator;
  using typename base::lazy_iterator;

  using base::capacity;
  using base::empty;
  using base::get_allocator;
  using base::hash_function;
  using base::key_eq;
  using base::inline_capacity;
  using base::max_load_factor;
  using base::max_size;
  using base::size;

  using base::is_heap;
  using base::is_stack;

private:
  using typename base::group_type;
  using typename base::prober_type;

  using base::allocate_unchecked_;
  using base::capacity_unchecked_;
  using base::construct_element_unchecked_;
  using base::deallocate_unchecked_;
  using base::destroy_element_unchecked_;
  using base::destroy_elements_unchecked_;
  using base::demote_to_stack_;
  using base::promote_to_heap_;
  using base::elements_address_;
  using base::groups_address_;
  using base::group_count_;
  using base::group_index_;
  using base::group_size_;
  using base::initial_max_load_;
  using base::required_capacity_;
  using base::required_capacity_for_growth_;
  using base::for_each_occupied_slot_;
  using base::emplace_at_available_slot_;

  static constexpr bool is_pocca_v = allocator_traits::propagate_on_container_copy_assignment::value;
  static constexpr bool is_pocma_v = allocator_traits::propagate_on_container_move_assignment::value;
  static constexpr bool is_pocs_v  = allocator_traits::propagate_on_container_swap::value;
  static constexpr bool is_always_equal_v  = allocator_traits::is_always_equal::value;
  
  static constexpr bool allocators_equal_(allocator_type const &left, allocator_type const &right) noexcept
  {
    if constexpr (allocator_traits::is_always_equal::value) {
      return true;
    } else {
      return left == right;
    }
  }

public:
  constexpr hash_table(hasher const &hash_arg, key_equal const &key_eq, allocator_type const &alloc)
      : base { hash_arg, key_eq, alloc }
  {
  }

  constexpr ~hash_table() = default;
  // clang-format on

private:
  struct destroy_elements_on_exit
  {
    ~destroy_elements_on_exit() { holder.destroy_elements_unchecked_(); }

    hash_table &holder;
  };

  template <bool Move>
  constexpr void transfer_elements_unchecked_(std::conditional_t<Move, hash_table &, hash_table const &> other)
  {
    if constexpr (Move) destroy_elements_on_exit g { other };

    other.for_each_occupied_slot_([&other, this](auto *occupied_ptr, auto *, unsigned)
    {
      auto *dst = elements_address_() + (occupied_ptr - other.elements_address_());
      if constexpr (Move) {
        construct_element_unchecked_(dst, std::move(*occupied_ptr));
      } else {
        construct_element_unchecked_(dst, *occupied_ptr);
      } 
    });
  }

  template <bool Move>
  constexpr void transfer_storage_same_layout_(std::conditional_t<Move, hash_table &, hash_table const &> other)
  {
    if constexpr (std::is_trivially_copy_constructible_v<value_type> &&
                  (std::is_same_v<allocator_type, std::allocator<value_type>> || 
                  !requires (allocator_type &a, value_type *p, value_type const &v) { a.construct(p, v); })) {
      if consteval {
        transfer_elements_unchecked_<false>(other);
      } else {
        std::memcpy(elements_address_(), other.elements_address_(), other.capacity() * sizeof(value_type));
      }
    } else {
      transfer_elements_unchecked_<Move>(other);
    }
    std::copy_n(other.groups_address_(), other.group_count_(), groups_address_());
    this->size_ = other.size_;
  }

  template <bool Move>
  constexpr void transfer_storage_with_rehash_(std::conditional_t<Move, hash_table &, hash_table const &> other)
  {
    if constexpr (Move) destroy_elements_on_exit g { other };

    other.for_each_occupied_slot_([this](auto *occupied_ptr, auto *, unsigned)
    {
      auto hash = this->hasher_(Traits::key_fn(*occupied_ptr));
      if constexpr (Move) {
        emplace_at_available_slot_(hash, std::move(*occupied_ptr));
      } else {
        emplace_at_available_slot_(hash, *occupied_ptr);
      }
    });
    this->size_ = other.size_;
  }

  template <bool Move>
  constexpr void transfer_from_unchecked_(std::conditional_t<Move, hash_table &, hash_table const &> other)
  {
    if (group_count_() == other.group_count_()) {
      transfer_storage_same_layout_<Move>(other);
    } else {
      transfer_storage_with_rehash_<Move>(other);
    }
  }

  template <bool Move>
  constexpr bool transfer_without_allocate_(std::conditional_t<Move, hash_table &, hash_table const &> other)
  {
    if (group_count_() == other.group_count_()) {
      transfer_storage_same_layout_<Move>(other);
      return true;
    }
    if (other.size() <= capacity_unchecked_()) {
      transfer_storage_with_rehash_<Move>(other);
      return true;
    }
    return false;
  }

  template <bool Move>
  constexpr void allocate_and_transfer_(std::conditional_t<Move, hash_table &, hash_table const &> other)
  {
    allocate_unchecked_(required_capacity_(other.size()));
    transfer_storage_with_rehash_<Move>(other);
  }

public:
  /// <summary>
 /// Copy construction:
 ///   this is heap (inline_capacity == 0):
 ///     1. initial state uses 29 groups by default with a 100% load factor
 ///     2. other is always a heap
 ///     3. other may be the initial 29 groups at 100% load; call transfer_storage_same_layout_
 ///     4. other may be at 87.5% load; call transfer_storage_with_rehash_
 ///     5. overall wrapper calls transfer_from_unchecked_
 ///   this is stack (inline_capacity != 0):
 ///     1. other has three states: stack, stack promoted to heap by insertion without erases shrinking via rehash, stack promoted to heap by insertion with erases shrinking via rehash
 ///     2. stack: group_count always matches with a 100% load factor; call transfer_storage_same_layout_
 ///     3. heap with more groups: check other's size; if small enough to fit keep it, else promote to heap; both call transfer_storage_with_rehash_
 ///     4. heap with fewer groups where this stack fits other's size: transfer_storage_with_rehash_
 ///     5. cases 2, 3 (fits without promoting to heap) and 4 need no max_load assignment
  /// </summary>
  /// <param name="other"></param>
  /// <param name="alloc"></param>
  constexpr hash_table(hash_table const &other, allocator_type const &alloc)
      : base { other.hash_function(), other.key_eq(), alloc }
  {
    if (other.empty()) [[unlikely]]
      return;

    if constexpr (inline_capacity == 0) {
      allocate_unchecked_(required_capacity_(other.size()));
      transfer_from_unchecked_<false>(other);
    } else if (!transfer_without_allocate_<false>(other)) {
      promote_to_heap_();
      allocate_and_transfer_<false>(other);
    }
  }

  /// <summary>
 /// Copy assignment:
 ///   1. copy hasher and key_eq first instead of destroy_elements_unchecked_, so a throw after destruction cannot lose destroyed elements
 ///   2. when is_pocca_v with unequal allocators: free with the old allocator, copy the new allocator, reallocate, copy elements
 ///   3. capacity insufficient: free memory, allocate with the allocator, copy elements
 ///   4. capacity sufficient: copy elements
  /// </summary>
  /// <param name="other"></param>
  /// <returns></returns>
  constexpr hash_table &operator= (hash_table const &other)
  {
    if (this == std::addressof(other)) [[unlikely]]
      return *this;

    auto hasher = other.hash_function();
    auto key_eq = other.key_eq();

    bool is_nullobj = elements_address_() == nullptr;
    if (!is_nullobj && !empty()) destroy_elements_unchecked_();

    std::ranges::swap(this->hasher_, hasher);
    std::ranges::swap(this->key_eq_, key_eq);

    bool const is_propagator = is_pocca_v && get_allocator() != other.get_allocator();
    bool const is_this_stack = is_stack();
    if (!is_this_stack) {
      size_type const needed_capacity = required_capacity_(other.size());
      bool const is_need_reallocate   = is_propagator || needed_capacity > capacity_unchecked_();

      if (!is_nullobj && is_need_reallocate) deallocate_unchecked_();
      if (is_propagator) this->alloc_ = other.get_allocator();
      if (is_nullobj || is_need_reallocate) allocate_unchecked_(needed_capacity);
      transfer_from_unchecked_<false>(other);
    } else if (is_this_stack) {
      // stack means equal inline_capacity, i.e. equal group_count allows a direct copy; note the allocator still decides whether to copy even when both sides are stacks
      // this is stack, other is heap; this needs no deallocation, the allocator may be copied first if allowed
      if (is_propagator) this->alloc_ = other.get_allocator();
      // equal group_count allows a direct copy; with enough capacity copy directly, max_load needs no assignment, load factor is 100%
      // capacity insufficient: this must promote to heap, allocating by required_capacity_ from the load factor, then copy; max_load must be assigned
      if (group_count_() == other.group_count_()) {
        transfer_storage_same_layout_<false>(other);
      } else if (other.size() <= capacity_unchecked_()) {
        transfer_storage_with_rehash_<false>(other);
      } else {
        promote_to_heap_();
        allocate_and_transfer_<false>(other);
      }
    }
    return *this;
  }

private:
  constexpr void steal_heap_from_(hash_table &other) noexcept
  {
    std::ranges::swap(this->heap_, other.heap_);
    std::ranges::swap(this->group_index_shift_, other.group_index_shift_);
    std::ranges::swap(this->group_count_mask_, other.group_count_mask_);
    std::ranges::swap(this->size_, other.size_);
    std::ranges::swap(this->max_load_, other.max_load_);
  }

public:
  constexpr hash_table(hash_table&& other, allocator_type const& alloc)
      : base { std::move(other.hasher_), std::move(other.key_eq_), alloc }
  {
    if (other.empty()) [[unlikely]]
      return;

    if constexpr (inline_capacity != 0) {
      if (transfer_without_allocate_<true>(other)) return;
      promote_to_heap_();
    }

    if (allocators_equal_(get_allocator(), other.get_allocator())) {
      steal_heap_from_(other);
    } else {
      allocate_and_transfer_<true>(other);
    }
  }

  constexpr hash_table(hash_table &&other)
      : base { std::move(other.hasher_), std::move(other.key_eq_), std::move(other.alloc_) }
  {
    if (other.empty()) [[unlikely]]
      return;

    if constexpr (inline_capacity != 0) {
      if (transfer_without_allocate_<true>(other)) return;
      promote_to_heap_();
    }
    steal_heap_from_(other);
  }

  constexpr hash_table& operator= (hash_table&& other)
  {
    if (this == std::addressof(other)) [[unlikely]]
      return *this;

    bool is_nullobj = elements_address_() == nullptr;
    if (!is_nullobj && !empty()) destroy_elements_unchecked_();

    std::ranges::swap(this->hasher_, other.hasher_);
    std::ranges::swap(this->key_eq_, other.key_eq_);

    bool const is_this_stack = is_stack(), is_other_stack = other.is_stack();
    if (!is_this_stack && !is_other_stack) {
      if (is_pocma_v || allocators_equal_(get_allocator(), other.get_allocator())) {
        if constexpr (is_pocma_v) this->alloc_ = std::move(other.alloc_);
        steal_heap_from_(other);
      } else {
        if (!is_nullobj) deallocate_unchecked_();
        allocate_and_transfer_<true>(other);
      }
    } else if (is_this_stack) {
      if constexpr (is_pocma_v) this->alloc_ = std::move(other.alloc_);
      if (transfer_without_allocate_<true>(other)) return *this;
      promote_to_heap_();
      if (is_pocma_v || allocators_equal_(get_allocator(), other.get_allocator())) {
        steal_heap_from_(other);
      } else {
        allocate_and_transfer_<true>(other);
      }
    } else /*!is_this_stack && is_other_stack*/ {
      // this started as a stack but promoted to heap on insertion; max_load comes from the exact capacity computed by required_capacity_,
      // then group_index_shift_by_ rounds up to the smallest power-of-two memory, then group_count_by_ derives the group count and multiplies (group size - 1) to get the real capacity before applying the factor for max_load_
      // max_load_ >= inline_capacity >= other.size(): the current heap elements (load factor may be 100% or 87.5%) fit the stack 100% load capacity
      bool const is_propagator = is_pocma_v && !allocators_equal_(get_allocator(), other.get_allocator());
      if (this->max_load_ >= other.size() && !is_propagator) {
        transfer_storage_with_rehash_<true>(other);
      } else {
        if (!is_nullobj) deallocate_unchecked_();
        if constexpr (is_pocma_v) this->alloc_ = std::move(other.alloc_);
        allocate_and_transfer_<true>(other);
      }
    }
    return *this;
  }

private:
  /// Precondition: this is heap and other is stack.  A stack table can only hold
  /// trivially copyable elements (the array is a union member, so a non-trivial
  /// type would need a live object there), which is what makes exchanging the
  /// whole inline storage a valid object-representation swap.  The heap side
  /// keeps its block: it simply moves to other.
  constexpr void swap_heap_to_stack_(hash_table &other)
  {
    auto const this_heap  = this->heap_;
    auto const this_shift = this->group_index_shift_;
    auto const this_mask  = this->group_count_mask_;
    auto const this_size  = this->size_;
    auto const this_load  = this->max_load_;

    demote_to_stack_();
    this->group_index_shift_ = other.group_index_shift_;
    this->group_count_mask_  = other.group_count_mask_;
    this->max_load_          = other.max_load_;
    std::ranges::swap(this->stack_.elements_, other.stack_.elements_);
    std::ranges::swap(this->stack_.groups_, other.stack_.groups_);
    this->size_              = other.size_;

    other.promote_to_heap_();
    other.heap_              = this_heap;
    other.group_index_shift_ = this_shift;
    other.group_count_mask_  = this_mask;
    other.size_              = this_size;
    other.max_load_          = this_load;
  }

public:
  constexpr void swap(hash_table &other)
    noexcept(std::is_nothrow_swappable_v<hasher> && std::is_nothrow_swappable_v<key_equal> &&
             (is_pocs_v || allocator_traits::is_always_equal::value))
  {
    if (this == std::addressof(other)) [[unlikely]]
      return;

    if constexpr (is_pocs_v) {
      std::ranges::swap(this->alloc_, other.alloc_);
    } else {
      assert(allocators_equal_(this->alloc_, other.alloc_) &&
             "[container.requirements.general]/8: if propagate_on_container_swap is false, swapping two containers with "
             "unequal allocators is undefined behavior.");
    }
    std::ranges::swap(this->hasher_, other.hasher_);
    std::ranges::swap(this->key_eq_, other.key_eq_);

    bool const is_this_heap = is_heap(), is_other_heap = other.is_heap();
    if (is_this_heap && is_other_heap) {
      std::ranges::swap(this->heap_, other.heap_);
      std::ranges::swap(this->group_index_shift_, other.group_index_shift_);
      std::ranges::swap(this->group_count_mask_, other.group_count_mask_);
      std::ranges::swap(this->size_, other.size_);
      std::ranges::swap(this->max_load_, other.max_load_);
    } else if (is_this_heap && !is_other_heap) {
      swap_heap_to_stack_(other);
    } else if (!is_this_heap && is_other_heap) {
      other.swap_heap_to_stack_(*this);
    } else {
      /// Both are stack: their layout metadata is identical, so only the inline
      /// storage and the element count have to move.
      std::ranges::swap(this->stack_.elements_, other.stack_.elements_);
      std::ranges::swap(this->stack_.groups_, other.stack_.groups_);
      std::ranges::swap(this->size_, other.size_);
    }
  }
};
} // namespace stdex::unordered_flat_detail
