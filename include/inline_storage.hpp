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

#include <algorithm>   // std::copy_n, std::fill_n, std::ranges::copy_n
#include <cassert>     // assert
#include <compare>     // std::common_comparison_category_t, std::strong_ordering
#include <concepts>    // std::constructible_from
#include <cstddef>     // std::size_t
#include <cstdlib>     // std::abort
#include <iterator>    // std::reverse_iterator
#include <limits>      // std::numeric_limits
#include <memory>      // std::allocator, std::allocator_traits, std::to_address,
                       // std::construct_at, std::destroy_at
#include <stdexcept>   // std::length_error
#include <type_traits> // std::is_unsigned_v, std::is_nothrow_default_constructible_v
#include <utility>     // std::forward

#include "pointer_iterator.hpp"
#include "uninitialized_algorithms.hpp"

#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wattributes"
#elif defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wattributes"
#elif defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable : 5030)
#endif

namespace stdex
{
struct stay_on_heap_t
{
  explicit constexpr stay_on_heap_t() = default;
};

inline constexpr stay_on_heap_t stay_on_heap {};

template <class T, size_t N, class Allocator = std::allocator<T>>
struct inline_storage
{
public:
  using value_type       = T;
  using allocator_type   = typename std::allocator_traits<Allocator>::template rebind_alloc<value_type>;
  using allocator_traits = std::allocator_traits<allocator_type>;

  using size_type       = typename allocator_traits::size_type;
  using difference_type = typename allocator_traits::difference_type;
  using const_pointer   = typename allocator_traits::const_pointer;
  using pointer         = typename allocator_traits::pointer;
  using const_reference = value_type const &;
  using reference       = value_type &;

  using const_iterator         = pointer_iterator<value_type, const_pointer, inline_storage>;
  using iterator               = pointer_iterator<value_type, pointer, inline_storage>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;
  using reverse_iterator       = std::reverse_iterator<iterator>;

  static constexpr size_type inline_capacity = N;

  /// LSB steals one bit for the heap/stack flag so the encoding is independent
  /// of the width of size_type; 0 = stack keeps zero-initialization valid.
  static constexpr size_type heap_flag  = size_type { 1 };
  static constexpr size_type stack_flag = size_type { 0 };
  static_assert(std::is_unsigned_v<size_type>, "size_type must be an unsigned integer type");
  static_assert(std::is_object_v<value_type>, "inline_storage requires an object value_type");
  static_assert(std::is_nothrow_destructible_v<value_type>, "value_type destructors must not throw");
  static_assert(std::is_same_v<typename allocator_traits::value_type, value_type>,
                "rebound allocator value_type must match T");

protected:
  static constexpr bool is_pocca_v        = allocator_traits::propagate_on_container_copy_assignment::value;
  static constexpr bool is_pocma_v        = allocator_traits::propagate_on_container_move_assignment::value;
  static constexpr bool is_pocs_v         = allocator_traits::propagate_on_container_swap::value;
  static constexpr bool is_always_equal_v = allocator_traits::is_always_equal::value;

  static constexpr bool allocators_equal_(allocator_type const &left, allocator_type const &right) noexcept
  {
    if constexpr (allocator_traits::is_always_equal::value) {
      return true;
    } else {
      return left == right;
    }
  }

  constexpr std::pair<pointer, size_type> allocate_unchecked_(size_type const required_capacity)
  {
#if __cpp_lib_allocate_at_least
    auto [elements_ptr, new_capacity] = allocator_traits::allocate_at_least(alloc_, required_capacity);
    return { elements_ptr, new_capacity };
#else
    return { allocator_traits::allocate(alloc_, required_capacity), required_capacity };
#endif
  }

  struct allocation_guard
  {
    pointer data_;
    size_type size_;
    allocator_type &alloc_;

    allocation_guard &operator= (allocation_guard const &) = delete;
    allocation_guard &operator= (allocation_guard &&)      = delete;

    constexpr ~allocation_guard() noexcept
    {
      if (data_ != pointer {}) {
        allocator_traits::deallocate(alloc_, data_, size_);
      }
    }

    constexpr void dismiss() noexcept { data_ = pointer {}; }
  };

  struct reallocation_guard
  {
    pointer data_;
    size_type capacity_;
    value_type *constructed_first_;
    value_type *constructed_last_;
    allocator_type &alloc_;

    reallocation_guard &operator= (reallocation_guard const &) = delete;
    reallocation_guard &operator= (reallocation_guard &&)      = delete;

    constexpr ~reallocation_guard() noexcept
    {
      if (data_ != pointer {}) {
        if constexpr (std::disjunction_v<std::negation<std::is_trivially_destructible<value_type>>,
                                         std::bool_constant<has_custom_destroy<allocator_type, value_type *>>>) {
          for (value_type *current = constructed_first_; current != constructed_last_; ++current) {
            allocator_traits::destroy(alloc_, current);
          }
        }
        allocator_traits::deallocate(alloc_, data_, capacity_);
      }
    }

    constexpr void dismiss() noexcept { data_ = pointer {}; }
  };

  /// Owns an already-constructed range inside current storage (nothing to
  /// deallocate, unlike reallocation_guard): destroys it on unwind unless
  /// dismissed. Covers discontiguous liveness that size_ cannot express, e.g.
  /// the relocated tail in insert_counted_range_ before the gap is filled.
  struct destruction_guard
  {
    value_type *constructed_first_;
    value_type *constructed_last_;
    allocator_type &alloc_;
    bool use_allocator_;
    bool dismissed_ = false;

    destruction_guard &operator= (destruction_guard const &) = delete;
    destruction_guard &operator= (destruction_guard &&)      = delete;

    constexpr ~destruction_guard() noexcept
    {
      if (dismissed_) return;
      if (use_allocator_) {
        if constexpr (std::disjunction_v<std::negation<std::is_trivially_destructible<value_type>>,
                                         std::bool_constant<has_custom_destroy<allocator_type, value_type *>>>) {
          for (value_type *current = constructed_first_; current != constructed_last_; ++current) {
            allocator_traits::destroy(alloc_, current);
          }
        }
      } else if constexpr (!std::is_trivially_destructible_v<value_type>) {
        std::destroy(constructed_first_, constructed_last_);
      }
    }

    constexpr void dismiss() noexcept { dismissed_ = true; }
  };

