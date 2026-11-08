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

#include <concepts>    // std::convertible_to, std::same_as
#include <iterator>    // std::input_iterator_tag, std::contiguous_iterator_tag
#include <memory>      // std::pointer_traits, std::to_address
#include <type_traits> // std::is_const_v, std::remove_cv_t, ...
#include <utility>     // std::declval, std::move

namespace stdex
{
template <class P>
concept pointer_incrementable = requires (P &p) { ++p; };

template <class P>
concept pointer_decrementable = pointer_incrementable<P> && requires (P &p) { --p; };

template <class P>
concept pointer_input =
    pointer_incrementable<P> &&
    std::movable<P> &&
    requires (P const &p) { *p; };

template <class P>
concept pointer_equality_comparable = requires (P const &a, P const &b) {
  { a == b } -> std::convertible_to<bool>;
};

// Only the operations the adapter actually uses: >, <=, >=, and n + p are
// synthesized by the iterator's own operators, and P's compound-assignment
// return type is irrelevant.
template <class P>
concept pointer_arithmetic =
    pointer_decrementable<P> &&
    requires (P &p, P const &a, P const &b, typename std::pointer_traits<P>::difference_type n) {
      { a + n } -> std::convertible_to<P>;
      { a - n } -> std::convertible_to<P>;
      { p += n };
      { p -= n };
      { a - b } -> std::convertible_to<typename std::pointer_traits<P>::difference_type>;
      { a == b } -> std::convertible_to<bool>;
      { a < b } -> std::convertible_to<bool>;
    };

template <class P>
concept pointer_forward =
    pointer_input<P> &&
    pointer_equality_comparable<P> &&
    std::copyable<P> &&
    std::default_initializable<P>;

template <class P>
concept pointer_bidirectional = pointer_forward<P> && pointer_decrementable<P>;

template <class P>
concept pointer_random_access = pointer_bidirectional<P> && pointer_arithmetic<P>;

// std::to_address is not SFINAE-friendly for pointers that support neither
// operator-> nor a pointer_traits::to_address specialization, so probe those
// first and only then require the to_address result type.
template <class P>
concept pointer_addressable =
    std::is_pointer_v<P> ||
    requires (P const &p) { p.operator->(); } ||
    requires (P const &p) {
      { std::pointer_traits<P>::to_address(p) }
          -> std::same_as<typename std::pointer_traits<P>::element_type *>;
    };

// Contiguity is a semantic promise that the underlying pointer's arithmetic
// is linear; the concept can only verify addressability and reference shape.
template <class P>
concept pointer_contiguous =
    pointer_random_access<P> &&
    std::is_lvalue_reference_v<decltype(*std::declval<P const &>())> &&
    pointer_addressable<P> &&
    requires (P const &p) {
      { std::to_address(p) }
          -> std::same_as<typename std::pointer_traits<P>::element_type *>;
    };

template <class ValueType, class Pointer, class Container = void>
struct fancy_pointer_iterator
{
  static_assert(!std::is_const_v<ValueType>, "ValueType must be non-const; constness comes from Pointer");
  static_assert(std::is_object_v<ValueType>, "ValueType must be an object type");

  using difference_type = typename std::pointer_traits<Pointer>::difference_type;
  using value_type      = ValueType;
  using pointer         = Pointer;
  using reference       = decltype(*std::declval<Pointer const &>());

  static_assert(pointer_input<Pointer>, "Pointer must be movable, dereferenceable, and incrementable");
  static_assert(std::same_as<std::remove_cv_t<typename std::pointer_traits<Pointer>::element_type>, ValueType>,
                "ValueType must match the pointer element type");
  static_assert(std::convertible_to<reference, ValueType>,
                "Dereferencing Pointer must produce a value convertible to ValueType");

private:
  template <class U>
  static constexpr bool nothrow_make_pointer_() noexcept
  {
    if constexpr (std::constructible_from<pointer, U *>) {
      return std::is_nothrow_constructible_v<pointer, U *>;
    } else {
      return std::is_nothrow_default_constructible_v<pointer> &&
             noexcept(std::pointer_traits<pointer>::pointer_to(*std::declval<U *>()));
    }
  }

  template <class U>
  static constexpr pointer make_pointer_(U *ptr)
  {
    if constexpr (std::constructible_from<pointer, U *>) {
      return pointer(ptr);
    } else {
      return ptr ? std::pointer_traits<pointer>::pointer_to(*ptr) : pointer {};
    }
  }

  static constexpr auto deduce_concept() noexcept
  {
    if constexpr (pointer_contiguous<Pointer>)
      return std::contiguous_iterator_tag {};
    else if constexpr (pointer_random_access<Pointer>)
      return std::random_access_iterator_tag {};
    else if constexpr (pointer_bidirectional<Pointer>)
      return std::bidirectional_iterator_tag {};
    else if constexpr (pointer_forward<Pointer>)
      return std::forward_iterator_tag {};
    else
      return std::input_iterator_tag {};
  }

