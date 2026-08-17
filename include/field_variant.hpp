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
#include <functional>
#include <limits>
#include <memory>
#include <meta>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>

#include "fixed_string.hpp"

namespace stdex { 

namespace field_variant_detail {
  
/// Reflection-driven metadata for T's non-static data members (P2996).
template <class T>
struct storage_meta 
{
  static constexpr auto nonstatic_data_members {
    std::define_static_array(std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()))
  };
  static constexpr size_t member_count = nonstatic_data_members.size();
  /// Tag width sized to the member count.
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
  /// Index of the unique member of type U; member_count serves as the
  /// sentinel when U is absent or matches more than one member.
  template <class U>
  static constexpr size_t index_of = index_of_impl<U, member_types>::value;
  template <class U>
  static constexpr bool has_member_type = index_of_impl<U, member_types>::count >= 1;
  
  /// Index of the member whose name equals Fs; member_count serves as the
  /// sentinel when no member matches. A length prefilter restricts the
  /// per-iteration comparison to an O(1) size check in the common case.
  ///
  /// P1306R5 permits `break` in expansion statements (jumping past the last
  /// expansion), but GCC's constant evaluator currently rejects it with a
  /// bogus "flows off the end of the function" error; see PR c++/124924.
  /// Once fixed, the scan may terminate early via `break` instead of
  /// carrying the sentinel to the end.
  template <fixed_string Fs>
  static consteval size_t index_of_name() noexcept {
    constexpr std::string_view target = Fs.view();
    size_t found = member_count;
    template for (constexpr auto I : std::views::iota(0zu, member_count)) {
      constexpr std::string_view id = std::meta::identifier_of(nonstatic_data_members[I]);
      if (id.size() == target.size() && id == target) { found = I; /* break; */ }
    }
    return found;
  }
  
  
  /// Storage type mapping for each member type:
  /// - Reference members are stored as a pointer to the referenced object,
  ///   since a reference member cannot be stored in a byte array.
  /// - Value members are stored without cv-qualifiers, because
  ///   std::construct_at does not accept const T*; const semantics are
  ///   preserved at the member_type API layer.
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
      bool are_all_mems_hashable = true;
      size_t ref_tag_bits = std::numeric_limits<size_t>::digits;
      size_t ref_tag_mask = 0;
      size_t bool_type_index = 0;
      size_t bool_type_cnt = 0;
      size_t empty_type_cnt = 0;
      /// Code table: 0/1 denote bool (false/true), 2..N denote empty members
      /// in declaration order, and N+1 denotes the disengaged state;
      /// the valid codes are exactly 0..N+1, N+2 in total.
      std::array<size_t, member_count + 2> index_of_code;
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
      
      /// std::hash availability: reference members hash the referred object,
      /// value members hash the stored value, and empty members hash by index
      /// alone. The requires-expression models the exact call the hash
      /// operator performs, so cv-qualified and array members are handled.
      if constexpr (std::is_reference_v<member_type<I>>) {
        m.are_all_mems_hashable &= requires { std::hash<std::remove_cv_t<std::remove_reference_t<member_type<I>>>>{}(std::declval<member_type<I> const&>()); };
      } else if constexpr (!std::is_empty_v<member_plain_t>) {
        m.are_all_mems_hashable &= !std::is_array_v<member_plain_t> && requires { std::hash<std::remove_cv_t<member_plain_t>>{}(std::declval<member_type<I> const&>()); };
      }
      
      if constexpr (std::is_reference_v<member_type<I>> && requires { sizeof(storage_plain_t); }) { 
        /// An aligned address is a multiple of alignof (a power of two),
        /// so its low log2(alignof) bits are always zero.
        m.ref_tag_bits = std::min<size_t>(m.ref_tag_bits, std::countr_zero(alignof(storage_plain_t)));
      } else if constexpr (std::is_reference_v<member_type<I>>) {
        m.are_all_mems_ref_qualified = false;  ///< Points to an incomplete type; the niche is abandoned.
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
        ++m.empty_type_cnt;
        /// Empty members are coded 2..N in declaration order; each bool
        /// member scanned before I shifts the code by one.
        m.index_of_code[2 + I - m.bool_type_cnt] = I;
      }
      
    }
    /// Code 0/1 = bool (false/true); N+1 = disengaged.
    m.index_of_code[0] = m.index_of_code[1] = m.bool_type_index;
    m.index_of_code[member_count + 1] = member_count;
    
