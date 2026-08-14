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

#include <array>
#include <bit>
#include <cassert>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <meta>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>


namespace stdex { 
  
template <class T>
struct storage_meta 
{
  static constexpr auto nonstatic_data_members {
    std::define_static_array(std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()))
  };
  static constexpr size_t member_count = nonstatic_data_members.size();
  using tag_type = std::conditional_t<
      (member_count <= 255), 
      std::uint8_t,
      std::conditional_t<
          (member_count <= 65535), 
          std::uint16_t, 
          std::conditional_t<
              (member_count <= 4294967295ULL), 
              std::uint32_t, 
              std::uint64_t
          >
      >
  >;

  template<size_t I>
  using member_type = typename [: std::meta::type_of(nonstatic_data_members[I]) :];
  template <size_t... Is>
  static auto member_tuple_impl(std::index_sequence<Is...>) -> std::tuple<member_type<Is>...>;
  using member_types = decltype(member_tuple_impl(std::make_index_sequence<member_count>{}));
  
  template <class U, class Tuple>
  struct index_of_impl;
  template <class U, class... Ts>
  struct index_of_impl<U, std::tuple<Ts...>> {
    static constexpr size_t count = (size_t{std::is_same_v<U, Ts>} + ...);
    template <size_t... Is>
    static constexpr size_t get_index(std::index_sequence<Is...>) noexcept {
      size_t idx = member_count;
      ((std::is_same_v<U, Ts> ? (idx = Is) : 0), ...);
      return idx;
    }
    static constexpr size_t value = count == 1
        ? get_index(std::make_index_sequence<sizeof...(Ts)>{})
        : member_count;
  };
  template <class U>
  static constexpr size_t index_of = index_of_impl<U, member_types>::value;
  template <class U>
  static constexpr bool has_member_type = index_of_impl<U, member_types>::count >= 1;
  
  // 1. cannot directly store reference members in a byte array; 
  //    store M& as a "pointer to the referenced object"; 
  // 2. construct_at does not support const T*;
  //    value members are stripped of cv before storage,
  //    and const semantics are preserved member_type API layer
  template <class M>
  struct storage_of { using type = std::remove_cv_t<M>; };
  template <class M>
  struct storage_of<M&>  { using type = std::remove_reference_t<M>*; };
  template <class M>
  struct storage_of<M&&> { using type = std::remove_reference_t<M>*; };
  template <size_t I>
  using storage_type = typename storage_of<member_type<I>>::type;

