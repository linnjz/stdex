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
#include <memory>       // std::to_address, std::addressof, std::pointer_traits
#include <type_traits>  // std::conditional_t
#include <utility>      // std::pair (reference may be a pair proxy)

namespace stdex::unordered_flat_detail
{
template <class Traits, class Hasher, class KeyEqual, class Allocator>
struct inline_storage;

struct const_iterator_cast_tag
{
};

/// The traits (the same object used as the storage policy) own the mapping from
/// a stored element to what users may see: a set exposes the whole element const,
/// a map exposes a pair of a const key and a mutable mapped value.  The iterator
/// only picks Traits::reference vs Traits::const_reference by IsConstant, so this
/// one template serves both flat_set and flat_map without IsConstant also having
/// to encode which of the two it is.
/// flat_set's iterator (a real const reference) also exposes pointer, exactly
/// like the underlying container's const_iterator; std::flat_map's proxy iterator
/// has none, since a synthesized reference cannot be addressed.  The member is
/// supplied only when the reference is a real reference, and inherited so that
/// `It::pointer` still resolves through qualified lookup.
template <class Reference, bool = std::is_reference_v<Reference>>
struct iterator_pointer
{
  using pointer = std::remove_reference_t<Reference> *;
};

template <class Reference>
struct iterator_pointer<Reference, false>
{
};

template <class Traits, bool IsConstant>
using iterator_reference_t = std::conditional_t<IsConstant, typename Traits::const_reference, typename Traits::reference>;

/// Storage is always raw: elements/groups live either in a single heap buffer
/// (accessed via to_address once) or in stack inline arrays that no allocator
/// fancy pointer could address. Keeping only raw here avoids requiring fancy
/// arithmetic across the overlaid group area and explicit raw->fancy builds
/// at every find/emplace/begin site (boost::foa keeps fancy in its iterator
/// and pays that cost via to_pointer; we have SBO on top, so raw is safer).
/// ValuePointer/GroupPointer are retained as phantom parameters so existing
/// aliases (inline_storage::iterator) keep their identity across allocators.
template <class Traits, class ValuePointer, class GroupPointer, bool IsConstant>
class hash_table_iterator : public iterator_pointer<iterator_reference_t<Traits, IsConstant>>
{
  using value_pointer = ValuePointer;
  using group_pointer = GroupPointer;

  using value_ptr_traits = std::pointer_traits<ValuePointer>;
  using group_ptr_traits = std::pointer_traits<GroupPointer>;

  using group_type       = typename group_ptr_traits::element_type;

public:
  using iterator_category = std::forward_iterator_tag;
  using iterator_concept  = std::forward_iterator_tag;
  using value_type        = typename Traits::value_type;
  using difference_type   = typename value_ptr_traits::difference_type;
  using reference         = iterator_reference_t<Traits, IsConstant>;
  using const_reference   = typename Traits::const_reference;

  /// Holds the proxy produced by operator* so operator-> can hand back a pointer
  /// to it; the temporary lives until the end of the full expression, which is
  /// all `it->member` needs.
  class arrow_proxy
  {
  public:
    constexpr explicit arrow_proxy(reference ref) noexcept
        : ref_ { ref }
    {
    }

    constexpr reference *operator->() noexcept { return std::addressof(ref_); }

  private:
    reference ref_;
  };

  constexpr hash_table_iterator() noexcept
      : element_ptr_ { nullptr }
      , group_ptr_ { nullptr }
      , slot_index_ { 0 }
  {
  }

  template <bool OtherConstant>
  constexpr hash_table_iterator(hash_table_iterator<Traits, value_pointer, group_pointer, OtherConstant> const &other) noexcept
  requires (!OtherConstant)
      : element_ptr_ { other.element_ptr_ }
      , group_ptr_ { other.group_ptr_ }
      , slot_index_ { other.slot_index_ }
  {
  }

  constexpr hash_table_iterator(const_iterator_cast_tag, hash_table_iterator<Traits, value_pointer, group_pointer, true> const &other) noexcept
      : element_ptr_ { other.element_ptr_ }
      , group_ptr_ { other.group_ptr_ }
      , slot_index_ { other.slot_index_ }
  {
  }

  constexpr reference operator* () const noexcept { return dereference_(); }

  /// A set's reference is a real const reference, so the whole iterator stays an
  /// ordinary one (`&*it` and `it->member` are real).  Only a map synthesizes a
  /// proxy, which cannot be addressed, so it is stashed in arrow_proxy instead.
  constexpr auto operator->() const noexcept
  {
    if constexpr (std::is_reference_v<reference>) {
      return std::addressof(dereference_());
    } else {
      return arrow_proxy { dereference_() };
    }
  }

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
  constexpr explicit hash_table_iterator(value_type *element_ptr, group_type *group_ptr, size_t slot_index) noexcept
      : element_ptr_ { element_ptr }
      , group_ptr_ { group_ptr }
      , slot_index_ { slot_index }
  {
  }

  constexpr value_type *element_address_() const noexcept { return element_ptr_; }

  constexpr reference dereference_() const noexcept
  {
    assert(element_ptr_ != nullptr && "dereference: iterator must not be end()");
    if constexpr (IsConstant) {
      return Traits::to_const_reference(*element_address_());
    } else {
      return Traits::to_reference(*element_address_());
    }
  }

  constexpr group_type *group_address_() const noexcept { return group_ptr_; }

  constexpr void increment_() noexcept
  {
    assert(element_ptr_ != nullptr && group_ptr_ != nullptr && "increment: cannot advance end()");
    ++element_ptr_;
    ++slot_index_;

    while (slot_index_ < group_type::capacity()) {
      if (group_ptr_->is_occupied_by_slot(slot_index_)) {
        if (group_ptr_->is_sentinel_by_slot(slot_index_)) [[unlikely]]
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
        auto slot_index = std::countr_zero(static_cast<unsigned>(mask));
        if (group_ptr_->is_sentinel_by_slot(slot_index)) [[unlikely]] {
          element_ptr_ = nullptr;
        } else {
          element_ptr_ += static_cast<difference_type>(slot_index);
          slot_index_ = slot_index;
        }
        return;
      }
      element_ptr_ += group_type::capacity();
      ++group_ptr_;
    }
  }

private:
  template <class, class, class, bool>
  friend class hash_table_iterator;
  template <class>
  friend class hash_table_lazy_iterator;
  template <class, class, class, class>
  friend class hash_table;
  template <class, class, class, class>
  friend struct inline_storage;

  value_type *element_ptr_;
  group_type *group_ptr_;
  size_t slot_index_;
};

template <class HashTableIterator>
class hash_table_lazy_iterator;

template <class Traits, class ValuePointer, class GroupPointer, bool IsConstant>
class hash_table_lazy_iterator<hash_table_iterator<Traits, ValuePointer, GroupPointer, IsConstant>>
{
  using iterator       = hash_table_iterator<Traits, ValuePointer, GroupPointer, IsConstant>;
  using const_iterator = hash_table_iterator<Traits, ValuePointer, GroupPointer, true>;

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
  constexpr hash_table_lazy_iterator(const_iterator pos) noexcept
      : pos_(pos) { };

  template <class, class, class, class>
  friend class hash_table;

  const_iterator pos_;
};
} // namespace stdex::unordered_flat_detail