    /// Empty members contribute zero size; clamp to 1 byte, since a
    /// zero-length storage array is not permitted.
    m.max_size = m.max_size ? m.max_size : 1;
    m.ref_tag_mask = m.ref_tag_bits >= std::numeric_limits<size_t>::digits
        ? 0 : (1uz << m.ref_tag_bits) - 1;
    return m;
  }();

  
  static constexpr bool niche_opt_in = requires { typename T::is_niche; };
  /// Reference niche: every member is a reference.
  static constexpr bool niche_ref_capable =
      niche_opt_in && metadata.are_all_mems_ref_qualified &&
      (metadata.ref_tag_bits >= std::numeric_limits<size_t>::digits ||
      member_count <= (1uz << metadata.ref_tag_bits));
  /// Bool niche: exactly one bool member; all others are empty classes.
  /// The bool's two states, one state per empty member, and the disengaged
  /// state must fit into a single byte (N+2 <= 256).
  static constexpr bool niche_bool_capable = 
      niche_opt_in && metadata.are_all_empty_type_aggregated && metadata.bool_type_cnt == 1 &&
      (2 + metadata.empty_type_cnt + 1 <= 256) &&
      (metadata.bool_type_cnt + metadata.empty_type_cnt) == member_count;
};

template <class T>
struct storage_base;

/// Default storage: a byte array holding the active member, tagged by tag.
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
  using base::nonstatic_data_members;
  using base::member_count;
  using base::metadata;


/// GCC has not yet implemented the P2996R13 feature of injecting empty
/// destructors, so a std::byte array with reinterpret_cast is used for now.
/// Non-POD types are supported, though constant evaluation is limited.
#ifndef P2996R13_meta_reflection_define_aggregate
  alignas(metadata.max_align) std::byte storage[metadata.max_size]{};
#else
  /// The reflection-defined union path supports POD types only.
  union field_union_type;
  consteval {
    std::meta::define_aggregate(^^field_union_type, [] consteval noexcept /* std::span */ {
      std::array<std::meta::info, member_count> members;
      template for (constexpr auto I : std::views::iota(0zu, member_count)) {
        members[I] = std::meta::data_member_spec(^^storage_type<I>, { 
          .name = std::meta::identifier_of(nonstatic_data_members[I]) 
        });
      }
      return members;
    }());
  }
  field_union_type u_{};

  template <size_t I>
  static consteval std::meta::info member_refl() noexcept {
    return std::meta::nonstatic_data_members_of(^^field_union_type, std::meta::access_context::unchecked())[I];
  }
#endif
  tag_type tag { static_cast<tag_type>(member_count) };  ///< Default: disengaged (tag = member_count).

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
    using member_unref_t = std::remove_reference_t<member_type<I>>;
    if constexpr (std::is_reference_v<member_type<I>>) {
#ifdef P2996R13_meta_reflection_define_aggregate
      return *(self.u_.[: member_refl<I>() :]);
#else
      return **reinterpret_cast<std::add_const_t<storage_type<I>> *>(self.storage);
#endif
    } else {
      using cv_member_t = std::conditional_t<std::is_const_v<std::remove_reference_t<self_t>>,
                                             std::add_const_t<member_unref_t>, member_unref_t>;
#ifdef P2996R13_meta_reflection_define_aggregate
      return std::forward_like<self_t>(*static_cast<cv_member_t *>(std::addressof(self.u_.[: member_refl<I>() :])));
#else
      return std::forward_like<self_t>(*reinterpret_cast<cv_member_t *>(self.storage));
#endif
    }
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
#ifdef P2996R13_meta_reflection_define_aggregate
      std::construct_at(std::addressof(u_.[: member_refl<I>() :]),
                        std::addressof(std::forward<Args>(args)...));
#else
      std::construct_at(reinterpret_cast<storage_t *>(storage), std::addressof(std::forward<Args>(args)...));
#endif
    } else if constexpr (std::is_array_v<storage_t>) {
    /// [specialized.construct]/2 (N5032) forbids construct_at with arguments
    /// for array types, so arrays are initialized via braced placement new:
    /// each argument value-initializes one element in place.
    static_assert(sizeof...(Args) == 0 || sizeof...(Args) == std::extent_v<storage_t>,
                  "array member: pass exactly one initializer per element, or none for value-initialization");
#ifdef P2996R13_meta_reflection_define_aggregate
    ::new (static_cast<void *>(std::addressof(u_.[: member_refl<I>() :]))) storage_t{std::forward<Args>(args)...};
#else
    ::new (static_cast<void *>(storage)) storage_t{std::forward<Args>(args)...};
#endif
    } else {
#ifdef P2996R13_meta_reflection_define_aggregate
      std::construct_at(std::addressof(u_.[: member_refl<I>() :]), std::forward<Args>(args)...);
#else
      std::construct_at(reinterpret_cast<storage_t *>(storage), std::forward<Args>(args)...);
#endif
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
#ifdef P2996R13_meta_reflection_define_aggregate
      std::destroy_at(std::addressof(u_.[: member_refl<I>() :]));
#else
      std::destroy_at(reinterpret_cast<storage_type<I> *>(storage));
#endif
    }
    tag = static_cast<tag_type>(member_count);
  }

