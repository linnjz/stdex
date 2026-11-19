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

#include <cstddef>      // std::size_t, std::ptrdiff_t
#include <memory>       // std::allocator_traits, std::to_address
#include <type_traits>  // std::is_trivially_destructible_v, std::is_same_v

namespace stdex::minimal_perfect_hash_detail
{
/**
 * @brief Non-owning handle to a fixed, exactly-sized runway of @c T objects.
 *
 * @details The owning container keeps the single allocator alive elsewhere and
 * stores the element count once; this handle stores only the data pointer. That
 * is intentional: a per-buffer allocator copy would defeat the whole point of
 * the minimal-perfect-hash layout (an immutable table never grows, so capacity
 * bookkeeping and allocator duplication are pure overhead). Because the handle
 * owns nothing, construction and release take the allocator as an argument at
 * the call site, and `destroy_and_deallocate` must be driven by the owner.
 *
 * The buffer never grows: the element count is fixed at build time. Element
 * lifetime is managed by the owner through @c stdex::uninitialized_* algorithms
 * and @c destroy_and_deallocate, which mirrors how @c uninitialized_buffer keeps
 * storage raw until the caller fills it.
 */
template <class T, class Allocator>
class fixed_buffer
{
public:
  using allocator_type   = typename std::allocator_traits<Allocator>::template rebind_alloc<T>;
  using allocator_traits = std::allocator_traits<allocator_type>;

  using value_type      = T;
  using size_type       = std::size_t;
  using difference_type = std::ptrdiff_t;
  using pointer         = typename allocator_traits::pointer;
  using const_pointer   = typename allocator_traits::const_pointer;
  using reference       = T &;
  using const_reference = T const &;
  using iterator        = T *;
  using const_iterator  = T const *;

  constexpr fixed_buffer() noexcept = default;
  constexpr explicit fixed_buffer(pointer const data) noexcept
      : data_ { data }
  {
  }

  [[nodiscard]] constexpr T *data() const noexcept
  {
    if constexpr (std::is_same_v<pointer, T *>) {
      return data_;
    } else {
      return data_ == pointer {} ? nullptr : std::to_address(data_);
    }
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return data_ != pointer {}; }

  [[nodiscard]] constexpr reference operator[] (size_type const index) const noexcept { return data()[index]; }

  [[nodiscard]] constexpr iterator begin() const noexcept { return data(); }
  [[nodiscard]] constexpr iterator end(size_type const count) const noexcept { return data() + count; }

  constexpr void reset() noexcept { data_ = pointer {}; }

  /// Allocate raw storage for @p count objects without constructing any of them.
  [[nodiscard]] constexpr static fixed_buffer allocate(size_type const count, allocator_type &alloc)
  {
    if (count == 0) return {};
    return fixed_buffer { allocator_traits::allocate(alloc, count) };
  }

  /// Destroy the first @p constructed elements, then release the whole @p capacity run.
  /// Safe to call on an empty handle and after a partial build (constructed < capacity).
  constexpr void destroy_and_deallocate(size_type const constructed, size_type const capacity, allocator_type &alloc) noexcept
  {
    if (data_ == pointer {}) return;
    if constexpr (!std::is_trivially_destructible_v<T>) {
      T *const raw = data();
      for (size_type i = 0; i < constructed; ++i) allocator_traits::destroy(alloc, raw + i);
    } else {
      (void) constructed;
    }
    allocator_traits::deallocate(alloc, data_, capacity);
    data_ = pointer {};
  }

private:
  pointer data_ {};
};
} // namespace stdex::minimal_perfect_hash_detail
