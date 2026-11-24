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
#include <version>     // __cpp_lib_define_static, __cpp_lib_optional

#if defined(__cpp_lib_define_static)
#  include <meta> // std::define_static_array
#  include <span>
#  include <vector>
#endif

#if defined(__cpp_lib_optional) && __cpp_lib_optional >= 202506L
#  include <optional>
#endif

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
  a.deallocate(a.allocate(std::size_t{1}), std::size_t{1});
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
  using base::try_for_each_occupied_slot_unchecked_;
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
  using base::reset_load_threshold_;
  using base::countr_zero_unchecked_;

  using base::uses_fancy_pointers_v;
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
  constexpr hash_table(hasher const &hash_arg, key_equal const &key_eq_arg, allocator_type const &alloc) noexcept(
        std::is_nothrow_copy_constructible_v<hasher> &&
        std::is_nothrow_copy_constructible_v<key_equal> &&
        std::is_nothrow_copy_constructible_v<allocator_type>)
      : base { hash_arg, key_eq_arg, alloc }
  {
  }

  constexpr ~hash_table() = default;

private:
  template <bool Move>
  struct destroy_source_elements_on_exit
  {
    constexpr ~destroy_source_elements_on_exit()
    requires (Move)
    {
      holder_.destroy_elements_unchecked_();
    }

    constexpr ~destroy_source_elements_on_exit() requires (!Move) = default;

    std::conditional_t<Move, hash_table &, hash_table const &> holder_;
  };

  /// Groups are bulk-copied only after every element transferred, so a throwing
  /// transfer would leave dest.size_ > 0 with empty groups and leak the partial
  /// elements. The guard mirror-destroys exactly the constructed prefix on unwind.
  struct destroy_dest_elements_guard
  {
    constexpr ~destroy_dest_elements_guard()
    {
      if (dismissed_ || dest_.size_ == 0) return;
      auto *dest_elements   = dest_.elements_address_();
      auto *source_elements = source_.elements_address_();
      source_.for_each_occupied_slot_unchecked_([&](auto *occupied_ptr, auto *, unsigned)
      {
        // Stop at exactly the constructed prefix; later mirrored slots were never built.
        if (dest_.size_ == 0) return;
        dest_.destroy_element_unchecked_(dest_elements + (occupied_ptr - source_elements));
        --dest_.size_;
      });
    }

    constexpr void dismiss() noexcept { dismissed_ = true; }

    hash_table const &source_;
    hash_table &dest_;
    bool dismissed_ = false;
  };

  template <bool Move>
  static constexpr void transfer_same_layout_unchecked_(std::conditional_t<Move, hash_table &, hash_table const &> source, hash_table &dest)
  {
    assert(source.group_count_() == dest.group_count_() && "same-layout transfer requires equal group counts");

    [[maybe_unused]] destroy_source_elements_on_exit<Move> se { source };

    destroy_dest_elements_guard dg { source, dest };

    auto transfer_elements = [&]() constexpr
    {
      auto *dest_elements   = dest.elements_address_();
      auto *source_elements = source.elements_address_();
      source.for_each_occupied_slot_unchecked_([&](auto *occupied_ptr, auto *, unsigned)
      {
        value_type *dst = dest_elements + (occupied_ptr - source_elements);
        if constexpr (Move) {
          dest.construct_element_unchecked_(dst, std::move(*occupied_ptr));
        } else {
          dest.construct_element_unchecked_(dst, *occupied_ptr);
        }
        ++dest.size_;
      });
    };

    if constexpr (std::is_trivially_copy_constructible_v<value_type> && std::is_trivially_destructible_v<value_type> &&
                  !base::has_custom_destroy_v &&
                  (std::is_same_v<allocator_type, std::allocator<value_type>> || !requires (allocator_type &a, value_type *p, value_type const &v) { a.construct(p, v); })) {
      if (std::is_constant_evaluated()) {
        transfer_elements();
      } else {
        // `dest` is raw storage and the branch requires an implicit-lifetime
        // value_type, so memcpy starts the lifetime of the copied elements
        // instead of assigning over live objects.
#if defined(__GNUC__) && !defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif
        if (source.capacity() != 0) {
          std::memcpy(dest.elements_address_(), source.elements_address_(), source.capacity() * sizeof(value_type));
        }
#if defined(__GNUC__) && !defined(__clang__)
#  pragma GCC diagnostic pop
#endif
        dest.size_ = source.size_;
      }
    } else {
      transfer_elements();
    }
    dg.dismiss();

    std::copy_n(source.groups_address_(), source.group_count_(), dest.groups_address_());
    dest.reset_load_threshold_();
  }

  template <bool Move>
  static constexpr void transfer_with_rehash_unchecked_(std::conditional_t<Move, hash_table &, hash_table const &> source, hash_table &dest)
  {
    assert(dest.size_ == 0 && "rehash transfer destination must be empty");

    [[maybe_unused]] destroy_source_elements_on_exit<Move> e { source };

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
    dest.reset_load_threshold_();
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
    if (source.size_ <= inline_capacity) { // 100% max load
      transfer_with_rehash_unchecked_<Move>(source, dest);
      return true;
    }
    return false;
  }