  static constexpr auto metadata = [] consteval noexcept {
    struct metadata {
      size_t max_size = 0;
      size_t max_align = 1;
      bool are_all_mems_copy_assignment_nothrow = true;
      bool are_all_mems_copy_constructor_nothrow = true;
      bool are_all_mems_move_assignment_nothrow = true;
      bool are_all_mems_move_constructor_nothrow = true;
      bool are_all_mems_default_constructor_nothrow = true;
      bool are_all_mems_ref_qualified = true;
      bool are_all_empty_type_aggregated = true;
      size_t ref_tag_bits = std::numeric_limits<size_t>::digits;
      size_t ref_tag_mask = 0;
      size_t bool_type_index = 0;
      size_t bool_type_cnt = 0;
      size_t empty_type_cnt = 0;
      std::array<size_t, member_count> empty_type_indices{};
      std::array<unsigned char, member_count> empty_type_niche_tags{};
    } m;
    template for (constexpr auto I : std::views::iota(0zu, member_count)) {
      using storage_t = storage_type<I>;
      using storage_plain_t = std::remove_pointer_t<storage_t>;
      if (m.max_size < sizeof(storage_t)) { m.max_size = sizeof(storage_t); }
      if (m.max_align < alignof(storage_t)) { m.max_align = alignof(storage_t); }
      m.are_all_mems_move_constructor_nothrow &= std::is_nothrow_move_constructible_v<storage_t>;
      m.are_all_mems_default_constructor_nothrow &= std::is_nothrow_default_constructible_v<storage_t>;
      m.are_all_mems_copy_constructor_nothrow &= std::is_nothrow_copy_constructible_v<storage_t>;
      m.are_all_mems_copy_assignment_nothrow &= std::is_nothrow_copy_assignable_v<storage_t>;
      m.are_all_mems_move_assignment_nothrow &= std::is_nothrow_move_assignable_v<storage_t>;
      m.are_all_mems_ref_qualified &= std::is_reference_v<member_type<I>>;
      if constexpr (std::is_reference_v<member_type<I>> && requires { sizeof(storage_plain_t); }) { 
        // the address must be divisible by alignof (alignof is a power of 2),
        // with the lower log2(alignof) bits always being 0.
        m.ref_tag_bits = std::min<size_t>(m.ref_tag_bits, std::countr_zero(alignof(storage_plain_t)));
      } else if constexpr (std::is_reference_v<member_type<I>>) {
        m.are_all_mems_ref_qualified = false;  // points to an incomplete type, giving up the niche
      }
      if constexpr (std::is_same_v<storage_t, bool>) {
        m.bool_type_index = I;
        ++m.bool_type_cnt;
      } else if constexpr (std::is_empty_v<storage_t>) {
        m.are_all_empty_type_aggregated &= std::is_aggregate_v<storage_t> && std::is_trivially_destructible_v<storage_t>;
        m.empty_type_indices[m.empty_type_cnt] = I;
        m.empty_type_niche_tags[I] = static_cast<unsigned char>(2 + m.empty_type_cnt);
        ++m.empty_type_cnt;
      }
    }
    // empty class/zero-length arrays aren't allowed, so they end up as 1 byte
    m.max_size = m.max_size ? m.max_size : 1;
    m.ref_tag_mask = m.ref_tag_bits >= std::numeric_limits<size_t>::digits
        ? 0 : (1uz << m.ref_tag_bits) - 1;
    return m;
  }();

  static constexpr bool niche_opt_in = requires { typename T::is_niche; };
  // ref niche: all referenced members
  static constexpr bool niche_ref_capable =
      niche_opt_in && metadata.are_all_mems_ref_qualified &&
      (metadata.ref_tag_bits >= std::numeric_limits<size_t>::digits ||
      member_count <= (1uz << metadata.ref_tag_bits));
  // bool niche: exactly one bool member, the rest are empty classes.
  //             bool 2 states + each slot variant 1 state  + 1 disengaged state <= 256
  static constexpr bool niche_bool_capable = 
      niche_opt_in && metadata.are_all_empty_type_aggregated && metadata.bool_type_cnt == 1 &&
      (2 + metadata.empty_type_cnt + 1 <= 256) &&
      (metadata.bool_type_cnt + metadata.empty_type_cnt) == member_count;

#pragma region
  constexpr size_t size() const noexcept {
    return member_count;
  }
  
  constexpr size_t npos() const noexcept {
    return member_count;
  }
#pragma endregion
};

template <size_t I, class T>
using member_type_at = typename storage_meta<T>::template member_type<I>;

template <class T>
struct storage_base;

template <class T>
requires (!storage_meta<T>::niche_ref_capable && !storage_meta<T>::niche_bool_capable)
struct storage_base<T> : storage_meta<T>
{
  using base = storage_meta<T>;
  using tag_type = typename base::tag_type;
  template <size_t I>
  using member_type = typename base::template member_type<I>;
  template <size_t I>
  using storage_type = typename base::template storage_type<I>;
  using base::member_count;
  using base::metadata;

  alignas(metadata.max_align) std::byte storage[metadata.max_size];
  tag_type tag { static_cast<tag_type>(member_count) };

#pragma region
  constexpr std::size_t index() const noexcept {
    return static_cast<size_t>(tag);
  }

