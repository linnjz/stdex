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

#include <array>       // std::array
#include <bit>         // std::bit_cast
#include <cassert>     // assert
#include <concepts>    // std::constructible_from
#include <cstddef>     // size_t
#include <cstring>     // std::memcpy, std::memset
#include <iterator>    // std::contiguous_iterator, std::iter_difference_t, std::iter_value_t,
#include <memory>      // std::to_address, std::construct_at, std::destroy
#include <new>         // ::new (placement)
#include <type_traits> // std::is_same_v, std::remove_const_t, ...
#include <utility>     // std::forward, std::move, std::pair

#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wattributes"
#elif defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wattributes"
#elif defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable : 5030)
#endif

namespace stdex
{
namespace uninitialized_detail
{
template <class T>
struct unwrap_enum
{
  using type = T;
};

template <class T>
requires (std::is_enum_v<T>)
struct unwrap_enum<T>
{
  using type = std::underlying_type_t<T>;
};

template <class T>
using unwrap_enum_t = typename unwrap_enum<T>::type;

template <class DestRef, class SourceRef>
concept is_trivially_assignable_returning_same_reference = 
    std::is_trivially_assignable_v<DestRef, SourceRef> && 
    requires { { std::declval<DestRef>() = std::declval<SourceRef>() } -> std::same_as<DestRef>; };

template <class Source, class Dest>
concept is_pointer_address_convertible = std::disjunction_v<std::is_void<Source>, std::is_void<Dest>,
                       std::is_same<std::remove_cv_t<Source>, std::remove_cv_t<Dest>>,
                       std::is_pointer_interconvertible_base_of<Dest, Source>>;

template <class Iter>
using iter_ref_t = std::iter_reference_t<Iter>;

template <class Iter>
using iter_value_t = std::iter_value_t<Iter>;

template <class Iter>
concept iterator_is_contiguous = std::contiguous_iterator<Iter>;

template <class Iter>
concept iterator_is_volatile = std::is_volatile_v<std::remove_reference_t<iter_ref_t<Iter>>>;

template <class Iter1, class Iter2>
concept iterators_are_contiguous = iterator_is_contiguous<Iter1> && iterator_is_contiguous<Iter2>;

template <class Source, class Dest, class SourceRef, class DestRef>
struct trivial_category
{
  using unwarp_source = unwrap_enum_t<Source>;
  using unwarp_dest   = unwrap_enum_t<Dest>;

  static constexpr bool same_size_and_compatible = std::conjunction_v<
      std::bool_constant<sizeof(Source) == sizeof(Dest)>,
      std::bool_constant<(std::is_same_v<bool, unwarp_source> >= std::is_same_v<bool, unwarp_dest>)>,
      std::disjunction<std::is_same<unwarp_source, unwarp_dest>,
                       std::conjunction<std::is_integral<unwarp_source>, std::is_integral<unwarp_dest>>,
                       std::conjunction<std::is_floating_point<unwarp_source>, std::is_floating_point<unwarp_dest>>>>;

  static constexpr bool bitcopy_constructible = std::conjunction_v<
      std::bool_constant<same_size_and_compatible>,
      std::is_trivially_constructible<Dest, SourceRef>>;

  static constexpr bool bitcopy_assignable = same_size_and_compatible && 
    is_trivially_assignable_returning_same_reference<DestRef, SourceRef>;
};

template <class Source, class Dest, class SourceRef, class DestRef>
struct trivial_category<Source *, Dest *, SourceRef, DestRef>
{
  static constexpr bool bitcopy_constructible = is_pointer_address_convertible<Source, Dest> && 
    std::is_trivially_constructible_v<Dest *, SourceRef>;