  constexpr void initialize_storage_() noexcept
  {
    if constexpr (inline_capacity == 0) {
      std::construct_at(&heap_);
    } else {
#if defined(__cpp_lib_start_lifetime) && __cpp_lib_start_lifetime >= 202603L
      std::start_lifetime(stack_);
#else
      std::construct_at(&stack_);
#endif
    }
  }

public:
  template <class Al = allocator_type>
  requires std::constructible_from<allocator_type, Al>
  explicit constexpr inline_storage(Al &&alloc) noexcept(std::is_nothrow_constructible_v<allocator_type, Al>)
      : alloc_ { std::forward<Al>(alloc) }
  {
    initialize_storage_();
  }

protected:
  template <typename... Args>
  requires std::constructible_from<value_type, Args...> &&
           requires (allocator_type &a, value_type *p, Args &&...args) {
             allocator_traits::construct(a, p, std::forward<Args>(args)...);
           }
  constexpr void construct_element_unchecked_(value_type *const element_ptr, Args &&...args) noexcept(
      noexcept(allocator_traits::construct(
          std::declval<allocator_type &>(),
          std::declval<value_type *>(),
          std::declval<Args>()...)) &&
      noexcept(std::construct_at(
          std::declval<value_type *>(),
          std::declval<Args>()...)))
  {
    assert(element_ptr != nullptr && "construct_element_unchecked_() requires non-null element");
    if (is_heap()) {
      allocator_traits::construct(alloc_, element_ptr, std::forward<Args>(args)...);
    } else {
      std::construct_at(element_ptr, std::forward<Args>(args)...);
    }
  }

  constexpr void construct_elements_unchecked_(value_type *const dst_first, size_type const count)
  requires std::default_initializable<value_type>
  {
    assert((dst_first != nullptr || count == size_type {}) && "construct_elements_unchecked_() requires non-null destination unless empty");
    if (is_heap()) {
      (void) uninitialized_value_construct_n(dst_first, count, alloc_);
    } else {
      (void) uninitialized_value_construct_n(dst_first, count);
    }
  }

  constexpr void construct_elements_unchecked_(value_type *const dst_first, size_type const count, const_reference fill_value)
  {
    assert((dst_first != nullptr || count == size_type {}) && "construct_elements_unchecked_() requires non-null destination unless empty");
    if (is_heap()) {
      (void) uninitialized_fill_n(dst_first, count, fill_value, alloc_);
    } else {
      (void) uninitialized_fill_n(dst_first, count, fill_value);
    }
  }

  template <std::input_iterator SrcIt, std::sentinel_for<SrcIt> SrcSent>
  requires std::constructible_from<value_type, std::iter_reference_t<SrcIt>>
  constexpr void construct_elements_unchecked_(value_type *const dst_first, size_type const count, SrcIt src_first, SrcSent src_last)
  {
    assert((dst_first != nullptr || count == size_type {}) && "construct_elements_unchecked_() requires non-null destination unless empty");
    if constexpr (std::sized_sentinel_for<SrcSent, SrcIt>) {
      assert(count == static_cast<size_type>(src_last - src_first) && "construct_elements_unchecked_() sized range must match count");
    }
    if (is_heap()) {
      (void) uninitialized_copy(src_first, src_last, dst_first, dst_first + count, alloc_);
    } else {
      (void) uninitialized_copy(src_first, src_last, dst_first, dst_first + count);
    }
  }

  template <class... Args>
  constexpr void construct_heap_n_unchecked_(size_type const count, Args &&...args)
  {
    auto const [new_block, new_capacity] = allocate_unchecked_(count);
    allocation_guard alloc_guard { new_block, new_capacity, alloc_ };
    value_type *const raw_new_block = std::to_address(new_block);
    if constexpr (sizeof...(Args) == 0) {
      (void) uninitialized_value_construct_n(raw_new_block, count, alloc_);
    } else if constexpr (sizeof...(Args) == 1) {
      if constexpr ((std::input_iterator<std::remove_cvref_t<Args>> && ...)) {
        (void) uninitialized_copy_n(std::forward<Args>(args)..., count, raw_new_block, raw_new_block + count, alloc_);
      } else {
        (void) uninitialized_fill_n(raw_new_block, count, std::forward<Args>(args)..., alloc_);
      }
    } else if constexpr (sizeof...(Args) == 2) {
      (void) uninitialized_copy(std::forward<Args>(args)..., raw_new_block, raw_new_block + count, alloc_);
    } else {
      static_assert(sizeof...(Args) <= 2, "unexpected number of arguments");
    }
    alloc_guard.dismiss();

    heap_.elements_ptr_ = new_block;
    heap_.capacity_     = new_capacity;
    set_size_(count, heap_flag);
  }

  template <class... Args>
  constexpr bool try_construct_stack_n_unchecked_(size_type const count, Args &&...args)
  {
    if constexpr (inline_capacity == 0) {
      return false;
    } else {
      if (count > inline_capacity) return false;

      if constexpr (sizeof...(Args) == 0) {
        (void) uninitialized_value_construct_n(stack_.elements_, count);
      } else if constexpr (sizeof...(Args) == 1) {
        if constexpr ((std::input_iterator<std::remove_cvref_t<Args>> && ...)) {
          (void) uninitialized_copy_n(std::forward<Args>(args)..., count, stack_.elements_, stack_.elements_ + count);
        } else {
          (void) uninitialized_fill_n(stack_.elements_, count, std::forward<Args>(args)...);
        }
      } else if constexpr (sizeof...(Args) == 2) {
        (void) uninitialized_copy(std::forward<Args>(args)..., stack_.elements_, stack_.elements_ + count);
      } else {
        static_assert(sizeof...(Args) <= 2, "unexpected number of arguments");
      }
      set_size_(count, stack_flag);
      return true;
    }
  }

public:
  explicit constexpr inline_storage(size_type n, allocator_type const &alloc = allocator_type {})
      : inline_storage { alloc }
  {
    if (n > max_size()) [[unlikely]] throw_length_error_();
    if (n == 0) [[unlikely]] return;

    if constexpr (inline_capacity != 0) {
      if (try_construct_stack_n_unchecked_(n)) return;
      promote_to_heap_();
    }
    construct_heap_n_unchecked_(n);
  }

