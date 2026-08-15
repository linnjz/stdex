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
      bool are_all_mems_three_way_comparable = true;
      bool are_all_mems_compare_nothrow = true;
      bool are_all_empty_type_aggregated = true;
      bool are_all_mems_destructible = true;
      bool are_all_mems_trivially_destructible = true;
      bool are_all_mems_copy_constructible = true;
      bool are_all_mems_trivially_copy_constructible = true;
      bool are_all_mems_copy_assignable = true;
      bool are_all_mems_trivially_copy_assignable = true;
      bool are_all_mems_move_constructible = true;
      bool are_all_mems_trivially_move_constructible = true;
      bool are_all_mems_move_assignable = true;
      bool are_all_mems_trivially_move_assignable = true;
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
      using member_plain_t = std::remove_cvref_t<member_type<I>>;
      if (m.max_size < sizeof(storage_t)) { m.max_size = sizeof(storage_t); }
      if (m.max_align < alignof(storage_t)) { m.max_align = alignof(storage_t); }
      m.are_all_mems_copy_assignment_nothrow &= std::is_nothrow_copy_assignable_v<storage_t>;
      m.are_all_mems_copy_constructor_nothrow &= std::is_nothrow_copy_constructible_v<storage_t>;
      m.are_all_mems_move_assignment_nothrow &= std::is_nothrow_move_assignable_v<storage_t>;
      m.are_all_mems_move_constructor_nothrow &= std::is_nothrow_move_constructible_v<storage_t>;
      m.are_all_mems_default_constructor_nothrow &= std::is_nothrow_default_constructible_v<storage_t>;
      // availability: folded on member types (not storage) so that e.g. const value members
      // correctly make assignment unavailable; references to incomplete types are fine.
      m.are_all_mems_destructible &= std::is_destructible_v<member_type<I>>;
      m.are_all_mems_trivially_destructible &= std::is_trivially_destructible_v<member_type<I>>;
      m.are_all_mems_copy_constructible &= std::is_copy_constructible_v<member_type<I>>;
      m.are_all_mems_trivially_copy_constructible &= std::is_trivially_copy_constructible_v<member_type<I>>;
      m.are_all_mems_copy_assignable &= std::is_copy_constructible_v<member_type<I>> && std::is_copy_assignable_v<member_type<I>>;
      m.are_all_mems_trivially_copy_assignable &= std::is_trivially_copy_constructible_v<member_type<I>> && std::is_trivially_copy_assignable_v<member_type<I>>;
      m.are_all_mems_move_constructible &= std::is_move_constructible_v<member_type<I>>;
      m.are_all_mems_trivially_move_constructible &= std::is_trivially_move_constructible_v<member_type<I>>;
      m.are_all_mems_move_assignable &= std::is_move_constructible_v<member_type<I>> && std::is_move_assignable_v<member_type<I>>;
      m.are_all_mems_trivially_move_assignable &= std::is_trivially_move_constructible_v<member_type<I>> && std::is_trivially_move_assignable_v<member_type<I>>;
      m.are_all_mems_ref_qualified &= std::is_reference_v<member_type<I>>;
      
      if constexpr (std::is_reference_v<member_type<I>> && requires { sizeof(storage_plain_t); }) { 
        // the address must be divisible by alignof (alignof is a power of 2),
        // with the lower log2(alignof) bits always being 0.
        m.ref_tag_bits = std::min<size_t>(m.ref_tag_bits, std::countr_zero(alignof(storage_plain_t)));
      } else if constexpr (std::is_reference_v<member_type<I>>) {
        m.are_all_mems_ref_qualified = false;  // points to an incomplete type, giving up the niche
      }
      
      if constexpr (std::is_reference_v<member_type<I>> && !requires { sizeof(member_plain_t); }) {
          m.are_all_mems_three_way_comparable = false;
          m.are_all_mems_compare_nothrow = false;
      } else if constexpr (!std::is_empty_v<member_plain_t>) {
          m.are_all_mems_three_way_comparable &= std::three_way_comparable<member_plain_t>;
          m.are_all_mems_compare_nothrow &= std::three_way_comparable<member_plain_t> &&
              requires (member_plain_t const &a, member_plain_t const &b) { { a <=> b } noexcept; };
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
      using storage_ptr_t = std::add_const_t<storage_type<I>> *;
      return **reinterpret_cast<storage_ptr_t>(self.storage);

    } else {
      using member_ptr_t = std::conditional_t<std::is_const_v<std::remove_reference_t<self_t>>,
            member_unref_t const *, member_unref_t *>;
      return std::forward_like<self_t>(*reinterpret_cast<member_ptr_t>(self.storage));
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
      (std::is_array_v<storage_type<I>>
         ? (sizeof...(Args) == 0 ||
            (std::is_nothrow_constructible_v<std::remove_extent_t<storage_type<I>>, Args> && ...))
         : std::is_nothrow_constructible_v<storage_type<I>, Args...>)
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
    } else if constexpr (std::is_array_v<storage_t>) {
    // C++23 N5032 [specialized.construct]/2 forbids construct_at with arguments for array
    // types, so arrays go through a braced placement new instead: each argument
    // value-initializes one element in place (also works for class element types).
    static_assert(sizeof...(Args) == 0 || sizeof...(Args) == std::extent_v<storage_t>,
                  "array member: pass exactly one initializer per element, or none for value-initialization");
    ::new (static_cast<void *>(storage)) storage_t{std::forward<Args>(args)...};
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
  template <size_t I>
  using storage_type = typename base::template storage_type<I>;
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
    using member_ptr_t = std::conditional_t<std::is_const_v<std::remove_reference_t<self_t>>,
          member_unref_t const *, member_unref_t *>;
    if constexpr (I == metadata.bool_type_index) {
      return std::forward_like<self_t>(*reinterpret_cast<member_ptr_t>(&self.slot));
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

// ============================================================================
// field_variant single-class definition: C++23 P0848 "conditional trivial special members"
//   libc++'s five-layer inheritance skeleton exists to express the "three states" (trivial/available/unavailable) in C++17;
//   P0848 allows member functions of class templates to carry a requires-clause ([dcl.decl.general]/4 +
//   [temp.pre]/1), [special]/6 eligibility, [class.prop] triviality,
//   [dcl.fct.def.default]/4 (non-eligible defaulted special members are defined as deleted)
//   together cover exactly the same three states, so they collapse into one class:
//     trivially available -> = default (compiler bitwise copy, preserves triviality)
//     available     -> hand-written member-wise operations (same-index assignment / destroy-then-construct)
//     unavailable   -> = delete (propagation from const members / deleted members)
//   Note: a declared constrained copy constructor still suppresses the implicit move constructor; all five dimensions must be declared explicitly;
//   the three overloads' requires-clauses must be mutually exclusive.
// ============================================================================

// Dispatch index from runtime to compile-time I: the visitor receives std::integral_constant<size_t, I>.
// The visitor must return the same type for all I (currently void within this skeleton).
template <class Self, class F>
constexpr decltype(auto) at_index(Self&& self, F&& f) {
  constexpr size_t N = std::remove_cvref_t<Self>::member_count;
  return [&]<size_t... Is>(std::index_sequence<Is...>) -> decltype(auto) {
    using R = std::common_reference_t<decltype(f(std::integral_constant<size_t, Is>{}))...>;
    auto dispatch = [&]<size_t I>(this auto&& d) -> R {
      if constexpr (I == N) {
        std::unreachable();  // disengaged
      } else if (self.index() == I) {
        return f(std::integral_constant<size_t, I>{});
      } else {
        return d.template operator()<I + 1>();
      }
    };
    return dispatch.template operator()<0>();
  }(std::make_index_sequence<N>{});
}

template <class T>
class field_variant {
  using meta = storage_meta<T>;
  static_assert(meta::member_count > 0 && 
      []<std::size_t... Is>(std::index_sequence<Is...>) constexpr noexcept -> bool {
        return (std::is_destructible_v<std::tuple_element_t<Is, typename meta::member_types>> && ...);
      }(std::make_index_sequence<meta::member_count>{}),
      "field_variant<T>: all non-static data members of T must satisfy Cpp17Destructible "
      "requirements (N4950 [variant.variant.general]/2).");
  
public:
  constexpr field_variant() = default;

  // ---- destructor ----
  constexpr ~field_variant() requires (meta::metadata.are_all_mems_trivially_destructible) = default;
  constexpr ~field_variant() requires (!meta::metadata.are_all_mems_trivially_destructible &&
                               meta::metadata.are_all_mems_destructible) {
    if (s_.has_value()) {
      at_index(s_, [&]<size_t I>(std::integral_constant<size_t, I>) 
          noexcept(noexcept(s_.template destroy<I>())) 
          -> void {
        s_.template destroy<I>();
      });
    }
  }
  constexpr ~field_variant() requires (!meta::metadata.are_all_mems_destructible) = delete;

  // ---- copy constructor ----
  constexpr field_variant(const field_variant&) 
      requires (meta::metadata.are_all_mems_trivially_copy_constructible)
      = default;
  constexpr field_variant(const field_variant& other)
      noexcept(meta::metadata.are_all_mems_copy_constructor_nothrow)
      requires (!meta::metadata.are_all_mems_trivially_copy_constructible &&
                meta::metadata.are_all_mems_copy_constructible)
      : s_() {
    if (other.s_.has_value()) {
      at_index(other.s_, [&]<size_t I>(std::integral_constant<size_t, I>) 
          noexcept(noexcept(s_.template construct<I>(other.s_.template get<I>()))) 
          -> void {
        s_.template construct<I>(other.s_.template get<I>());
      });
    }
  }
  constexpr field_variant(const field_variant&) 
      requires (!meta::metadata.are_all_mems_copy_constructible)
      = delete;

  // ---- move constructor ----
  constexpr field_variant(field_variant&&) 
      requires (meta::metadata.are_all_mems_trivially_move_constructible) 
      = default;
  constexpr field_variant(field_variant&& other)
      noexcept(meta::metadata.are_all_mems_move_constructor_nothrow)
      requires (!meta::metadata.are_all_mems_trivially_move_constructible &&
                meta::metadata.are_all_mems_move_constructible)
      : s_() {
    if (other.s_.has_value()) {
      at_index(other.s_, [&]<size_t I>(std::integral_constant<size_t, I>) noexcept(
          std::is_reference_v<typename meta::template member_type<I>> ||
          std::is_nothrow_move_constructible_v<typename meta::template storage_type<I>>) 
          -> void {
        if constexpr (std::is_reference_v<typename meta::template member_type<I>>) {
          s_.template construct<I>(other.s_.template get<I>());  // reference members are always copied
        } else {
          s_.template construct<I>(std::move(other.s_.template get<I>()));
        }
      });
    }
  }
  constexpr field_variant(field_variant&&) 
      requires (!meta::metadata.are_all_mems_move_constructible) 
      = delete;

  // ---- copy assignment ----
  constexpr field_variant& operator=(const field_variant&) 
      requires (meta::metadata.are_all_mems_trivially_copy_assignable) 
      = default;
  constexpr field_variant& operator=(const field_variant& other)
      noexcept(meta::metadata.are_all_mems_copy_assignment_nothrow &&
               meta::metadata.are_all_mems_copy_constructor_nothrow)
      requires (!meta::metadata.are_all_mems_trivially_copy_assignable &&
                meta::metadata.are_all_mems_copy_assignable) {
    if (s_.index() == other.s_.index()) {
      if (s_.has_value()) {
        at_index(s_, [&]<size_t I>(std::integral_constant<size_t, I>) 
          noexcept(noexcept(s_.template get<I>() = other.s_.template get<I>())) 
          -> void {
          s_.template get<I>() = other.s_.template get<I>();
        });
      }
    } else {
      if (s_.has_value()) {
        at_index(s_, [&]<size_t I>(std::integral_constant<size_t, I>) 
          noexcept(noexcept(s_.template destroy<I>())) 
          -> void {
          s_.template destroy<I>();
        });
      }
      if (other.s_.has_value()) {
        at_index(other.s_, [&]<size_t I>(std::integral_constant<size_t, I>) 
          noexcept(noexcept(s_.template construct<I>(other.s_.template get<I>()))) 
          -> void {
          s_.template construct<I>(other.s_.template get<I>());
        });
      }
    }
    return *this;
  }
  constexpr field_variant& operator=(const field_variant&) 
      requires (!meta::metadata.are_all_mems_copy_assignable) 
      = delete;

  // ---- move assignment ----
  constexpr field_variant& operator=(field_variant&&) 
      requires (meta::metadata.are_all_mems_trivially_move_assignable) 
      = default;
  constexpr field_variant& operator=(field_variant&& other)
      noexcept(meta::metadata.are_all_mems_move_assignment_nothrow &&
               meta::metadata.are_all_mems_move_constructor_nothrow)
      requires (!meta::metadata.are_all_mems_trivially_move_assignable &&
                meta::metadata.are_all_mems_move_assignable) {
    if (s_.index() == other.s_.index()) {
      if (s_.has_value()) {
        at_index(s_, [&]<size_t I>(std::integral_constant<size_t, I>) 
          noexcept(noexcept(s_.template get<I>() = std::move(other.s_.template get<I>()))) 
          -> void {
          s_.template get<I>() = std::move(other.s_.template get<I>());
        });
      }
    } else {
      if (s_.has_value()) {
        at_index(s_, [&]<size_t I>(std::integral_constant<size_t, I>) 
          noexcept(noexcept(s_.template destroy<I>())) 
          -> void {
          s_.template destroy<I>();
        });
      }
      if (other.s_.has_value()) {
        at_index(other.s_, [&]<size_t I>(std::integral_constant<size_t, I>) noexcept(
            std::is_reference_v<typename meta::template member_type<I>> ||
            std::is_nothrow_move_constructible_v<typename meta::template storage_type<I>>) 
            -> void {
          if constexpr (std::is_reference_v<typename meta::template member_type<I>>) {
            s_.template construct<I>(other.s_.template get<I>());
          } else {
            s_.template construct<I>(std::move(other.s_.template get<I>()));
          }
        });
      }
    }
    return *this;
  }
  constexpr field_variant& operator=(field_variant&&) 
      requires (!meta::metadata.are_all_mems_move_assignable) 
      = delete;

  constexpr std::size_t size() const noexcept { return meta::member_count; }
  constexpr std::size_t npos() const noexcept { return meta::member_count; }
  constexpr bool has_value() const noexcept { return s_.has_value(); }
  constexpr std::size_t index() const noexcept { return s_.index(); }

  template <size_t I>
  constexpr decltype(auto) get(this auto&& self) noexcept {
    return std::forward_like<decltype(self)>(self).s_.template get<I>();
  }
  template <class U>
  constexpr decltype(auto) get(this auto&& self) noexcept {
    return std::forward_like<decltype(self)>(self).s_.template get<U>();
  }

  template <size_t I, class... Args>
  constexpr decltype(auto) emplace(Args&&... args) noexcept(
      std::is_reference_v<typename meta::template member_type<I>> ||
      std::is_nothrow_constructible_v<typename meta::template storage_type<I>, Args...>) {
    if (s_.has_value()) {
      at_index(s_, [&]<size_t J>(std::integral_constant<size_t, J>) 
        noexcept(noexcept(s_.template destroy<J>())) 
        -> void {
        s_.template destroy<J>();
      });
    }
    return s_.template construct<I>(std::forward<Args>(args)...);
  }

  // ---- The following three are declared only; bodies implemented later ----
  constexpr auto operator<=>(const field_variant& other) const
      noexcept(meta::metadata.are_all_mems_compare_nothrow)
      requires (meta::metadata.are_all_mems_three_way_comparable);

  template <class F>
  constexpr decltype(auto) visit(this auto&& self, F&& f);

  constexpr void swap(field_variant& other) noexcept(
      meta::metadata.are_all_mems_move_constructor_nothrow &&
      meta::metadata.are_all_mems_move_assignment_nothrow);
      
private:
  storage_base<T> s_;
};

} // namespace stdex
