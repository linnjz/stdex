#pragma once

#include <algorithm>    // std::copy_n
#include <concepts>     // std::constructible_from
#include <cstddef>      // std::size_t
#include <iterator>     // std::iter_difference_t
#include <memory>       // std::allocator, std::allocator_traits, std::to_address
#include <type_traits>  // std::is_same_v, std::is_trivially_copyable_v, std::is_trivially_default_constructible_v, ...
#include <utility>      // std::exchange, std::forward, std::move, std::move_if_noexcept

#include "uninitialized_algorithms.hpp"

namespace stdex
{
/**
 * @brief Fixed-capacity raw storage for trivially default-constructible types.
 *
 * @details Storage is allocated through @p Allocator, and elements are left
 * uninitialized until the caller fills them. Allocator-aware uninitialized
 * algorithms are used when the buffer itself has to copy or move elements.
 *
 * Copy construction and copy assignment are enabled only when the element
 * type is trivially copyable and the corresponding copy operation is
 * available, so traits such as std::is_copy_constructible_v report what the
 * buffer can actually instantiate.
 */
template <class T, class Allocator = std::allocator<T>>
class uninitialized_buffer
{
public:
  using allocator_type  = typename std::allocator_traits<Allocator>::template rebind_alloc<T>;
  using value_type      = T;
  using size_type       = typename std::allocator_traits<allocator_type>::size_type;
  using difference_type = typename std::allocator_traits<allocator_type>::difference_type;
  using pointer         = typename std::allocator_traits<allocator_type>::pointer;
  using const_pointer   = typename std::allocator_traits<allocator_type>::const_pointer;
  using reference       = T &;
  using const_reference = T const &;

private:
  using allocator_traits = std::allocator_traits<allocator_type>;

  static_assert(std::is_same_v<typename allocator_traits::value_type, T>,
                "uninitialized_buffer requires an allocator whose rebound value_type is T");
  static_assert(std::is_trivially_default_constructible_v<T> && std::is_trivially_destructible_v<T>,
                "uninitialized_buffer requires a trivially default-constructible type");

  struct allocation_guard
  {
    pointer data_;
    size_type size_;
    allocator_type &allocator_;

    allocation_guard &operator= (allocation_guard const &) = delete;
    allocation_guard &operator= (allocation_guard &&)      = delete;

    constexpr ~allocation_guard() noexcept
    {
      if (data_ != pointer {}) {
        allocator_traits::deallocate(allocator_, data_, size_);
      }
    }

    constexpr void dismiss() noexcept { data_ = pointer {}; }
  };

public:
  constexpr uninitialized_buffer() noexcept(std::is_nothrow_default_constructible_v<allocator_type>) = default;

  template <class Al = allocator_type>
  requires std::constructible_from<allocator_type, Al>
  constexpr explicit uninitialized_buffer(size_type const count, Al &&allocator = allocator_type {})
      : allocator_(std::forward<Al>(allocator))
      , count_(count)
  {
    if (count_ != 0) {
      data_ = allocator_traits::allocate(allocator_, count_);
      allocation_guard guard { data_, count_, allocator_ };
      ::stdex::uninitialized_default_construct_n(std::to_address(data_), static_cast<std::iter_difference_t<T *>>(count_), allocator_);
      guard.dismiss();
    }
  }

  constexpr uninitialized_buffer(uninitialized_buffer const &other)
  requires (std::is_trivially_copyable_v<T> && std::is_copy_constructible_v<T> &&
            std::is_copy_constructible_v<allocator_type>)
      : allocator_(allocator_traits::select_on_container_copy_construction(other.allocator_))
      , count_(other.count_)
  {
    if (count_ != 0) {
      data_ = allocator_traits::allocate(allocator_, count_);
      allocation_guard guard { data_, count_, allocator_ };
      ::stdex::uninitialized_copy_n(other.data(), static_cast<std::iter_difference_t<T *>>(count_),
                                    data(), data() + count_, allocator_);
      guard.dismiss();
    }
  }

  uninitialized_buffer(uninitialized_buffer const &)
  requires (!(std::is_trivially_copyable_v<T> && std::is_copy_constructible_v<T> &&
              std::is_copy_constructible_v<allocator_type>))
  = delete;

  template <class Al = allocator_type>
  requires (std::is_trivially_copyable_v<T> && std::is_copy_constructible_v<T> &&
            std::constructible_from<allocator_type, Al>)
  constexpr uninitialized_buffer(uninitialized_buffer const &other, Al &&allocator)
      : allocator_(std::forward<Al>(allocator))
      , count_(other.count_)
  {
    if (count_ != 0) {
      data_ = allocator_traits::allocate(allocator_, count_);
      allocation_guard guard { data_, count_, allocator_ };
      ::stdex::uninitialized_copy_n(other.data(), static_cast<std::iter_difference_t<T *>>(count_),
                                    data(), data() + count_, allocator_);
      guard.dismiss();
    }
  }

  constexpr uninitialized_buffer &operator= (uninitialized_buffer const &other)
  requires (std::is_trivially_copyable_v<T> && std::is_copy_constructible_v<T> &&
            std::is_copy_assignable_v<T> && std::is_copy_assignable_v<allocator_type>)
  {
    if (this == std::addressof(other)) return *this;

    if constexpr (allocator_traits::propagate_on_container_copy_assignment::value) {
      if constexpr (!allocator_traits::is_always_equal::value) {
        if (!(allocator_ == other.allocator_)) {
          release();
          allocator_ = other.allocator_;
        }
      } else {
        allocator_ = other.allocator_;
      }
    }

    if (count_ == other.count_) {
      std::copy_n(other.data(), count_, data());
      return *this;
    }

    pointer new_data {};
    size_type const count = other.count_;
    if (count != 0) {
      new_data = allocator_traits::allocate(allocator_, count);
      allocation_guard guard { new_data, count, allocator_ };
      ::stdex::uninitialized_copy_n(other.data(), static_cast<std::iter_difference_t<T *>>(count),
                                    std::to_address(new_data), std::to_address(new_data) + count, allocator_);
      guard.dismiss();
    }

    release();
    data_  = new_data;
    count_ = count;
    return *this;
  }

