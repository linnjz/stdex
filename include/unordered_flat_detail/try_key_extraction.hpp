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

#include <tuple>       // std::tuple_size, std::tuple_size_v, std::tuple_element, std::get
#include <type_traits> // std::is_same_v, std::remove_cvref_t, std::remove_cv_t
#include <utility>     // std::forward, std::declval, std::piecewise_construct_t

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

namespace stdex::unordered_flat_detail
{
// clang-format off
struct extract_key_priority_tag0 {};
// tag1 derives from tag0 so that a tag1 overload whose constraints fail still
// lets the tag0 overload be selected; without this the without_key fallback is
// never a candidate and key-free emplace cannot compile.
struct extract_key_priority_tag1 : extract_key_priority_tag0 {};

// clang-format on

template <class KeyT, class Ret, class WithKey, class WithoutKey, class... Args>
[[msvc::forceinline, gnu::always_inline]] constexpr Ret try_key_extraction_impl(extract_key_priority_tag0, WithKey, WithoutKey without_key, Args &&...args)
{
  return without_key(std::forward<Args>(args)...);
}

template <class KeyT, class Ret, class WithKey, class WithoutKey, class Arg>
requires std::is_same_v<KeyT, std::remove_cvref_t<Arg>>
[[msvc::forceinline, gnu::always_inline]]
constexpr Ret try_key_extraction_impl(extract_key_priority_tag1, WithKey with_key, WithoutKey, Arg &&arg)
{
  return with_key(arg, std::forward<Arg>(arg));
}

template <class KeyT, class Ret, class WithKey, class WithoutKey, class Arg>
requires requires { typename Arg::first_type; } && std::is_same_v<std::remove_cv_t<typename Arg::first_type>, KeyT>
[[msvc::forceinline, gnu::always_inline]] 
constexpr Ret try_key_extraction_impl(extract_key_priority_tag1, WithKey with_key, WithoutKey, Arg &&arg)
{
  return with_key(arg.first, std::forward<Arg>(arg));
}

template <class KeyT, class Ret, class WithKey, class WithoutKey, class Arg1, class Arg2>
requires std::is_same_v<KeyT, std::remove_cvref_t<Arg1>>
[[msvc::forceinline, gnu::always_inline]] 
constexpr Ret try_key_extraction_impl(extract_key_priority_tag1, WithKey with_key, WithoutKey, Arg1 &&arg1, Arg2 &&arg2)
{
  return with_key(arg1, std::forward<Arg1>(arg1), std::forward<Arg2>(arg2));
}

template <class KeyT, class Ret, class WithKey, class WithoutKey, class PiecewiseConstruct, class Tuple1, class Tuple2>
requires std::is_same_v<std::remove_cvref_t<PiecewiseConstruct>, std::piecewise_construct_t> &&
         requires { typename std::tuple_size<std::remove_cvref_t<Tuple1>>::type; } &&
          (std::tuple_size_v<std::remove_cvref_t<Tuple1>> == 1) &&
          std::is_same_v<std::remove_cvref_t<typename std::tuple_element<0, std::remove_cvref_t<Tuple1>>::type>, KeyT>
[[msvc::forceinline, gnu::always_inline]] 
constexpr Ret try_key_extraction_impl(extract_key_priority_tag1, WithKey with_key, WithoutKey, PiecewiseConstruct &&pc, Tuple1 &&tuple1, Tuple2 &&tuple2)
{
  return with_key(std::get<0>(tuple1), std::forward<PiecewiseConstruct>(pc), std::forward<Tuple1>(tuple1), std::forward<Tuple2>(tuple2));
}

template <class KeyT, class WithKey, class WithoutKey, class... Args>
[[msvc::forceinline, gnu::always_inline]] constexpr decltype(std::declval<WithoutKey>()(std::declval<Args>()...))
try_key_extraction(WithKey with_key, WithoutKey without_key, Args &&...args)
{
  using Ret = decltype(without_key(std::forward<Args>(args)...));
  return try_key_extraction_impl<KeyT, Ret>(extract_key_priority_tag1 {}, with_key, without_key, std::forward<Args>(args)...);
}
} // namespace stdex::unordered_flat_detail

#if defined(__clang__)
#  pragma clang diagnostic pop
#elif defined(__GNUC__)
#  pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#  pragma warning(pop)
#endif
