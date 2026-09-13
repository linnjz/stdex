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

#include <bit>          // std::countr_zero
#include <cstddef>      // std::size_t / size_t
#include <iterator>     // std::forward_iterator_tag
#include <memory>       // std::to_address
#include <type_traits>  // std::conditional_t

namespace stdex::unordered_flat_detail
{
template <class HashTable, bool IsConstant>
class hash_table_iterator
{
  using inline_storage_type = typename HashTable::inline_storage_type;
  using group_type          = typename inline_storage_type::group_type;
  using pointer             = typename inline_storage_type::pointer;
  using group_pointer       = typename inline_storage_type::group_pointer;

  static constexpr auto group_size = group_type::capacity();

public:
  using iterator_category = std::forward_iterator_tag;
  using iterator_concept  = std::forward_iterator_tag;
  using value_type        = typename HashTable::value_type;
  using difference_type   = typename HashTable::difference_type;
  using pointer           = std::conditional_t<IsConstant, typename HashTable::const_pointer, typename HashTable::pointer>;
  using reference         = std::conditional_t<IsConstant, typename HashTable::const_reference, typename HashTable::reference>;

  constexpr hash_table_iterator() noexcept
      : element_ptr_ { nullptr }
      , group_ptr_ { nullptr }
      , slot_index_ { 0 }
  {
  }

  template <bool OtherConst>
  constexpr hash_table_iterator(hash_table_iterator<HashTable, OtherConst> const &other) noexcept
  requires (!OtherConst)
      : element_ptr_ { other.element_ptr_ }
      , group_ptr_ { other.group_ptr_ }
      , slot_index_ { other.slot_index_ }
  {
  }

  constexpr hash_table_iterator(const_iterator_cast_tag, hash_table_iterator<HashTable, true> const &other)
      : element_ptr_ { other.element_ptr_ }
      , group_ptr_ { other.group_ptr_ }
      , slot_index_ { other.slot_index_ }
  {
  }

  constexpr reference operator* () const noexcept { return *element_address_(); }

  constexpr pointer operator->() const noexcept { return element_address_(); }

  constexpr hash_table_iterator &operator++ () noexcept
  {
    increment_();
    return *this;
  }

  constexpr hash_table_iterator operator++ (int) noexcept
  {
    auto tmp = *this;
    increment_();
    return tmp;
  }

  friend constexpr bool operator== (hash_table_iterator const &a, hash_table_iterator const &b) noexcept
  {
    return a.element_address_() == b.element_address_();
  }

  friend constexpr bool operator!= (hash_table_iterator const &a, hash_table_iterator const &b) noexcept
  {
    return !(a == b);
  }

private:
  constexpr explicit hash_table_iterator(pointer element_ptr, group_pointer group_ptr, size_t slot_index) noexcept
      : element_ptr_ { element_ptr }
      , group_ptr_ { group_ptr }
      , slot_index_ { slot_index }
  {
  }

  constexpr value_type *element_address_() const noexcept { return std::to_address(element_ptr_); }

  constexpr group_type *group_address_() const noexcept { return std::to_address(group_ptr_); }

  constexpr void increment_() noexcept
  {
    ++element_ptr_;
    ++slot_index_;

    while (slot_index_ < group_size) {
      if (group_ptr_->is_occupied(slot_index_)) {
        if (group_ptr_->is_sentinel(slot_index_)) [[unlikely]]
          element_ptr_ = nullptr;
        return;
      }
      ++element_ptr_;
      ++slot_index_;
    }

    ++group_ptr_;
    slot_index_ = 0;

    while (true) {
      int mask = group_ptr_->occupied_mask();
      if (mask != 0) {
        auto n = std::countr_zero(static_cast<unsigned>(mask));
        if (group_ptr_->is_sentinel(n)) [[unlikely]] {
          element_ptr_ = nullptr;
        } else {
          element_ptr_ += static_cast<difference_type>(n);
          slot_index_ = n;
        }
        return;
      }
      element_ptr_ += group_size;
      ++group_ptr_;
    }
  }

private:
  template <class, bool>
  friend class hash_table_iterator;
  template <class>
  friend class hash_table_lazy_iterator;
  template <class>
  friend class hash_table;

  pointer element_ptr_;
  group_pointer group_ptr_;
  size_t slot_index_;
};

template <class HashTableIterator>
class hash_table_lazy_iterator;

template <class HashTable, bool IsConstant>
class hash_table_lazy_iterator<hash_table_iterator<HashTable, IsConstant>>
{
  struct stack_block;
  struct heap_block;

  using iterator       = hash_table_iterator<HashTable, IsConstant>;
  using const_iterator = hash_table_iterator<HashTable, true>;

public:
  constexpr hash_table_lazy_iterator(hash_table_lazy_iterator const &)   = default;
  hash_table_lazy_iterator &operator= (hash_table_lazy_iterator const &) = delete;
  hash_table_lazy_iterator(hash_table_lazy_iterator &&)                  = delete;
  hash_table_lazy_iterator &operator= (hash_table_lazy_iterator &&)      = delete;
  constexpr ~hash_table_lazy_iterator()                                  = default;

  constexpr operator iterator() const noexcept
  {
    auto it = pos_;
    it.increment_();
    return iterator(const_iterator_cast_tag {}, it);
  }

  constexpr operator const_iterator() const noexcept
  requires (!IsConstant)
  {
    return this->operator iterator();
  }

private:
  constexpr hash_table_lazy_iterator(const_iterator pos)
      : pos_(pos) { };

  template <class>
  friend class hash_table;

  const_iterator pos_;
};
} // namespace stdex::unordered_flat_detail
