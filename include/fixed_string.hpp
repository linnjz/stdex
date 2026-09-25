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

#include <concepts>      // std::convertible_to, std::integral
#include <cstddef>       // std::size_t, std::ptrdiff_t
#include <format>        // std::formatter
#include <iterator>      // std::input_iterator, std::sentinel_for, std::iter_value_t, std::reverse_iterator
#include <ostream>       // std::basic_ostream
#include <ranges>        // std::ranges::input_range, std::ranges::range_reference_t, std::ranges::begin/end, std::from_range_t
#include <stdexcept>     // std::runtime_error, std::out_of_range
#include <string_view>   // std::basic_string_view, std::hash<basic_string_view>
#include <type_traits>   // std::is_pointer_v, std::integral_constant, std::bool_constant
#include <utility>       // std::index_sequence, std::make_index_sequence, std::move, std::forward
#include <version>

namespace std
{

namespace basic_fixed_string_detail
{

/// Mandates check: the last element of the array must be a null terminator (see proposal sections [P3094R5]/9.4.3.4/9.4.3.8/9.4.3.9).
/// Only called in a consteval context, using throw to trigger a compile-time error.
template <class charT, size_t N>
consteval void fixed_string_require_nt(charT const (&arr)[N], char const *what)
{
  if (arr[N - 1] != charT()) {
    throw std::runtime_error(what);
  }
}

/// Initialize the storage array from a character pack expanded from index_sequence.
/// Writes characters to indices [0, N) and explicitly sets the terminator to charT{}.
///
/// @note Deviation from P3094R5:6.1 (synopsis: cb7-6 "charT data_[N+1] = {};").
///
/// The proposal relies on the default member initializer "= {}" to implicitly
/// provide the null terminator at index N. However, that initializer makes the
/// type non-trivial ("__is_trivial==false", not "is_trivially_default_constructible"),
/// which breaks "constexpr" usage in "std::inplace_vector". The libstdc++ implementation
/// requires a trivial element type in its "consteval" path
/// (libstdc++-v3/include/std/inplace_vector:845-857):
///   - "if constexpr (is_trivial_v<Tp>)" is used;
///   - otherwise, "__builtin_unreachable()" is invoked.
///
/// To resolve this, this implementation explicitly guarantees the terminator
/// by writing "out[Size-1] = charT{}". This allows the class to remain trivial,
/// as documented in the notes on "data_" and the defaulted default constructor.
template <class charT, size_t Size, size_t... Is, std::convertible_to<charT>... Chars>
constexpr void fixed_string_init(charT (&out)[Size], std::index_sequence<Is...> /*unused*/, Chars... chars) noexcept
{
  ((out[Is] = static_cast<charT>(chars)), ...);
  out[Size - 1] = charT {};
}

/// Copy values from the range [first, last) into the storage array.
/// At most "Size-1" payload elements are copied; any remaining payload
/// and the terminator are zero-initialized with "charT{}".
///
/// @note Deviation from P3094R5:6.1 -- same rationale as "fixed_string_init".
///
/// The proposal's wording assumes that "data_[N+1] = {}" zero-initializes
/// the tail and the terminator. However, because "= {}" is removed in this
/// implementation to preserve triviality, the tail must be cleared explicitly.
///
/// Consequently, the loop "for (; i < Size; ++i) out[i] = charT{}" writes both:
///   - the unused payload elements, and
///   - "out[Size-1]" (the exposition-only null terminator, per
///     [P3094R5]/9.4.3.3 p4: "[data(), data()+size()]" is valid and
///     "data()[size] == charT{}").
///
/// Without this explicit clearing, "consteval" construction leaves
/// "data_[N]" indeterminate and triggers the error:
///   "error: is not a constant expression because it refers to an
///    incompletely initialized variable"
/// when used in "constexpr std::inplace_vector" (observed on GCC 16.2).
template <class charT, size_t Size, std::input_iterator It, std::sentinel_for<It> S>
constexpr void fixed_string_copy(charT (&out)[Size], It first, S last) noexcept
{
  size_t i = 0;
  for (; first != last && i < Size - 1; ++first, ++i) {
    out[i] = static_cast<charT>(*first);
  }
  for (; i < Size; ++i) {
    out[i] = charT {};
  }
}

} // namespace basic_fixed_string_detail

template <std::integral charT, size_t N>
class basic_fixed_string
{
public:
  /// C++20 NTTP(non-type template parameters)-[P1907R1]
  /// requires a structural type to expose all its members publicly.
  ///
  /// exposition-only
  ///
  /// @note Deviation from P3094R5:6.1 (synopsis: cb7-6 "charT data_[N+1] = {};").
  ///
  /// The proposal makes "data_" exposition-only with a default member initializer "= {}"
  /// and provides no default constructor. As a result, the type is not default-constructible
  /// and, because of the initializer, not trivial
  /// ("__is_trivial==false", "is_trivially_default_constructible==false"
  /// on GCC 16.2 / MSVC 19.51 / Clang 22).
  ///
  /// https://github.com/gcc-mirror/gcc/blob/master/libstdc%2B%2B-v3/include/std/inplace_vector As of Jul 2, 2026
  ///
  /// This violates the "is_trivial_v<Tp>" requirement of "std::inplace_vector"'s
  /// "consteval" "_M_init()" ("inplace_vector:845-857"), causing
  /// "constexpr inplace_vector<fixed_string>" to fail with
  /// "__builtin_unreachable() is not a constant expression" and,
  /// without an explicit terminator store, with "incompletely initialized variable".
  ///
  /// This implementation intentionally diverges as follows:
  ///   - "data_" has no default member initializer, preserving triviality;
  ///   - the null terminator invariant ("data()[N]==charT{}" per
  ///     [P3094R5]/9.4.3.3 p4) is established explicitly by
  ///     "fixed_string_init" / "fixed_string_copy" (see above);
  ///   - an explicit trivial default constructor is provided.
  ///
  /// This makes the type default-constructible and trivial ("__is_trivial==true"),
  /// which deviates from [P3094R5]/6.1 "No default constructor" but is required
  /// for "constexpr" compatibility with "std::inplace_vector" and other
  /// "trivial"-constrained containers. "N==0" remains well-formed via the
  /// variadic constructor with an empty pack; the defaulted constructor is
  /// harmless for "N>0" and never participates in the proposal's API.
  charT data_[N + 1];

