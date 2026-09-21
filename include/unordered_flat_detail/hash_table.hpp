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
#include <memory>      // std::allocator
#include <ranges>      // std::ranges::input_range, std::ranges::range_reference_t, std::ranges::begin, std::ranges::end
#include <type_traits> // std::conditional_t, std::is_same_v,
                       // std::is_convertible_v,
                       // std::is_trivially_copyable_v,
                       // std::is_standard_layout_v

#include "inline_storage.hpp"
#include "try_key_extraction.hpp"

namespace stdex::unordered_flat_detail
{
template <std::input_iterator Iter>
using guide_key_t = std::remove_const_t<std::tuple_element_t<0, typename std::iterator_traits<Iter>::value_type>>;
template <std::input_iterator Iter>
using guide_mapped_t = std::tuple_element_t<1, typename std::iterator_traits<Iter>::value_type>;
template <class Rng>
using range_key_t = std::remove_const_t<std::tuple_element_t<0, std::ranges::range_value_t<Rng>>>;
template <class Rng>
using range_mapped_t = std::tuple_element_t<1, std::ranges::range_value_t<Rng>>;

template <class A>
concept is_allocator = requires (A &a) {
  typename A::value_type;
  a.deallocate(a.allocate(size_t{1}), size_t{1});
};

template <class Alloc>
using guide_size_type_t = typename std::allocator_traits<std::conditional_t<is_allocator<Alloc>, Alloc, std::allocator<int>>>::size_type;

template <class H, class K>
concept is_hasher_for = std::invocable<H const &, K const &> && std::convertible_to<std::invoke_result_t<H const &, K const &>, std::size_t>;

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
  
  using base::inline_capacity;
  
  using base::get_allocator;
  using base::hash_function;
  using base::key_eq;

  using base::load_factor;
  using base::max_load_factor;
  using base::empty;
  using base::size;
  using base::max_size;
  using base::capacity;
  using base::bucket_count;

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
  using base::for_each_occupied_slot_unchecked_;
  using base::emplace_at_available_slot_unchecked_;
  using base::find_fingerprint_slot_unchecked_;
  using base::demote_to_stack_;
  using base::promote_to_heap_;
  using base::elements_address_;
  using base::groups_address_;
  using base::group_count_;
  using base::group_index_;
  using base::group_size_;
  using base::group_index_shift_by_;
  using base::group_count_by_;

  using base::initial_max_load_;
  using base::required_capacity_;
  using base::required_capacity_for_growth_;
  using base::aligned_capacity_;

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

  constexpr hash_table(allocator_type const &alloc) noexcept(
        std::is_nothrow_copy_constructible_v<allocator_type>)
      : base { alloc }
  {
  }

public:
  constexpr hash_table(hasher const &hash_arg, key_equal const &key_eq, allocator_type const &alloc) noexcept(
        std::is_nothrow_copy_constructible_v<hasher> &&
        std::is_nothrow_copy_constructible_v<key_equal> &&
        std::is_nothrow_copy_constructible_v<allocator_type>)
      : base { hash_arg, key_eq, alloc }
  {
  }

  constexpr ~hash_table() = default;

private:
  struct destroy_elements_on_exit
  {
    constexpr ~destroy_elements_on_exit() { holder_.destroy_elements_unchecked_(); }

    hash_table &holder_;
  };

  template <bool Move>
  static constexpr void transfer_same_layout_unchecked_(std::conditional_t<Move, hash_table &, hash_table const &> source, hash_table &dest)
  {
    if constexpr (Move) destroy_elements_on_exit e { source };

    auto transfer_elements = [&]() constexpr
    {
      source.for_each_occupied_slot_unchecked_([&](auto *occupied_ptr, auto *, unsigned)
      {
        value_type *dst = dest.elements_address_() + (occupied_ptr - source.elements_address_());
        if constexpr (Move) {
          dest.construct_element_unchecked_(dst, std::move(*occupied_ptr));
        } else {
          dest.construct_element_unchecked_(dst, *occupied_ptr);
        }
        ++dest.size_;
      });
    };

    if constexpr (std::is_trivially_copy_constructible_v<value_type> && (std::is_same_v<allocator_type, std::allocator<value_type>> || !requires (allocator_type &a, value_type *p, value_type const &v) { a.construct(p, v); })) {
      if consteval {
        transfer_elements();
      } else {
        std::memcpy(dest.elements_address_(), source.elements_address_(), source.capacity() * sizeof(value_type));
        dest.size_ = source.size_;
      }
    } else {
      transfer_elements();
    }

    std::copy_n(source.groups_address_(), source.group_count_(), dest.groups_address_());
  }