  static constexpr bool bitcopy_assignable = is_pointer_address_convertible<Source, Dest> && 
    is_trivially_assignable_returning_same_reference<DestRef, SourceRef>;
};

struct false_trivial_category
{
  static constexpr bool bitcopy_constructible = false;
  static constexpr bool bitcopy_assignable    = false;
};

template <class SourceIt, class DestIt, bool AreContiguous = iterators_are_contiguous<SourceIt, DestIt> && 
                                                             !iterator_is_volatile<SourceIt> && 
                                                             !iterator_is_volatile<DestIt>>
struct iter_move_cat : trivial_category<iter_value_t<SourceIt>, 
                                        iter_value_t<DestIt>, 
                                        std::remove_reference_t<iter_ref_t<SourceIt>> &&, 
                                        iter_ref_t<DestIt>>
{
};

template <class SourceIt, class DestIt>
struct iter_move_cat<SourceIt, DestIt, false> : false_trivial_category
{
};

template <class SourceIt, class DestIt>
struct iter_move_cat<std::move_iterator<SourceIt>, DestIt, false> : iter_move_cat<SourceIt, DestIt>
{
};

template <class SourceIt, class DestIt, bool AreContiguous = iterators_are_contiguous<SourceIt, DestIt> && 
                                                             !iterator_is_volatile<SourceIt> && 
                                                             !iterator_is_volatile<DestIt>>
struct iter_copy_cat : trivial_category<iter_value_t<SourceIt>,
                                        iter_value_t<DestIt>,
                                        iter_ref_t<SourceIt>,
                                        iter_ref_t<DestIt>>
{
};

template <class SourceIt, class DestIt>
struct iter_copy_cat<SourceIt, DestIt, false> : false_trivial_category
{
};

template <class SourceIt, class DestIt>
struct iter_copy_cat<std::move_iterator<SourceIt>, DestIt, false> : iter_move_cat<SourceIt, DestIt>
{
};

template <class Sent, class It>
concept sized_or_unreachable_sentinel_for = 
    std::sized_sentinel_for<Sent, It> || std::same_as<Sent, std::unreachable_sentinel_t>;

template <class Alloc, class Ptr, class... Args>
concept has_custom_construct = requires (Alloc &a, Ptr p, Args &&...args) {
  a.construct(p, std::forward<Args>(args)...);
};

template <class Alloc, class Ptr>
concept has_custom_destroy = requires (Alloc &a, Ptr p) {
  a.destroy(p);
};

template <class It>
constexpr auto to_address_unwrap(It const &it) noexcept
{
  if constexpr (requires { it.base(); }) {
    return std::to_address(it.base());
  } else {
    return std::to_address(it);
  }
}

template <class InIt, class InSent, class OutIt, class OutSent>
[[nodiscard]]
#if !defined(DEBUG) && !defined(_DEBUG)
[[msvc::forceinline, gnu::always_inline]]
#endif
auto invoke_memcpy(InIt first, InSent last, OutIt dest_first, OutSent dest_last)
    -> std::ranges::in_out_result<InIt, OutIt>
{
  constexpr bool sized_src = std::sized_sentinel_for<InSent, InIt>;
  constexpr bool sized_dst = std::sized_sentinel_for<OutSent, OutIt>;
  static_assert(sized_src || sized_dst, "invoke_memcpy requires at least one sized side");

  ptrdiff_t n = 0;
  if constexpr (sized_src && sized_dst) {
    ptrdiff_t const src_n = static_cast<ptrdiff_t>(last - first);
    ptrdiff_t const dst_n = static_cast<ptrdiff_t>(dest_last - dest_first);
    n                     = src_n < dst_n ? src_n : dst_n;
  } else if constexpr (sized_src) {
    n = static_cast<ptrdiff_t>(last - first);
  } else {
    n = static_cast<ptrdiff_t>(dest_last - dest_first);
  }

  std::memcpy(static_cast<void *>(to_address_unwrap(dest_first)),
                static_cast<void const *>(to_address_unwrap(first)),
                static_cast<size_t>(n) * sizeof(std::iter_value_t<OutIt>));

  return { first + static_cast<std::iter_difference_t<InIt>>(n),
           dest_first + static_cast<std::iter_difference_t<OutIt>>(n) };
}
} // namespace uninitialized_detail

template <class T>
#if defined(__cpp_lib_constexpr_new)
constexpr 
#endif
void default_construct_in_place(T &obj) noexcept(std::is_nothrow_default_constructible_v<T>)
{
  ::new (static_cast<void *>(std::addressof(obj))) T;
}

template <class T, class... Args>
constexpr void construct_in_place(T &obj, Args &&...args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
{
  if (std::is_constant_evaluated()) {
    std::construct_at(std::addressof(obj), std::forward<Args>(args)...);
  } else {
    ::new (static_cast<void *>(std::addressof(obj))) T(std::forward<Args>(args)...);
  }
}

template <class NoThrowForwardIt>
struct [[nodiscard]] uninitialized_backout
{
  constexpr explicit uninitialized_backout(NoThrowForwardIt dest)
      : first_ { dest }
      , last_ { dest }
  {
  }

  constexpr uninitialized_backout(NoThrowForwardIt first, NoThrowForwardIt last)
      : first_ { first }
      , last_ { last }
  {
  }

  uninitialized_backout(uninitialized_backout const &)             = delete;
  uninitialized_backout &operator= (uninitialized_backout const &) = delete;

  constexpr ~uninitialized_backout() noexcept
  {
    if constexpr (!std::is_trivially_destructible_v<std::iter_value_t<NoThrowForwardIt>>) {
      std::destroy(first_, last_);
    }
  }

  template <class... Args>
  constexpr void emplace_back(Args &&...args) noexcept(
      noexcept(construct_in_place(*last_, std::forward<Args>(args)...)) && noexcept(++last_))
  {
    construct_in_place(*last_, std::forward<Args>(args)...);
    ++last_;
  }

#if defined(__cpp_lib_constexpr_new)
  constexpr
#endif
  void emplace_default() noexcept(noexcept(default_construct_in_place(*last_)) && noexcept(++last_))
  {
    default_construct_in_place(*last_);
    ++last_;
  }

  constexpr NoThrowForwardIt release() noexcept
  {
    first_ = last_;
    return last_;
  }

  constexpr NoThrowForwardIt const &last() noexcept { return last_;  }

private:
  NoThrowForwardIt first_;
  NoThrowForwardIt last_;
};

template <class Allocator>
struct [[nodiscard]] uninitialized_backout_al
{
private:
  using allocator_type   = Allocator;
  using allocator_traits = std::allocator_traits<allocator_type>;

  using value_type = typename allocator_traits::value_type;
  using pointer    = typename allocator_traits::pointer;

public:
  constexpr explicit uninitialized_backout_al(pointer dest, Allocator &alloc)
      : first_ { dest }
      , last_ { dest }
      , alloc_ { alloc }
  {
  }

  uninitialized_backout_al(uninitialized_backout_al const &)             = delete;
  uninitialized_backout_al &operator= (uninitialized_backout_al const &) = delete;

  constexpr ~uninitialized_backout_al() noexcept
  {
    if constexpr (std::disjunction_v<std::negation<std::is_trivially_destructible<value_type>>,
                                     std::bool_constant<uninitialized_detail::has_custom_destroy<Allocator, value_type *>>>) {
      for (; first_ != last_; ++first_) {
        allocator_traits::destroy(alloc_, std::to_address(first_));
      }
    }
  }

  template <class... Args>
  constexpr void emplace_back(Args &&...args) noexcept(
      noexcept(allocator_traits::construct(alloc_, std::to_address(last_), std::forward<Args>(args)...)) && 
      noexcept(++last_))
  {
    allocator_traits::construct(alloc_, std::to_address(last_), std::forward<Args>(args)...);
    ++last_;
  }

#if defined(__cpp_lib_constexpr_new)
  constexpr
#endif 
  void emplace_default() noexcept(
      noexcept(allocator_traits::construct(alloc_, std::to_address(last_))) && noexcept(++last_))
  {
    allocator_traits::construct(alloc_, std::to_address(last_));
    ++last_;
  }

  constexpr pointer release() noexcept
  {
    first_ = last_;
    return last_;
  }

  constexpr pointer const &last() const noexcept { return last_; }

private:
  pointer first_;
  pointer last_;
  Allocator &alloc_;
};

template <std::input_iterator InIt, std::sentinel_for<InIt> InSent,
          std::forward_iterator OutIt, std::sentinel_for<OutIt> OutSent>
requires std::constructible_from<std::iter_value_t<OutIt>, std::iter_rvalue_reference_t<InIt>>
constexpr auto uninitialized_move(InIt first, InSent last, OutIt dest_first, OutSent dest_last)
    -> std::ranges::in_out_result<InIt, OutIt>
{
  constexpr bool sized_src  = std::sized_sentinel_for<InSent, InIt>;
  constexpr bool sized_dst  = std::sized_sentinel_for<OutSent, OutIt>;
  constexpr bool can_memcpy = uninitialized_detail::iter_move_cat<InIt, OutIt>::bitcopy_constructible &&
                              uninitialized_detail::sized_or_unreachable_sentinel_for<InSent, InIt> &&
                              uninitialized_detail::sized_or_unreachable_sentinel_for<OutSent, OutIt>;

  if constexpr (can_memcpy && (sized_src || sized_dst)) {
    if (!std::is_constant_evaluated()) {
      return uninitialized_detail::invoke_memcpy(first, last, dest_first, dest_last);
    }
  } else if constexpr (can_memcpy) {
    assert(false && "uninitialized_move() with two unreachable sentinels cannot terminate");
  }

  uninitialized_backout<OutIt> backout { std::move(dest_first) };
  for (; first != last && backout.last() != dest_last; ++first) {
    backout.emplace_back(std::ranges::iter_move(first));
  }
  return { std::move(first), backout.release() };
}

template <std::input_iterator InIt, std::sentinel_for<InIt> InSent,
          std::contiguous_iterator OutIt, std::sentinel_for<OutIt> OutSent, class Allocator>
requires std::constructible_from<std::iter_value_t<OutIt>, std::iter_rvalue_reference_t<InIt>> &&
         std::same_as<typename std::allocator_traits<Allocator>::value_type, std::iter_value_t<OutIt>> &&
         std::convertible_to<OutIt, typename std::allocator_traits<Allocator>::pointer>
constexpr auto uninitialized_move(InIt first, InSent last, OutIt dest_first, OutSent dest_last, Allocator &alloc)
    -> std::ranges::in_out_result<InIt, OutIt>
{
  constexpr bool sized_src  = std::sized_sentinel_for<InSent, InIt>;
  constexpr bool sized_dst  = std::sized_sentinel_for<OutSent, OutIt>;
  constexpr bool can_memcpy = uninitialized_detail::iter_move_cat<InIt, OutIt>::bitcopy_constructible &&
                              uninitialized_detail::sized_or_unreachable_sentinel_for<InSent, InIt> &&
                              uninitialized_detail::sized_or_unreachable_sentinel_for<OutSent, OutIt> &&
                              (!uninitialized_detail::has_custom_construct<Allocator,
                                typename std::allocator_traits<Allocator>::value_type *,
                                std::iter_rvalue_reference_t<InIt>>);

  if constexpr (can_memcpy && (sized_src || sized_dst)) {
    if (!std::is_constant_evaluated()) {
      return uninitialized_detail::invoke_memcpy(first, last, dest_first, dest_last);
    }
  } else if constexpr (can_memcpy) {
    assert(false && "uninitialized_move() with two unreachable sentinels cannot terminate");
  }

  uninitialized_backout_al<Allocator> backout { dest_first, alloc };
  for (; first != last && backout.last() != dest_last; ++first) {
    backout.emplace_back(std::ranges::iter_move(first));
  }
  return { std::move(first), backout.release() };
}

template <std::input_iterator InIt,
          std::forward_iterator OutIt, std::sentinel_for<OutIt> OutSent>
requires std::constructible_from<std::iter_value_t<OutIt>, std::iter_rvalue_reference_t<InIt>>
constexpr auto uninitialized_move_n(InIt first, std::iter_difference_t<InIt> count,
                                    OutIt dest_first, OutSent dest_last)
    -> std::ranges::uninitialized_move_n_result<InIt, OutIt>
{
  if (count <= 0) {
    return { std::move(first), std::move(dest_first) };
  }

  constexpr bool can_memcpy = uninitialized_detail::iter_move_cat<InIt, OutIt>::bitcopy_constructible &&
                              uninitialized_detail::sized_or_unreachable_sentinel_for<OutSent, OutIt>;

  if constexpr (can_memcpy) {
    if (!std::is_constant_evaluated()) {
      return uninitialized_detail::invoke_memcpy(first, first + count, dest_first, dest_last);
    }
  }

  uninitialized_backout<OutIt> backout { std::move(dest_first) };
  for (; count > 0 && backout.last() != dest_last; --count, ++first) {
    backout.emplace_back(std::ranges::iter_move(first));
  }
  return { std::move(first), backout.release() };
}

template <std::input_iterator InIt,
          std::contiguous_iterator OutIt, std::sentinel_for<OutIt> OutSent, class Allocator>
requires std::constructible_from<std::iter_value_t<OutIt>, std::iter_rvalue_reference_t<InIt>> &&
         std::same_as<typename std::allocator_traits<Allocator>::value_type, std::iter_value_t<OutIt>> &&
         std::convertible_to<OutIt, typename std::allocator_traits<Allocator>::pointer>
constexpr auto uninitialized_move_n(InIt first, std::iter_difference_t<InIt> count,
                                    OutIt dest_first, OutSent dest_last, Allocator &alloc)
    -> std::ranges::uninitialized_move_n_result<InIt, OutIt>
{
  if (count <= 0) {
    return { std::move(first), std::move(dest_first) };
  }

  constexpr bool can_memcpy = uninitialized_detail::iter_move_cat<InIt, OutIt>::bitcopy_constructible &&
                              uninitialized_detail::sized_or_unreachable_sentinel_for<OutSent, OutIt> &&
                              (!uninitialized_detail::has_custom_construct<Allocator,
                                typename std::allocator_traits<Allocator>::value_type *,
                                std::iter_rvalue_reference_t<InIt>>);

  if constexpr (can_memcpy) {
    if (!std::is_constant_evaluated()) {
      return uninitialized_detail::invoke_memcpy(first, first + count, dest_first, dest_last);
    }
  }

  uninitialized_backout_al<Allocator> backout { dest_first, alloc };
  for (; count > 0 && backout.last() != dest_last; --count, ++first) {
    backout.emplace_back(std::ranges::iter_move(first));
  }
  return { std::move(first), backout.release() };
}

template <std::input_iterator InIt, std::sentinel_for<InIt> InSent,
          std::forward_iterator OutIt, std::sentinel_for<OutIt> OutSent>
requires std::constructible_from<std::iter_value_t<OutIt>, std::iter_reference_t<InIt>>
constexpr auto uninitialized_copy(InIt first, InSent last, OutIt dest_first, OutSent dest_last)
    -> std::ranges::uninitialized_copy_result<InIt, OutIt>
{
  constexpr bool sized_src  = std::sized_sentinel_for<InSent, InIt>;
  constexpr bool sized_dst  = std::sized_sentinel_for<OutSent, OutIt>;
  constexpr bool can_memcpy = uninitialized_detail::iter_copy_cat<InIt, OutIt>::bitcopy_constructible &&
                              uninitialized_detail::sized_or_unreachable_sentinel_for<InSent, InIt> &&
                              uninitialized_detail::sized_or_unreachable_sentinel_for<OutSent, OutIt>;

  if constexpr (can_memcpy && (sized_src || sized_dst)) {
    if (!std::is_constant_evaluated()) {
      return uninitialized_detail::invoke_memcpy(first, last, dest_first, dest_last);
    }
  } else if constexpr (can_memcpy) {
    assert(false && "uninitialized_copy() with two unreachable sentinels cannot terminate");
  }

  uninitialized_backout<OutIt> backout { std::move(dest_first) };
  for (; first != last && backout.last() != dest_last; ++first) {
    backout.emplace_back(*first);
  }
  return { std::move(first), backout.release() };
}

template <std::input_iterator InIt, std::sentinel_for<InIt> InSent,
          std::contiguous_iterator OutIt, std::sentinel_for<OutIt> OutSent, class Allocator>
requires std::constructible_from<std::iter_value_t<OutIt>, std::iter_reference_t<InIt>> &&
         std::same_as<typename std::allocator_traits<Allocator>::value_type, std::iter_value_t<OutIt>> &&
         std::convertible_to<OutIt, typename std::allocator_traits<Allocator>::pointer>
constexpr auto uninitialized_copy(InIt first, InSent last, OutIt dest_first, OutSent dest_last, Allocator &alloc)
    -> std::ranges::uninitialized_copy_result<InIt, OutIt>
{
  constexpr bool sized_src  = std::sized_sentinel_for<InSent, InIt>;
  constexpr bool sized_dst  = std::sized_sentinel_for<OutSent, OutIt>;
  constexpr bool can_memcpy = uninitialized_detail::iter_copy_cat<InIt, OutIt>::bitcopy_constructible &&
                              uninitialized_detail::sized_or_unreachable_sentinel_for<InSent, InIt> &&
                              uninitialized_detail::sized_or_unreachable_sentinel_for<OutSent, OutIt> &&
                              (!uninitialized_detail::has_custom_construct<Allocator,
                                typename std::allocator_traits<Allocator>::value_type *,
                                std::iter_reference_t<InIt>>);

  if constexpr (can_memcpy && (sized_src || sized_dst)) {
    if (!std::is_constant_evaluated()) {
      return uninitialized_detail::invoke_memcpy(first, last, dest_first, dest_last);
    }
  } else if constexpr (can_memcpy) {
    assert(false && "uninitialized_copy() with two unreachable sentinels cannot terminate");
  }

  uninitialized_backout_al<Allocator> backout { dest_first, alloc };
  for (; first != last && backout.last() != dest_last; ++first) {
    backout.emplace_back(*first);
  }
  return { std::move(first), backout.release() };
}

template <std::input_iterator InIt,
          std::forward_iterator OutIt, std::sentinel_for<OutIt> OutSent>
requires std::constructible_from<std::iter_value_t<OutIt>, std::iter_reference_t<InIt>>
constexpr auto uninitialized_copy_n(InIt first, std::iter_difference_t<InIt> count,
                                    OutIt dest_first, OutSent dest_last)
    -> std::ranges::uninitialized_copy_n_result<InIt, OutIt>
{
  if (count <= 0) {
    return { std::move(first), std::move(dest_first) };
  }

  constexpr bool can_memcpy = uninitialized_detail::iter_copy_cat<InIt, OutIt>::bitcopy_constructible &&
                              uninitialized_detail::sized_or_unreachable_sentinel_for<OutSent, OutIt>;

  if constexpr (can_memcpy) {
    if (!std::is_constant_evaluated()) {
      return uninitialized_detail::invoke_memcpy(first, first + count, dest_first, dest_last);
    }
  }

  uninitialized_backout<OutIt> backout { std::move(dest_first) };
  for (; count > 0 && backout.last() != dest_last; --count, ++first) {
    backout.emplace_back(*first);
  }
  return { std::move(first), backout.release() };
}

template <std::input_iterator InIt,
          std::contiguous_iterator OutIt, std::sentinel_for<OutIt> OutSent, class Allocator>
requires std::constructible_from<std::iter_value_t<OutIt>, std::iter_reference_t<InIt>> &&
         std::same_as<typename std::allocator_traits<Allocator>::value_type, std::iter_value_t<OutIt>> &&
         std::convertible_to<OutIt, typename std::allocator_traits<Allocator>::pointer>
constexpr auto uninitialized_copy_n(InIt first, std::iter_difference_t<InIt> count,
                                    OutIt dest_first, OutSent dest_last, Allocator &alloc)
    -> std::ranges::uninitialized_copy_n_result<InIt, OutIt>
{
  if (count <= 0) {
    return { std::move(first), std::move(dest_first) };
  }

  constexpr bool can_memcpy = uninitialized_detail::iter_copy_cat<InIt, OutIt>::bitcopy_constructible &&
                              uninitialized_detail::sized_or_unreachable_sentinel_for<OutSent, OutIt> &&
                              (!uninitialized_detail::has_custom_construct<Allocator,
                                typename std::allocator_traits<Allocator>::value_type *,
                                std::iter_reference_t<InIt>>);

  if constexpr (can_memcpy) {
    if (!std::is_constant_evaluated()) {
      return uninitialized_detail::invoke_memcpy(first, first + count, dest_first, dest_last);
    }
  }

  uninitialized_backout_al<Allocator> backout { dest_first, alloc };
  for (; count > 0 && backout.last() != dest_last; --count, ++first) {
    backout.emplace_back(*first);
  }
  return { std::move(first), backout.release() };
}

} // namespace stdex

#if defined(__clang__)
#  pragma clang diagnostic pop
#elif defined(__GNUC__)
#  pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#  pragma warning(pop)
#endif
