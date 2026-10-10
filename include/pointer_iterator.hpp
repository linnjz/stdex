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

#include <concepts>    // std::convertible_to
#include <iterator>    // std::random_access_iterator_tag, std::contiguous_iterator_tag
#include <memory>      // std::pointer_traits
#include <type_traits> // std::is_const_v, std::is_same_v, ...

namespace stdex
{
template <class ValueType, class Pointer, class Container>
struct pointer_iterator
{
  static_assert(!std::is_const_v<ValueType>, "ValueType must be non-const; constness comes from Pointer");

  using difference_type   = typename std::pointer_traits<Pointer>::difference_type;
  using value_type        = ValueType;
  using pointer           = Pointer;
  using reference         = decltype(*std::declval<Pointer const &>());
  using iterator_category = std::random_access_iterator_tag;
  using iterator_concept  = std::contiguous_iterator_tag;

  constexpr pointer_iterator() noexcept = default;

  explicit constexpr pointer_iterator(pointer ptr) noexcept
      : base_ { ptr }
  {
  }

  template <class OtherPointer>
  requires (!std::is_same_v<OtherPointer, pointer> && std::convertible_to<OtherPointer const &, pointer>)
  constexpr pointer_iterator(pointer_iterator<value_type, OtherPointer, Container> const &other) noexcept(
      std::is_nothrow_constructible_v<pointer, OtherPointer const &>)
      : base_ { other.base() }
  {
  }

  [[nodiscard]] constexpr pointer base() const noexcept { return base_; }
  [[nodiscard]] constexpr reference operator* () const noexcept { return *base_; }
  [[nodiscard]] constexpr pointer operator->() const noexcept { return base_; }
  [[nodiscard]] constexpr reference operator[] (difference_type index) const noexcept { return *(base_ + index); }

  constexpr pointer_iterator &operator++ () noexcept
  {
    ++base_;
    return *this;
  }

  constexpr pointer_iterator operator++ (int) noexcept
  {
    auto tmp = *this;
    ++base_;
    return tmp;
  }

  constexpr pointer_iterator &operator-- () noexcept
  {
    --base_;
    return *this;
  }

  constexpr pointer_iterator operator-- (int) noexcept
  {
    auto tmp = *this;
    --base_;
    return tmp;
  }

  constexpr pointer_iterator &operator+= (difference_type n) noexcept
  {
    base_ += n;
    return *this;
  }

  constexpr pointer_iterator &operator-= (difference_type n) noexcept
  {
    base_ -= n;
    return *this;
  }

  [[nodiscard]] constexpr pointer_iterator operator+ (difference_type n) const noexcept
  {
    auto tmp = *this;
    tmp += n;
    return tmp;
  }

  [[nodiscard]] constexpr pointer_iterator operator- (difference_type n) const noexcept
  {
    auto tmp = *this;
    tmp -= n;
    return tmp;
  }

  template <class OtherPointer>
  [[nodiscard]] constexpr difference_type operator- (pointer_iterator<value_type, OtherPointer, Container> const &other) const noexcept
  {
    return base_ - other.base();
  }

  /// Only participates when the underlying pointers are three-way comparable;
  /// fancy pointers without <=> keep using the six classic operators above.
  template <class OtherPointer>
  requires requires (pointer const &a, OtherPointer const &b) { a <=> b; }
  [[nodiscard]] constexpr auto operator<=> (pointer_iterator<value_type, OtherPointer, Container> const &other) const
      noexcept(noexcept(std::declval<pointer const &>() <=> std::declval<OtherPointer const &>()))
  {
    return base_ <=> other.base();
  }

private:
  pointer base_ {};
};

template <class V, class P, class C>
[[nodiscard]] constexpr pointer_iterator<V, P, C> operator+ (
    typename pointer_iterator<V, P, C>::difference_type n,
    pointer_iterator<V, P, C> it) noexcept
{
  return it += n;
}

template <class V, class P1, class P2, class C>
requires requires (P1 const &a, P2 const &b) { a == b; }
[[nodiscard]] constexpr bool operator== (
    pointer_iterator<V, P1, C> const &a,
    pointer_iterator<V, P2, C> const &b) noexcept(noexcept(
    std::declval<P1 const &>() == std::declval<P2 const &>()))
{
  return a.base() == b.base();
}

template <class V, class P1, class P2, class C>
requires requires (P1 const &a, P2 const &b) { a == b; }
[[nodiscard]] constexpr bool operator!= (
    pointer_iterator<V, P1, C> const &a,
    pointer_iterator<V, P2, C> const &b) noexcept(noexcept(
    std::declval<P1 const &>() == std::declval<P2 const &>()))
{
  return !(a == b);
}

template <class V, class P1, class P2, class C>
requires requires (P1 const &a, P2 const &b) { a < b; }
[[nodiscard]] constexpr bool operator< (
    pointer_iterator<V, P1, C> const &a,
    pointer_iterator<V, P2, C> const &b) noexcept(noexcept(
    std::declval<P1 const &>() < std::declval<P2 const &>()))
{
  return a.base() < b.base();
}

template <class V, class P1, class P2, class C>
requires requires (P1 const &a, P2 const &b) { a < b; }
[[nodiscard]] constexpr bool operator> (
    pointer_iterator<V, P1, C> const &a,
    pointer_iterator<V, P2, C> const &b) noexcept(noexcept(
    std::declval<P1 const &>() < std::declval<P2 const &>()))
{
  return b < a;
}

template <class V, class P1, class P2, class C>
requires requires (P1 const &a, P2 const &b) { a < b; }
[[nodiscard]] constexpr bool operator<= (
    pointer_iterator<V, P1, C> const &a,
    pointer_iterator<V, P2, C> const &b) noexcept(noexcept(
    std::declval<P1 const &>() < std::declval<P2 const &>()))
{
  return !(b < a);
}

template <class V, class P1, class P2, class C>
requires requires (P1 const &a, P2 const &b) { a < b; }
[[nodiscard]] constexpr bool operator>= (
    pointer_iterator<V, P1, C> const &a,
    pointer_iterator<V, P2, C> const &b) noexcept(noexcept(
    std::declval<P1 const &>() < std::declval<P2 const &>()))
{
  return !(a < b);
}
} // namespace stdex

namespace std
{
template <class V, class P, class C>
struct pointer_traits<stdex::pointer_iterator<V, P, C>>
{
  using pointer         = stdex::pointer_iterator<V, P, C>;
  using element_type    = typename pointer_traits<P>::element_type;
  using difference_type = typename pointer::difference_type;

  [[nodiscard]] static constexpr element_type *to_address(pointer const &it) noexcept(
      noexcept(std::to_address(it.base())))
  {
    return std::to_address(it.base());
  }
};
} // namespace std
