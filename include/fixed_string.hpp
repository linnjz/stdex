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

#include <compare>
#include <concepts>
#include <cstddef>
#include <format>
#include <iterator>
#include <ostream>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

#ifndef __cpp_lib_fixed_string
#define __cpp_lib_fixed_string 202410L
#endif

namespace std {

namespace __basic_fixed_string_detail {

/// Mandates check: the last element of the array must be a null terminator (see proposal sections [P3094R5]/9.4.3.4/9.4.3.8/9.4.3.9).
/// Only called in a consteval context, using throw to trigger a compile-time error.
template <class charT, size_t N>
consteval void __fixed_string_require_nt(const charT (&arr)[N], const char* what) {
  if (arr[N - 1] != charT()) {
    throw std::runtime_error(what);
  }
}

/// Initialize the storage array from a character pack expanded from index_sequence (writes [0, N), the terminator is guaranteed by {}).
template <class charT, size_t Size, size_t... Is, convertible_to<charT>... Chars>
constexpr void __fixed_string_init(charT (&out)[Size], index_sequence<Is...>, Chars... chars) noexcept {
  ((out[Is] = static_cast<charT>(chars)), ...);
  (void)out;
}

/// Copy values from the range [first, last) into the storage array (at most Size-1 elements, the terminator is guaranteed by {}).
template <class charT, size_t Size, input_iterator It, sentinel_for<It> S>
constexpr void __fixed_string_copy(charT (&out)[Size], It first, S last) noexcept {
  size_t i = 0;
  for (; first != last && i < Size - 1; ++first, ++i) {
    out[i] = static_cast<charT>(*first);
  }
}

} // namespace __basic_fixed_string_detail

template <integral charT, size_t N>
class basic_fixed_string {
public:
  /// C++20 NTTP(non-type template parameters)-[P1907R1] 
  /// requires a structural type to expose all its members publicly.
  ///
  /// exposition-only
  charT __data_[N + 1] = {};

  /// types
  using value_type         = charT;
  using pointer            = value_type*;
  using const_pointer      = const value_type*;
  using reference          = value_type&;
  using const_reference    = const value_type&;
  using const_iterator     = const charT*;
  using iterator           = const_iterator;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;
  using reverse_iterator       = const_reverse_iterator;
  using size_type          = size_t;
  using difference_type    = ptrdiff_t;

  /// construction and assignment
  template <convertible_to<charT>... Chars>
  requires (sizeof...(Chars) == N) && (... && !is_pointer_v<Chars>)
  constexpr explicit basic_fixed_string(Chars... chars) noexcept {
    __basic_fixed_string_detail::__fixed_string_init<charT>(__data_, make_index_sequence<N>{}, static_cast<charT>(chars)...);
  }

  consteval basic_fixed_string(const charT (&txt)[N + 1]) noexcept {
    __basic_fixed_string_detail::__fixed_string_require_nt(txt, "basic_fixed_string: array argument must be null-terminated");
    __basic_fixed_string_detail::__fixed_string_copy<charT>(__data_, txt, txt + N);
  }

  template <input_iterator It, sentinel_for<It> S>
  requires convertible_to<iter_value_t<It>, charT>
  constexpr basic_fixed_string(It begin, S end) {
    __basic_fixed_string_detail::__fixed_string_copy<charT>(__data_, std::move(begin), std::move(end));
  }

  template <ranges::input_range R>
  requires convertible_to<ranges::range_reference_t<R>, charT>
  constexpr basic_fixed_string(from_range_t, R&& r) {
    __basic_fixed_string_detail::__fixed_string_copy<charT>(__data_, ranges::begin(r), ranges::end(r));
  }

  constexpr basic_fixed_string(const basic_fixed_string&) noexcept = default;
  constexpr basic_fixed_string& operator=(const basic_fixed_string&) noexcept = default;