  // iterator_category predates contiguous_iterator_tag; reporting random
  // access there keeps pre-C++20 algorithms from seeing a foreign tag.
  static constexpr auto deduce_category() noexcept
  {
    if constexpr (pointer_contiguous<Pointer> || pointer_random_access<Pointer>)
      return std::random_access_iterator_tag {};
    else if constexpr (pointer_bidirectional<Pointer>)
      return std::bidirectional_iterator_tag {};
    else if constexpr (pointer_forward<Pointer>)
      return std::forward_iterator_tag {};
    else
      return std::input_iterator_tag {};
  }

public:
  using iterator_concept  = decltype(deduce_concept());
  using iterator_category = decltype(deduce_category());

  constexpr fancy_pointer_iterator() = default;

  explicit constexpr fancy_pointer_iterator(pointer ptr)
      noexcept(std::is_nothrow_move_constructible_v<pointer>)
      : base_ { std::move(ptr) }
  {
  }

  template <class U>
  requires std::same_as<std::remove_cv_t<U>, value_type> &&
           (!std::same_as<U *, pointer>) &&
           (std::constructible_from<pointer, U *> ||
            requires (U *p) { std::pointer_traits<pointer>::pointer_to(*p); })
  explicit constexpr fancy_pointer_iterator(U *ptr)
      noexcept(nothrow_make_pointer_<U>())
      : base_ { make_pointer_(ptr) }
  {
  }

  template <class OtherPointer>
  requires (!std::is_same_v<OtherPointer, pointer> && std::convertible_to<OtherPointer const &, pointer>)
  constexpr fancy_pointer_iterator(
      fancy_pointer_iterator<value_type, OtherPointer, Container> const &other)
      noexcept(std::is_nothrow_constructible_v<pointer, OtherPointer const &>)
      : base_ { other.base() }
  {
  }

  [[nodiscard]] constexpr pointer const &base() const noexcept { return base_; }

  [[nodiscard]] constexpr reference operator* () const
      noexcept(noexcept(*std::declval<pointer const &>()))
  {
    return *base_;
  }

  // Like standard containers, operator-> returns the stored fancy pointer;
  // use std::to_address(it) when the raw element address is required.
  [[nodiscard]] constexpr pointer operator->() const
      noexcept(std::is_nothrow_copy_constructible_v<pointer>)
  {
    return base_;
  }

  constexpr fancy_pointer_iterator &operator++ ()
      noexcept(noexcept(++std::declval<pointer &>()))
  {
    ++base_;
    return *this;
  }

  constexpr fancy_pointer_iterator operator++ (int)
      noexcept(std::is_nothrow_copy_constructible_v<pointer> &&
               std::is_nothrow_move_constructible_v<pointer> &&
               noexcept(++std::declval<pointer &>()))
  {
    auto tmp = *this;
    ++base_;
    return tmp;
  }

  constexpr fancy_pointer_iterator &operator-- ()
      noexcept(noexcept(--std::declval<pointer &>()))
  requires pointer_decrementable<pointer>
  {
    --base_;
    return *this;
  }

  constexpr fancy_pointer_iterator operator-- (int)
      noexcept(std::is_nothrow_copy_constructible_v<pointer> &&
               std::is_nothrow_move_constructible_v<pointer> &&
               noexcept(--std::declval<pointer &>()))
  requires pointer_decrementable<pointer>
  {
    auto tmp = *this;
    --base_;
    return tmp;
  }

  constexpr fancy_pointer_iterator &operator+= (difference_type n)
      noexcept(noexcept(std::declval<pointer &>() += std::declval<difference_type>()))
  requires pointer_arithmetic<pointer>
  {
    base_ += n;
    return *this;
  }

  constexpr fancy_pointer_iterator &operator-= (difference_type n)
      noexcept(noexcept(std::declval<pointer &>() -= std::declval<difference_type>()))
  requires pointer_arithmetic<pointer>
  {
    base_ -= n;
    return *this;
  }

  [[nodiscard]] constexpr fancy_pointer_iterator operator+ (difference_type n) const
      noexcept(std::is_nothrow_copy_constructible_v<pointer> &&
               std::is_nothrow_move_constructible_v<pointer> &&
               noexcept(std::declval<pointer &>() += std::declval<difference_type>()))
  requires pointer_arithmetic<pointer>
  {
    auto tmp = *this;
    tmp += n;
    return tmp;
  }

  [[nodiscard]] constexpr fancy_pointer_iterator operator- (difference_type n) const
      noexcept(std::is_nothrow_copy_constructible_v<pointer> &&
               std::is_nothrow_move_constructible_v<pointer> &&
               noexcept(std::declval<pointer &>() -= std::declval<difference_type>()))
  requires pointer_arithmetic<pointer>
  {
    auto tmp = *this;
    tmp -= n;
    return tmp;
  }