  constexpr bool has_value() const noexcept {
    return static_cast<size_t>(tag) != member_count;
  }

  template <size_t I>
  constexpr decltype(auto) get(this auto &&self) noexcept {
    static_assert(I < member_count, "Index out of range");
    assert(self.index() == I && "get: member is not the active one");
    using self_t = decltype(self);
    using storage_t = storage_type<I>;
    using member_unref_t = std::remove_reference_t<member_type<I>>;
    if constexpr (std::is_reference_v<member_type<I>>) {
      return **reinterpret_cast<storage_t *>(self.storage);

    } else {
      return std::forward_like<self_t>(*reinterpret_cast<member_unref_t *>(self.storage));
    }
  }

  template <class U>
  constexpr decltype(auto) get(this auto &&self) noexcept {
    static_assert(base::template index_of<U> != member_count,
                  "get<U>: U must match exactly one member type");
    return self.template get<base::template index_of<U>>();
  }

  template <size_t I, class... Args>
  constexpr decltype(auto) construct(Args&&...args) noexcept(
      std::is_reference_v<member_type<I>> ||
      std::is_nothrow_constructible_v<storage_type<I>, Args...>
  ) {
    static_assert(I < member_count, "Index out of range");
    assert(!has_value() && "construct: storage already engaged");
    using storage_t = storage_type<I>;
    if constexpr (std::is_reference_v<member_type<I>>) {
      static_assert(sizeof...(Args) == 1 && (std::is_lvalue_reference_v<Args> && ...),
                    "construct for a reference member requires exactly one lvalue argument");
      static_assert(!(std::reference_constructs_from_temporary_v<member_type<I>, Args> && ...),
                    "construct for a reference member would bind to a temporary");
      std::construct_at(reinterpret_cast<storage_t *>(storage), std::addressof(std::forward<Args>(args)...));
    } else {
      std::construct_at(reinterpret_cast<storage_t *>(storage), std::forward<Args>(args)...);
    }
    tag = static_cast<tag_type>(I);
    return (*this).template get<I>();
  }

  template <size_t I>
  constexpr void destroy() noexcept(std::is_nothrow_destructible_v<storage_type<I>>) {
    static_assert(I < member_count, "Index out of range");
    assert(index() == I && "destroy: member is not the active one");
    if constexpr (!std::is_reference_v<member_type<I>> &&
                  !std::is_trivially_destructible_v<storage_type<I>>) {
      std::destroy_at(reinterpret_cast<storage_type<I> *>(storage));
    }
    tag = static_cast<tag_type>(member_count);
  }

#pragma endregion
};

/// niche version: all members are reference, and member_cnt <= min(std::countr_zero(alignof(M))...)
template <class T>
requires (storage_meta<T>::niche_ref_capable)
struct storage_base<T> : storage_meta<T>
{
  using base = storage_meta<T>;
  template <size_t I>
  using member_type = typename base::template member_type<I>;
  using base::member_count;
  using base::metadata;
  
  // directly reading and writing uintptr_t objects: no strict aliasing issues
  // avoids UB from reinterpret_cast<std::byte* to uintptr_t*>
  std::uintptr_t slot = 0;

#pragma region
  constexpr std::size_t index() const noexcept {
    // when there's only one member, ref_tag_mask=0: engaged always returns 0;
    // disengaged returns member_count
    return slot == 0 ? member_count
                       : static_cast<size_t>(slot & metadata.ref_tag_mask);
  }

  constexpr bool has_value() const noexcept {
    return slot != 0;
  }

  template <size_t I>
  constexpr decltype(auto) get(this auto &&self) noexcept {
    static_assert(I < member_count, "Index out of range");
    assert(self.index() == I && "get: member is not the active one");
    using storage_t = storage_type<I>;
    return *reinterpret_cast<storage_t>(self.slot & ~metadata.ref_tag_mask);
  }