public:
  constexpr hash_table(hash_table const &other, allocator_type const &alloc)
      : base { other.hasher_, other.key_eq_, alloc }
  {
    if (other.empty()) [[unlikely]]
      return;

    if constexpr (inline_capacity == 0) {
      allocate_unchecked_(required_capacity_(other.size_));
      transfer_to_heap_unchecked_<false>(other, *this);
    } else if (!transfer_to_stack_unchecked_<false>(other, *this)) {
      promote_to_heap_();
      allocate_unchecked_(required_capacity_(other.size_));
      transfer_with_rehash_unchecked_<false>(other, *this);
    }
  }

  constexpr hash_table &operator= (hash_table const &other)
  {
    if (this == std::addressof(other)) [[unlikely]]
      return *this;

    auto hasher_copy = other.hasher_;
    auto key_eq_copy = other.key_eq_;

    if (!empty()) destroy_elements_unchecked_();

    std::ranges::swap(this->hasher_, hasher_copy);
    std::ranges::swap(this->key_eq_, key_eq_copy);

    bool const is_propagator = is_pocca_v && !allocators_equal_(this->alloc_, other.alloc_);
    if (is_heap()) {
      size_type const needed_capacity = required_capacity_(other.size_);
      bool const is_need_reallocate   = is_propagator || needed_capacity > capacity_unchecked_();

      bool is_nullobj = this->heap_.elements_ptr_ == pointer {};
      if (!is_nullobj && is_need_reallocate) deallocate_unchecked_();
      if (is_propagator) this->alloc_ = other.alloc_;
      if (other.empty()) return *this;
      if (is_nullobj || is_need_reallocate) allocate_unchecked_(needed_capacity);

      transfer_to_heap_unchecked_<false>(other, *this);
    } else {
      if (is_propagator) this->alloc_ = other.alloc_;
      if (other.empty() || transfer_to_stack_unchecked_<false>(other, *this)) return *this;
      promote_to_heap_();
      allocate_unchecked_(required_capacity_(other.size_));
      transfer_with_rehash_unchecked_<false>(other, *this);
    }
    return *this;
  }

private:
  constexpr void steal_heap_from_(hash_table &other) noexcept
  {
    assert(is_heap() && other.is_heap() && "steal: both tables must be heap-resident");

    this->heap_              = other.heap_;
    this->group_index_shift_ = other.group_index_shift_;
    this->group_count_mask_  = other.group_count_mask_;
    this->size_              = other.size_;
    this->max_load_          = other.max_load_;

    other.heap_              = {};
    other.group_index_shift_ = static_cast<std::uint8_t>(group_index_shift_by_(0));
    other.group_count_mask_  = group_count_by_(static_cast<size_type>(other.group_index_shift_)) - 1;
    other.size_              = 0;
    other.reset_load_threshold_();
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

    if (allocators_equal_(this->alloc_, other.alloc_)) {
      steal_heap_from_(other);
    } else {
      allocate_unchecked_(required_capacity_(other.size_));
      transfer_with_rehash_unchecked_<true>(other, *this);
    }
  }

  constexpr hash_table(hash_table &&other) noexcept(
        std::is_nothrow_move_constructible_v<hasher> &&
        std::is_nothrow_move_constructible_v<key_equal> &&
        std::is_nothrow_move_constructible_v<allocator_type> &&
        allocator_traits::is_always_equal::value &&
        !uses_fancy_pointers_v &&
        (inline_capacity == 0 ||
         (std::is_nothrow_move_constructible_v<value_type> &&
          std::is_nothrow_invocable_v<hasher&, key_type const&> &&
          std::is_nothrow_invocable_v<key_equal&, key_type const&, key_type const&>)))
      : base { std::move(other.hasher_), std::move(other.key_eq_), std::move(other.alloc_) }
  {
    if (other.is_heap()) {
      if (other.heap_.elements_ptr_ == pointer {}) return;
      if constexpr (inline_capacity != 0) promote_to_heap_();
      steal_heap_from_(other);
      return;
    }

    transfer_same_layout_unchecked_<true>(other, *this);
  }

  constexpr hash_table &operator= (hash_table &&other) noexcept(
      inline_capacity == 0 &&
      std::is_nothrow_move_assignable_v<hasher> &&
      std::is_nothrow_move_assignable_v<key_equal> &&
      std::is_nothrow_move_assignable_v<allocator_type> &&
      std::is_nothrow_move_constructible_v<value_type> &&
      std::is_nothrow_destructible_v<value_type> &&
      allocator_traits::is_always_equal::value &&
      !uses_fancy_pointers_v &&
      std::is_nothrow_invocable_v<hasher &, key_type const &> &&
      std::is_nothrow_invocable_v<key_equal &, key_type const &, key_type const &>)
  {
    if (this == std::addressof(other)) [[unlikely]]
      return *this;

    if (!empty()) destroy_elements_unchecked_();

    this->hasher_ = std::move(other.hasher_);
    this->key_eq_ = std::move(other.key_eq_);

    bool const is_this_stack = is_stack(), is_other_stack = other.is_stack();
    if (!is_this_stack && !is_other_stack) {
      if (this->heap_.elements_ptr_ != pointer {}) deallocate_unchecked_();
      if (is_pocma_v || allocators_equal_(this->alloc_, other.alloc_)) {
        if constexpr (is_pocma_v) this->alloc_ = std::move(other.alloc_);
        steal_heap_from_(other);
      } else if (!other.empty()) {
        allocate_unchecked_(required_capacity_(other.size_));
        transfer_with_rehash_unchecked_<true>(other, *this);
      }
    } else if constexpr (inline_capacity != 0) {
      if (is_this_stack) {
        if (other.size_ > inline_capacity) {
          promote_to_heap_();
          if (is_pocma_v || allocators_equal_(this->alloc_, other.alloc_)) {
            steal_heap_from_(other);
          } else {
            allocate_unchecked_(required_capacity_(other.size_));
            transfer_with_rehash_unchecked_<true>(other, *this);
          }
        } else {
          if (!other.empty()) {
            (void) transfer_to_stack_unchecked_<true>(other, *this);
          }
          if (!is_other_stack && other.heap_.elements_ptr_ != pointer {}) {
            other.deallocate_unchecked_();
          }
        }
        if constexpr (is_pocma_v) this->alloc_ = std::move(other.alloc_);
      } else /*!is_this_stack && is_other_stack*/ {
        bool const is_propagator = is_pocma_v && !allocators_equal_(this->alloc_, other.alloc_);
        if (this->capacity() >= other.size_ && !is_propagator) {
          if (!other.empty()) transfer_with_rehash_unchecked_<true>(other, *this);
        } else {
          if (this->heap_.elements_ptr_ != pointer {}) deallocate_unchecked_();
          if constexpr (is_pocma_v) this->alloc_ = std::move(other.alloc_);
          if (!other.empty()) {
            allocate_unchecked_(required_capacity_(other.size_));
            transfer_with_rehash_unchecked_<true>(other, *this);
          }
        }
      }
    }
    return *this;
  }