  constexpr inline_storage(size_type n, const_reference val, allocator_type const &alloc = allocator_type {})
      : inline_storage { alloc }
  {
    if (n > max_size()) [[unlikely]] throw_length_error_();
    if (n == 0) [[unlikely]] return;

    if constexpr (inline_capacity != 0) {
      if (try_construct_stack_n_unchecked_(n, val)) return;
      promote_to_heap_();
    }
    construct_heap_n_unchecked_(n, val);
  }

protected:
  template <class... Args>
  constexpr value_type* emplace_reallocate_(Args &&...args)
  {
    assert(size() == capacity() && "emplace_reallocate_() requires full storage");
    size_type const old_size = size();
    if (old_size >= max_size()) [[unlikely]] {
      throw_length_error_();
    }

    size_type const new_size = old_size + 1;
    size_type const required_capacity = calculate_growth_(new_size);
    auto const [new_block, new_capacity] = allocate_unchecked_(required_capacity);
    value_type *const new_ptr = std::to_address(new_block);
    allocation_guard alloc_guard { new_block, new_capacity, alloc_ };
     
    value_type *const old_data = data();
    // Args may alias old storage (e.g. push_back(front())), so the new tail is
    // materialized before old elements move. The destination is always heap, hence
    // the allocator path; construct_element_unchecked_ would follow the old flag.
    uninitialized_backout_al<allocator_type, value_type *> tail_guard { new_ptr + old_size, alloc_ };
    tail_guard.emplace_back(std::forward<Args>(args)...);
    transfer_unchecked_(old_data, old_size, new_ptr, alloc_);
    tail_guard.release();
    alloc_guard.dismiss();

    destroy_elements_unchecked_(old_data, old_size);
    if (is_heap()) {
      if (heap_.elements_ptr_ != pointer {}) deallocate_unchecked_();
    } else if constexpr (inline_capacity != 0) {
      promote_to_heap_();
    }

    heap_.elements_ptr_ = new_block;
    heap_.capacity_     = new_capacity;
    set_size_(new_size, heap_flag);
    return new_ptr + old_size;
  }


  template <class... Args>
  constexpr reference emplace_back_with_unused_capacity_(Args &&...args)
  {
    assert(size() < capacity());
    size_type const count = size();
    value_type *const dst = data() + count;
    construct_element_unchecked_(dst, std::forward<Args>(args)...);
    set_size_(count + 1, get_flag_());
    return *dst;
  }

  template <class... Args>
  constexpr reference emplace_one_at_back_(Args &&...args)
  {
    if (size() < capacity()) {
      return emplace_back_with_unused_capacity_(std::forward<Args>(args)...);
    }

    return *emplace_reallocate_(std::forward<Args>(args)...);
  }

  template <class InIt, class InSent>
  constexpr void append_uncounted_range_(InIt first, InSent const last)
  {
    for (; first != last; ++first) {
      emplace_one_at_back_(*first);
    }
  }

  template <std::input_iterator InIt, std::sentinel_for<InIt> InSent>
  requires std::constructible_from<value_type, std::iter_reference_t<InIt>>
  constexpr void insert_uncounted_range_(size_type const pos, InIt first, InSent const last)
  {
    assert(pos <= size() && "insert_uncounted_range_() position out of range");
    if (first == last) [[unlikely]] return;
    size_type const old_size = size();
    append_uncounted_range_(first, last);
    value_type *const storage = data();
    std::rotate(storage + pos, storage + old_size, storage + size());
  }

  template <std::input_iterator InIt>
  [[nodiscard]] constexpr bool source_overlaps_storage_(InIt first, size_type const count) const noexcept
  {
    // Same value type only: cross-type contiguous sources (e.g. narrower
    // unsigned block ranges) have no element-pointer conversion, and cannot
    // alias through well-defined code, so there is nothing to detect.
    if constexpr (std::contiguous_iterator<InIt> &&
                  std::same_as<std::remove_cv_t<std::iter_value_t<InIt>>, value_type>) {
      if (count == 0 || empty()) return false;
      value_type const *const src_first = std::to_address(first);
      value_type const *const dst_first = data();
      value_type const *const dst_last  = dst_first + size();
      // A source starting inside our storage necessarily overlaps (this also
      // avoids forming src + count past our end); otherwise both ends are
      // compared, which is valid because the source range is a valid range by
      // contract. Unrelated-pointer comparison is implementation-defined but
      // correct on flat address spaces, as assumed throughout.
      if (src_first >= dst_first && src_first < dst_last) return true;
      return src_first < dst_first && dst_first < src_first + count;
    } else {
      return false;
    }
  }

  template <std::input_iterator InIt>
  requires std::constructible_from<value_type, std::iter_reference_t<InIt>> &&
           std::assignable_from<reference, std::iter_reference_t<InIt>>
  constexpr void insert_counted_range_(size_type const pos, InIt first, size_type const count)
  {
    assert(pos <= size() && "insert_counted_range_() position out of range");
    if (count == 0) [[unlikely]] return;
    size_type const old_size = size();
    if (count > max_size() - old_size) [[unlikely]] throw_length_error_();

    size_type const new_size = old_size + count;
    // A fitting but overlapping source takes the reallocation path too,
    // trading one allocation for correctness (gap-first stays alias-safe).
    if (new_size > capacity() || source_overlaps_storage_(first, count)) {
      value_type *const old_data = data();
      size_type const growth     = calculate_growth_(new_size);
      auto const [new_block, new_capacity] = allocate_unchecked_(growth);
      value_type *const raw_new   = std::to_address(new_block);
      value_type *const gap_first = raw_new + pos;

      reallocation_guard guard { new_block, new_capacity, gap_first, gap_first, alloc_ };
      guard.constructed_last_  = uninitialized_copy_n(first, count, gap_first, gap_first + count, alloc_).out;
      transfer_unchecked_(old_data, pos, raw_new, alloc_);
      guard.constructed_first_ = raw_new;
      transfer_unchecked_(old_data + pos, old_size - pos, gap_first + count, alloc_);
      guard.dismiss();

      destroy_elements_unchecked_(old_data, old_size);
      if (is_heap()) {
        if (heap_.elements_ptr_ != pointer {}) deallocate_unchecked_();
      } else if constexpr (inline_capacity != 0) {
        promote_to_heap_();
      }

      heap_.elements_ptr_ = new_block;
      heap_.capacity_     = new_capacity;
      set_size_(new_size, heap_flag);
      return;
    }

    // In-place flow (fits, no overlap): relocate the tail right by count, then
    // fill the vacated gap. Only one pass over existing elements, and a single
    // sequential pass over the source (shared-state inputs stay correct).
    // Basic guarantee: a throwing element copy leaves moved/partial state.
    value_type *const storage = data();
    value_type *const pos_ptr = storage + pos;
    value_type *const old_end = storage + old_size;
    size_type const tail      = old_size - pos;
    if (count <= tail) {
      if (is_heap()) {
        transfer_unchecked_(old_end - count, count, old_end, alloc_);
      } else {
        transfer_unchecked_(old_end - count, count, old_end);
      }
      set_size_(new_size, get_flag_());
      std::move_backward(pos_ptr, old_end - count, old_end);
      std::copy_n(first, count, pos_ptr);
    } else {
      size_type const spill      = count - tail;
      value_type *const shifted  = old_end + spill;
      if (is_heap()) {
        transfer_unchecked_(pos_ptr, tail, shifted, alloc_);
      } else {
        transfer_unchecked_(pos_ptr, tail, shifted);
      }
      // The relocated tail leaves discontiguous liveness ([pos, old_end) plus
      // the copies past old_end) that size_ cannot express: a destruction
      // guard owns the copies until the gap is filled.
      destruction_guard tail_holder { shifted, shifted + tail, alloc_, is_heap() };
      value_type *dst = pos_ptr;
      for (size_type i = 0; i < tail; ++i, ++dst, ++first) {
        *dst = *first;
      }
      if (is_heap()) {
        (void) uninitialized_copy_n(first, spill, old_end, old_end + spill, alloc_);
      } else {
        (void) uninitialized_copy_n(first, spill, old_end, old_end + spill);
      }
      tail_holder.dismiss();
      set_size_(new_size, get_flag_());
    }
  }