#pragma endregion
};

/// Reference-niche specialization: every member is a reference, and the
/// member count fits within the alignment-guaranteed zero bits of an
/// address (member_cnt <= 2^ref_tag_bits).
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
  
  /// The pointer and tag are stored in a single uintptr_t slot, read and
  /// written as a real object; this avoids the strict-aliasing UB that a
  /// reinterpret_cast from std::byte* to uintptr_t* would entail.
  std::uintptr_t slot = 0;

#pragma region
  constexpr std::size_t index() const noexcept {
    /// With a single member, ref_tag_mask == 0: an engaged slot always
    /// yields index 0, while a disengaged slot yields member_count.
    return slot == 0 ? member_count
                       : static_cast<size_t>(slot & metadata.ref_tag_mask);
  }

  constexpr bool has_value() const noexcept {
    return slot != 0;
  }

  template <size_t I>
  decltype(auto) get(this auto &&self) noexcept {
    static_assert(I < member_count, "Index out of range");
    assert(self.index() == I && "get: member is not the active one");
    /// Clears the tag bits and dereferences the stored pointer.
    return *reinterpret_cast<storage_type<I>>(self.slot & ~metadata.ref_tag_mask);
  }

  template <size_t I, class... Args>
  decltype(auto) construct(Args&&...args) noexcept {
    static_assert(I < member_count, "Index out of range");
    assert(!has_value() && "construct: storage already engaged");
    static_assert((std::is_same_v<std::remove_cvref_t<Args>,
                              std::remove_cvref_t<member_type<I>>> && ...),
                  "argument type must match the reference member type");
    static_assert(sizeof...(Args) == 1 && (std::is_lvalue_reference_v<Args> && ...),
                  "construct for a reference member requires exactly one lvalue argument");
    static_assert(!(std::reference_constructs_from_temporary_v<member_type<I>, Args> && ...),
                  "construct for a reference member would bind to a temporary");
    auto addr = reinterpret_cast<std::uintptr_t>(std::addressof(std::forward<Args>(args)...));
    assert((addr & metadata.ref_tag_mask) == 0);  ///< Alignment guarantee (defensive check).
    slot = addr | I;
    return (*this).template get<I>();
  }

  template <size_t I>
  constexpr void destroy() noexcept {
    static_assert(I < member_count, "Index out of range");
    assert(index() == I && "destroy: member is not the active one");
    slot = 0;  ///< Disengaged.
  }

#pragma endregion
};

/// Bool-niche specialization: exactly one bool member; all others are empty classes.
template <class T>
requires (storage_meta<T>::niche_bool_capable)
struct storage_base<T> : storage_meta<T>
{
  using base = storage_meta<T>;
  template <size_t I>
  using member_type = typename base::template member_type<I>;
  using base::member_count;  ///< Fits one byte (codes 0..255): at most 254
                                 ///< members, leaving codes for the bool states
                                 ///< and the disengaged state.
  using base::metadata;
  
  static constexpr unsigned char disengaged = static_cast<unsigned char>(member_count + 1);

  /// Code 0/1 are reserved for bool; empty members start at 2, skipping
  /// the bool slot.
  static constexpr unsigned char code_of_member(size_t I) noexcept {
    return static_cast<unsigned char>(2 + I - (I > metadata.bool_type_index));
  }
  
  union field_union_type {
    bool bool_value;
    unsigned char state_code = disengaged;
  } u_;

#pragma region
  /// O(1) lookup of the member index via the state-code table.
  std::size_t index() const noexcept {
    return metadata.index_of_code[std::bit_cast<unsigned char>(u_)];
  }

  bool has_value() const noexcept {
    return std::bit_cast<unsigned char>(u_) != disengaged;
  }

  template <size_t I>
  constexpr decltype(auto) get(this auto &&self) noexcept {
      static_assert(I < member_count, "Index out of range");
      assert(self.index() == I && "get: member is not the active one");
      if constexpr (I == metadata.bool_type_index) {
          return std::forward_like<decltype(self)>(self.u_.bool_value);
      } else {
          return member_type<I>{};
      }
  }

  template <size_t I, class... Args>
  constexpr decltype(auto) construct(Args&&...args) noexcept {
    static_assert(I < member_count, "Index out of range");
    assert(!has_value() && "construct: storage already engaged");
    if constexpr (I == metadata.bool_type_index) {
      std::construct_at(&u_.bool_value, std::forward<Args>(args)...);
    } else {
      static_assert(sizeof...(Args) == 0 ||
                    (sizeof...(Args) == 1 &&
                     (std::is_same_v<std::remove_cvref_t<Args>,
                                     std::remove_cv_t<member_type<I>>> && ...)),
                    "construct for an empty member requires no arguments");
      std::construct_at(&u_.state_code, code_of_member(I));
    }
    return (*this).template get<I>();
  }