  /// types
  using value_type             = charT;
  using pointer                = value_type *;
  using const_pointer          = value_type const *;
  using reference              = value_type &;
  using const_reference        = value_type const &;
  using const_iterator         = charT const *;
  using iterator               = const_iterator;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;
  using reverse_iterator       = const_reverse_iterator;
  using size_type              = size_t;
  using difference_type        = ptrdiff_t;

  /// @brief Trivial default constructor -- deviation from P3094R5:6.1 (see "data_" note).
  /// Required to keep the type trivial and to satisfy "std::inplace_vector"'s "is_trivial_v" / "_Tp()" initialization path.
  constexpr basic_fixed_string() noexcept                                       = default;
  constexpr basic_fixed_string(basic_fixed_string const &) noexcept             = default;
  constexpr basic_fixed_string &operator= (basic_fixed_string const &) noexcept = default;
  constexpr basic_fixed_string(basic_fixed_string &&) noexcept                  = default;
  constexpr basic_fixed_string &operator= (basic_fixed_string &&) noexcept      = default;
  constexpr ~basic_fixed_string()                                               = default;

  /// construction and assignment
  template <std::convertible_to<charT>... Chars>
  requires (sizeof...(Chars) == N) && (... && !std::is_pointer_v<Chars>)
  constexpr explicit basic_fixed_string(Chars... chars) noexcept
  {
    basic_fixed_string_detail::fixed_string_init<charT>(data_, std::make_index_sequence<N> {}, static_cast<charT>(chars)...);
  }

  consteval basic_fixed_string(charT const (&txt)[N + 1]) noexcept
  {
    basic_fixed_string_detail::fixed_string_require_nt(txt, "basic_fixed_string: array argument must be null-terminated");
    basic_fixed_string_detail::fixed_string_copy<charT>(data_, txt, txt + N);
  }

  template <std::input_iterator It, std::sentinel_for<It> S>
  requires std::convertible_to<std::iter_value_t<It>, charT>
  constexpr basic_fixed_string(It begin, S end)
  {
    basic_fixed_string_detail::fixed_string_copy<charT>(data_, std::move(begin), std::move(end));
  }

#ifdef __cpp_lib_containers_ranges
  template <std::ranges::input_range R>
  requires std::convertible_to<std::ranges::range_reference_t<R>, charT>
  constexpr basic_fixed_string(std::from_range_t /*unused*/, R &&r)
  {
    auto &&range = std::forward<R>(r);
    basic_fixed_string_detail::fixed_string_copy<charT>(data_, std::ranges::begin(range), std::ranges::end(range));
  }
#endif // __cpp_lib_containers_ranges