private:
  template <class Fn>
  static constexpr void for_each_stack_slot_(Fn &&fn) noexcept(
      std::is_nothrow_invocable_v<std::remove_reference_t<Fn> &, size_type, size_type, size_type>)
  {
    for (size_type i = 0; i < inline_capacity; ++i) {
      fn(i, static_cast<size_type>(i / group_size_()), static_cast<size_type>(i % group_size_()));
    }
  }

  struct swap_stack_elements_guard
  {
    hash_table &a_;
    hash_table &b_;
    size_type constructed_to_a_ { 0 };
    size_type constructed_to_b_ { 0 };
    bool dismissed_ { false };

    constexpr ~swap_stack_elements_guard() noexcept
    {
      if (dismissed_) return;

      // Only one-sided slots have newly constructed destination objects; the
      // source objects have not been destroyed yet. Walk the original group
      // layout in the same order as the transfer loop and destroy exactly the
      // constructed prefix in each direction.
      for_each_stack_slot_([&](size_type i, size_type group_index, size_type slot_index) noexcept
      {
        bool const a_occupied = a_.stack_.groups_[group_index].is_occupied_by_slot(slot_index);
        bool const b_occupied = b_.stack_.groups_[group_index].is_occupied_by_slot(slot_index);

        if (a_occupied && !b_occupied && constructed_to_b_ != 0) {
          std::destroy_at(std::addressof(b_.stack_.elements_[i]));
          --constructed_to_b_;
        } else if (!a_occupied && b_occupied && constructed_to_a_ != 0) {
          std::destroy_at(std::addressof(a_.stack_.elements_[i]));
          --constructed_to_a_;
        }
      });
    }

    constexpr void dismiss() noexcept { dismissed_ = true; }
  };

  static constexpr bool is_nothrow_swap_stack_elements_v =
      std::is_nothrow_swappable_v<value_type> &&
      std::is_nothrow_swappable_v<group_type> &&
      std::is_nothrow_destructible_v<value_type> &&
      (std::is_nothrow_move_constructible_v<value_type> ||
       std::is_nothrow_copy_constructible_v<value_type>);

  template <bool DeferSourceDestruction>
  constexpr void swap_stack_elements_impl_(hash_table &a, hash_table &b) noexcept(is_nothrow_swap_stack_elements_v)
  {
    if constexpr (inline_capacity != 0) {
      swap_stack_elements_guard guard { a, b };
      if constexpr (!DeferSourceDestruction) guard.dismiss();

      for_each_stack_slot_([&](size_type i, size_type group_index, size_type slot_index) noexcept(is_nothrow_swap_stack_elements_v)
      {
        bool const a_occupied = a.stack_.groups_[group_index].is_occupied_by_slot(slot_index);
        bool const b_occupied = b.stack_.groups_[group_index].is_occupied_by_slot(slot_index);

        if (a_occupied && b_occupied) {
          std::ranges::swap(a.stack_.elements_[i], b.stack_.elements_[i]);
        } else if (a_occupied) {
          std::construct_at(std::addressof(b.stack_.elements_[i]), std::move_if_noexcept(a.stack_.elements_[i]));
          if constexpr (DeferSourceDestruction) {
            ++guard.constructed_to_b_;
          } else {
            std::destroy_at(std::addressof(a.stack_.elements_[i]));
          }
        } else if (b_occupied) {
          std::construct_at(std::addressof(a.stack_.elements_[i]), std::move_if_noexcept(b.stack_.elements_[i]));
          if constexpr (DeferSourceDestruction) {
            ++guard.constructed_to_a_;
          } else {
            std::destroy_at(std::addressof(b.stack_.elements_[i]));
          }
        }
      });

      if constexpr (DeferSourceDestruction) {
        for_each_stack_slot_([&](size_type i, size_type group_index, size_type slot_index) noexcept
        {
          bool const a_occupied = a.stack_.groups_[group_index].is_occupied_by_slot(slot_index);
          bool const b_occupied = b.stack_.groups_[group_index].is_occupied_by_slot(slot_index);

          if (a_occupied && !b_occupied) {
            std::destroy_at(std::addressof(a.stack_.elements_[i]));
          } else if (!a_occupied && b_occupied) {
            std::destroy_at(std::addressof(b.stack_.elements_[i]));
          }
        });
      }

      std::ranges::swap(a.stack_.groups_, b.stack_.groups_);
      std::ranges::swap(a.size_, b.size_);
      if constexpr (DeferSourceDestruction) guard.dismiss();
    }
  }

  constexpr void swap_stack_elements_(hash_table &a, hash_table &b) noexcept(is_nothrow_swap_stack_elements_v)
  {
    if constexpr (is_nothrow_swap_stack_elements_v) {
      swap_stack_elements_impl_<false>(a, b);
    } else {
      swap_stack_elements_impl_<true>(a, b);
    }
  }

  struct swap_heap_to_stack_guard
  {
    typename base::heap_block this_heap_;
    size_type this_mask_;
    size_type this_size_;
    size_type this_load_;
    std::uint8_t this_shift_;
    hash_table &this_;
    bool dismissed_ { false };

    constexpr ~swap_heap_to_stack_guard() noexcept
    {
      if (dismissed_) return;

      // `this_` is stack-resident here. Groups were copied from `other` before
      // construction, so the first size_ occupied slots are exactly the live
      // elements constructed so far.
      size_type remaining = this_.size_;
      if (remaining != 0) {
        (void) this_.try_for_each_occupied_slot_unchecked_([&](value_type *element_ptr, group_type *, unsigned) {
          std::destroy_at(element_ptr);
          return --remaining != 0;
        });
      }

      this_.promote_to_heap_();
      this_.heap_              = this_heap_;
      this_.group_index_shift_ = this_shift_;
      this_.group_count_mask_  = this_mask_;
      this_.size_              = this_size_;
      this_.max_load_          = this_load_;
    }

    constexpr void dismiss() noexcept { dismissed_ = true; }
  };

  constexpr void swap_heap_to_stack_(hash_table &other) noexcept(
      std::is_nothrow_copy_assignable_v<group_type> &&
      std::is_nothrow_destructible_v<value_type> &&
      (std::is_nothrow_move_constructible_v<value_type> ||
       std::is_nothrow_copy_constructible_v<value_type>) )
  {
    if constexpr (inline_capacity != 0) {
      swap_heap_to_stack_guard guard {
        this->heap_,
        this->group_count_mask_,
        this->size_,
        this->max_load_,
        this->group_index_shift_,
        *this
      };

      demote_to_stack_();

      // Copy the final group layout first. size_ then doubles as the count of
      // successfully constructed elements, which lets the guard destroy exactly
      // the live prefix if construction throws.
      std::copy_n(other.stack_.groups_, group_count_(), this->stack_.groups_);

      for_each_stack_slot_([&](size_type i, size_type group_index, size_type slot_index)
      {
        if (other.stack_.groups_[group_index].is_occupied_by_slot(slot_index)) {
          std::construct_at(std::addressof(this->stack_.elements_[i]), std::move_if_noexcept(other.stack_.elements_[i]));
          ++this->size_;
        }
      });
      assert(this->size_ == other.size_ && "heap-to-stack transfer must construct every source element");

      // Destroy the source only after every destination element has been built.
      // If the copy path throws in the loop above, other is still untouched;
      // this gives the strong guarantee for copyable value types.
      other.destroy_elements_unchecked_();

      other.promote_to_heap_();
      other.heap_              = guard.this_heap_;
      other.group_index_shift_ = guard.this_shift_;
      other.group_count_mask_  = guard.this_mask_;
      other.size_              = guard.this_size_;
      other.max_load_          = guard.this_load_;
      guard.dismiss();
    }
  }