  uninitialized_buffer &operator= (uninitialized_buffer const &)
  requires (!(std::is_trivially_copyable_v<T> && std::is_copy_constructible_v<T> &&
              std::is_copy_assignable_v<T> && std::is_copy_assignable_v<allocator_type>))
  = delete;

  constexpr uninitialized_buffer(uninitialized_buffer &&other) noexcept(std::is_nothrow_move_constructible_v<allocator_type>)
      : allocator_(std::move(other.allocator_))
      , data_(std::exchange(other.data_, pointer {}))
      , count_(std::exchange(other.count_, 0))
  {
  }

  template <class Al = allocator_type>
  requires std::constructible_from<allocator_type, Al>
  constexpr uninitialized_buffer(uninitialized_buffer &&other, Al &&allocator)
      noexcept(std::is_nothrow_constructible_v<allocator_type, Al>)
      : allocator_(std::forward<Al>(allocator))
      , data_(std::exchange(other.data_, pointer {}))
      , count_(std::exchange(other.count_, 0))
  {
  }

  constexpr uninitialized_buffer &operator= (uninitialized_buffer &&other)
      requires (allocator_traits::propagate_on_container_move_assignment::value ||
                allocator_traits::is_always_equal::value || std::is_move_constructible_v<T>)
  {
    if (this == std::addressof(other)) return *this;

    if constexpr (allocator_traits::propagate_on_container_move_assignment::value ||
                  allocator_traits::is_always_equal::value) {
      release();
      if constexpr (allocator_traits::propagate_on_container_move_assignment::value) {
        allocator_ = std::move(other.allocator_);
      }
      data_  = std::exchange(other.data_, pointer {});
      count_ = std::exchange(other.count_, 0);
    } else {
      if constexpr (requires { allocator_ == other.allocator_; }) {
        if (allocator_ == other.allocator_) {
          release();
          data_  = std::exchange(other.data_, pointer {});
          count_ = std::exchange(other.count_, 0);
          return *this;
        }
      }

      // Same-sized buffers owned by unequal allocators can be overwritten
      // element-wise; this avoids an allocation and keeps both allocations
      // attached to their original allocators.
      if constexpr (std::is_move_assignable_v<T>) {
        if (count_ == other.count_) {
          for (size_type i = 0; i < count_; ++i) data()[i] = std::move(other.data()[i]);
          other.release();
          return *this;
        }
      }

      size_type const count = other.count_;
      pointer new_data {};
      if (count != 0) {
        new_data = allocator_traits::allocate(allocator_, count);
        allocation_guard guard { new_data, count, allocator_ };
        ::stdex::uninitialized_move_n(other.data(), static_cast<std::iter_difference_t<T *>>(count),
                                      std::to_address(new_data), std::to_address(new_data) + count, allocator_);
        guard.dismiss();
      }
      release();
      data_  = new_data;
      count_ = count;
      other.release();
    }
    return *this;
  }

  constexpr ~uninitialized_buffer() noexcept { release(); }

  [[nodiscard]] constexpr allocator_type get_allocator() const noexcept(std::is_nothrow_copy_constructible_v<allocator_type>)
  {
    return allocator_;
  }

  [[nodiscard]] constexpr T *data() noexcept
  {
    if constexpr (std::is_same_v<pointer, T *>) {
      return data_;
    } else {
      return data_ == pointer {} ? nullptr : std::to_address(data_);
    }
  }

  [[nodiscard]] constexpr T const *data() const noexcept
  {
    if constexpr (std::is_same_v<pointer, T *>) {
      return data_;
    } else {
      return data_ == pointer {} ? nullptr : std::to_address(data_);
    }
  }

  [[nodiscard]] constexpr bool empty() const noexcept { return count_ == 0; }
  [[nodiscard]] constexpr size_type max_size() const noexcept { return allocator_traits::max_size(allocator_); }
  [[nodiscard]] constexpr size_type size_bytes() const noexcept { return count_ * sizeof(T); }
  [[nodiscard]] constexpr size_type size() const noexcept { return count_; }

  [[nodiscard]] constexpr T *begin() noexcept { return data(); }
  [[nodiscard]] constexpr T *end() noexcept
  {
    T *p = data();
    return p == nullptr ? p : p + count_;
  }

  [[nodiscard]] constexpr T const *begin() const noexcept { return data(); }
  [[nodiscard]] constexpr T const *end() const noexcept
  {
    T const *p = data();
    return p == nullptr ? p : p + count_;
  }

  [[nodiscard]] constexpr T const *cbegin() const noexcept { return data(); }
  [[nodiscard]] constexpr T const *cend() const noexcept { return end(); }

  [[nodiscard]] constexpr T &operator[] (size_type const index) noexcept { return data()[index]; }
  [[nodiscard]] constexpr T const &operator[] (size_type const index) const noexcept { return data()[index]; }

private:
  constexpr void release() noexcept
  {
    if (data_ != pointer {}) {
      allocator_traits::deallocate(allocator_, data_, count_);
    }
    data_  = pointer {};
    count_ = 0;
  }

  [[no_unique_address]] allocator_type allocator_;
  pointer data_ {};
  size_type count_ {};
};
} // namespace stdex