  template <std::input_iterator InIt, std::sentinel_for<InIt> InSent>
  requires std::constructible_from<value_type, std::iter_reference_t<InIt>>
  constexpr void insert_range_(size_type const pos, InIt first, InSent const last)
  {
    assert(pos <= size() && "insert_range_() position out of range");
    if constexpr (std::sized_sentinel_for<InSent, InIt>) {
      insert_counted_range_(pos, first, static_cast<size_type>(last - first));
    } else if constexpr (std::forward_iterator<InIt>) {
      // ranges::distance: the sentinel may differ from the iterator
      // (non-common forward ranges); std::distance requires one type.
      insert_counted_range_(pos, first, static_cast<size_type>(std::ranges::distance(first, last)));
    } else {
      insert_uncounted_range_(pos, first, last);
    }
  }

public:
  template <std::input_iterator InputIt>
  constexpr inline_storage(InputIt first, InputIt last, allocator_type const &alloc = allocator_type {})
      : inline_storage { alloc }
  {
    if constexpr (std::forward_iterator<InputIt>) {
      auto const n = static_cast<size_type>(std::distance(first, last));
      if (n > max_size()) [[unlikely]] throw_length_error_();
      if (n == 0) [[unlikely]] return;

      if constexpr (inline_capacity != 0) {
        if (try_construct_stack_n_unchecked_(n, first, last)) return;
        promote_to_heap_();
      }
      construct_heap_n_unchecked_(n, first, last);
    } else {
      append_uncounted_range_(first, last);
    }
  }


protected:
  constexpr void destroy_element_unchecked_(value_type *const element_ptr) noexcept
  {
    assert(element_ptr != nullptr && "destroy_element_unchecked_() requires non-null element");
    if (is_heap()) {
      if constexpr (std::disjunction_v<std::negation<std::is_trivially_destructible<value_type>>,
                                       std::bool_constant<has_custom_destroy<allocator_type, value_type *>>>) {
        allocator_traits::destroy(alloc_, element_ptr);
      }
    } else if constexpr (!std::is_trivially_destructible_v<value_type>) {
      std::destroy_at(element_ptr);
    }
    // Don't decrement by yourself, leave it to the outside to handle, to avoid the overhead
    // of repeatedly calling the decrement instruction in destroy_elements_unchecked_.
  }

  constexpr void destroy_elements_unchecked_(value_type *const elements_ptr, size_type const n) noexcept
  {
    if (is_heap()) {
      if constexpr (std::disjunction_v<std::negation<std::is_trivially_destructible<value_type>>, 
                                       std::bool_constant<has_custom_destroy<allocator_type, value_type *>>>) {
        for (size_type i = 0; i < n; ++i) {
          allocator_traits::destroy(alloc_, elements_ptr + i);
        }
      }
    } else if constexpr (!std::is_trivially_destructible_v<value_type>) {
      for (size_type i = 0; i < n; ++i) {
        std::destroy_at(elements_ptr + i);
      }
    }
    // Do not reset size; call the reset_size_() function from outside.
  }

  constexpr void deallocate_unchecked_() noexcept
  {
    assert(is_heap() && "deallocate_unchecked_() requires heap allocation");
    allocator_traits::deallocate(alloc_, heap_.elements_ptr_, heap_.capacity_);
  }

  constexpr void dispose_() noexcept
  {
    destroy_elements_unchecked_(data(), size());
    if (is_heap() && heap_.elements_ptr_ != pointer {}) {
      deallocate_unchecked_();
      heap_.elements_ptr_ = pointer {};
      heap_.capacity_     = size_type {};
    }
    set_size_(size_type {}, get_flag_());
  }

public:
  constexpr ~inline_storage() noexcept { dispose_(); }

protected:
  constexpr void steal_heap_from_(inline_storage &other) noexcept
  {
    assert(other.is_heap() && "steal: destination and source must be heap-resident");

    heap_ = other.heap_;
    size_ = other.size_;

    other.heap_ = {};
    other.set_size_(size_type {}, heap_flag);
  }

  constexpr void copy_construct_from_(inline_storage const &other)
  {
    if (other.empty()) [[unlikely]]
      return;

    size_type const count = other.size();
    if constexpr (inline_capacity == 0) {
      construct_heap_n_unchecked_(count, std::to_address(other.heap_.elements_ptr_));
    } else if (!try_construct_stack_n_unchecked_(count, other.data())) {
      promote_to_heap_();
      construct_heap_n_unchecked_(count, std::to_address(other.heap_.elements_ptr_));
    }
  }

