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
#include <cstddef>     // std::size_t
#include <cstdint>     // std::uintptr_t / uintptr_t
#include <cstring>     // std::memset
#include <memory>      // std::allocator, std::allocator_traits,
                       // std::pointer_traits, std::to_address,
                       // std::construct_at
#include <new>         // placement new
#include <type_traits> // std::is_same_v, std::add_pointer_t

#include "group15.hpp"
#include "hash_table_iterator.hpp"

namespace stdex::unordered_flat_detail
{
template <class Traits, class Hasher, class KeyEqual, class Allocator>
struct inline_storage : public Traits
{
  using hasher    = Hasher;
  using key_equal = KeyEqual;

  using typename Traits::const_reference;
  using typename Traits::key_type;
  using typename Traits::reference;
  using typename Traits::value_type;
  using group_type = group15;

  using allocator_type         = typename std::allocator_traits<Allocator>::template rebind_alloc<value_type>;
  using allocator_traits       = std::allocator_traits<allocator_type>;
  using group_allocator_type   = typename std::allocator_traits<Allocator>::template rebind_alloc<group_type>;
  using group_allocator_traits = std::allocator_traits<group_allocator_type>;

  using size_type            = typename allocator_type::size_type;
  using difference_type      = typename allocator_traits::difference_type;
  using const_pointer        = typename allocator_traits::const_pointer;
  using pointer              = typename allocator_traits::pointer;
  using pointer_traits       = std::pointer_traits<pointer>;
  using group_pointer        = typename pointer_traits::template rebind<group_type>;
  using group_pointer_traits = std::pointer_traits<group_pointer>;

  using const_iterator = hash_table_iterator<Traits, pointer, group_pointer, true>;
  using iterator       = std::conditional_t<std::is_same_v<key_type, value_type>, const_iterator, hash_table_iterator<Traits, pointer, group_pointer, false>>;
  using lazy_iterator  = hash_table_lazy_iterator<iterator>;

  using Traits::inline_capacity;
  static constexpr float max_load_factor_radio   = 0.875F;
  static constexpr size_type max_load_factor_den = 7;
  static constexpr size_type max_load_factor_num = 8;
  static constexpr size_type min_group_count     = 2;

  /// boost In the following two functions, dummy empty logic is used, but actually 
  /// we can determine it by size capacity or whether the pointer is nullptr.
  /// find(const Key& x, std::size_t pos0, std::size_t hash) const
  /// nosize_unchecked_emplace_at(std::size_t pos0, std::size_t hash, Value&& x)
 /* static constexpr group_type dummy_groups[min_group_count] = {
    group_type::make_dummy_group(), group_type::make_dummy_group()
  };*/

  enum class storage_mode : uint8_t { heap = 0, stack = 1 };

  struct alignas(16) heap_block;
  struct alignas(16) stack_block;

  constexpr inline_storage(allocator_type const &alloc) noexcept
      : alloc_ { alloc }
  {
    dissociate_();
  }

  template <class H = hasher, class K = key_equal, class Al = allocator_type>
  constexpr inline_storage(H &&hash_arg, K &&key_eq, Al &&alloc) noexcept(
      std::is_nothrow_constructible_v<hasher, H> &&
      std::is_nothrow_constructible_v<key_equal, K> &&
      std::is_nothrow_constructible_v<allocator_type, Al>)
      : hasher_ { std::forward<H>(hash_arg) }
      , key_eq_ { std::forward<K>(key_eq) }
      , alloc_ { std::forward<Al>(alloc) }
  {
    dissociate_();
  }

  constexpr ~inline_storage() noexcept { dispose_(); }

  inline_storage(inline_storage &&other)             = delete;
  inline_storage &operator= (inline_storage &&other) = delete;
  inline_storage(inline_storage const &)             = delete;
  inline_storage &operator= (inline_storage const &) = delete;