public:
  constexpr void swap(hash_table &other) noexcept(
      std::is_nothrow_swappable_v<hasher> &&
      std::is_nothrow_swappable_v<key_equal> &&
      std::is_nothrow_swappable_v<allocator_type> &&
      std::is_nothrow_swappable_v<value_type> &&
      std::is_nothrow_swappable_v<group_type> &&
      std::is_nothrow_copy_assignable_v<group_type> &&
      std::is_nothrow_destructible_v<value_type> &&
      (std::is_nothrow_move_constructible_v<value_type> ||
       std::is_nothrow_copy_constructible_v<value_type>) &&
      (is_pocs_v || allocator_traits::is_always_equal::value))
  {
    if (this == std::addressof(other)) [[unlikely]]
      return;

    if constexpr (!is_pocs_v) {
      assert(allocators_equal_(this->alloc_, other.alloc_) &&
             "[container.requirements.general]/8: if propagate_on_container_swap is false, swapping two containers with "
             "unequal allocators is undefined behavior.");
    }

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
      swap_stack_elements_(*this, other);
    }

    // Swap hasher/allocator state only after the storage has been moved, so an
    // exception while constructing stack elements cannot leave a heap block
    // owned by an allocator that has already been exchanged, nor leave a
    // partially swapped element set paired with the other table's hasher.
    std::ranges::swap(this->hasher_, other.hasher_);
    std::ranges::swap(this->key_eq_, other.key_eq_);
    if constexpr (is_pocs_v) std::ranges::swap(this->alloc_, other.alloc_);
  }