  template <bool Move>
  static constexpr void transfer_with_rehash_unchecked_(std::conditional_t<Move, hash_table &, hash_table const &> source, hash_table &dest)
  {
    if constexpr (Move) destroy_elements_on_exit e { source };

    source.for_each_occupied_slot_unchecked_([&dest](auto *occupied_ptr, auto *, unsigned)
    {
      auto hash = dest.hasher_(Traits::key_fn(*occupied_ptr));
      if constexpr (Move) {
        dest.emplace_at_available_slot_unchecked_(hash, std::move(*occupied_ptr));
      } else {
        dest.emplace_at_available_slot_unchecked_(hash, *occupied_ptr);
      }
      ++dest.size_;
    });
  }

  template <bool Move>
  static constexpr void transfer_to_heap_unchecked_(std::conditional_t<Move, hash_table &, hash_table const &> source, hash_table &dest)
  {
    if (source.group_count_() == dest.group_count_()) {
      transfer_same_layout_unchecked_<Move>(source, dest);
    } else {
      transfer_with_rehash_unchecked_<Move>(source, dest);
    }
  }

  template <bool Move>
  static constexpr bool transfer_to_stack_unchecked_(std::conditional_t<Move, hash_table &, hash_table const &> source, hash_table &dest)
  {
    if (source.group_count_() == dest.group_count_()) {
      transfer_same_layout_unchecked_<Move>(source, dest);
      return true;
    }
    if (source.size() <= inline_capacity) { // 100% max load
      transfer_with_rehash_unchecked_<Move>(source, dest);
      return true;
    }
    return false;
  }

public:
  constexpr hash_table(hash_table const &other, allocator_type const &alloc)
      : base { other.hash_function(), other.key_eq(), alloc }
  {
    if (other.empty()) [[unlikely]]
      return;

    if constexpr (inline_capacity == 0) {
      allocate_unchecked_(required_capacity_(other.size()));
      transfer_to_heap_unchecked_<false>(other, *this);
    } else if (!transfer_to_stack_unchecked_<false>(other, *this)) {
      promote_to_heap_();
      allocate_unchecked_(required_capacity_(other.size()));
      transfer_with_rehash_unchecked_<false>(other, *this);
    }
  }

  constexpr hash_table &operator= (hash_table const &other)
  {
    if (this == std::addressof(other)) [[unlikely]]
      return *this;

    auto hasher = other.hash_function();
    auto key_eq = other.key_eq();

    if (!empty()) destroy_elements_unchecked_();

    std::ranges::swap(this->hasher_, hasher);
    std::ranges::swap(this->key_eq_, key_eq);

    bool const is_propagator = is_pocca_v && get_allocator() != other.get_allocator();
    if (is_heap()) {
      size_type const needed_capacity = required_capacity_(other.size());
      bool const is_need_reallocate   = is_propagator || needed_capacity > capacity_unchecked_();

      bool is_nullobj = this->heap_.elements_ptr_ == nullptr;
      if (!is_nullobj && is_need_reallocate) deallocate_unchecked_();
      if (is_propagator) this->alloc_ = other.get_allocator();
      if (other.empty()) return *this;
      if (is_nullobj || is_need_reallocate) allocate_unchecked_(needed_capacity);
      
      transfer_to_heap_unchecked_<false>(other, *this);
    } else {
      if (is_propagator) this->alloc_ = other.get_allocator();
      if (other.empty() || transfer_to_stack_unchecked_<false>(other, *this)) return *this;
      promote_to_heap_();
      allocate_unchecked_(required_capacity_(other.size()));
      transfer_with_rehash_unchecked_<false>(other, *this);
    }
    return *this;
  }

private:
  constexpr void steal_heap_from_(hash_table &other) noexcept
  {
    this->heap_              = other.heap_;
    this->group_index_shift_ = other.group_index_shift_;
    this->group_count_mask_  = other.group_count_mask_;
    this->size_              = other.size_;
    this->max_load_          = other.max_load_;

    other.heap_              = {};
    other.group_index_shift_ = static_cast<uint8_t>(group_index_shift_by_(0));
    other.group_count_mask_  = group_count_by_(static_cast<size_type>(other.group_index_shift_)) - 1;
    other.size_              = 0;
    other.max_load_          = other.initial_max_load_(other.capacity_unchecked_());
  }

public: 
  constexpr hash_table(hash_table &&other, allocator_type const &alloc)
      : base { std::move(other.hasher_), std::move(other.key_eq_), alloc }
  {
    if (other.empty()) [[unlikely]]
      return;

    if constexpr (inline_capacity != 0) {
      if (transfer_to_stack_unchecked_<true>(other, *this)) return;
      promote_to_heap_();
    }

    if (allocators_equal_(get_allocator(), other.get_allocator())) {
      steal_heap_from_(other);
    } else {
      allocate_unchecked_(required_capacity_(other.size()));
      transfer_with_rehash_unchecked_<true>(other, *this);
    }
  }

