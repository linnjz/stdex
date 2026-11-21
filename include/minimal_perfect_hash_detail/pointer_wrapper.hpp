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

#include <concepts>     // std::convertible_to
#include <cstddef>      // std::size_t, std::ptrdiff_t
#include <memory>       // std::to_address
#include <type_traits>  // std::is_pointer_v, std::remove_pointer_t
#include <utility>      // std::declval

namespace stdex::minimal_perfect_hash_detail
{
/// A pointer-like allocator handle: either a raw @c T* or a fancy pointer that
/// @c std::to_address can reduce to @c T*. The wrapper stores only the handle;
/// ownership (allocation/deallocation) is driven by the owning container through
/// the allocator, so the immutable table carries no per-buffer allocator copy.
template <class P>
concept PointerLike = requires (P p) {
  std::to_address(p);
  { p == P {} } -> std::convertible_to<bool>;
};

template <PointerLike P>
struct pointer_wrapper
{
  using size_type       = std::size_t;
  using difference_type = std::ptrdiff_t;
  using pointer         = P;
  using value_type      = std::remove_pointer_t<decltype(std::to_address(std::declval<P>()))>;
  using reference       = value_type &;
  using const_reference = value_type const &;

  constexpr pointer_wrapper() noexcept = default;
  constexpr explicit pointer_wrapper(pointer const data) noexcept
      : data_ { data }
  {
  }

  [[nodiscard]] constexpr value_type *data() noexcept
  {
    if constexpr (std::is_pointer_v<P>) {
      return data_;
    } else {
      return data_ == pointer {} ? nullptr : std::to_address(data_);
    }
  }

  [[nodiscard]] constexpr value_type const *data() const noexcept
  {
    if constexpr (std::is_pointer_v<P>) {
      return data_;
    } else {
      return data_ == pointer {} ? nullptr : std::to_address(data_);
    }
  }

  [[nodiscard]] constexpr pointer get() const noexcept { return data_; }

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return data_ != pointer {}; }

  [[nodiscard]] constexpr reference operator[] (size_type const index) noexcept { return data()[index]; }

  [[nodiscard]] constexpr const_reference operator[] (size_type const index) const noexcept { return data()[index]; }

  constexpr void reset() noexcept { data_ = pointer {}; }

  pointer data_ {};
};
} // namespace stdex::minimal_perfect_hash_detail