  template <typename... Args>
  requires std::constructible_from<value_type, Args...> &&
           requires (allocator_type &a, value_type *p, Args &&...args) {
             allocator_traits::construct(a, p, std::forward<Args>(args)...);
           }
  constexpr void construct_element_unchecked_(value_type *element_ptr, Args &&...args) noexcept(
      noexcept(allocator_traits::construct(
          std::declval<allocator_type &>(),
          std::declval<value_type *>(),
          std::declval<Args>()...)) &&
      noexcept(std::construct_at(
          std::declval<value_type *>(),
          std::declval<Args>()...)))
  {
    if (is_heap()) {
      allocator_traits::construct(alloc_, element_ptr, std::forward<Args>(args)...);
    } else {
      std::construct_at(element_ptr, std::forward<Args>(args)...);
    }
  }

  constexpr void destroy_element_unchecked_(value_type *element_ptr) noexcept
  {
    if (is_heap()) {
      allocator_traits::destroy(alloc_, element_ptr);
    } else {
      std::destroy_at(element_ptr);
    }
  }

  constexpr void destroy_elements_unchecked_() noexcept
  {
    group_type *groups_ptr = groups_address_();
    group_type *const sentinel_group = groups_ptr + group_count_() - 1;

    if constexpr (std::is_trivially_destructible_v<value_type>) {
      std::fill_n(groups_address_(), group_count_(), group_type {});
    } else {
      value_type *elements_ptr = elements_address_();
      group_type const *const groups_end = std::next(sentinel_group);

      for (; groups_ptr != groups_end; ++groups_ptr, elements_ptr += group_size_()) {
        auto occupied_mask = group_type::occupied_mask_excluding_sentinel(groups_ptr, sentinel_group);
        while (occupied_mask) {
          unsigned slot_index = std::countr_zero(static_cast<unsigned>(occupied_mask));
          destroy_element_unchecked_(elements_ptr + slot_index);
          occupied_mask &= occupied_mask - 1;
        }
        groups_ptr->reset();
      }
    }
    sentinel_group->set_sentinel_mark();
    size_     = 0;
    //< max_load_ = initial_max_load_(capacity());
  }

  constexpr void allocate_unchecked_(size_type capacity)
  {
    auto const new_group_index_shift = static_cast<uint8_t>(group_index_shift_by_(capacity));
    auto const new_group_count_mask  = group_count_by_(static_cast<size_type>(new_group_index_shift)) - 1;
    auto const new_group_count       = new_group_count_mask + 1;
    auto const new_capacity          = new_group_count * group_size_() - 1;
    auto const new_max_load          = initial_max_load_(new_capacity);

    if consteval {
      auto elements_ptr = allocator_traits::allocate(alloc_, new_capacity);
      element_allocation_guard elements_guard { *this, elements_ptr, new_capacity };
      auto groups_alloc = group_allocator_type { alloc_ };
      auto groups_ptr   = group_allocator_traits::allocate(groups_alloc, new_group_count);

      initial_group_array_(groups_ptr, new_group_count);

      heap_ = { elements_ptr, groups_ptr };
      elements_guard.release();
    } else {
      auto elements_ptr = allocator_traits::allocate(alloc_, calculate_storage_size_(new_group_count));
      element_allocation_guard elements_guard { *this, elements_ptr, calculate_storage_size_(new_group_count) };
      auto byte_ptr     = reinterpret_cast<unsigned char *>(std::to_address(elements_ptr) + new_capacity);

      // The group must be 16-byte aligned
      byte_ptr += (uintptr_t(sizeof(group_type)) - reinterpret_cast<uintptr_t>(byte_ptr)) % sizeof(group_type);

      auto groups_ptr = reinterpret_cast<group_type *>(byte_ptr);
      initial_group_array_(groups_ptr, new_group_count);

      heap_ = { elements_ptr, group_pointer_traits::pointer_to(*groups_ptr) };
      elements_guard.release();
    }

    group_index_shift_ = new_group_index_shift;
    group_count_mask_  = new_group_count_mask;
    max_load_          = new_max_load;
  }