  template <size_t I>
  constexpr void destroy() noexcept {
    static_assert(I < member_count, "Index out of range");
    assert(index() == I && "destroy: member is not the active one");
    std::construct_at(&u_.state_code, disengaged);
  }

#pragma endregion

};

/// Dispatches f over the active member index; used by field_variant's
/// special members. Two paths (mirroring MSVC STL's _Visit_strategy):
/// a macro-stamped switch of 4..256 cases for N <= 256, and a
/// function-pointer table (with a disengaged slot) for larger N. Both are
/// usable in constant evaluation, since calling a constexpr function
/// through a function pointer is a constant expression.
#define MV_CASE(n)                                                          \
  case (n):                                                                   \
    if constexpr ((n) < N) {                                                  \
      return static_cast<R>(f(std::integral_constant<size_t, (n)>{}));        \
    }                                                                         \
    std::unreachable();

#define MV_CASE2(n) MV_CASE(n) MV_CASE(n + 1)
#define MV_CASE4(n) MV_CASE2(n) MV_CASE2(n + 2)
#define MV_CASE8(n) MV_CASE4(n) MV_CASE4(n + 4)
#define MV_CASE16(n) MV_CASE8(n) MV_CASE8(n + 8)
#define MV_CASE32(n) MV_CASE16(n) MV_CASE16(n + 16)
#define MV_CASE64(n) MV_CASE32(n) MV_CASE32(n + 32)
#define MV_CASE128(n) MV_CASE64(n) MV_CASE64(n + 64)
#define MV_CASE256(n) MV_CASE128(n) MV_CASE128(n + 128)

template <size_t Stamped, size_t N, class R, class Self, class F>
constexpr R at_index_switch(Self&& self, F&& f) {
  if constexpr (Stamped == 4) {
    switch (self.index()) { MV_CASE4(0) default: std::unreachable(); }
  } else if constexpr (Stamped == 8) {
    switch (self.index()) { MV_CASE8(0) default: std::unreachable(); }
  } else if constexpr (Stamped == 16) {
    switch (self.index()) { MV_CASE16(0) default: std::unreachable(); }
  } else if constexpr (Stamped == 32) {
    switch (self.index()) { MV_CASE32(0) default: std::unreachable(); }
  } else if constexpr (Stamped == 64) {
    switch (self.index()) { MV_CASE64(0) default: std::unreachable(); }
  } else if constexpr (Stamped == 128) {
    switch (self.index()) { MV_CASE128(0) default: std::unreachable(); }
  } else {
    switch (self.index()) { MV_CASE256(0) default: std::unreachable(); }
  }
}

#undef MV_CASE
#undef MV_CASE2
#undef MV_CASE4
#undef MV_CASE8
#undef MV_CASE16
#undef MV_CASE32
#undef MV_CASE64
#undef MV_CASE128
#undef MV_CASE256

/// Calls f with the compile-time index I; the uniform table entry signature.
template <size_t I, class R, class Self, class F>
constexpr R at_dispatch(Self&& self, F&& f) {
  return static_cast<R>(f(std::integral_constant<size_t, I>{}));
}

/// Last table slot, never selected by valid callers (all reach at_index
/// only when has_value()); mirrors the I == N sentinel of the old recursion.
template <class R, class Self, class F>
[[noreturn]] constexpr R at_dispatch_disengaged(Self&&, F&&) {
  std::unreachable();
}

template <size_t N, class R, class Self, class F>
constexpr R at_index_table(Self&& self, F&& f) {
  return [&]<size_t... Is>(std::index_sequence<Is...>) {
    using dispatch_fn = R (*)(Self&&, F&&);
    constexpr dispatch_fn table[N + 1] = { &at_dispatch<Is, R, Self, F>..., &at_dispatch_disengaged<R, Self, F>};
    return table[self.index()](static_cast<Self&&>(self), static_cast<F&&>(f));
  }(std::make_index_sequence<N>{});
}

template <size_t N, class Self, class F>
constexpr decltype(auto) at_index(Self&& self, F&& f) {
  return [&]<size_t... Is>(std::index_sequence<Is...>) -> decltype(auto) {
    using R = std::common_reference_t<decltype(f(std::integral_constant<size_t, Is>{}))...>;
    if constexpr (N <= 4) {
      return at_index_switch<4, N, R>(self, f);
    } else if constexpr (N <= 8) {
      return at_index_switch<8, N, R>(self, f);
    } else if constexpr (N <= 16) {
      return at_index_switch<16, N, R>(self, f);
    } else if constexpr (N <= 32) {
      return at_index_switch<32, N, R>(self, f);
    } else if constexpr (N <= 64) {
      return at_index_switch<64, N, R>(self, f);
    } else if constexpr (N <= 128) {
      return at_index_switch<128, N, R>(self, f);
    } else if constexpr (N <= 256) {
      return at_index_switch<256, N, R>(self, f);
    } else {
      return at_index_table<N, R>(self, f);
    }
  }(std::make_index_sequence<N>{});
}