  // iterator support
  constexpr const_iterator begin() const noexcept { return __data_; }
  constexpr const_iterator end() const noexcept { return __data_ + N; }
  constexpr const_iterator cbegin() const noexcept { return __data_; }
  constexpr const_iterator cend() const noexcept { return __data_ + N; }
  constexpr const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
  constexpr const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
  constexpr const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(cend()); }
  constexpr const_reverse_iterator crend() const noexcept { return const_reverse_iterator(cbegin()); }

  // capacity
  static constexpr integral_constant<size_type, N> size{};
  static constexpr integral_constant<size_type, N> length{};
  static constexpr integral_constant<size_type, N> max_size{};
  static constexpr bool_constant<N == 0> empty{};

  // element access
  constexpr const_reference operator[](size_type pos) const noexcept { return __data_[pos]; }
  constexpr const_reference at(size_type pos) const {
    if (pos >= N) {
      throw out_of_range("basic_fixed_string::at: index out of range");
    }
    return __data_[pos];
  }
  constexpr const_reference front() const noexcept { return __data_[0]; }
  constexpr const_reference back() const noexcept { return __data_[N - 1]; }

  // modifiers
  constexpr void swap(basic_fixed_string& s) noexcept {
    for (size_type i = 0; i < N; ++i) {
      charT tmp = __data_[i];
      __data_[i] = s.__data_[i];
      s.__data_[i] = tmp;
    }
  }

  // string operations
  constexpr const_pointer c_str() const noexcept { return __data_; }
  constexpr const_pointer data() const noexcept { return __data_; }
  constexpr basic_string_view<charT> view() const noexcept { return basic_string_view<charT>(__data_, N); }
  constexpr operator basic_string_view<charT>() const noexcept { return view(); }

  template <size_t N2>
  constexpr friend basic_fixed_string<charT, N + N2>
  operator+(const basic_fixed_string& lhs, const basic_fixed_string<charT, N2>& rhs) noexcept {
    return [&]<size_t... Is>(index_sequence<Is...>) {
      return basic_fixed_string<charT, N + N2>{(Is < N ? lhs[Is] : rhs[Is - N])...};
    }(make_index_sequence<N + N2>{});
  }

  constexpr friend basic_fixed_string<charT, N + 1>
  operator+(const basic_fixed_string& lhs, charT rhs) noexcept {
    return [&]<size_t... Is>(index_sequence<Is...>) {
      return basic_fixed_string<charT, N + 1>{(Is < N ? lhs[Is] : rhs)...};
    }(make_index_sequence<N + 1>{});
  }

  constexpr friend basic_fixed_string<charT, 1 + N>
  operator+(charT lhs, const basic_fixed_string& rhs) noexcept {
    return [&]<size_t... Is>(index_sequence<Is...>) {
      return basic_fixed_string<charT, 1 + N>{(Is == 0 ? lhs : rhs[Is - 1])...};
    }(make_index_sequence<1 + N>{});
  }

  template <size_t N2>
  consteval friend basic_fixed_string<charT, N + N2 - 1>
  operator+(const basic_fixed_string& lhs, const charT (&rhs)[N2]) noexcept {
    __basic_fixed_string_detail::__fixed_string_require_nt(rhs, "basic_fixed_string: array argument must be null-terminated");
    return [&]<size_t... Is>(index_sequence<Is...>) {
      return basic_fixed_string<charT, N + N2 - 1>{(Is < N ? lhs[Is] : rhs[Is - N])...};
    }(make_index_sequence<N + N2 - 1>{});
  }

  template <size_t N1>
  consteval friend basic_fixed_string<charT, N1 + N - 1>
  operator+(const charT (&lhs)[N1], const basic_fixed_string& rhs) noexcept {
    __basic_fixed_string_detail::__fixed_string_require_nt(lhs, "basic_fixed_string: array argument must be null-terminated");
    return [&]<size_t... Is>(index_sequence<Is...>) {
      return basic_fixed_string<charT, N1 + N - 1>{(Is < N1 - 1 ? lhs[Is] : rhs[Is - (N1 - 1)])...};
    }(make_index_sequence<N1 + N - 1>{});
  }