  // iterator support
  constexpr const_iterator begin() const noexcept { return data_; }

  constexpr const_iterator end() const noexcept { return data_ + N; }

  constexpr const_iterator cbegin() const noexcept { return data_; }

  constexpr const_iterator cend() const noexcept { return data_ + N; }

  constexpr const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }

  constexpr const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }

  constexpr const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(cend()); }

  constexpr const_reverse_iterator crend() const noexcept { return const_reverse_iterator(cbegin()); }

  // capacity
  static constexpr std::integral_constant<size_type, N> size {};
  static constexpr std::integral_constant<size_type, N> length {};
  static constexpr std::integral_constant<size_type, N> max_size {};
  static constexpr std::bool_constant<N == 0> empty {};

  // element access
  constexpr const_reference operator[] (size_type pos) const noexcept { return data_[pos]; }

  constexpr const_reference at(size_type pos) const
  {
    if (pos >= N) {
      throw out_of_range("basic_fixed_string::at: index out of range");
    }
    return data_[pos];
  }

  constexpr const_reference front() const noexcept { return data_[0]; }

  constexpr const_reference back() const noexcept { return data_[N - 1]; }

  // modifiers
  constexpr void swap(basic_fixed_string &s) noexcept
  {
    for (size_type i = 0; i < N; ++i) {
      charT tmp  = data_[i];
      data_[i]   = s.data_[i];
      s.data_[i] = tmp;
    }
  }

  // string operations
  constexpr const_pointer c_str() const noexcept { return data_; }

  constexpr const_pointer data() const noexcept { return data_; }

  constexpr std::basic_string_view<charT> view() const noexcept { return basic_string_view<charT>(data_, N); }

  constexpr operator std::basic_string_view<charT>() const noexcept { return view(); }

  template <size_t N2>
  friend constexpr basic_fixed_string<charT, N + N2>
  operator+ (basic_fixed_string const &lhs, basic_fixed_string<charT, N2> const &rhs) noexcept
  {
    return [&]<size_t... Is>(std::index_sequence<Is...>)
    {
      return basic_fixed_string<charT, N + N2> { (Is < N ? lhs[Is] : rhs[Is - N])... };
    }(std::make_index_sequence<N + N2> {});
  }

  friend constexpr basic_fixed_string<charT, N + 1>
  operator+ (basic_fixed_string const &lhs, charT rhs) noexcept
  {
    return [&]<size_t... Is>(std::index_sequence<Is...>)
    {
      return basic_fixed_string<charT, N + 1> { (Is < N ? lhs[Is] : rhs)... };
    }(std::make_index_sequence<N + 1> {});
  }

  friend constexpr basic_fixed_string<charT, 1 + N>
  operator+ (charT lhs, basic_fixed_string const &rhs) noexcept
  {
    return [&]<size_t... Is>(std::index_sequence<Is...>)
    {
      return basic_fixed_string<charT, 1 + N> { (Is == 0 ? lhs : rhs[Is - 1])... };
    }(std::make_index_sequence<1 + N> {});
  }

  template <size_t N2>
  consteval friend basic_fixed_string<charT, N + N2 - 1>
  operator+ (basic_fixed_string const &lhs, charT const (&rhs)[N2]) noexcept
  {
    basic_fixed_string_detail::fixed_string_require_nt(rhs, "basic_fixed_string: array argument must be null-terminated");
    return [&]<size_t... Is>(std::index_sequence<Is...>)
    {
      return basic_fixed_string<charT, N + N2 - 1> { (Is < N ? lhs[Is] : rhs[Is - N])... };
    }(std::make_index_sequence<N + N2 - 1> {});
  }

  template <size_t N1>
  consteval friend basic_fixed_string<charT, N1 + N - 1>
  operator+ (charT const (&lhs)[N1], basic_fixed_string const &rhs) noexcept
  {
    basic_fixed_string_detail::fixed_string_require_nt(lhs, "basic_fixed_string: array argument must be null-terminated");
    return [&]<size_t... Is>(std::index_sequence<Is...>)
    {
      return basic_fixed_string<charT, N1 + N - 1> { (Is < N1 - 1 ? lhs[Is] : rhs[Is - (N1 - 1)])... };
    }(std::make_index_sequence<N1 + N - 1> {});
  }

  // non-member comparison functions
  template <size_t N2>
  friend constexpr bool operator== (basic_fixed_string const &lhs, basic_fixed_string<charT, N2> const &rhs)
  {
    return lhs.view() == rhs.view();
  }

  template <size_t N2>
  friend consteval bool operator== (basic_fixed_string const &lhs, charT const (&rhs)[N2])
  {
    basic_fixed_string_detail::fixed_string_require_nt(rhs, "basic_fixed_string: array argument must be null-terminated");
    return lhs.view() == basic_string_view<charT>(rhs, rhs + N2 - 1);
  }

  template <size_t N2>
  friend constexpr auto operator<=> (basic_fixed_string const &lhs, basic_fixed_string<charT, N2> const &rhs)
  {
    return lhs.view() <=> rhs.view();
  }

  template <size_t N2>
  friend consteval auto operator<=> (basic_fixed_string const &lhs, charT const (&rhs)[N2])
  {
    basic_fixed_string_detail::fixed_string_require_nt(rhs, "basic_fixed_string: array argument must be null-terminated");
    return lhs.view() <=> basic_string_view<charT>(rhs, rhs + N2 - 1);
  }

  // inserters and extractors
  friend std::basic_ostream<charT> &operator<< (std::basic_ostream<charT> &os, basic_fixed_string const &str)
  {
    return os << str.view();
  }
};