  enum class move_outcome { no_op, heap_steal, move_elements };

  [[nodiscard]] 
  constexpr move_outcome move_construct_from_(inline_storage &&other) noexcept(
      allocator_traits::is_always_equal::value &&
       (inline_capacity == 0 || std::is_nothrow_move_constructible_v<value_type>) )
  {
    if (other.empty()) [[unlikely]]
      return move_outcome::no_op;

    if constexpr (inline_capacity != 0) {
      if (try_construct_stack_n_unchecked_(other.size(), std::make_move_iterator(other.data()))) {
        return move_outcome::move_elements;
      }
      promote_to_heap_();
    }
    steal_heap_from_(other);
    return move_outcome::heap_steal;
  }

  [[nodiscard]] 
  constexpr move_outcome move_construct_from_(inline_storage &&other, allocator_type const &alloc)
  {
    if (other.empty()) [[unlikely]]
      return move_outcome::no_op;

    size_type const count = other.size();
    if constexpr (inline_capacity != 0) {
      if (try_construct_stack_n_unchecked_(count, std::make_move_iterator(other.data()))) {
        return move_outcome::move_elements;
      }
      promote_to_heap_();
    }
    if (allocators_equal_(alloc, other.alloc_)) {
      steal_heap_from_(other);
      return move_outcome::heap_steal;
    } 
    construct_heap_n_unchecked_(count, std::make_move_iterator(std::to_address(other.heap_.elements_ptr_)));
    return move_outcome::move_elements;
  }

  template <std::input_iterator SrcIt>
  constexpr void assign_grow_from_(SrcIt src, size_type const count, bool const was_stack, bool const already_disposed)
  {
    size_type const growth = calculate_growth_(count);
    if (!already_disposed) dispose_();
    if constexpr (inline_capacity != 0) {
      if (was_stack) promote_to_heap_();
    }
    auto const [new_block, new_capacity] = allocate_unchecked_(growth);
    allocation_guard alloc_guard { new_block, new_capacity, alloc_ };
    value_type *const raw_new_block = std::to_address(new_block);
    (void) uninitialized_copy_n(src, count, raw_new_block, raw_new_block + count, alloc_);
    alloc_guard.dismiss();

    heap_.elements_ptr_ = new_block;
    heap_.capacity_     = new_capacity;
    set_size_(count, heap_flag);
  }

  template <std::input_iterator InputIt>
  requires std::constructible_from<value_type, std::iter_reference_t<InputIt>> &&
           std::assignable_from<reference, std::iter_reference_t<InputIt>>
  constexpr void assign_from_range_unchecked_(InputIt other_data, size_type other_size)
  {
    assert(other_size <= capacity() && "in-place assign requires sufficient capacity");
    value_type *const this_data = data();
    size_type const this_size   = size();
    if (other_size > this_size) {
      // Single-pass: the assign loop consumes the first this_size inputs, and
      // the copy result resumes exactly where it stopped for tail construct
      // (ranges::copy_n returns both ends; std::copy_n returns only the output).
      auto const assigned = std::ranges::copy_n(other_data, this_size, this_data);
      if (is_heap()) {
        (void) uninitialized_copy_n(assigned.in, other_size - this_size, 
                                    this_data + this_size, this_data + other_size, alloc_);
      } else {
        (void) uninitialized_copy_n(assigned.in, other_size - this_size, 
                                    this_data + this_size, this_data + other_size);
      }
    } else {
      std::copy_n(other_data, other_size, this_data);
      destroy_elements_unchecked_(this_data + other_size, this_size - other_size);
    }
    set_size_(other_size, get_flag_());
  }

  template <std::input_iterator InIt>
  requires std::constructible_from<value_type, std::iter_reference_t<InIt>> &&
           std::assignable_from<reference, std::iter_reference_t<InIt>>
  constexpr void assign_counted_range_(InIt first, size_type const count)
  {
    if (count > max_size()) [[unlikely]] throw_length_error_();
    if (count > capacity()) {
      assign_grow_from_(first, count, is_stack(), false);
      return;
    }
    assign_from_range_unchecked_(first, count);
  }

  template <std::input_iterator InIt, std::sentinel_for<InIt> InSent>
  requires std::constructible_from<value_type, std::iter_reference_t<InIt>> &&
           std::assignable_from<reference, std::iter_reference_t<InIt>>
  constexpr void assign_uncounted_range_(InIt first, InSent const last)
  {
    value_type *next          = data();
    value_type *const old_end = next + size();
    for (; first != last && next != old_end; ++first, ++next) {
      *next = *first;
    }
    size_type const tail = static_cast<size_type>(old_end - next);
    destroy_elements_unchecked_(next, tail);
    set_size_(size() - tail, get_flag_());
    append_uncounted_range_(first, last);
  }

  template <std::input_iterator InIt, std::sentinel_for<InIt> InSent>
  requires std::constructible_from<value_type, std::iter_reference_t<InIt>> &&
           std::assignable_from<reference, std::iter_reference_t<InIt>>
  constexpr void assign_range_(InIt first, InSent const last)
  {
    if constexpr (std::sized_sentinel_for<InSent, InIt>) {
      assign_counted_range_(first, static_cast<size_type>(last - first));
    } else if constexpr (std::forward_iterator<InIt>) {
      // ranges::distance: the sentinel may differ from the iterator
      // (non-common forward ranges); std::distance requires one type.
      assign_counted_range_(first, static_cast<size_type>(std::ranges::distance(first, last)));
    } else {
      assign_uncounted_range_(first, last);
    }
  }

  constexpr void copy_assignment_from_(inline_storage const &other)
  {
    if (this == std::addressof(other)) [[unlikely]]
      return;

    bool is_already_dispose = false;
    bool const is_this_heap = is_heap();
    if constexpr (is_pocca_v && !is_always_equal_v) {
      if (!allocators_equal_(alloc_, other.alloc_)) {
        if (is_this_heap) {
          dispose_();
          is_already_dispose = true;
        }
        alloc_ = other.alloc_;
      }
    }

    value_type const *const other_data = other.data();
    size_type const other_size   = other.size();
    if (other_size > capacity()) {
      assign_grow_from_(other_data, other_size, !is_this_heap, is_already_dispose);
      return;
    }

    assign_from_range_unchecked_(other_data, other_size);
  }