  struct element_allocation_guard
  {
    constexpr ~element_allocation_guard()
    {
      if (active_) allocator_traits::deallocate(owner_.alloc_, pointer_, count_);
    }

    constexpr void release() noexcept { active_ = false; }

    inline_storage &owner_;
    pointer pointer_;
    size_type count_;
    bool active_ = true;
  };

  constexpr void deallocate_unchecked_() noexcept(noexcept(allocator_traits::deallocate(alloc_, nullptr, 0)))
  {
    if consteval {
      auto groups_alloc = group_allocator_type { alloc_ };
      allocator_traits::deallocate(alloc_, heap_.elements_ptr_, group_count_() * group_size_() - 1);
      group_allocator_traits::deallocate(groups_alloc, heap_.groups_ptr_, group_count_());
    } else {
      allocator_traits::deallocate(alloc_, heap_.elements_ptr_, calculate_storage_size_(group_count_()));
    }
    heap_ = heap_block {};
  }

  // clang-format off
  [[nodiscard]] float load_factor() const noexcept { return elements_address_() ? static_cast<float>(size()) / static_cast<float>(capacity_unchecked_()) : 0.F; }
  constexpr void max_load_factor([[maybe_unused]] float new_value) const noexcept { }
  [[nodiscard]] constexpr float max_load_factor() const noexcept { return max_load_factor_radio; }
  [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] constexpr size_type size() const noexcept { return size_; }
  [[nodiscard]] constexpr size_type max_size() const noexcept { return std::numeric_limits<size_type>::max(); }
  [[nodiscard]] constexpr size_type capacity() const noexcept { return elements_address_() ? group_count_() * group_size_() - 1 : 0; }
  [[nodiscard]] constexpr size_type bucket_count() const noexcept{ return capacity(); }

  [[nodiscard]] constexpr hasher hash_function() const noexcept { return hasher_; }
  [[nodiscard]] constexpr key_equal key_eq() const noexcept { return key_eq_; }
  [[nodiscard]] constexpr allocator_type get_allocator() const noexcept { return alloc_; }

  [[nodiscard]] constexpr bool is_heap() const noexcept { return inline_capacity == 0 || storage_mode_ == storage_mode::heap; }
  [[nodiscard]] constexpr bool is_stack() const noexcept { return !is_heap(); }
  
  [[nodiscard]] static constexpr size_type group_size_() noexcept { return group_type::capacity(); }
  [[nodiscard]] constexpr size_type group_count_() const noexcept { return group_count_mask_ + 1; }
  [[nodiscard]] constexpr size_type group_index_(size_type hash) const noexcept { return hash >> group_index_shift_; }
  [[nodiscard]] constexpr size_type capacity_unchecked_() const noexcept { return group_count_() * group_size_() - 1; }

[[nodiscard]] constexpr value_type *elements_address_() const noexcept
{
  if (is_heap()) return std::to_address(heap_.elements_ptr_);
  if constexpr (inline_capacity > 0) {
    return const_cast<value_type *>(static_cast<value_type const *>(stack_.elements_));
  }
  return nullptr;
}

[[nodiscard]] constexpr group_type *groups_address_() const noexcept
{
  if (is_heap()) return std::to_address(heap_.groups_ptr_);
  if constexpr (inline_capacity != 0) {
    return const_cast<group_type *>(static_cast<group_type const *>(stack_.groups_));
  }
  return nullptr;
}

  // clang-format on

  constexpr void initial_group_array_(group_type *groups_ptr, size_type group_count) noexcept
  {
    if consteval {
      // reset() after construct_at zeroes every byte through a member write;
      // MSVC's constant evaluator needs that write (not just value-init) to
      // consider the union member live before available_mask reads it.
      for (size_type i = 0; i < group_count; ++i) {
        std::construct_at(groups_ptr + i);
        groups_ptr[i].reset();
      }
    } else {
      std::memset(groups_ptr, 0, sizeof(group_type) * group_count);
    }
    groups_ptr[group_count - 1].set_sentinel_mark();
  }