public:
  [[nodiscard]] constexpr iterator begin() noexcept
  {
    if (empty()) return end();
    auto elements_ptr = elements_address_();
    auto groups_ptr   = groups_address_();
    iterator it { elements_ptr, groups_ptr, 0 };
    return !(groups_ptr[0].occupied_mask() & 0x1) ? ++it : it;
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

    // Anti-drift may have lowered max_load_ even though every element was
    // erased; clear() must restore the storage's full threshold.
    reset_load_threshold_();
  }

  constexpr void reset() noexcept
  {
    if (!empty()) destroy_elements_unchecked_();
    reset_load_threshold_(); // Anti-drift

    if constexpr (inline_capacity != 0)
    {
      if (is_stack()) return;

      if (elements_address_()) deallocate_unchecked_();

      demote_to_stack_();
    }
  }

private:
  struct source_recover_guard
  {
    hash_table &source_;
    size_type num_destroyed_ { 0 };
    bool dismissed_ { false };

    constexpr ~source_recover_guard() noexcept
    {
      if (dismissed_ || num_destroyed_ == 0) return;
      size_type remaining = num_destroyed_;
      // The traversal order is the same as for_each_occupied_slot_unchecked_,
      // so the first num_destroyed_ slots are exactly the destroyed prefix.
      (void) source_.try_for_each_occupied_slot_unchecked_([&](value_type *, group_type *group_ptr, unsigned slot_index) {
        source_.max_load_ -= group_ptr->is_overflow_by_slot(slot_index);
        group_ptr->reset(slot_index);
        --source_.size_;
        return --remaining != 0;
      });
    }

    constexpr void dismiss() noexcept { dismissed_ = true; }
  };

  struct destroy_element_on_exit
  {
    constexpr ~destroy_element_on_exit() { holder_.destroy_element_unchecked_(element_ptr_); }

    hash_table &holder_;
    value_type *element_ptr_;
  };

  static constexpr void force_rehash_(hash_table &source, hash_table &dest)
  {
    assert(std::addressof(source) != std::addressof(dest) && "force_rehash: source and destination must be distinct objects");

    constexpr bool move_path = std::is_nothrow_move_constructible_v<value_type> ||
                               !std::is_copy_constructible_v<value_type>;

    bool const is_source_stack = source.is_stack();

    source_recover_guard guard { source };
    if (!source.empty()) {
      source.for_each_occupied_slot_unchecked_([&](value_type *occupied_ptr, group_type *, unsigned) {
        auto hash = dest.hasher_(Traits::key_fn(*occupied_ptr));
        if constexpr (move_path) {
          ++guard.num_destroyed_;
          [[maybe_unused]] destroy_element_on_exit e { source, occupied_ptr };
          dest.emplace_at_available_slot_unchecked_(hash, std::move(*occupied_ptr));
        } else {
          dest.emplace_at_available_slot_unchecked_(hash, *occupied_ptr);
        }
        ++dest.size_;
      });
    }
    guard.dismiss();

    if constexpr (inline_capacity != 0) {
      // Copy path: source elements are still in stack_; promotion discards that storage,
      // so they must be manually destroyed first; otherwise it both leaks and makes
      // dest's size_ disagree with the actual number of elements.
      if (is_source_stack) {
        if constexpr (!move_path && !std::is_trivially_destructible_v<value_type>) {
          if (!source.empty()) {
            source.for_each_occupied_slot_unchecked_([](value_type *occupied_ptr, group_type *, unsigned) {
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
    size_type actual_demand_capacity = required_capacity_(this->size_);
    if (actual_demand_capacity > count) count = actual_demand_capacity;
    if (count > 0) count = aligned_capacity_(count);
    if (count != capacity_unchecked_()) {
      hash_table new_storage { this->hasher_, this->key_eq_, this->alloc_ };
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
    if (auto hash = this->hasher_(key_val); elements_address_() != nullptr) [[likely]] {
      // Check for duplicates first
      if (it = find_fingerprint_slot_unchecked_(key_val, hash); it != end()) {
        return { it, false };
      }

      if (this->size_ < this->max_load_) [[likely]] {
        it = emplace_at_available_slot_unchecked_(hash, std::forward<Args>(args)...);
      } else {
        // Max load reached: stack storage is promoted to heap, or the heap is expanded
        hash_table new_storage { this->hasher_, this->key_eq_, this->alloc_ };
        if constexpr (inline_capacity != 0) new_storage.promote_to_heap_();
        new_storage.allocate_unchecked_(required_capacity_for_growth_(this->size_));

        it = new_storage.emplace_at_available_slot_unchecked_(hash, std::forward<Args>(args)...);
        // Keep the pre-inserted element counted so an exception during
        // force_rehash_ cannot leave it unconstructed but undestroyed.
        new_storage.size_ = 1;

        // Note: force_rehash_ internally handles the case where source is promoted from stack to heap.
        // Do not promote *this early here, otherwise the elements pending migration in stack_ will be lost.
        force_rehash_(*this, new_storage);
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
    insert(std::ranges::begin(std::forward<R>(range)), std::ranges::end(std::forward<R>(range)));
  }

public:
  [[nodiscard]] constexpr iterator find(key_type const &key_val)
  {
    if (empty()) [[unlikely]] return end();
    return find_fingerprint_slot_unchecked_(key_val, this->hasher_(key_val));
  }

  [[nodiscard]] constexpr const_iterator find(key_type const &key_val) const
  {
    if (empty()) [[unlikely]] return end();
    return find_fingerprint_slot_unchecked_(key_val, this->hasher_(key_val));
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
    return find_fingerprint_slot_unchecked_(key_val, this->hasher_(key_val));
  }

  template <class K> requires heterogeneous_key<K const &, hasher, key_equal, iterator, const_iterator>
  [[nodiscard]] constexpr const_iterator find(K const &key_val) const
  {
    if (empty()) [[unlikely]] return end();
    return find_fingerprint_slot_unchecked_(key_val, this->hasher_(key_val));
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
    assert(position != end() && "erase: position must be dereferenceable (not end())");
    auto element_ptr = position.element_address_();
    auto group_ptr   = position.group_address_();
    destroy_element_unchecked_(element_ptr);
    this->max_load_ -= group_ptr->is_overflow_by_slot(position.slot_index_);
    group_ptr->reset(position.slot_index_);
    --this->size_;
    return { position };
  }
  constexpr iterator erase(const_iterator first, const_iterator last) noexcept
  {
    while (first != last) {
      first = erase(first);
    }
    return iterator(const_iterator_cast_tag {}, last);
  }
  constexpr size_type erase(key_type const& key_val)
  {
    auto it = find(key_val);
    return it != end() ? (erase(it), 1) : 0;
  }
  template <class K> requires heterogeneous_key<K, hasher, key_equal, iterator, const_iterator>
  constexpr size_type erase(K &&key_val)
  {
    auto it = find(key_val);
    return it != end() ? (erase(it), 1) : 0;
  }

private:
  template <class H2, class KE2>
  struct erase_guard
  {
    constexpr ~erase_guard()
    {
      if (!dismissed_) holder_.erase(element_iterator_);
    }

    constexpr void dismiss() noexcept { dismissed_ = true; }

    hash_table<Traits, H2, KE2, Allocator> &holder_;
    const_iterator element_iterator_;
    bool dismissed_ = false;
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

    constexpr value_type &value() & noexcept
    {
      assert(has_value_ && "node: empty node has no value");
      return value_;
    }

    constexpr value_type const &value() const & noexcept
    {
      assert(has_value_ && "node: empty node has no value");
      return value_;
    }

    constexpr value_type &&value() && noexcept
    {
      assert(has_value_ && "node: empty node has no value");
      return std::move(value_);
    }

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

    // emplace_with_key_ already probes once: on duplicates it returns without
    // touching val, so the node is still full and goes back intact.
    auto [it, ok] = emplace_with_key_(key, std::move(val));
    if (!ok) return { it, false, std::move(node) };
    return { it, true, node_type {} };
  }

  constexpr iterator insert(const_iterator /*hint*/, node_type &&node)
  {
    return insert(std::move(node)).position;
  }

  constexpr node_type extract(const_iterator position)
  {
    assert(position != end() && "extract: position must be dereferenceable (not end())");
    auto *element_ptr = position.element_address_();
    node_type node { std::move(*element_ptr) };
    [[maybe_unused]] erase_guard e { *this, position };
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
      erase_guard<H2, KE2> e { other, const_iterator { occupied_ptr, group_ptr, slot_index } };
      if (!emplace(std::move(*occupied_ptr)).second) e.dismiss();
    });
  }

  template <class H2, class KE2>
  constexpr void merge(hash_table<Traits, H2, KE2, Allocator> &&other) { merge(other); }

public:
  friend bool operator== (hash_table const &a, hash_table const &b)
  {
    if (std::addressof(a) == std::addressof(b)) return true;
    if (a.size_ != b.size_) return false;
    // Fresh heap tables hold null storage; there is nothing to walk when empty.
    if (a.empty()) return true;

    value_type *a_elements = a.elements_address_();
    group_type *a_groups   = a.groups_address_();
    group_type *a_sentinel = a_groups + a.group_count_() - 1;
    for (size_type group_index = 0; group_index < a.group_count_(); ++group_index) {
      group_type *group_ptr = a_groups + group_index;
      auto occupied_mask    = group_type::occupied_mask_excluding_sentinel(group_ptr, a_sentinel);
      while (occupied_mask) {
        unsigned slot_index     = static_cast<unsigned>(countr_zero_unchecked_(static_cast<unsigned>(occupied_mask)));
        value_type *element_ptr = a_elements + group_index * group_size_() + slot_index;
        auto it                 = b.find(Traits::key_fn(*element_ptr));
        if (it == b.end() || !(*it == *element_ptr)) return false;
        occupied_mask &= occupied_mask - 1;
      }
    }
    return true;
  }
  friend bool operator!= (hash_table const& a, hash_table const& b) { return !(a == b); }

private:
  static constexpr const_iterator make_const_iterator_(value_type const *e, group_type const *g, size_type slot) noexcept
  {
    return const_iterator(const_cast<value_type *>(e), const_cast<group_type *>(g), slot);
  }

public:
  /// Non-owning read-only descriptor over an open-addressing table's live storage.
  /// The view borrows the table's element and group arrays and must not outlive
  /// them; it exists so constant-evaluated / statically-stored tables can be
  /// queried through a small pointer-only handle.
  class const_view
  {
  public:
    using key_type        = typename hash_table::key_type;
    using value_type      = typename hash_table::value_type;
    using size_type       = typename hash_table::size_type;
    using difference_type = typename hash_table::difference_type;
    using hasher          = typename hash_table::hasher;
    using key_equal       = typename hash_table::key_equal;
    using reference       = typename hash_table::const_reference;
    using const_reference = typename hash_table::const_reference;
    using pointer         = typename hash_table::const_pointer;
    using const_pointer   = typename hash_table::const_pointer;
    using iterator        = typename hash_table::const_iterator;
    using const_iterator  = typename hash_table::const_iterator;

    const_view() = delete;

    // observers
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] constexpr size_type size() const noexcept { return size_; }
    [[nodiscard]] constexpr size_type bucket_count() const noexcept
    {
      return groups_ptr_ == nullptr ? 0 : (group_count_mask_ + 1) * group_type::capacity() - 1;
    }
    [[nodiscard]] constexpr size_type capacity() const noexcept { return bucket_count(); }
    [[nodiscard]] constexpr float load_factor() const noexcept
    {
      size_type const cap = bucket_count();
      return cap == 0 ? 0.F : static_cast<float>(size_) / static_cast<float>(cap);
    }
    [[nodiscard]] constexpr float max_load_factor() const noexcept { return 0.875F; }
    [[nodiscard]] constexpr hasher hash_function() const noexcept { return hasher_; }
    [[nodiscard]] constexpr key_equal key_eq() const noexcept { return key_eq_; }

    // iteration (via group metadata; skips holes and the sentinel)
    [[nodiscard]] constexpr const_iterator begin() const noexcept
    {
      if (empty() || groups_ptr_ == nullptr) return end();
      const_iterator it = hash_table::make_const_iterator_(elements_ptr_, groups_ptr_, size_type { 0 });
      return (groups_ptr_[0].occupied_mask() & 0x1) ? it : ++it;
    }
    [[nodiscard]] constexpr const_iterator end() const noexcept { return const_iterator {}; }
    [[nodiscard]] constexpr const_iterator cbegin() const noexcept { return begin(); }
    [[nodiscard]] constexpr const_iterator cend() const noexcept { return end(); }

    // lookup
    [[nodiscard]] constexpr const_iterator find(key_type const &key) const { return find_impl_(key); }
    [[nodiscard]] constexpr bool contains(key_type const &key) const { return find_impl_(key) != end(); }
    [[nodiscard]] constexpr size_type count(key_type const &key) const { return contains(key) ? 1U : 0U; }
    [[nodiscard]] constexpr std::pair<const_iterator, const_iterator> equal_range(key_type const &key) const
    {
      const_iterator const it = find_impl_(key);
      return { it, it != end() ? std::next(it) : it };
    }

    template <class K> requires heterogeneous_key<K const &, hasher, key_equal, iterator, const_iterator>
    [[nodiscard]] constexpr const_iterator find(K const &key) const { return find_impl_(key); }
    template <class K> requires heterogeneous_key<K const &, hasher, key_equal, iterator, const_iterator>
    [[nodiscard]] constexpr bool contains(K const &key) const { return find_impl_(key) != end(); }
    template <class K> requires heterogeneous_key<K const &, hasher, key_equal, iterator, const_iterator>
    [[nodiscard]] constexpr size_type count(K const &key) const { return contains(key) ? 1U : 0U; }
    template <class K> requires heterogeneous_key<K const &, hasher, key_equal, iterator, const_iterator>
    [[nodiscard]] constexpr std::pair<const_iterator, const_iterator> equal_range(K const &key) const
    {
      const_iterator const it = find_impl_(key);
      return { it, it != end() ? std::next(it) : it };
    }

    // element access / lookup: only for map-like traits (a set's Traits has no
    // mapped_type, matching unordered_flat_set which exposes neither at nor lookup).
    template <class Tr = Traits> requires requires { typename Tr::mapped_type; }
    [[nodiscard]] constexpr typename Tr::mapped_type const &at(key_type const &key) const
    {
      const_iterator const it = find_impl_(key);
      if (it == end()) [[unlikely]] throw std::out_of_range("unordered_flat hash_table::at: key not found");
      return it->second;
    }

    template <class K, class Tr = Traits>
    requires heterogeneous_key<K const &, hasher, key_equal, iterator, const_iterator> && requires { typename Tr::mapped_type; }
    [[nodiscard]] constexpr typename Tr::mapped_type const &at(K const &key) const
    {
      const_iterator const it = find_impl_(key);
      if (it == end()) [[unlikely]] throw std::out_of_range("unordered_flat hash_table::at: key not found");
      return it->second;
    }

#if defined(__cpp_lib_optional) && __cpp_lib_optional >= 202506L
    template <class Tr = Traits> requires requires { typename Tr::mapped_type; }
    [[nodiscard]] constexpr std::optional<typename Tr::mapped_type const &> lookup(key_type const &key) const
    {
      const_iterator const it = find_impl_(key);
      if (it == end()) [[unlikely]] return std::nullopt;
      return it->second;
    }

    template <class K, class Tr = Traits>
    requires heterogeneous_key<K const &, hasher, key_equal, iterator, const_iterator> && requires { typename Tr::mapped_type; }
    [[nodiscard]] constexpr std::optional<typename Tr::mapped_type const &> lookup(K const &key) const
    {
      const_iterator const it = find_impl_(key);
      if (it == end()) [[unlikely]] return std::nullopt;
      return it->second;
    }
#endif

  private:
    friend class hash_table;

    constexpr const_view(value_type const *elements, group_type const *groups, std::uint8_t shift,
                         size_type mask, size_type size, hasher h, key_equal eq) noexcept
        : elements_ptr_ { elements }
        , groups_ptr_ { groups }
        , group_index_shift_ { shift }
        , group_count_mask_ { mask }
        , size_ { size }
        , hasher_ { h }
        , key_eq_ { eq }
    {
    }

    // Same fingerprint probe as find_fingerprint_slot_unchecked_, re-expressed over
    // the view's read-only raw pointers (holes are never touched: only occupied
    // slots are probed).
    template <class K>
    [[nodiscard]] constexpr const_iterator find_impl_(K const &key) const
    {
      if (empty() || elements_ptr_ == nullptr || groups_ptr_ == nullptr) [[unlikely]] return end();
      size_type const hash = static_cast<size_type>(hasher_(key));

      prober_type prober(static_cast<size_type>(hash >> group_index_shift_));
      do [[likely]] {
        size_type const position          = prober.position();
        group_type const *const group_ptr = groups_ptr_ + position;
        if (int fingerprint_mask = group_ptr->fingerprint_mask(hash)) {
          value_type const *const element_ptr = elements_ptr_ + position * group_size_();
          if (!std::is_constant_evaluated()) {
#if defined(_MSC_VER)
            _mm_prefetch(static_cast<char const *>(static_cast<void const *>(element_ptr)), _MM_HINT_T0);
#elif defined(__GNUC__) || defined(__clang__)
            __builtin_prefetch(element_ptr, 0, 3);
#endif
          }
          do {
            unsigned const slot_index = static_cast<unsigned>(hash_table::countr_zero_unchecked_(static_cast<unsigned>(fingerprint_mask)));
            if (key_eq_(key, Traits::key_fn(element_ptr[slot_index]))) [[likely]] {
              return hash_table::make_const_iterator_(element_ptr + slot_index, group_ptr, slot_index);
            }
            fingerprint_mask &= fingerprint_mask - 1;
          } while (fingerprint_mask);
        }
        if (!group_ptr->is_overflow_by_hash(hash)) [[likely]] {
          return end();
        }
      } while (prober.next(group_count_mask_));

      return end();
    }

    value_type const *elements_ptr_ = nullptr;
    group_type const *groups_ptr_   = nullptr;
    std::uint8_t group_index_shift_ = 0;
    size_type group_count_mask_     = 0;
    size_type size_                 = 0;
    [[no_unique_address]] hasher hasher_ {};
    [[no_unique_address]] key_equal key_eq_ {};
  };

  [[nodiscard]] constexpr const_view as_const_view() const noexcept
  {
    return const_view { elements_address_(), groups_address_(), static_cast<std::uint8_t>(this->group_index_shift_),
                        this->group_count_mask_, this->size(), this->hash_function(), this->key_eq() };
  }

#if defined(__cpp_lib_define_static)
  /// Bake the table's element and group arrays into static storage and return a
  /// read-only view over them that is independent of `src`'s lifetime.
  ///
  /// Requires a structural value_type (e.g. a pair of scalars) so P3491 can turn
  /// the element array into a template argument; `group_type` was made structural
  /// for the same reason.
  [[nodiscard]] static consteval const_view freeze(hash_table const &src)
  {
    static_assert(std::is_structural_v<value_type>,
                  "unordered_flat freeze(): value_type must be a structural type (P3491 std::define_static_array)");
    static_assert(std::is_structural_v<group_type>,
                  "unordered_flat freeze(): group_type must be a structural type (P3491 std::define_static_array)");
    size_type const group_count = src.groups_address_() != nullptr ? src.group_count_() : 0;

    std::vector<value_type> elements;
    if (group_count != 0 && src.size() != 0) {
      size_type const capacity = src.capacity_unchecked_();

      // Open addressing leaves unoccupied element slots uninitialised, but the
      // whole array is copied into static storage, so every slot must hold a
      // live value. Fill the holes with a copy of any live element (a view never
      // reads hole slots), then write the real elements back into their slots.
      if constexpr (std::is_copy_assignable_v<value_type>) {
        value_type const *filler = nullptr;
        (void) src.try_for_each_occupied_slot_unchecked_([&](value_type *element, group_type *, unsigned) {
          filler = element;
          return false; // stop at the first live element
        });
        elements.assign(capacity, *filler);
        src.for_each_occupied_slot_unchecked_([&](value_type *element, group_type *group, unsigned slot) {
          elements[static_cast<size_type>(group - src.groups_address_()) * group_size_() + slot] = *element;
        });
      } else {
        // Structural types without copy assignment: record each slot's source and
        // copy-construct every slot instead (no assignment required).
        std::vector<value_type const *> slot_source(capacity, nullptr);
        src.for_each_occupied_slot_unchecked_([&](value_type *element, group_type *group, unsigned slot) {
          slot_source[static_cast<size_type>(group - src.groups_address_()) * group_size_() + slot] = element;
        });
        value_type const *filler = nullptr;
        for (value_type const *p : slot_source) {
          if (p != nullptr) {
            filler = p;
            break;
          }
        }
        elements.reserve(capacity);
        for (size_type i = 0; i < capacity; ++i) {
          elements.push_back(slot_source[i] != nullptr ? *slot_source[i] : *filler);
        }
      }
    }

    auto const baked_elements = std::define_static_array(std::span<value_type const>(elements.data(), elements.size()));
    auto const baked_groups   = std::define_static_array(std::span<group_type const>(src.groups_address_(), group_count));

    return const_view { baked_elements.data(), baked_groups.data(), static_cast<std::uint8_t>(src.group_index_shift_),
                        src.group_count_mask_, src.size(), src.hash_function(), src.key_eq() };
  }
#endif
};
} // namespace stdex::unordered_flat_detail