  template <class U>
  constexpr decltype(auto) get(this auto &&self) noexcept {
    static_assert(base::template index_of<U> != member_count,
                  "get<U>: U must match exactly one member type");
    return self.template get<base::template index_of<U>>();
  }

  template <size_t I, class... Args>
  constexpr decltype(auto) construct(Args&&...args) noexcept {
    static_assert(I < member_count, "Index out of range");
    assert(!has_value() && "construct: storage already engaged");
    static_assert(sizeof...(Args) == 1 && (std::is_lvalue_reference_v<Args> && ...),
                  "construct for a reference member requires exactly one lvalue argument");
    static_assert(!(std::reference_constructs_from_temporary_v<member_type<I>, Args> && ...),
                  "construct for a reference member would bind to a temporary");
    auto addr = reinterpret_cast<std::uintptr_t>(std::addressof(std::forward<Args>(args)...));
    assert((addr & metadata.ref_tag_mask) == 0);  // alignment guarantee, defensive checks
    slot = addr | I;
    return (*this).template get<I>();
  }

  template <size_t I>
  constexpr void destroy() noexcept {
    static_assert(I < member_count, "Index out of range");
    assert(index() == I && "destroy: member is not the active one");
    slot = 0; // disengaged
  }

#pragma endregion
};

/// bool niche version: exactly 1 bool member, the rest are empty classes/structs
template <class T>
requires (storage_meta<T>::niche_bool_capable)
struct storage_base<T> : storage_meta<T>
{
  using base = storage_meta<T>;
  template <size_t I>
  using member_type = typename base::template member_type<I>;
  using base::member_count; // [1...254]
  using base::metadata;
  
  static constexpr unsigned char disengaged = static_cast<unsigned char>(member_count + 1);
  unsigned char slot = disengaged;

#pragma region
  constexpr std::size_t index() const noexcept {
    if (slot <= 1) { return metadata.bool_type_index; }
    if (slot <= member_count) { return metadata.empty_type_indices[slot - 2]; }
    return member_count; // index, member_count = disengaged - 1
  }

  constexpr bool has_value() const noexcept {
    return slot != disengaged;
  }

  template <size_t I>
  constexpr decltype(auto) get(this auto &&self) noexcept {
    static_assert(I < member_count, "Index out of range");
    assert(self.index() == I && "get: member is not the active one");
    using self_t = decltype(self);
    using member_unref_t = std::remove_reference_t<member_type<I>>;
    if constexpr (I == metadata.bool_type_index) {
      return std::forward_like<self_t>(*reinterpret_cast<member_unref_t *>(&self.slot));
    } else {
      return member_type<I>{};
    }
  }

  template <class U>
  constexpr decltype(auto) get(this auto &&self) noexcept {
    static_assert(base::template index_of<U> != member_count,
                  "get<U>: U must match exactly one member type");
    return self.template get<base::template index_of<U>>();
  }
  
  template <size_t I, class... Args>
  constexpr decltype(auto) construct(Args&&...args) noexcept {
    static_assert(I < member_count, "Index out of range");
    assert(!has_value() && "construct: storage already engaged");
    if constexpr (I == metadata.bool_type_index) {
      std::construct_at(reinterpret_cast<bool *>(&slot), std::forward<Args>(args)...);
    } else {
      static_assert(sizeof...(Args) == 0, // empty variant
                    "construct for an empty member requires no arguments");
      slot = metadata.empty_type_niche_tags[I];
    }
    return (*this).template get<I>();
  }

  template <size_t I>
  constexpr void destroy() noexcept {
    static_assert(I < member_count, "Index out of range");
    assert(index() == I && "destroy: member is not the active one");
    if constexpr (I == metadata.bool_type_index) {
      std::destroy_at(reinterpret_cast<bool *>(&slot));
    }
    slot = disengaged;
  }

#pragma endregion

};

} // namespace stdex