  /// Given the capacity, figure out how much it can hold.
  [[nodiscard]] constexpr size_type initial_max_load_(size_type capacity) const noexcept
  {
    //< if (is_stack()) return capacity; // 100%

    if (capacity <= 2 * group_size_() - 1) {
      return capacity; // <= 29, 100%
    } else {
      return static_cast<size_type>(max_load_factor() * static_cast<float>(capacity)); // > 29, 87.5%
    }
  }

  /// Given the number of elements, calculate the actual required capacity based on the max load factor
  static constexpr size_type required_capacity_(size_type elem_count) noexcept
  {
    return (elem_count * max_load_factor_num + max_load_factor_den - 1) / max_load_factor_den;
  }

  static constexpr size_type required_capacity_for_growth_(size_type elem_count) noexcept
  {
    return required_capacity_(elem_count + elem_count / 61 + 1);
  }

  /// The actual number of groups, aligned to a power of two, is calculated based on
  /// the capacity calculated from the load factor and serves as the actual capacity.
  static constexpr size_type aligned_capacity_(size_type capacity) noexcept
  {
    return group_count_by_(group_index_shift_by_(capacity)) * group_size_() - 1;
  }

  static constexpr size_type group_index_shift_by_(size_type elem_count) noexcept
  {
    size_type const group_count = elem_count / group_size_() + 1; // extra +1 for the sentinel
    return sizeof(size_type) * CHAR_BIT -
           (group_count <= min_group_count ? 1 : static_cast<size_type>(std::bit_width(group_count - 1)));
  }

  static constexpr size_type group_count_by_(size_type index_shift) noexcept
  {
    return size_type(1) << (sizeof(size_type) * CHAR_BIT - index_shift);
  }

  static constexpr size_type align_up_(size_type value, size_type alignment) noexcept
  {
    return (value + alignment - 1) & ~(alignment - 1);
  }

  static constexpr size_type calculate_storage_size_(size_type group_count) noexcept
  {
    size_type capacity      = group_count * group_size_() - 1;
    size_type groups_offset = align_up_(capacity * sizeof(value_type), alignof(group_type));
    size_type total_bytes   = groups_offset + group_count * sizeof(group_type);
    return (total_bytes + sizeof(value_type) - 1) / sizeof(value_type);
  }

  constexpr void demote_to_stack_() noexcept
  {
    if constexpr (inline_capacity != 0) {
      std::destroy_at(&heap_);
#if __cplusplus >= 202603L
      ::new (static_cast<void *>(&stack_)) stack_block;
#else
      std::construct_at(&stack_);
#endif

      storage_mode_      = storage_mode::stack;
      group_index_shift_ = static_cast<uint8_t>(group_index_shift_by_(inline_capacity));
      group_count_mask_  = (inline_capacity + 1) / group_size_() - 1;
      size_              = 0;
      max_load_          = inline_capacity;

      initial_group_array_(stack_.groups_, group_count_());
    }
  }

  constexpr void promote_to_heap_() noexcept
  {
    std::destroy_at(&stack_);
    std::construct_at(&heap_);
    storage_mode_ = storage_mode::heap;
  }

  constexpr void dispose_() noexcept
  {
    if (!empty()) destroy_elements_unchecked_();
    if (is_heap() && elements_address_()) deallocate_unchecked_();
  }

  constexpr void dissociate_() noexcept
  {
    if constexpr (inline_capacity == 0) {
      std::construct_at(&heap_);

      heap_.elements_ptr_ = pointer {};
      heap_.groups_ptr_   = group_pointer {};
      storage_mode_       = storage_mode::heap;
      group_index_shift_  = static_cast<uint8_t>(group_index_shift_by_(0));
      group_count_mask_   = min_group_count - 1;
      size_               = 0;
      max_load_           = initial_max_load_(0);
    } else {
#if __cplusplus >= 202603L
      ::new (static_cast<void *>(&stack_)) stack_block;
#else
      std::construct_at(&stack_);
#endif

      storage_mode_      = storage_mode::stack;
      group_index_shift_ = static_cast<uint8_t>(group_index_shift_by_(inline_capacity));
      group_count_mask_  = (inline_capacity + 1) / group_size_() - 1;
      size_              = 0;
      max_load_          = inline_capacity;

      // Only the group bytes are initialized: empty slots hold no live element
      // (elements_ stays raw storage and is constructed on insert).
      initial_group_array_(stack_.groups_, group_count_());
    }
  }

