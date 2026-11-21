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

#include <cassert>      // assert
#include <cstddef>      // std::size_t
#include <memory>       // std::construct_at, std::destroy, std::addressof
#include <type_traits>  // std::is_nothrow_move_constructible_v, std::is_trivially_destructible_v
#include <utility>      // std::forward

#include "../inplace_storage.hpp"
#include "../uninitialized_algorithms.hpp"

namespace stdex::minimal_perfect_hash_detail
{
/// Fixed-capacity inline storage buffer supporting arbitrary-index placement.
///
/// Unlike @c inplace_vector, whose live elements must form the contiguous prefix
/// `[0, size())`, this buffer lets the minimal-perfect-hash builder construct
/// each element directly at its final index via @c emplace_at, and publish the
/// element count afterwards with @c set_size.
///
/// This enables single-pass stack construction: no permutation / @c order scratch
/// space, and no move-assignability requirement for the value type @c T.
///
/// @note The buffer does **not** track which individual slots are live.
///       @c size_ is merely the caller-maintained element count; it does not imply
///       that indices `[0, size())` are valid. Validity of each index is managed by
///       the upper MPH builder.
///
/// The underlying storage is raw (@c inplace_storage), so @c T need not be
/// default-constructible. This buffer owns the lifetime of placed elements.
template <class T, std::size_t N>
class random_placement_buffer
{
  static_assert(N > 0, "random_placement_buffer requires a non-zero capacity");

  using raw_storage = ::stdex::inplace_storage<::stdex::inplace_storage_detail::storage_no_size_base, T, N>;

public:
  using value_type      = raw_storage::value_type;
  using size_type       = raw_storage::size_type;
  using difference_type = raw_storage::difference_type;
  using pointer         = raw_storage::pointer;
  using const_pointer   = raw_storage::const_pointer;
  using reference       = raw_storage::reference;
  using const_reference = raw_storage::const_reference;

  constexpr random_placement_buffer() noexcept = default;

  constexpr random_placement_buffer(random_placement_buffer const &other)
  {
    (void) ::stdex::uninitialized_copy_n(other.data(), static_cast<difference_type>(other.size_), data(), data() + other.size_);
    size_ = other.size_;
  }

  constexpr random_placement_buffer(random_placement_buffer &&other) noexcept(std::is_nothrow_move_constructible_v<T>)
  {
    (void) ::stdex::uninitialized_move_n(other.data(), static_cast<difference_type>(other.size_), data(), data() + other.size_);
    size_ = other.size_;
  }

  constexpr random_placement_buffer &operator= (random_placement_buffer const &other)
  {
    if (this == std::addressof(other)) return *this;
    destroy_all_();
    (void) ::stdex::uninitialized_copy_n(other.data(), static_cast<difference_type>(other.size_), data(), data() + other.size_);
    size_ = other.size_;
    return *this;
  }

  constexpr random_placement_buffer &operator= (random_placement_buffer &&other) noexcept(std::is_nothrow_move_constructible_v<T>)
  {
    if (this == std::addressof(other)) return *this;
    destroy_all_();
    (void) ::stdex::uninitialized_move_n(other.data(), static_cast<difference_type>(other.size_), data(), data() + other.size_);
    size_ = other.size_;
    return *this;
  }

  constexpr ~random_placement_buffer() { destroy_all_(); }

  template <class... Args>
  constexpr reference emplace_at(size_type const index, Args &&...args)
      noexcept(std::is_nothrow_constructible_v<T, Args...>)
  {
    assert(index < N);
    return *std::construct_at(data() + index, std::forward<Args>(args)...);
  }

  constexpr void set_size(size_type const count) noexcept { size_ = count; }

  [[nodiscard]] constexpr T *data() noexcept { return storage_.data(); }
  [[nodiscard]] constexpr T const *data() const noexcept { return storage_.data(); }
  [[nodiscard]] constexpr size_type size() const noexcept { return size_; }
  [[nodiscard]] constexpr reference operator[] (size_type const index) noexcept { return data()[index]; }
  [[nodiscard]] constexpr const_reference operator[] (size_type const index) const noexcept { return data()[index]; }

private:
  constexpr void destroy_all_() noexcept
  {
    if constexpr (!std::is_trivially_destructible_v<T>) {
      std::destroy(data(), data() + static_cast<difference_type>(size_));
    }
    size_ = 0;
  }

  raw_storage storage_ {};
  size_type size_ { 0 };
};
} // namespace stdex::minimal_perfect_hash_detail