/// Cartesian visit (std::visit alignment). A single variant dispatches through
/// at_index<size()>. For two variants the N×M product is flattened: when
/// K = N·M <= MV_FLAT_LIMIT a single stamped switch over flat = I*M + J is
/// used (fastest per the r04 probe); when K > MV_FLAT_LIMIT two at_index
/// dispatches are nested (faster than any function-pointer table at that size).
/// Three or more variants dispatch the leading variant, wrap the visitor to
/// record its value, and recurse over the remaining variants.
inline constexpr std::size_t MV_FLAT_LIMIT = 256;

/// Proxy whose index() returns the flattened pair index I*M + J.
struct flat_self {
  std::size_t flat;
  constexpr std::size_t index() const noexcept { return flat; }
};

/// Cartesian dispatch over exactly two variants.
template <class A, class B, class Visitor>
constexpr decltype(auto) visit_2(Visitor&& vis, A&& a, B&& b) {
  constexpr std::size_t NA = std::remove_cvref_t<A>::size();
  constexpr std::size_t NB = std::remove_cvref_t<B>::size();
  constexpr std::size_t K  = NA * NB;
  if constexpr (K <= MV_FLAT_LIMIT) {
    return at_index<K>(flat_self{a.index() * NB + b.index()},
                         [&]<size_t n>(std::integral_constant<size_t, n>) -> decltype(auto) {
                           return vis(std::forward_like<A>(a).template get<n / NB>(),
                                      std::forward_like<B>(b).template get<n % NB>());
                         });
  } else {
    return at_index<NA>(a, [&]<size_t I>(std::integral_constant<size_t, I>) -> decltype(auto) {
      return at_index<NB>(b, [&]<size_t J>(std::integral_constant<size_t, J>) -> decltype(auto) {
        return vis(std::forward_like<A>(a).template get<I>(),
                   std::forward_like<B>(b).template get<J>());
      });
    });
  }
}

/// Base case: a single variant.
template <class V, class Visitor>
constexpr decltype(auto) visit_impl(Visitor&& vis, V&& v) {
  using VV = std::remove_cvref_t<V>;
  return at_index<VV::size()>(v, [&]<size_t I>(std::integral_constant<size_t, I>) -> decltype(auto) {
    return vis(std::forward_like<V>(v).template get<I>());
  });
}

/// Two or more variants: two use the Cartesian fast path; three or more
/// dispatch the leading variant and recurse with a wrapped visitor.
template <class V, class W, class... Rest, class Visitor>
constexpr decltype(auto) visit_impl(Visitor&& vis, V&& v, W&& w, Rest&&... rest) {
  if constexpr (sizeof...(Rest) == 0) {
    return visit_2(vis, v, w);
  } else {
    using VV = std::remove_cvref_t<V>;
    return at_index<VV::size()>(v, [&]<size_t I>(std::integral_constant<size_t, I>) -> decltype(auto) {
      return visit_impl(
          [&](auto&&... vals) -> decltype(auto) {
            return vis(std::forward_like<V>(v).template get<I>(),
                       std::forward<decltype(vals)>(vals)...);
          },
          w, rest...);
    });
  }
}

/// Boost-style hash combination; used by std::hash<field_variant<T>>.
constexpr std::size_t hash_combine(std::size_t seed, std::size_t value) noexcept {
  return seed ^ (value + 0x9e3779b9 + (seed << 6) + (seed >> 2));
}

/// Per-member <=> result type: an empty member (value or referenced) compares
/// as strong_ordering::equal by definition, since empty types carry no state;
/// all other members contribute their own comparison category. The decltype is
/// confined to the non-empty partial specialization so that an empty member is
/// never required to provide operator<=>.
template <class M, bool = std::is_empty_v<std::remove_cvref_t<M>>>
struct member_cmp_impl {
  using type = std::strong_ordering;
};

template <class M>
struct member_cmp_impl<M, false> {
  using type = decltype(std::declval<M const&>() <=> std::declval<M const&>());
};

template <class M>
using member_cmp_t = typename member_cmp_impl<M>::type;

/// Common comparison category of all members plus strong_ordering (which the
/// index/disengaged ordering returns).
template <class Tuple>
struct compare_cat_of;