  template <class Fn>
  requires std::is_invocable_v<Fn &, value_type *, group_type *, unsigned>
  constexpr void for_each_occupied_slot_unchecked_(Fn &&fn) const
      noexcept(std::is_nothrow_invocable_v<Fn &, value_type *, group_type *, unsigned>)
  {
    auto *elements_ptr = elements_address_();
    auto *groups_ptr   = groups_address_();

    auto *const sentinel_group = groups_ptr + group_count_() - 1;

    for (size_type group_index = 0; group_index < group_count_(); ++group_index) {
      auto *const group_ptr = groups_ptr + group_index;
      auto occupied_mask = group_type::occupied_mask_excluding_sentinel(group_ptr, sentinel_group);
      while (occupied_mask) {
        unsigned slot_index = std::countr_zero(static_cast<unsigned>(occupied_mask));
        fn(elements_ptr + group_index * group_size_() + slot_index, group_ptr, slot_index);
        occupied_mask &= occupied_mask - 1;
      }
    }
  }

  struct triangular_probing
  {
    constexpr triangular_probing(size_t pos) noexcept : pos_ { pos } , step_ { } {}

    constexpr size_t position() const noexcept { return pos_; }

    constexpr bool next(size_t group_count_mask) noexcept
    {
      ++step_;
      pos_ = (pos_ + step_) & group_count_mask;
      return step_ <= group_count_mask;
    }

  private:
    size_t pos_, step_;
  };
  using prober_type = triangular_probing;

  template <class... Args>
  constexpr iterator emplace_at_available_slot_unchecked_(size_type hash, Args &&...args) noexcept(noexcept(
      std::declval<inline_storage &>().construct_element_unchecked_(
          std::declval<value_type *>(),
          std::declval<Args>()...)))
  {
    for (prober_type prober(group_index_(hash));; prober.next(group_count_mask_)) {
      size_type position    = prober.position();
      group_type *group_ptr = groups_address_() + position;
      if (int avail_mask = group_ptr->available_mask()) [[likely]] {
        unsigned slot_index     = std::countr_zero(static_cast<unsigned>(avail_mask));
        value_type *element_ptr = elements_address_() + position * group_size_() + slot_index;
        construct_element_unchecked_(element_ptr, std::forward<Args>(args)...);
        group_ptr->set_fingerprint_hash(slot_index, hash);
        return iterator { element_ptr, group_ptr, slot_index };
      } else {
        group_ptr->set_overflow_mark(hash);
      }
    }
  }

  template <class K>
  constexpr iterator find_fingerprint_slot_unchecked_(K const &key_val, size_type hash) const
  {
    value_type *elements_ptr = elements_address_();
    prober_type prober(group_index_(hash));
    do [[likely]] {
        size_type position  = prober.position();
        group_type *group_ptr = groups_address_() + position;
        if (auto fingerprint_mask = group_ptr->fingerprint_mask(hash)) {
          [[assume(elements_ptr != nullptr)]];
          value_type *element_ptr = elements_ptr + position * group_size_();
          do {
            unsigned slot_index = std::countr_zero(static_cast<unsigned>(fingerprint_mask));
            if (key_eq()(key_val, Traits::key_fn(element_ptr[slot_index]))) [[likely]] {
              return iterator { element_ptr + slot_index, group_ptr, slot_index };
            }
            fingerprint_mask &= fingerprint_mask - 1;
          } while (fingerprint_mask);
        }
        if (!group_ptr->is_overflow_by_hash(hash)) [[likely]] {
          return {};
        }
    } while (prober.next(group_count_mask_));

    return {};
  }
  