  constexpr hash_table(hash_table &&other) noexcept(
        std::is_nothrow_move_constructible_v<hasher> &&
        std::is_nothrow_move_constructible_v<key_equal> &&
        std::is_nothrow_move_constructible_v<allocator_type> &&
        allocator_traits::is_always_equal::value &&
        (inline_capacity == 0 ||
         (std::is_nothrow_move_constructible_v<value_type> &&
          std::is_nothrow_invocable_v<hasher&, key_type const&> &&
          std::is_nothrow_invocable_v<key_equal&, key_type const&, key_type const&>)))
      : base { std::move(other.hasher_), std::move(other.key_eq_), std::move(other.alloc_) }
  {
    if (other.empty()) [[unlikely]]
      return;

    if constexpr (inline_capacity != 0) {
      if (transfer_to_stack_unchecked_<true>(other, *this)) return;
      promote_to_heap_();
    }
    steal_heap_from_(other);
  }

  constexpr hash_table& operator= (hash_table&& other) noexcept(
        std::is_nothrow_swappable_v<hasher> &&
        std::is_nothrow_swappable_v<key_equal> &&
        std::is_nothrow_move_assignable_v<allocator_type> &&
        std::is_nothrow_move_constructible_v<value_type> &&
        std::is_nothrow_destructible_v<value_type> &&
        allocator_traits::is_always_equal::value &&
        std::is_nothrow_invocable_v<hasher&, key_type const&> &&
        std::is_nothrow_invocable_v<key_equal&, key_type const&, key_type const&>)
  {
    if (this == std::addressof(other)) [[unlikely]]
      return *this;

    if (!empty()) destroy_elements_unchecked_();

    std::ranges::swap(this->hasher_, other.hasher_);
    std::ranges::swap(this->key_eq_, other.key_eq_);

    bool const is_this_stack = is_stack(), is_other_stack = other.is_stack();
    if (!is_this_stack && !is_other_stack) {
      if (this->heap_.elements_ptr_ != nullptr) deallocate_unchecked_();
      if (is_pocma_v || allocators_equal_(get_allocator(), other.get_allocator())) {
        if constexpr (is_pocma_v) this->alloc_ = std::move(other.alloc_);
        steal_heap_from_(other);
      } else {
        allocate_unchecked_(required_capacity_(other.size()));
        if (!other.empty()) transfer_with_rehash_unchecked_<true>(other, *this);
      }
    } else if (is_this_stack) {
      if constexpr (is_pocma_v) this->alloc_ = std::move(other.alloc_);
      if (other.empty() || transfer_to_stack_unchecked_<true>(other, *this)) return *this;
      promote_to_heap_();
      if (is_pocma_v || allocators_equal_(get_allocator(), other.get_allocator())) {
        steal_heap_from_(other);
      } else {
        allocate_unchecked_(required_capacity_(other.size()));
        if (!other.empty()) transfer_with_rehash_unchecked_<true>(other, *this);
      }
    } else /*!is_this_stack && is_other_stack*/ {
      bool const is_propagator = is_pocma_v && !allocators_equal_(get_allocator(), other.get_allocator());
      if (this->max_load_ >= other.size() && !is_propagator) {
        if (!other.empty()) transfer_with_rehash_unchecked_<true>(other, *this);
      } else {
        if (this->heap_.elements_ptr_ != nullptr) deallocate_unchecked_();
        if constexpr (is_pocma_v) this->alloc_ = std::move(other.alloc_);
        allocate_unchecked_(required_capacity_(other.size()));
        if (!other.empty()) transfer_with_rehash_unchecked_<true>(other, *this);
      }
    }
    return *this;
  }

private:
  constexpr void swap_stack_elements_(hash_table &a, hash_table &b) noexcept(
      std::is_nothrow_swappable_v<value_type> &&
      (std::is_nothrow_move_constructible_v<value_type> ||
       std::is_nothrow_copy_constructible_v<value_type>))
  {
    if constexpr (inline_capacity != 0) {
      for (size_type i = 0; i < inline_capacity; ++i) {
        size_type const group_index = i / group_size_();
        size_type const slot_index  = i % group_size_();

        bool const a_occupied = a.stack_.groups_[group_index].is_occupied_by_slot(slot_index);
        bool const b_occupied = b.stack_.groups_[group_index].is_occupied_by_slot(slot_index);

        if (a_occupied && b_occupied) {
          std::ranges::swap(a.stack_.elements_[i], b.stack_.elements_[i]);
        } else if (a_occupied) {
          std::construct_at(std::addressof(b.stack_.elements_[i]), std::move_if_noexcept(a.stack_.elements_[i]));
          std::destroy_at(std::addressof(a.stack_.elements_[i]));
        } else if (b_occupied) {
          std::construct_at(std::addressof(a.stack_.elements_[i]), std::move_if_noexcept(b.stack_.elements_[i]));
          std::destroy_at(std::addressof(b.stack_.elements_[i]));
        }
      }
    }
  }