// deduction guides
template <typename CharT, std::convertible_to<CharT>... Rest>
basic_fixed_string(CharT, Rest...) -> basic_fixed_string<CharT, 1 + sizeof...(Rest)>;

template <typename charT, size_t N>
basic_fixed_string(charT const (&str)[N]) -> basic_fixed_string<charT, N - 1>;

#ifdef __cpp_lib_containers_ranges
template <typename CharT, size_t N>
basic_fixed_string(std::from_range_t, std::array<CharT, N>) -> basic_fixed_string<CharT, N>;
#endif // __cpp_lib_containers_ranges

// specialized algorithms
template <class charT, size_t N>
constexpr void swap(basic_fixed_string<charT, N> &x, basic_fixed_string<charT, N> &y) noexcept
{
  x.swap(y);
}

// basic_fixed_string typedef-names
template <size_t N>
using fixed_string = basic_fixed_string<char, N>;
template <size_t N>
using fixed_u8string = basic_fixed_string<char8_t, N>;
template <size_t N>
using fixed_u16string = basic_fixed_string<char16_t, N>;
template <size_t N>
using fixed_u32string = basic_fixed_string<char32_t, N>;
template <size_t N>
using fixed_wstring = basic_fixed_string<wchar_t, N>;

// The UDL proposal was rejected [P3094R5]/6.8
inline namespace literals
{

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wuser-defined-literals"
#elif defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable : 4455)
#endif
inline namespace [[deprecated("Proposal [P3094R5]/6.8 on user-defined literals was rejected.")]] fixed_string_literals
{

template <fixed_string fs>
constexpr auto operator""_fs () noexcept
{
  return fs;
}

template <fixed_wstring fws>
constexpr auto operator""_fws () noexcept
{
  return fws;
}

template <fixed_u8string fu8s>
constexpr auto operator""_fu8s () noexcept
{
  return fu8s;
}

template <fixed_u16string fu16s>
constexpr auto operator""_fu16s () noexcept
{
  return fu16s;
}

template <fixed_u32string fu32s>
constexpr auto operator""_fu32s () noexcept
{
  return fu32s;
}

} // namespace fixed_string_literals

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#  pragma warning(pop)
#endif

} // namespace literals

} // namespace std

namespace std
{

// hash support
template <size_t N>
struct hash<std::fixed_string<N>> : hash<string_view>
{
};

template <size_t N>
struct hash<std::fixed_u8string<N>> : hash<u8string_view>
{
};

template <size_t N>
struct hash<std::fixed_u16string<N>> : hash<u16string_view>
{
};

template <size_t N>
struct hash<std::fixed_u32string<N>> : hash<u32string_view>
{
};

template <size_t N>
struct hash<std::fixed_wstring<N>> : hash<wstring_view>
{
};

// formatting support (debug-enabled string type specialization)
template <class charT, size_t N>
struct formatter<std::basic_fixed_string<charT, N>, charT> : formatter<basic_string_view<charT>, charT>
{
};
} // namespace std