  [[nodiscard]] 
  constexpr move_outcome move_assignment_from_(inline_storage &&other)
  {
    if (this == std::addressof(other)) [[unlikely]]
      return move_outcome::no_op;

    bool const stealable = is_pocma_v || allocators_equal_(alloc_, other.alloc_);

    size_type const other_size = other.size();
    bool const is_this_stack   = !is_heap();
    bool const keep_stack      = is_this_stack && other_size <= inline_capacity;
    if (stealable && other.is_heap() && !keep_stack) {
      dispose_();
      if constexpr (inline_capacity != 0) {
        if (is_this_stack) promote_to_heap_();
      }
      if constexpr (is_pocma_v) alloc_ = std::move(other.alloc_);
      steal_heap_from_(other);
      return move_outcome::heap_steal;
    }

    bool is_already_dispose = false;
    if constexpr (is_pocma_v && !is_always_equal_v) {
      if (!allocators_equal_(alloc_, other.alloc_)) {
        if (!is_this_stack) {
          dispose_();
          is_already_dispose = true;
        }
        alloc_ = std::move(other.alloc_);
      }
    }

    value_type *const other_data = other.data();
    if (other_size > capacity()) {
      assign_grow_from_(std::make_move_iterator(other_data), other_size, is_this_stack, is_already_dispose);
    } else {
      assign_from_range_unchecked_(std::make_move_iterator(other_data), other_size);
    }
    return move_outcome::move_elements;
  }

public:
  inline_storage(inline_storage &&other)             = delete;
  inline_storage &operator= (inline_storage &&other) = delete;
  inline_storage(inline_storage const &)             = delete;
  inline_storage &operator= (inline_storage const &) = delete;

protected:
  constexpr void swap_stack_elements_(inline_storage &a, inline_storage &b) noexcept(
      std::is_nothrow_swappable_v<value_type> &&
      (std::is_nothrow_move_constructible_v<value_type> ||
       std::is_nothrow_copy_constructible_v<value_type>) )
  {
    if constexpr (inline_capacity != 0) {
      size_type const a_size = a.size(), b_size = b.size();
      size_type const common = (a_size < b_size) ? a_size : b_size;
      size_type const extra  = (a_size > b_size) ? a_size : b_size;
      for (size_type i = 0; i < common; ++i) {
        std::ranges::swap(a.stack_.elements_[i], b.stack_.elements_[i]);
      }
      inline_storage *const src = (a_size > b_size) ? &a : &b;
      inline_storage *const dst = (a_size > b_size) ? &b : &a;
      for (size_type i = common; i < extra; ++i) {
        std::construct_at(std::addressof(dst->stack_.elements_[i]), std::move_if_noexcept(src->stack_.elements_[i]));
        std::destroy_at(std::addressof(src->stack_.elements_[i]));
      }
    }
  }

  constexpr void swap_heap_to_stack_(inline_storage &other) noexcept(
      std::is_nothrow_swappable_v<value_type> &&
      (std::is_nothrow_move_constructible_v<value_type> ||
       std::is_nothrow_copy_constructible_v<value_type>) )
  {
    if constexpr (inline_capacity != 0) {
      heap_block const this_heap = heap_;
      size_type const this_size  = size_;

      demote_to_stack_();
      set_size_(size_type {}, stack_flag);
      swap_stack_elements_(*this, other);
      size_ = other.size_;

      other.promote_to_heap_();
      other.heap_ = this_heap;
      other.size_ = this_size;
    }
  }

public:
  constexpr void swap(inline_storage &other) noexcept(
      std::is_nothrow_swappable_v<allocator_type> &&
      std::is_nothrow_swappable_v<value_type> &&
      (is_pocs_v || allocator_traits::is_always_equal::value) &&
      (std::is_nothrow_move_constructible_v<value_type> ||
       std::is_nothrow_copy_constructible_v<value_type>))
  {
    if (this == std::addressof(other)) [[unlikely]]
      return;

    if constexpr (is_pocs_v) {
      std::ranges::swap(alloc_, other.alloc_);
    } else {
      assert(allocators_equal_(alloc_, other.alloc_) &&
             "[container.requirements.general]/8: swapping containers with unequal "
             "allocators is undefined behavior when propagate_on_container_swap is false.");
    }

    bool const is_this_heap = is_heap(), is_other_heap = other.is_heap();
    if (is_this_heap && is_other_heap) {
      std::ranges::swap(heap_, other.heap_);
      std::ranges::swap(size_, other.size_);
    } else if (is_this_heap && !is_other_heap) {
      swap_heap_to_stack_(other);
    } else if (!is_this_heap && is_other_heap) {
      other.swap_heap_to_stack_(*this);
    } else {
      // Both stack: layouts are identical, only elements and counts move.
      swap_stack_elements_(*this, other);
      std::ranges::swap(size_, other.size_);
    }
  }

protected:
  template <size_t M, class OtherAlloc>
  [[nodiscard]] constexpr bool storage_equal_(inline_storage<value_type, M, OtherAlloc> const &other) const
      noexcept(requires (value_type const &a, value_type const &b) { { a == b } noexcept; })
  {
    if (static_cast<void const *>(this) == static_cast<void const *>(std::addressof(other))) return true;
    size_type const this_size  = size();
    size_type const other_size = static_cast<size_type>(other.size());
    if (this_size != other_size) return false;
    if (this_size == size_type {}) return true;
    value_type const *const this_data = data();
    auto const *const other_data      = other.data();
    for (size_type i = 0; i < this_size; ++i) {
      if (!(this_data[i] == other_data[i])) return false;
    }
    return true;
  }

  template <size_t M, class OtherAlloc>
  requires std::three_way_comparable<value_type>
  [[nodiscard]] constexpr auto storage_compare_(inline_storage<value_type, M, OtherAlloc> const &other) const
      noexcept(requires (value_type const &a, value_type const &b) { { a <=> b } noexcept; })
  {
    // TODO: maybe use memcmp to optimize
    using result_type = std::common_comparison_category_t<std::compare_three_way_result_t<value_type>, std::strong_ordering>;
    if (static_cast<void const *>(this) == static_cast<void const *>(std::addressof(other))) {
      return result_type::equivalent;
    }
    value_type const *const this_data = data();
    auto const *const other_data      = other.data();
    size_type const this_size         = size();
    size_type const other_size        = static_cast<size_type>(other.size());
    size_type const common            = this_size < other_size ? this_size : other_size;
    for (size_type i = 0; i < common; ++i) {
      auto const element_cmp = this_data[i] <=> other_data[i];
      if (element_cmp != 0) return result_type(element_cmp);
    }
    return result_type(this_size <=> other_size);

  }