  constexpr void swap_heap_to_stack_(hash_table &other) noexcept(
      std::is_nothrow_swappable_v<value_type> &&
      std::is_nothrow_swappable_v<group_type>)
  {
    auto const this_heap  = this->heap_;
    auto const this_shift = this->group_index_shift_;
    auto const this_mask  = this->group_count_mask_;
    auto const this_size  = this->size_;
    auto const this_load  = this->max_load_;

    demote_to_stack_();
    swap_stack_elements_(*this, other);
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
  constexpr void swap(hash_table &other) noexcept(
      std::is_nothrow_swappable_v<hasher> &&
      std::is_nothrow_swappable_v<key_equal> &&
      std::is_nothrow_swappable_v<allocator_type> &&
      std::is_nothrow_swappable_v<value_type> &&
      std::is_nothrow_swappable_v<group_type> &&
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
      // Both are stack: their layout metadata is identical, so only the inline
      // storage and the element count have to move.
      swap_stack_elements_(*this, other);
      std::ranges::swap(this->stack_.groups_, other.stack_.groups_);
      std::ranges::swap(this->size_, other.size_);
    }
  }

public:
  [[nodiscard]] constexpr iterator begin() noexcept
  {
    auto elements_ptr = elements_address_();
    auto groups_ptr   = groups_address_();
    iterator it { elements_ptr, groups_ptr, 0 };
    return elements_ptr && !(groups_ptr[0].occupied_mask() & 0x1) ? ++it : it;
  }

  [[nodiscard]] constexpr iterator end() noexcept { return {}; }
  [[nodiscard]] constexpr const_iterator begin() const noexcept { return const_cast<hash_table *>(this)->begin(); }
  [[nodiscard]] constexpr const_iterator end() const noexcept { return {}; }
  [[nodiscard]] constexpr const_iterator cbegin() const noexcept { return begin(); }
  [[nodiscard]] constexpr const_iterator cend() const noexcept { return {}; }

public:
  constexpr void clear() noexcept
  {
    if (empty()) return;
    destroy_elements_unchecked_();
  }