template <class... Ms>
struct compare_cat_of<std::tuple<Ms...>> {
  using type = std::common_comparison_category_t<member_cmp_t<Ms>..., std::strong_ordering>;
};

} // namespace field_variant_detail

/// Variant-like wrapper holding exactly one non-static data member of T;
/// special members follow std::variant's P0848 conditional-trivial rules.
template <class T>
class field_variant {
  template <class U>
  using storage_meta = field_variant_detail::storage_meta<U>;
  template <class U>
  using storage_base = field_variant_detail::storage_base<U>;
  
  using meta = storage_meta<T>;
  static_assert(meta::member_count > 0 && 
      []<std::size_t... Is>(std::index_sequence<Is...>) constexpr noexcept -> bool {
        return (std::is_destructible_v<std::tuple_element_t<Is, typename meta::member_types>> && ...);
      }(std::make_index_sequence<meta::member_count>{}),
      "field_variant<T>: all non-static data members of T must satisfy Cpp17Destructible "
      "requirements (N4950 [variant.variant.general]/2).");
  
public:
  using member_types = typename meta::member_types;
  template <size_t I>
  using member_type = typename meta::template member_type<I>;
  template <size_t I>
  using storage_type = typename meta::template storage_type<I>;

  constexpr field_variant() = default;

  /// ---- destructor ----
  constexpr ~field_variant() requires (meta::metadata.are_all_mems_trivially_destructible) = default;
  constexpr ~field_variant() requires (!meta::metadata.are_all_mems_trivially_destructible &&
                               meta::metadata.are_all_mems_destructible) {
    if (s_.has_value()) {
      field_variant_detail::at_index<size()>(s_, [&]<size_t I>(std::integral_constant<size_t, I>) 
          noexcept(noexcept(s_.template destroy<I>())) 
          -> void {
        s_.template destroy<I>();
      });
    }
  }
  constexpr ~field_variant() requires (!meta::metadata.are_all_mems_destructible) = delete;

  /// ---- copy constructor ----
  constexpr field_variant(const field_variant&) 
      requires (meta::metadata.are_all_mems_trivially_copy_constructible)
      = default;
  constexpr field_variant(const field_variant& other)
      noexcept(meta::metadata.are_all_mems_copy_constructor_nothrow)
      requires (!meta::metadata.are_all_mems_trivially_copy_constructible &&
                meta::metadata.are_all_mems_copy_constructible)
      : s_() {
    if (other.s_.has_value()) {
      field_variant_detail::at_index<size()>(other.s_, [&]<size_t I>(std::integral_constant<size_t, I>) 
          noexcept(noexcept(s_.template construct<I>(other.s_.template get<I>()))) 
          -> void {
        s_.template construct<I>(other.s_.template get<I>());
      });
    }
  }
  constexpr field_variant(const field_variant&) 
      requires (!meta::metadata.are_all_mems_copy_constructible)
      = delete;

  /// ---- move constructor ----
  constexpr field_variant(field_variant&&) 
      requires (meta::metadata.are_all_mems_trivially_move_constructible) 
      = default;
  constexpr field_variant(field_variant&& other)
      noexcept(meta::metadata.are_all_mems_move_constructor_nothrow)
      requires (!meta::metadata.are_all_mems_trivially_move_constructible &&
                meta::metadata.are_all_mems_move_constructible)
      : s_() {
    if (other.s_.has_value()) {
      field_variant_detail::at_index<size()>(other.s_, [&]<size_t I>(std::integral_constant<size_t, I>) noexcept(
          std::is_reference_v<member_type<I>> ||
          std::is_nothrow_move_constructible_v<storage_type<I>>) 
          -> void {
        if constexpr (std::is_reference_v<member_type<I>>) {
          s_.template construct<I>(other.s_.template get<I>());  ///< Reference members are always copied.
        } else {
          s_.template construct<I>(std::move(other.s_.template get<I>()));
        }
      });
    }
  }
  constexpr field_variant(field_variant&&) 
      requires (!meta::metadata.are_all_mems_move_constructible) 
      = delete;