  [[nodiscard]] static constexpr size_type max_size_(allocator_type const &alloc) noexcept
  {
    return (std::min) (allocator_traits::max_size(alloc), std::numeric_limits<size_type>::max() / 2);
  }

public:
  [[nodiscard]] constexpr allocator_type get_allocator() const noexcept(std::is_nothrow_copy_constructible_v<allocator_type>) { return alloc_; }
  [[nodiscard]] constexpr bool empty() const noexcept { return size() == size_type {}; }
  [[nodiscard]] constexpr size_type size() const noexcept { return static_cast<size_type>(size_ >> 1); }
  [[nodiscard]] constexpr size_type max_size() const noexcept { return max_size_(alloc_); }
  [[nodiscard]] constexpr size_type capacity() const noexcept { return is_heap() ? heap_.capacity_ : inline_capacity; }
  [[nodiscard]] constexpr bool is_heap() const noexcept { return inline_capacity == 0 || get_flag_() != stack_flag; }
  [[nodiscard]] constexpr bool is_stack() const noexcept { return !is_heap(); }

  [[nodiscard]] constexpr value_type const *data() const noexcept
  {
    if (is_heap()) return std::to_address(heap_.elements_ptr_);
    if constexpr (inline_capacity != 0) {
      return stack_.elements_;
    } else {
      return nullptr;
    }
  }

  [[nodiscard]] constexpr value_type *data() noexcept
  {
    if (is_heap()) return std::to_address(heap_.elements_ptr_);
    if constexpr (inline_capacity != 0) {
      return stack_.elements_;
    } else {
      return nullptr;
    }
  }