  constexpr void reset() noexcept
  {
    if (!empty()) destroy_elements_unchecked_();
    if constexpr (inline_capacity != 0)
    {
      if (is_stack()) return;

      if (elements_address_()) deallocate_unchecked_();
      
      demote_to_stack_();
    }
  }

private:
  struct destroy_element_on_exit
  {
    constexpr ~destroy_element_on_exit() { holder_.destroy_element_unchecked_(element_ptr_); }

    hash_table &holder_;
    value_type *element_ptr_;
  };

  static constexpr void force_rehash_(hash_table &source, hash_table &dest)
  {
    constexpr bool move_path = std::is_nothrow_move_constructible_v<value_type> ||
                               !std::is_copy_constructible_v<value_type>;

    bool const is_source_stack = source.is_stack();

    if (!source.empty()) {
      source.for_each_occupied_slot_unchecked_([&](value_type *occupied_ptr, group_type *, unsigned)
      {
        auto hash = dest.hash_function()(Traits::key_fn(*occupied_ptr));
        if constexpr (move_path) {
          destroy_element_on_exit e { source, occupied_ptr };
          dest.emplace_at_available_slot_unchecked_(hash, std::move(*occupied_ptr));
        } else {
          dest.emplace_at_available_slot_unchecked_(hash, *occupied_ptr);
        }
      });
    }

    if constexpr (inline_capacity != 0) {
      // Copy path: source elements are still in stack_; promotion discards that storage,
      // so they must be manually destroyed first; otherwise it both leaks and makes
      // dest's size_ disagree with the actual number of elements.
      if (is_source_stack) {
        if constexpr (!move_path && !std::is_trivially_destructible_v<value_type>) {
          if (!source.empty()) {
            source.for_each_occupied_slot_unchecked_([](value_type *occupied_ptr, group_type *, unsigned)
            {
              std::destroy_at(occupied_ptr);
            });
          }
        }
        source.promote_to_heap_();
      }
    }

    std::ranges::swap(source.heap_, dest.heap_);
    std::ranges::swap(source.group_index_shift_, dest.group_index_shift_);
    std::ranges::swap(source.group_count_mask_, dest.group_count_mask_);
    std::ranges::swap(source.max_load_, dest.max_load_);

    if constexpr (move_path) {
      // Source elements have already been destroyed one by one in the loop.
      dest.size_ = 0;
    } else {
      // Source is heap: the old heap together with N live elements goes to dest,
      // and is left to dest's destructor.
      // Source is stack: elements were manually destroyed, and the heap storage
      // was already empty.
      dest.size_ = is_source_stack ? 0 : source.size_;
    }
  }

public:
  constexpr void rehash(size_type count)
  {
    size_type actual_demand_capacity = required_capacity_(size());
    if (actual_demand_capacity > count) count = actual_demand_capacity;
    if (count > 0) count = aligned_capacity_(count);
    if (count != capacity_unchecked_()) {
      hash_table new_storage { get_allocator() };
      if constexpr (inline_capacity != 0) new_storage.promote_to_heap_();
      new_storage.allocate_unchecked_(count);
      force_rehash_(*this, new_storage);
    }
  }