  /// ---- copy assignment ----
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
        field_variant_detail::at_index<size()>(s_, [&]<size_t I>(std::integral_constant<size_t, I>) 
          noexcept(noexcept(s_.template get<I>() = other.s_.template get<I>())) 
          -> void {
          s_.template get<I>() = other.s_.template get<I>();
        });
      }
    } else {
      if (s_.has_value()) {
        field_variant_detail::at_index<size()>(s_, [&]<size_t I>(std::integral_constant<size_t, I>) 
          noexcept(noexcept(s_.template destroy<I>())) 
          -> void {
          s_.template destroy<I>();
        });
      }
      if (other.s_.has_value()) {
        field_variant_detail::at_index<size()>(other.s_, [&]<size_t I>(std::integral_constant<size_t, I>) 
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

  /// ---- move assignment ----
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
        field_variant_detail::at_index<size()>(s_, [&]<size_t I>(std::integral_constant<size_t, I>) 
          noexcept(noexcept(s_.template get<I>() = std::move(other.s_.template get<I>()))) 
          -> void {
          s_.template get<I>() = std::move(other.s_.template get<I>());
        });
      }
    } else {
      if (s_.has_value()) {
        field_variant_detail::at_index<size()>(s_, [&]<size_t I>(std::integral_constant<size_t, I>) 
          noexcept(noexcept(s_.template destroy<I>())) 
          -> void {
          s_.template destroy<I>();
        });
      }
      if (other.s_.has_value()) {
        field_variant_detail::at_index<size()>(other.s_, [&]<size_t I>(std::integral_constant<size_t, I>) noexcept(
            std::is_reference_v<member_type<I>> ||
            std::is_nothrow_move_constructible_v<storage_type<I>>) 
            -> void {
          if constexpr (std::is_reference_v<member_type<I>>) {
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

  static consteval std::size_t size() noexcept { return meta::member_count; }
  constexpr std::size_t index() const noexcept { return s_.index(); }
  constexpr bool has_value() const noexcept { return s_.has_value(); }
  template <class M>
  static consteval bool has_type() noexcept { return meta::template has_member_type<M>; }
  template <fixed_string Fs>
  static consteval bool has_name() noexcept { return meta::template index_of_name<Fs>() != meta::member_count; }
    
  template <size_t I>
  constexpr decltype(auto) get(this auto&& self) noexcept {
    return std::forward_like<decltype(self)>(self).s_.template get<I>();
  }
  template <class U>
  constexpr decltype(auto) get(this auto &&self) noexcept {
    constexpr size_t I = meta::template index_of<U>;
    static_assert(I != meta::member_count, "get<U>: U must match exactly one member type");
    return self.template get<I>();
  }
  template <fixed_string Fs>
  constexpr decltype(auto) get(this auto &&self) noexcept {
    constexpr size_t I = meta::template index_of_name<Fs>();
    static_assert(I != meta::member_count, "get<Fs>: no member with this name");
    return self.template get<I>();
  }
  
  /// Destroys the active member, then constructs index I in place.
  template <size_t I, class... Args>
  constexpr decltype(auto) emplace(Args&&... args) noexcept(
      std::is_reference_v<member_type<I>> ||
      std::is_nothrow_constructible_v<storage_type<I>, Args...>)
      requires (I < meta::member_count) {
    if (s_.has_value()) {
      field_variant_detail::at_index<size()>(s_, [&]<size_t J>(std::integral_constant<size_t, J>) 
        noexcept(noexcept(s_.template destroy<J>())) 
        -> void {
        s_.template destroy<J>();
      });
    }
    return s_.template construct<I>(std::forward<Args>(args)...);
  }

  /// ---- operator<=> ----
  /// std::variant comparison semantics: engaged members compare by value when
  /// the index matches, otherwise indices compare first; the disengaged state
  /// behaves like valueless_by_exception (less than any engaged value).
  constexpr auto operator<=>(const field_variant& other) const
      noexcept(meta::metadata.are_all_mems_compare_nothrow)
      requires (meta::metadata.are_all_mems_three_way_comparable) {
    using cmp_t = typename field_variant_detail::compare_cat_of<typename meta::member_types>::type;
    if (s_.has_value() && other.s_.has_value() && s_.index() == other.s_.index()) {
      return field_variant_detail::at_index<size()>(s_, [&]<size_t I>(std::integral_constant<size_t, I>) -> cmp_t {
        if constexpr (std::is_empty_v<std::remove_cvref_t<member_type<I>>>) {
          return cmp_t(std::strong_ordering::equal);  ///< Empty member (value or reference): trivially equal.
        } else {
          return static_cast<cmp_t>(s_.template get<I>() <=> other.s_.template get<I>());
        }
      });
    }
    if (!s_.has_value()) {
      return cmp_t(other.s_.has_value() ? std::strong_ordering::less : std::strong_ordering::equal);
    }
    if (!other.s_.has_value()) {
      return cmp_t(std::strong_ordering::greater);
    }
    return static_cast<cmp_t>(s_.index() <=> other.s_.index());
  }

  /// ---- operator== ----
  /// Per P1185R2, == is NOT rewritten from <=>: equality operators only use
  /// operator== candidates (and != is rewritten from ==). std::variant's
  /// equality is therefore provided explicitly, defined in terms of the
  /// same ordering semantics as operator<=>.
  constexpr bool operator==(const field_variant& other) const
      noexcept(meta::metadata.are_all_mems_compare_nothrow)
      requires (meta::metadata.are_all_mems_three_way_comparable) {
    return (*this <=> other) == 0;
  }

  template <class F>
  constexpr decltype(auto) visit(this auto&& self, F&& f) {
    return field_variant_detail::visit_impl(std::forward<F>(f), std::forward<decltype(self)>(self));
  }

  template <class R, class F>
  constexpr R visit(this auto&& self, F&& f) {
    if constexpr (std::is_void_v<R>) {
      field_variant_detail::visit_impl(std::forward<F>(f), std::forward<decltype(self)>(self));
    } else {
      return static_cast<R>(field_variant_detail::visit_impl(std::forward<F>(f), std::forward<decltype(self)>(self)));
    }
  }

  constexpr void swap(field_variant& other)
      noexcept(meta::metadata.are_all_mems_move_constructor_nothrow &&
               meta::metadata.are_all_mems_move_assignment_nothrow)
      requires (meta::metadata.are_all_mems_move_constructible &&
                meta::metadata.are_all_mems_move_assignable) {
    if (s_.index() == other.s_.index()) {
      if (s_.has_value()) {
        field_variant_detail::at_index<size()>(s_, [&]<size_t I>(std::integral_constant<size_t, I>) -> void {
          if constexpr (std::is_reference_v<member_type<I>> ||
                        !std::is_empty_v<std::remove_cvref_t<member_type<I>>>) {
            using std::swap;
            swap(s_.template get<I>(), other.s_.template get<I>());
          }  ///< Empty member: no state to exchange.
        });
      }
    } else {
      field_variant tmp(std::move(*this));
      *this = std::move(other);
      other = std::move(tmp);
    }
  }
      
private:
  storage_base<T> s_;
};

template <class T>
constexpr void swap(field_variant<T>& a, field_variant<T>& b)
    noexcept(field_variant_detail::storage_meta<T>::metadata.are_all_mems_move_constructor_nothrow &&
             field_variant_detail::storage_meta<T>::metadata.are_all_mems_move_assignment_nothrow)
    requires (field_variant_detail::storage_meta<T>::metadata.are_all_mems_move_constructible &&
              field_variant_detail::storage_meta<T>::metadata.are_all_mems_move_assignable) {
  a.swap(b);
}

/// Hash support: combines the active member index with the member's own hash;
/// the disengaged state hashes via its sentinel index. Requires every member
/// to be std::hash-able (reference members hash the referred object, empty
/// members hash by index only).
template <class T>
struct hash<field_variant<T>> {
  constexpr std::size_t operator()(const field_variant<T>& v) const
      requires (field_variant_detail::storage_meta<T>::metadata.are_all_mems_hashable) {
    if (!v.has_value()) {
      return field_variant_detail::hash_combine(v.index(), 0);
    }
    return field_variant_detail::at_index<std::remove_cvref_t<decltype(v)>::size()>(v, [&]<size_t I>(std::integral_constant<size_t, I>) -> std::size_t {
      using M = typename field_variant<T>::template member_type<I>;
      if constexpr (std::is_reference_v<M> ||
                    !std::is_empty_v<std::remove_cvref_t<M>>) {
        return field_variant_detail::hash_combine(
            I, std::hash<std::remove_cv_t<std::remove_reference_t<M>>>{}(v.template get<I>()));
      } else {
        return field_variant_detail::hash_combine(I, 0);
      }
    });
  }
};

namespace field_variant_detail {

template <class T>
struct is_field_variant : std::false_type {};
template <class T>
struct is_field_variant<field_variant<T>> : std::true_type {};

} // namespace field_variant_detail

/// Dispatches the visitor over the active members of every variant
/// (Cartesian product). Constrained to field_variant so that a user who also
/// includes <variant> still gets an unambiguous overload; our constrained
/// candidate is preferred over std::variant's unconstrained one.
template <class Visitor, class... Variants>
requires (sizeof...(Variants) > 0 &&
          (field_variant_detail::is_field_variant<std::remove_cvref_t<Variants>>::value && ...))
constexpr decltype(auto) visit(Visitor&& vis, Variants&&... values) {
  return field_variant_detail::visit_impl(std::forward<Visitor>(vis), std::forward<Variants>(values)...);
}

template <class R, class Visitor, class... Variants>
requires (sizeof...(Variants) > 0 &&
          (field_variant_detail::is_field_variant<std::remove_cvref_t<Variants>>::value && ...))
constexpr R visit(Visitor&& vis, Variants&&... values) {
  if constexpr (std::is_void_v<R>) {
    field_variant_detail::visit_impl(std::forward<Visitor>(vis), std::forward<Variants>(values)...);
  } else {
    return static_cast<R>(field_variant_detail::visit_impl(std::forward<Visitor>(vis), std::forward<Variants>(values)...));
  }
}
} // namespace stdex