  struct alignas(16) heap_block
  {
    pointer elements_ptr_     = nullptr;
    group_pointer groups_ptr_ = nullptr;
  };

  template <typename T, std::size_t N> struct storage_for { using type = T[N]; };
  template <typename T> struct storage_for<T, 0> { using type = char; };

  /// The arrays are raw storage: the default constructor deliberately starts no
  /// member lifetime, so elements are constructed on demand and only the group
  /// bytes are initialized.  (A defaulted constructor would be deleted because
  /// value_type is not trivially default constructible.)
  struct alignas(16) stack_block
  {
    union { typename storage_for<value_type, inline_capacity>::type elements_; };
    union { typename storage_for<group_type, (inline_capacity + 1) / group_type::capacity()>::type groups_; };
    constexpr stack_block() noexcept {}
    constexpr ~stack_block() noexcept {}
  };

  union
  {
    heap_block heap_;
    stack_block stack_;
  };

  storage_mode storage_mode_;
  uint8_t group_index_shift_;
  size_type group_count_mask_;
  size_type size_;
  size_type max_load_;
  [[no_unique_address]] hasher hasher_;
  [[no_unique_address]] key_equal key_eq_;
  [[no_unique_address]] allocator_type alloc_;

  static_assert(
    inline_capacity == 0 || inline_capacity == group_count_by_(group_index_shift_by_(inline_capacity)) * group_size_() - 1,
    "The number of groups has to be a power of 2 (at least 2), and the inline_capacity "
    "has to be the value of groups multiplied by group size (15) minus 1 (the sentinel). "
    "So the minimum inline_capacity is 29 (2 * 15 - 1), then 59 (4 * 15 - 1), and so on. ");
};
} // namespace stdex::unordered_flat_detail

#include <format>
#include <print>
#include <cstdint>
namespace std
{

template <class Traits, class Hasher, class KeyEqual, class Allocator>
struct formatter<
    stdex::unordered_flat_detail::inline_storage<Traits, Hasher, KeyEqual, Allocator>,
    char>
{
  constexpr auto parse(std::format_parse_context &ctx) { return ctx.begin(); }

  template <class FormatContext>
  auto format(
      stdex::unordered_flat_detail::inline_storage<Traits, Hasher, KeyEqual, Allocator> const &s,
      FormatContext &ctx) const
  {
    using storage_t = stdex::unordered_flat_detail::inline_storage<
        Traits, Hasher, KeyEqual, Allocator>;

    auto out = ctx.out();

    char const *mode_str =
        (s.storage_mode_ == storage_t::storage_mode::heap) ? "heap" : "stack";

    out = std::format_to(
        out,
        "inline_storage {{\n"
        "  storage_mode_      = {} ({})\n"
        "  group_index_shift_ = {}\n"
        "  group_count_mask_  = {}  -> group_count = {}\n"
        "  size_              = {}\n"
        "  max_load_          = {}\n"
        "  capacity()         = {}\n"
        "  load_factor()      = {:.4f}\n"
        "  is_heap()          = {}\n",
        mode_str,
        static_cast<unsigned>(s.storage_mode_),
        static_cast<unsigned>(s.group_index_shift_),
        s.group_count_mask_,
        static_cast<unsigned long long>(s.group_count_mask_) + 1ULL,
        s.size_,
        s.max_load_,
        s.capacity(),
        s.load_factor(),
        s.is_heap());

    if (s.is_heap()) {
      out = std::format_to(
          out,
          "  heap_.elements_ptr_ = {}\n"
          "  heap_.groups_ptr_   = {}\n",
          static_cast<void const *>(s.elements_address_()),
          static_cast<void const *>(s.groups_address_()));
    } else {
      out = std::format_to(
          out,
          "  stack_.elements_    = {}\n"
          "  stack_.groups_      = {}\n",
          static_cast<void const *>(s.elements_address_()),
          static_cast<void const *>(s.groups_address_()));
    }

    return std::format_to(out, "}}");
  }
};

} // namespace std