  constexpr void reserve(size_type count) { rehash(required_capacity_(count)); }

private:
  template <class... Args>
  constexpr std::pair<iterator, bool> emplace_with_key_(key_type const &key_val, Args &&...args)
  {
    iterator it;
    if (auto hash = hash_function()(key_val); elements_address_() != nullptr) {
      // Check for duplicates first
      if (it = find_fingerprint_slot_unchecked_(key_val, hash); it != end()) {
        return { it, false };
      }

      // Max load reached: stack storage is promoted to heap, or the heap is expanded
      if (size() >= this->max_load_) {
        hash_table new_storage { get_allocator() };
        if constexpr (inline_capacity != 0) new_storage.promote_to_heap_();
        new_storage.allocate_unchecked_(required_capacity_for_growth_(size()));

        it = new_storage.emplace_at_available_slot_unchecked_(hash, std::forward<Args>(args)...);

        // Note: force_rehash_ internally handles the case where source is promoted from stack to heap.
        // Do not promote *this early here, otherwise the elements pending migration in stack_ will be lost.
        force_rehash_(*this, new_storage);
      } else {
        it = emplace_at_available_slot_unchecked_(hash, std::forward<Args>(args)...);
      }
    } else {
      // elements_address_() == nullptr is true only when using heap storage and it has not been allocated yet
      allocate_unchecked_(required_capacity_(1));
      it = emplace_at_available_slot_unchecked_(hash, std::forward<Args>(args)...);
    }

    ++this->size_;
    return { it, true };
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
      value_type tmp(std::forward<Args>(args2)...);
      return emplace_with_key_(Traits::key_fn(tmp), std::move(tmp));
    }, std::forward<Args>(args)...);
  }

  template <class... Args>
  constexpr iterator emplace_hint(const_iterator /*hint*/, Args &&...args)
  {
    return emplace(std::forward<Args>(args)...).first;
  }

  constexpr std::pair<iterator, bool> insert(value_type const &obj) 
  {
    return emplace(obj); 
  }

  constexpr std::pair<iterator, bool> insert(value_type &&obj) 
  { 
    return emplace(std::move(obj)); 
  }

  template <std::convertible_to<value_type> U>
  constexpr std::pair<iterator, bool> insert(U &&obj)
  {
    return emplace(std::forward<U>(obj));
  }
  
  constexpr iterator insert(const_iterator, value_type const &obj) 
  {
    return emplace(obj).first; 
  }

  constexpr iterator insert(const_iterator, value_type &&obj) 
  { 
    return emplace(std::move(obj)).first; 
  }

  template <std::convertible_to<value_type> U>
  constexpr iterator insert(const_iterator, U &&obj)
  {
    return emplace(std::forward<U>(obj)).first;
  }

  template <class InputIter>
  constexpr void insert(InputIter first, InputIter last)
  {
    for (auto pos = first; pos != last; ++pos) emplace(*pos);
  }

  constexpr void insert(std::initializer_list<value_type> list)
  {
    insert(list.begin(), list.end());
  }

  template <container_compatible_range<value_type> R>
  constexpr void insert_range(R &&range)
  {
    insert(std::ranges::begin(range), std::ranges::end(range));
  }

public:
  [[nodiscard]] constexpr iterator find(key_type const &key_val)
  {
    if (empty()) [[unlikely]] return end();
    return find_fingerprint_slot_unchecked_(key_val, hash_function()(key_val));
  }

  [[nodiscard]] constexpr const_iterator find(key_type const &key_val) const
  {
    if (empty()) [[unlikely]] return end();
    return find_fingerprint_slot_unchecked_(key_val, hash_function()(key_val));
  }

  [[nodiscard]] constexpr std::pair<iterator, iterator> equal_range(key_type const &key_val)
  {
    auto it = find(key_val);
    return { it, it != end() ? std::next(it) : it };
  }

  [[nodiscard]] constexpr std::pair<const_iterator, const_iterator> equal_range(key_type const &key_val) const
  {
    auto it = find(key_val);
    return { it, it != end() ? std::next(it) : it };
  }

  [[nodiscard]] constexpr size_type count(key_type const &key_val) const { return find(key_val) != end() ? 1 : 0; }
  [[nodiscard]] constexpr bool contains(key_type const &key_val) const { return find(key_val) != end(); }

  template <class K> requires heterogeneous_key<K const &, hasher, key_equal, iterator, const_iterator>
  [[nodiscard]] constexpr iterator find(K const &key_val)
  {
    if (empty()) [[unlikely]] return end();
    return find_fingerprint_slot_unchecked_(key_val, hash_function()(key_val));
  }

  template <class K> requires heterogeneous_key<K const &, hasher, key_equal, iterator, const_iterator>
  [[nodiscard]] constexpr const_iterator find(K const &key_val) const
  {
    if (empty()) [[unlikely]] return end();
    return find_fingerprint_slot_unchecked_(key_val, hash_function()(key_val));
  }

  template <class K> requires heterogeneous_key<K const &, hasher, key_equal, iterator, const_iterator>
  [[nodiscard]] constexpr std::pair<iterator, iterator> equal_range(K const &key_val)
  {
    auto it = find(key_val);
    return { it, it != end() ? std::next(it) : it };
  }

  template <class K> requires heterogeneous_key<K const &, hasher, key_equal, iterator, const_iterator>
  [[nodiscard]] constexpr std::pair<const_iterator, const_iterator> equal_range(K const &key_val) const
  {
    auto it = find(key_val);
    return { it, it != end() ? std::next(it) : it };
  }

  template <class K> requires heterogeneous_key<K const &, hasher, key_equal, iterator, const_iterator>
  [[nodiscard]] constexpr size_type count(K const &key_val) const { return find(key_val) != end() ? 1 : 0; }
  template <class K> requires heterogeneous_key<K const &, hasher, key_equal, iterator, const_iterator>
  [[nodiscard]] constexpr bool contains(K const &key_val) const { return find(key_val) != end(); }

