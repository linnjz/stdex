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

  constexpr value_type *elements_address() const noexcept { return std::to_address(us_.heap_.elements_ptr_); }

  constexpr group_type *groups_address() const noexcept { return std::to_address(us_.heap_.groups_ptr_); }

  constexpr size_t group_count() const noexcept { return group_count_mask_ + 1; }

  constexpr size_t group_index(size_t hash) const noexcept { return hash >> group_index_shift_; }

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

    if constexpr (std::is_same_v<std::add_pointer_t<group_type>, group_pointer>) {
      if (capacity == 0) [[unlikely]] {
        s.us_.heap_.groups_ptr_ = const_cast<group_pointer>(dummy_groups);
        return s;
      }
    }

    ///< The actual number of elements that can be stored
    auto const actual_capacity = group_count * group_size - 1;

    if consteval {
      auto groups_alloc = group_allocator_type { alloc };

      auto elements_ptr = allocator_traits::allocate(alloc, actual_capacity);
      auto groups_ptr   = group_allocator_traits::allocate(groups_alloc, group_count);

      for (size_t i = 0; i < group_count; ++i) std::construct_at(groups_ptr + i);
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

  struct guard
  {
    inline_storage *guard_storage_;
    allocator_type guard_alloc_;

    constexpr ~guard()
    {
      if (guard_storage_) destroy(guard_alloc_, *guard_storage_);
    }

    constexpr void release() noexcept { guard_storage_ = nullptr; }
  };

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
};
} // namespace stdex::unordered_flat_detail