  // non-member comparison functions
  template <size_t N2>
  friend constexpr bool operator==(const basic_fixed_string& lhs, const basic_fixed_string<charT, N2>& rhs) {
    return lhs.view() == rhs.view();
  }

  template <size_t N2>
  friend consteval bool operator==(const basic_fixed_string& lhs, const charT (&rhs)[N2]) {
    __basic_fixed_string_detail::__fixed_string_require_nt(rhs, "basic_fixed_string: array argument must be null-terminated");
    return lhs.view() == basic_string_view<charT>(rhs, rhs + N2 - 1);
  }

  template <size_t N2>
  friend constexpr auto operator<=>(const basic_fixed_string& lhs, const basic_fixed_string<charT, N2>& rhs) {
    return lhs.view() <=> rhs.view();
  }

  template <size_t N2>
  friend consteval auto operator<=>(const basic_fixed_string& lhs, const charT (&rhs)[N2]) {
    __basic_fixed_string_detail::__fixed_string_require_nt(rhs, "basic_fixed_string: array argument must be null-terminated");
    return lhs.view() <=> basic_string_view<charT>(rhs, rhs + N2 - 1);
  }

  // inserters and extractors
  friend basic_ostream<charT>& operator<<(basic_ostream<charT>& os, const basic_fixed_string& str) {
    return os << str.view();
  }
};

// deduction guides
template <typename CharT, convertible_to<CharT>... Rest>
basic_fixed_string(CharT, Rest...) -> basic_fixed_string<CharT, 1 + sizeof...(Rest)>;

template <typename charT, size_t N>
basic_fixed_string(const charT (&str)[N]) -> basic_fixed_string<charT, N - 1>;

template <typename CharT, size_t N>
basic_fixed_string(from_range_t, array<CharT, N>) -> basic_fixed_string<CharT, N>;

// specialized algorithms
template <class charT, size_t N>
constexpr void swap(basic_fixed_string<charT, N>& x, basic_fixed_string<charT, N>& y) noexcept {
  x.swap(y);
}

// basic_fixed_string typedef-names
template <size_t N> using fixed_string    = basic_fixed_string<char, N>;
template <size_t N> using fixed_u8string  = basic_fixed_string<char8_t, N>;
template <size_t N> using fixed_u16string = basic_fixed_string<char16_t, N>;
template <size_t N> using fixed_u32string = basic_fixed_string<char32_t, N>;
template <size_t N> using fixed_wstring   = basic_fixed_string<wchar_t, N>;

// hash support
template <size_t N> struct hash<fixed_string<N>>    : hash<string_view> {};
template <size_t N> struct hash<fixed_u8string<N>>  : hash<u8string_view> {};
template <size_t N> struct hash<fixed_u16string<N>> : hash<u16string_view> {};
template <size_t N> struct hash<fixed_u32string<N>> : hash<u32string_view> {};
template <size_t N> struct hash<fixed_wstring<N>>   : hash<wstring_view> {};

// formatting support (debug-enabled string type specialization)
template <class charT, size_t N>
struct formatter<basic_fixed_string<charT, N>, charT> : formatter<basic_string_view<charT>, charT> {};

// The UDL proposal was rejected [P3094R5]/6.8
namespace [[deprecated("Proposal [P3094R5]/6.8 on user-defined literals was rejected.")]] literals::fixed_string_literals {
  
template<fixed_string fs>
constexpr auto operator""fs () { return fs; }

template<fixed_wstring fws>
constexpr auto operator""fws () { return fws; }

template<fixed_u8string fu8s>
constexpr auto operator""fu8s () { return fu8s; }

template<fixed_u16string fu16s>
constexpr auto operator""fu16s () { return fu16s; }

template<fixed_u32string fu32s>
constexpr auto operator""fu32s () { return fu32s; }

}  // namespace fixed_string_literals

} // namespace std