public:
  template <class Iter = iterator>
  requires std::is_same_v<Iter, iterator> && (!std::is_same_v<iterator, const_iterator>)
  constexpr lazy_iterator erase(Iter position) noexcept
  { 
    return erase(const_iterator { position }); 
  }
  constexpr lazy_iterator erase(const_iterator position) noexcept
  {
    auto element_ptr = position.element_address_();
    auto group_ptr   = position.group_address_();
    destroy_element_unchecked_(element_ptr);
    this->max_load_ -= group_ptr->is_overflow_by_slot(position.slot_index_);
    group_ptr->reset(position.slot_index_);
    --this->size_;
    return { position };
  }
  constexpr lazy_iterator erase(const_iterator first, const_iterator last) noexcept
  {
    while (first != last) first = erase(first);
    return first;
  }
  constexpr size_type erase(key_type const& key_val) noexcept
  {
    auto it = find(key_val);
    return it != end() ? (erase(it), 1) : 0;
  }
  template <class K> requires heterogeneous_key<K, hasher, key_equal, iterator, const_iterator>
  constexpr size_type erase(K &&key_val) noexcept
  {
    auto it = find(key_val);
    return it != end() ? (erase(it), 1) : 0;
  }

private:
  struct erase_on_exit
  {
    constexpr ~erase_on_exit()
    {
      if (!rollback_) holder_.erase(element_iterator_);
    }

    constexpr void rollback() noexcept { rollback_ = true; }

    hash_table &holder_;
    const_iterator element_iterator_;
    bool rollback_ = false;
  };

  template <class, class, class, class>
  friend class hash_table;