  [[nodiscard]] constexpr reference operator[] (difference_type n) const
      noexcept(noexcept(*(std::declval<pointer const &>() + std::declval<difference_type>())))
  requires pointer_arithmetic<pointer>
  {
    return *(base_ + n);
  }

  template <class OtherPointer>
  requires requires (pointer const &a, OtherPointer const &b) {
    { a - b } -> std::convertible_to<difference_type>;
  }
  [[nodiscard]] constexpr difference_type operator- (
      fancy_pointer_iterator<value_type, OtherPointer, Container> const &other) const
      noexcept(noexcept(std::declval<pointer const &>() - std::declval<OtherPointer const &>()))
  {
    return base_ - other.base();
  }

  template <class OtherPointer>
  requires requires (pointer const &a, OtherPointer const &b) { a <=> b; }
  [[nodiscard]] constexpr auto operator<=> (
      fancy_pointer_iterator<value_type, OtherPointer, Container> const &other) const
      noexcept(noexcept(std::declval<pointer const &>() <=> std::declval<OtherPointer const &>()))
  {
    return base_ <=> other.base();
  }

private:
  pointer base_ {};
};

template <class V, class P, class C>
[[nodiscard]] constexpr fancy_pointer_iterator<V, P, C> operator+ (
    typename fancy_pointer_iterator<V, P, C>::difference_type n,
    fancy_pointer_iterator<V, P, C> it)
    noexcept(std::is_nothrow_move_constructible_v<fancy_pointer_iterator<V, P, C>> &&
             noexcept(std::declval<P &>() += std::declval<typename fancy_pointer_iterator<V, P, C>::difference_type>()))
requires pointer_arithmetic<P>
{
  it += n;
  return it;
}

template <class V, class P1, class P2, class C>
requires requires (P1 const &a, P2 const &b) { a == b; }
[[nodiscard]] constexpr bool operator== (
    fancy_pointer_iterator<V, P1, C> const &a,
    fancy_pointer_iterator<V, P2, C> const &b)
        noexcept(noexcept(std::declval<P1 const &>() == std::declval<P2 const &>()))
{
  return a.base() == b.base();
}

template <class V, class P1, class P2, class C>
requires requires (P1 const &a, P2 const &b) { a == b; }
[[nodiscard]] constexpr bool operator!= (
    fancy_pointer_iterator<V, P1, C> const &a,
    fancy_pointer_iterator<V, P2, C> const &b)
        noexcept(noexcept(std::declval<P1 const &>() == std::declval<P2 const &>()))
{
  return !(a == b);
}

template <class V, class P1, class P2, class C>
requires requires (P1 const &a, P2 const &b) { a < b; }
[[nodiscard]] constexpr bool operator< (
    fancy_pointer_iterator<V, P1, C> const &a,
    fancy_pointer_iterator<V, P2, C> const &b)
        noexcept(noexcept(std::declval<P1 const &>() < std::declval<P2 const &>()))
{
  return a.base() < b.base();
}

template <class V, class P1, class P2, class C>
requires requires (P1 const &a, P2 const &b) { b < a; }
[[nodiscard]] constexpr bool operator> (
    fancy_pointer_iterator<V, P1, C> const &a,
    fancy_pointer_iterator<V, P2, C> const &b)
        noexcept(noexcept(std::declval<P2 const &>() < std::declval<P1 const &>()))
{
  return b < a;
}

template <class V, class P1, class P2, class C>
requires requires (P1 const &a, P2 const &b) { b < a; }
[[nodiscard]] constexpr bool operator<= (
    fancy_pointer_iterator<V, P1, C> const &a,
    fancy_pointer_iterator<V, P2, C> const &b)
        noexcept(noexcept(std::declval<P2 const &>() < std::declval<P1 const &>()))
{
  return !(b < a);
}

template <class V, class P1, class P2, class C>
requires requires (P1 const &a, P2 const &b) { a < b; }
[[nodiscard]] constexpr bool operator>= (
    fancy_pointer_iterator<V, P1, C> const &a,
    fancy_pointer_iterator<V, P2, C> const &b)
        noexcept(noexcept(std::declval<P1 const &>() < std::declval<P2 const &>()))
{
  return !(a < b);
}

} // namespace stdex

namespace std
{
template <class V, class P, class C>
struct pointer_traits<stdex::fancy_pointer_iterator<V, P, C>>
{
  using pointer         = stdex::fancy_pointer_iterator<V, P, C>;
  using element_type    = typename pointer_traits<P>::element_type;
  using difference_type = typename pointer::difference_type;

  [[nodiscard]] static constexpr element_type *to_address(pointer const &it) noexcept(
      noexcept(std::to_address(it.base())))
  {
    return std::to_address(it.base());
  }
};
} // namespace std