  [[nodiscard]] constexpr iterator begin() noexcept { return iterator(data()); }
  [[nodiscard]] constexpr const_iterator begin() const noexcept { return const_iterator(data()); }
  [[nodiscard]] constexpr iterator end() noexcept { return iterator(data() + size()); }
  [[nodiscard]] constexpr const_iterator end() const noexcept { return const_iterator(data() + size()); }
  [[nodiscard]] constexpr reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
  [[nodiscard]] constexpr const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
  [[nodiscard]] constexpr reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
  [[nodiscard]] constexpr const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
  [[nodiscard]] constexpr const_iterator cbegin() const noexcept { return begin(); }
  [[nodiscard]] constexpr const_iterator cend() const noexcept { return end(); }
  [[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept { return rbegin(); }
  [[nodiscard]] constexpr const_reverse_iterator crend() const noexcept { return rend(); }

protected:
  /// Stack and heap share the union: writing the first stack element
  /// already clobbers heap_. Keep flag heap until commit so destroy
  /// still uses the allocator path and the guard below can recover.
  struct heap_restore_on_exit
  {
    inline_storage *self_;
    pointer data_;
    size_type capacity_;
    bool dismissed_ = false;

    constexpr ~heap_restore_on_exit() noexcept
    {
      if (!dismissed_) {
        self_->heap_.elements_ptr_ = data_;
        self_->heap_.capacity_     = capacity_;
      }
    }

    constexpr void dismiss() noexcept { dismissed_ = true; }
  };

  template <class... AllocArgs>
  requires (sizeof...(AllocArgs) <= 1) &&
           (std::same_as<std::remove_cvref_t<AllocArgs>, allocator_type> && ...)
  constexpr void transfer_unchecked_(value_type *const src, size_type const count, value_type *const dst, AllocArgs &&...alloc_args)
  {
    if constexpr (std::is_nothrow_move_constructible_v<value_type> || !std::is_copy_constructible_v<value_type>) {
      (void) uninitialized_move(src, src + count, dst, dst + count, std::forward<AllocArgs>(alloc_args)...);
    } else {
      (void) uninitialized_copy(src, src + count, dst, dst + count, std::forward<AllocArgs>(alloc_args)...);
    }
  }

public:
  constexpr void shrink_to_fit()
  {
    if constexpr (inline_capacity == 0) {
      shrink_to_fit(stay_on_heap);
      return;
    } else {
      if (is_stack()) return;

      size_type const count = size();
      if (count > inline_capacity) {
        shrink_to_fit(stay_on_heap);
        return;
      }

      if (count == 0) {
        if (heap_.elements_ptr_ != pointer {}) deallocate_unchecked_();
        demote_to_stack_();
        set_size_(size_type {}, stack_flag);
        return;
      }

      pointer const old_data         = heap_.elements_ptr_;
      size_type const old_capacity   = heap_.capacity_;

      heap_restore_on_exit restore { this, old_data, old_capacity };
      transfer_unchecked_(std::to_address(old_data), count, stack_.elements_);
      restore.dismiss();

      destroy_elements_unchecked_(std::to_address(old_data), count);
      allocator_traits::deallocate(alloc_, old_data, old_capacity);

      demote_to_stack_();
      set_size_(count, stack_flag);
    }
  }

  constexpr void shrink_to_fit(stay_on_heap_t)
  {
    size_type const count = size();
    if (is_stack() || capacity() == count) return;

    if (count != 0) {
      auto const [new_block, new_capacity] = allocate_unchecked_(count);

      allocation_guard alloc_guard { new_block, new_capacity, alloc_ };
      transfer_unchecked_(std::to_address(heap_.elements_ptr_), count, std::to_address(new_block), alloc_);
      alloc_guard.dismiss();

      destroy_elements_unchecked_(std::to_address(heap_.elements_ptr_), count);
      deallocate_unchecked_();

      heap_.elements_ptr_ = new_block;
      heap_.capacity_     = new_capacity;
      set_size_(count, heap_flag);
    } else {
      if (heap_.elements_ptr_ != pointer {}) deallocate_unchecked_();
      heap_.elements_ptr_ = pointer {};
      heap_.capacity_     = size_type {};
      set_size_(size_type {}, heap_flag);
    }
  }

  /// inline_capacity == 0 keeps storage: reset == clear
  constexpr void reset() noexcept
  {
    destroy_elements_unchecked_(data(), size());
    if constexpr (inline_capacity != 0) {
      if (is_heap() && heap_.elements_ptr_ != pointer {}) {
        deallocate_unchecked_();
        demote_to_stack_();
        set_size_(size_type {}, stack_flag);
        return;
      }
    }
    set_size_(size_type {}, get_flag_());
  }

  constexpr void clear() noexcept
  {
    destroy_elements_unchecked_(data(), size());
    set_size_(size_type {}, get_flag_());
  }

  constexpr void reserve(size_type const n)
  {
    if (n > max_size()) [[unlikely]] {
      throw_length_error_();
    }

    if (n > capacity()) {
      value_type *const ptr = data();
      size_type const count = size();

      auto const [new_block, new_capacity] = allocate_unchecked_(n);
      if (!empty()) {
        allocation_guard alloc_guard { new_block, new_capacity, alloc_ };
        transfer_unchecked_(ptr, count, std::to_address(new_block), alloc_);
        alloc_guard.dismiss();

        destroy_elements_unchecked_(ptr, count);
      }

      if (is_heap()) {
        if (ptr) deallocate_unchecked_();
      } else if constexpr (inline_capacity != 0) {
        promote_to_heap_();
      }

      heap_.elements_ptr_ = new_block;
      heap_.capacity_     = new_capacity;
      set_size_(count, heap_flag);
    }
  }

protected:
  template <class... Args>
  requires (sizeof...(Args) <= 1)
  constexpr void resize_reallocate_(size_type const newsize, Args&&... args)
  {
    if (newsize > max_size()) [[unlikely]] throw_length_error_();
    size_type const oldsize = size();
    assert(oldsize < newsize && newsize > capacity() && "resize_reallocate_() requires growth beyond capacity");

    value_type *const old_data = data();
    size_type const growth      = calculate_growth_(newsize);
    auto const [new_block, new_capacity] = allocate_unchecked_(growth);
    value_type *const raw_new        = std::to_address(new_block);
    value_type *const appended_first = raw_new + oldsize;

    reallocation_guard guard { new_block, new_capacity, appended_first, appended_first, alloc_ };
    if constexpr (sizeof...(Args) == 0) {
      guard.constructed_last_ = uninitialized_value_construct_n(appended_first, newsize - oldsize, alloc_);
    } else {
      guard.constructed_last_ = uninitialized_fill_n(appended_first, newsize - oldsize, std::forward<Args>(args)..., alloc_);
    }
    transfer_unchecked_(old_data, oldsize, raw_new, alloc_);
    guard.dismiss();

    destroy_elements_unchecked_(old_data, oldsize);
    if (is_heap()) {
      if (heap_.elements_ptr_ != pointer {}) deallocate_unchecked_();
    } else if constexpr (inline_capacity != 0) {
      promote_to_heap_();
    }

    heap_.elements_ptr_ = new_block;
    heap_.capacity_     = new_capacity;
    set_size_(newsize, heap_flag);
  }

  template <class... Args>
  requires (sizeof...(Args) <= 1)
  constexpr void resize_impl_(size_type const newsize, Args&&... args)
  {
    size_type const oldsize = size();
    if (newsize < oldsize) {
      value_type *const newlast = data() + newsize;
      destroy_elements_unchecked_(newlast, oldsize - newsize);
      set_size_(newsize, get_flag_());
      return;
    }
    if (newsize > oldsize) {
      if (newsize > capacity()) {
        resize_reallocate_(newsize, std::forward<Args>(args)...);
        return;
      }
      construct_elements_unchecked_(data() + oldsize, newsize - oldsize, std::forward<Args>(args)...);
      set_size_(newsize, get_flag_());
    }
  }

public:
  constexpr void resize(size_type const newsize)
  requires std::default_initializable<value_type>
  {
    resize_impl_(newsize);
  }

  constexpr void resize(size_type const newsize, const_reference fill_value)
  requires std::constructible_from<value_type, const_reference>
  {
    resize_impl_(newsize, fill_value);
  }

protected:
  constexpr void demote_to_stack_() noexcept
  requires (inline_capacity != 0)
  {
    assert(is_heap());
    std::destroy_at(&heap_);
#if defined(__cpp_lib_start_lifetime) && __cpp_lib_start_lifetime >= 202603L
    std::start_lifetime(stack_);
#else
    std::construct_at(&stack_);
#endif
  }

  constexpr void promote_to_heap_() noexcept
  requires (inline_capacity != 0)
  {
    assert(is_stack());
    std::destroy_at(&stack_);
    std::construct_at(&heap_);
  }

  constexpr size_type calculate_growth_(size_type const new_size) const noexcept
  {
    size_type const old_capacity = capacity();
    size_type const max_count    = max_size();

    if (old_capacity > max_count - old_capacity / 2) {
      return max_count; // geometric growth would overflow
    }

    size_type const geometric = old_capacity + old_capacity / 2;

    if (geometric < new_size) {
      return new_size; // geometric growth would be insufficient
    }

    return geometric; // geometric growth is sufficient
  }

  constexpr size_type get_flag_() const noexcept { return size_ & heap_flag; }
  constexpr void set_size_(size_type const count, size_type const flag) noexcept
  {
    assert(count <= max_size() && "size exceeds encodable range");
    assert((flag & ~heap_flag) == size_type {} && "size flag must be heap/stack");
    size_ = (count << size_type(1)) | flag;
  }

  [[noreturn]] static void throw_length_error_()
  {
#if defined(_CPPUNWIND) || defined(__EXCEPTIONS)
    throw std::length_error("requested size exceeds max_size()");
#else 
    std::abort();
#endif
  }

protected:
  struct heap_block
  {
    pointer elements_ptr_ = pointer {};
    size_type capacity_   = size_type {};
  };

  template <typename U, std::size_t M> struct storage_for { using type = U[M]; };
  template <typename U> struct storage_for<U, 0> { using type = char; };

  struct stack_block
  {
    union { typename storage_for<value_type, inline_capacity>::type elements_; };
    constexpr stack_block() noexcept { }
    constexpr ~stack_block() noexcept { }
  };

  union
  {
    heap_block heap_;
    stack_block stack_;
  };

  size_type size_ = inline_capacity == 0 ? heap_flag : stack_flag;
  [[no_unique_address]] allocator_type alloc_;
};
} // namespace stdex

#if defined(__clang__)
#  pragma clang diagnostic pop
#elif defined(__GNUC__)
#  pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#  pragma warning(pop)
#endif