public:
  class node_type
  {
  public:
    constexpr node_type() noexcept : has_value_(false) {}

    constexpr explicit node_type(value_type &&v)
        noexcept(std::is_nothrow_move_constructible_v<value_type>)
        : has_value_(true)
    {
      std::construct_at(std::addressof(value_), std::move(v));
    }

    constexpr explicit node_type(value_type const &v)
        noexcept(std::is_nothrow_copy_constructible_v<value_type>)
      requires std::copy_constructible<value_type>
        : has_value_(true)
    {
      std::construct_at(std::addressof(value_), v);
    }

    constexpr node_type(node_type const &other)
        noexcept(std::is_nothrow_copy_constructible_v<value_type>)
      requires std::copy_constructible<value_type>
        : has_value_(other.has_value_)
    {
      if (has_value_) std::construct_at(std::addressof(value_), other.value_);
    }

    constexpr node_type(node_type &&other)
        noexcept(std::is_nothrow_move_constructible_v<value_type>)
        : has_value_(other.has_value_)
    {
      if (has_value_) std::construct_at(std::addressof(value_), std::move(other.value_));
    }

    constexpr node_type &operator= (node_type const &other)
        noexcept(std::is_nothrow_copy_assignable_v<value_type> &&
                 std::is_nothrow_copy_constructible_v<value_type>)
      requires std::copy_constructible<value_type> &&
               std::assignable_from<value_type &, value_type const &>
    {
      if (this == std::addressof(other)) return *this;
      if (has_value_ && other.has_value_) {
        value_ = other.value_;
      } else if (other.has_value_) {
        std::construct_at(std::addressof(value_), other.value_);
        has_value_ = true;
      } else {
        reset();
      }
      return *this;
    }

    constexpr node_type &operator= (node_type &&other)
        noexcept(std::is_nothrow_move_assignable_v<value_type> &&
                 std::is_nothrow_move_constructible_v<value_type>)
    {
      if (this == std::addressof(other)) return *this;
      if (has_value_ && other.has_value_) {
        value_ = std::move(other.value_);
      } else if (other.has_value_) {
        std::construct_at(std::addressof(value_), std::move(other.value_));
        has_value_ = true;
      } else {
        reset();
      }
      return *this;
    }

    constexpr ~node_type() { reset(); }

    constexpr explicit operator bool() const noexcept { return has_value_; }

    constexpr value_type &value() & noexcept { return value_; }
    constexpr value_type const &value() const & noexcept { return value_; }
    constexpr value_type &&value() && noexcept { return std::move(value_); }

    constexpr void reset() noexcept
    {
      if (has_value_) {
        std::destroy_at(std::addressof(value_));
        has_value_ = false;
      }
    }

  private:
    bool has_value_;
    union { value_type value_; };
  };

  struct insert_return_type
  {
    iterator position;
    bool inserted;
    node_type node;
  };

  constexpr insert_return_type insert(node_type &&node)
  {
    if (!static_cast<bool>(node)) [[unlikely]] {
      return { end(), false, node_type {} };
    }

    auto &val = node.value();
    auto &key = Traits::key_fn(val);

    if (elements_address_() != nullptr) {
      auto hash = hash_function()(key);
      if (auto it = find_fingerprint_slot_unchecked_(key, hash); it != end()) {
        return { it, false, std::move(node) };
      }
    }

    auto [it, ok] = emplace_with_key_(key, std::move(val));
    assert(ok && "insert(node_type&&): duplicate check raced with emplace");
    return { it, true, node_type {} };
  }

  constexpr iterator insert(const_iterator /*hint*/, node_type &&node)
  {
    return insert(std::move(node)).position;
  }

  constexpr node_type extract(const_iterator position)
  {
    auto *element_ptr = position.element_address_();
    node_type node { std::move(*element_ptr) };
    erase_on_exit e { *this, position };
    return node;
  }

  constexpr node_type extract(key_type const &key_val)
  {
    auto it = find(key_val);
    return it != end() ? extract(it) : node_type {};
  }

  template <class K> requires heterogeneous_key<K, hasher, key_equal, iterator, const_iterator>
  constexpr node_type extract(K &&key_val)
  {
    auto it = find(std::forward<K>(key_val));
    return it != end() ? extract(it) : node_type {};
  }

  template <class H2, class KE2>
  constexpr void merge(hash_table<Traits, H2, KE2, Allocator> &other)
  {
    if constexpr (std::is_same_v<hash_table, hash_table<Traits, H2, KE2, Allocator>>) {
      if (this == std::addressof(other)) [[unlikely]] return;
    }
    if (other.empty()) [[unlikely]] return;

    other.for_each_occupied_slot_unchecked_([&](value_type *occupied_ptr, group_type *group_ptr, unsigned slot_index) 
    {
      erase_on_exit g { other, const_iterator { occupied_ptr, group_ptr, slot_index } };
      if (!emplace(std::move(*occupied_ptr)).second) g.rollback();
    });
  }

  template <class H2, class KE2>
  constexpr void merge(hash_table<Traits, H2, KE2, Allocator> &&other) { merge(other); }

public:
  friend bool operator== (hash_table const &a, hash_table const &b) noexcept
  {
    if (std::addressof(a) == std::addressof(b)) return true;
    if (a.size() != b.size()) return false;

    value_type *a_elements = a.elements_address_();
    group_type *a_groups   = a.groups_address_();
    group_type *a_sentinel = a_groups + a.group_count_() - 1;
    for (size_type group_index = 0; group_index < a.group_count_(); ++group_index) {
      group_type *group_ptr = a_groups + group_index;
      auto occupied_mask    = group_type::occupied_mask_excluding_sentinel(group_ptr, a_sentinel);
      while (occupied_mask) {
        unsigned slot_index     = std::countr_zero(static_cast<unsigned>(occupied_mask));
        value_type *element_ptr = a_elements + group_index * group_size_() + slot_index;
        auto it                 = b.find(Traits::key_fn(*element_ptr));
        if (it == b.end() || !(*it == *element_ptr)) return false;
        occupied_mask &= occupied_mask - 1;
      }
    }
    return true;
  }
  friend bool operator!= (hash_table const& a, hash_table const& b) noexcept { return !(a == b); }

  // consteval auto freeze();
};
} // namespace stdex::unordered_flat_detail