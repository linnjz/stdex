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
  
consteval bool is_pod(std::meta::info type_info) noexcept {
  return std::meta::is_trivially_destructible_type(type_info) ||
         std::meta::is_trivially_default_constructible_type(type_info);
}

// ============ Shared metadata: run reflection once, share across both layouts ============
template <class T>
struct storage_meta 
{
  static constexpr auto nonstatic_data_members {
    // unchecked() is the "no access checking" context, can access private/protected members;
    // access_context::current() filters by the "point of evaluation", evaluating inside a class template would miss inaccessible members
    std::define_static_array(std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()))
  };
  template<size_t I>
  using member_type = typename [: std::meta::type_of(nonstatic_data_members[I]) :];

  // Storage adaptation: a byte array cannot hold reference members directly; modeled on libc++ optional<T&>
  // (optional_storage_base<_Tp, is_reference_v<_Tp>> partial specialization: raw_type* value_)
  // Store M& as a "pointer to the referenced object".
  // Value members are stored with cv stripped (matching libc++'s remove_cv_t<value_type> value_):
  // construct_at does not support const T* (libstdc++ implementation does void* loc = location; dropping const),
  // const semantics are preserved by member_type at the API layer (get returns const int&, object constructed as int).
  template <class M>
  struct storage_of { using type = std::remove_cv_t<M>; };
  template <class M>
  struct storage_of<M&>  { using type = std::remove_reference_t<M>*; };
  template <class M>
  struct storage_of<M&&> { using type = std::remove_reference_t<M>*; };
  template <size_t I>
  using storage_type = typename storage_of<member_type<I>>::type;

  static constexpr size_t member_count = nonstatic_data_members.size();
  using tag_type = std::conditional_t<
      (member_count <= 255), 
      std::uint8_t,
      std::conditional_t<
          (member_count <= 65536), 
          std::uint16_t, 
          std::conditional_t<
              (member_count <= 4294967295ULL), 
              std::uint32_t, 
              std::uint64_t
          >
      >
  >;

  // ---- Compute all metadata in one template for: avoid expanding the loop multiple times, faster compilation ----
  static constexpr auto metadata = [] consteval noexcept {
    struct metadata {
      size_t max_size = 0;
      size_t max_align = 1;
      bool are_all_mems_copy_assignment_nothrow = true;
      bool are_all_mems_copy_constructor_nothrow = true;
      bool are_all_mems_move_assignment_nothrow = true;
      bool are_all_mems_move_constructor_nothrow = true;
      bool are_all_mems_default_constructor_nothrow = true;
      bool are_all_mems_equality_comparison_nothrow = true;
      bool is_all_ref = true;                                // reference niche: all references
      size_t tag_bits = std::numeric_limits<size_t>::digits; // borrowed bits for the reference niche
      size_t bool_cnt = 0;                                   // bool niche: bool member count
      size_t empty_cnt = 0;                                  // bool niche: empty member count
      size_t bool_index = 0;                                 // bool niche: bool member index
      std::array<size_t, member_count> empty_members{};    // bool niche: empty-variant member order table (value-to-member reverse lookup)
      std::array<unsigned char, member_count> empty_code{}; // bool niche: member-to-code forward table (construct O(1))
    } m;
    template for (constexpr auto I : std::views::iota(0zu, member_count)) {
      using storage_t = storage_type<I>;        // reference members adapt to pointers
      using member_t = member_type<I>;
      using member_plain_t = std::remove_cvref_t<member_t>;
      // Storage size/alignment
      if (m.max_size < sizeof(storage_t)) { m.max_size = sizeof(storage_t); }
      if (m.max_align < alignof(storage_t)) { m.max_align = alignof(storage_t); }
      // nothrow traits: operations on reference members are really pointer copies, never throw; comparison semantics apply to the referenced object
      m.are_all_mems_move_constructor_nothrow &= std::is_nothrow_move_constructible_v<storage_t>;
      m.are_all_mems_default_constructor_nothrow &= std::is_nothrow_default_constructible_v<storage_t>;
      m.are_all_mems_copy_constructor_nothrow &= std::is_nothrow_copy_constructible_v<storage_t>;
      m.are_all_mems_copy_assignment_nothrow &= std::is_nothrow_copy_assignable_v<storage_t>;
      m.are_all_mems_move_assignment_nothrow &= std::is_nothrow_move_assignable_v<storage_t>;
      m.are_all_mems_equality_comparison_nothrow &=
          std::equality_comparable<member_plain_t> &&
          requires(const member_plain_t& lhs, const member_plain_t& rhs) { { lhs == rhs } noexcept; };
      m.is_all_ref = m.is_all_ref && std::is_reference_v<member_t>;
      // Reference niche: addresses are always divisible by alignof (alignof is a power of two), low log2(alignof) bits always 0
      m.tag_bits = std::min<size_t>(m.tag_bits, static_cast<size_t>(std::countr_zero(
          alignof(std::remove_reference_t<member_t>))));
      // bool niche (Rust semantics): bool lends only its {0,1} intrinsic values, overlapping ranges are indistinguishable
      if constexpr (std::is_same_v<member_plain_t, bool>) {
        ++m.bool_cnt;
        m.bool_index = I;
      } else if constexpr (std::is_empty_v<member_plain_t>) {
        m.empty_members[m.empty_cnt] = I;
        m.empty_code[I] = static_cast<unsigned char>(2 + m.empty_cnt); // current count is the ordinal
        ++m.empty_cnt;
      }
    }
    // Empty classes: zero-length arrays are illegal, degrade to 1 byte
    m.max_size = m.max_size ? m.max_size : 1;
    return m;
  }();

  // ---- Derived metadata (no template for, taken directly from metadata) ----
  static constexpr auto storage_traits = [] consteval noexcept {
    struct storage_traits { size_t max_size; size_t max_align; };
    return storage_traits{metadata.max_size, metadata.max_align};
  }();
  static constexpr auto nothrow_traits = [] consteval noexcept {
    struct nothrow_traits {
      bool are_all_mems_copy_assignment_nothrow;
      bool are_all_mems_copy_constructor_nothrow;
      bool are_all_mems_move_assignment_nothrow;
      bool are_all_mems_move_constructor_nothrow;
      bool are_all_mems_default_constructor_nothrow;
      bool are_all_mems_equality_comparison_nothrow;
    };
    return nothrow_traits{metadata.are_all_mems_copy_assignment_nothrow,
                          metadata.are_all_mems_copy_constructor_nothrow,
                          metadata.are_all_mems_move_assignment_nothrow,
                          metadata.are_all_mems_move_constructor_nothrow,
                          metadata.are_all_mems_default_constructor_nothrow,
                          metadata.are_all_mems_equality_comparison_nothrow};
  }();
  static constexpr size_t tag_bits = metadata.tag_bits;
  static constexpr size_t tag_mask = tag_bits ? (1uz << tag_bits) - 1 : 0;
  // niche opt-in (like map::is_transparent): declare using is_niche = void; inside the user type to explicitly enable the niche optimization,
  // Disabled by default, falls back to the tagged version. A type alias is not a data member, so reflection and layout are unaffected and a requires check suffices.
  static constexpr bool niche_opt_in = requires { typename T::is_niche; };
  // Reference niche: all reference members (adapted pointers are forced non-null, so null is freed) && enough alignment bits (bits=0 also works for a single member)
  static constexpr bool niche_capable =
      metadata.is_all_ref && (member_count <= (1uz << tag_bits)) && niche_opt_in;
  // bool niche: exactly 1 bool member + remaining empty classes; encoding space bool 2 states + 1 state per empty variant + disengaged 1 state <= 256
  static constexpr bool niche_bool_capable =
      metadata.bool_cnt == 1 && (2 + metadata.empty_cnt + 1 <= 256) && niche_opt_in;
  
  // Type mapping
  template <size_t... Is>
  static auto member_tuple_impl(std::index_sequence<Is...>) -> std::tuple<member_type<Is>...>;
  using member_types = decltype(member_tuple_impl(std::make_index_sequence<member_count>{}));

  template <class U, class Tuple>
  struct index_of_impl;

  template <class U, class... Ts>
  struct index_of_impl<U, std::tuple<Ts...>> {
    // Exact match: U must equal some member type exactly (including cvref), e.g. int& matches an int& member, int matches an int value member, const int& matches a const int& member
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

  // Whether the type list contains an exact match for U (including cvref); multiple matches (e.g. Amb{int a; int b;}) also return true
  template <class U>
  static constexpr bool has_member_type = index_of_impl<U, member_types>::count >= 1;

#pragma region
  constexpr size_t size() const noexcept {
    return member_count;
  }
  
  constexpr size_t npos() const noexcept {
    return member_count;
  }
#pragma endregion
};

// Helper alias: get the I-th data member type of T (matching tuple_element_t style: index first, type second)
template <size_t I, class T>
using member_type_at = typename storage_meta<T>::template member_type<I>;

// ============ Primary template only declared; constrained specializations are selected exclusively by niche_capable ============
template <class T>
struct storage_base;

// ---- General version: byte array + separate tag (fallback when members include value types/plain pointers/insufficient alignment bits/multiple bools) ----
template <class T>
  requires (!storage_meta<T>::niche_capable && !storage_meta<T>::niche_bool_capable)
struct storage_base<T> : storage_meta<T>
{
  using base = storage_meta<T>;
  using tag_type = typename base::tag_type;
  static constexpr size_t member_count = base::member_count;
  static constexpr auto storage_traits = base::storage_traits;
  template <size_t I>
  using member_type = typename base::template member_type<I>;
  template <size_t I>
  using storage_type = typename base::template storage_type<I>;

  alignas(storage_traits.max_align) std::byte storage[storage_traits.max_size];
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
    // Strip only the reference, keep cv: is_const must see the const of the object (remove_cvref would drop const)
    using self_t = std::remove_reference_t<decltype(self)>;
    using member_t = member_type<I>;
    using member_plain_t = std::remove_reference_t<member_t>; // preserve the member's own cv (e.g. const int)
    if constexpr (std::is_reference_v<member_t>) {
      // Reference members: storage holds a "pointer to the referenced object", requiring double dereference;
      // Consistent with real member access, the object's const does not propagate through reference members (like optional<T&>::get() const returning value_type&)
      // Add const to the target pointer type to avoid reinterpret_cast casting away constness from const std::byte*
      using storage_ptr_t = std::add_const_t<storage_type<I>> *;
      return **reinterpret_cast<storage_ptr_t>(self.storage);
    } else {
      using member_ptr_t = std::conditional_t<std::is_const_v<self_t>, member_plain_t const *, member_plain_t *>;
      return std::forward_like<decltype(self)>(*reinterpret_cast<member_ptr_t>(self.storage));
    }
  }

  template <class U>
  constexpr decltype(auto) get(this auto &&self) noexcept {
    static_assert(base::template index_of<U> != member_count,
                  "get<U>: U must match exactly one member type");
    return self.template get<base::template index_of<U>>();
  }

  // get_if<I>: when engaged and index==I return a pointer to the get<I>() result, otherwise nullptr;
  // Return type propagates const from self (const object -> const M*); empty members have no storage to point to, rejected at compile time
  template <size_t I>
  constexpr auto get_if(this auto &&self) noexcept {
    static_assert(I < member_count, "Index out of range");
    static_assert(!std::is_empty_v<std::remove_reference_t<member_type<I>>>,
                  "get_if: empty members have no addressable storage; use get<I>()");
    using pointer = std::add_pointer_t<decltype(self.template get<I>())>;
    if (self.index() != I) return pointer{nullptr};
    return std::addressof(self.template get<I>());
  }

  template <class U>
  constexpr auto get_if(this auto &&self) noexcept {
    static_assert(base::template index_of<U> != member_count,
                  "get_if<U>: U must match exactly one member type");
    return self.template get_if<base::template index_of<U>>();
  }

  template <size_t I, class... Args>
  constexpr decltype(auto) construct(Args&&...args) noexcept(
      std::is_reference_v<member_type<I>> ||
      std::is_nothrow_constructible_v<storage_type<I>, Args...>
  ) {
    static_assert(I < member_count, "Index out of range");
    assert(!has_value() && "construct: storage already engaged");
    if constexpr (std::is_reference_v<member_type<I>>) {
      // Reference members: like optional<T&>, store the address after binding; accept only lvalues to avoid dangling
      static_assert(sizeof...(Args) == 1 && (std::is_lvalue_reference_v<Args> && ...),
                    "construct for a reference member requires exactly one lvalue argument");
      static_assert(!(std::reference_constructs_from_temporary_v<member_type<I>, Args> && ...),
                    "construct for a reference member would bind to a temporary");
      std::construct_at(reinterpret_cast<storage_type<I> *>(storage), std::addressof(std::forward<Args>(args)...));
    } else {
      // Value members (including const/volatile qualifiers): construct directly via placement new,
      // cv-qualified types are valid new targets (e.g. ::new (p) const int(42))
      std::construct_at(reinterpret_cast<storage_type<I> *>(storage), std::forward<Args>(args)...);
    }
    tag = static_cast<tag_type>(I);
    return (*this).template get<I>();
  }

  template <size_t I>
  constexpr void destroy() noexcept(std::is_nothrow_destructible_v<storage_type<I>>) {
    static_assert(I < member_count, "Index out of range");
    assert(index() == I && "destroy: member is not the active one");
    // The adapted type of reference members is a pointer (trivially destructible), never destroying the referenced object itself
    if constexpr (!std::is_reference_v<member_type<I>> &&
                  !std::is_trivially_destructible_v<storage_type<I>>) {
      std::destroy_at(reinterpret_cast<storage_type<I> *>(storage));
    }
    tag = static_cast<tag_type>(member_count);
  }
#pragma endregion
};

// ---- niche version: all reference members, 8-byte uintptr_t slot, no tag field ----
// Stored value = real address | I (low tag_bits bits always 0, guaranteed by alignment); disengaged = 0
template <class T>
  requires (storage_meta<T>::niche_capable)
struct storage_base<T> : storage_meta<T>
{
  using base = storage_meta<T>;
  static constexpr size_t member_count = base::member_count;
  static constexpr size_t tag_mask = base::tag_mask;
  template <size_t I>
  using member_type = typename base::template member_type<I>;

  // Read/write the uintptr_t object directly: no strict-aliasing issue (avoids the UB of reinterpret_cast<std::byte*->uintptr_t*>)
  std::uintptr_t slot = 0;

#pragma region
  constexpr std::size_t index() const noexcept {
    // For a single member tag_mask=0: engaged always returns 0; disengaged returns member_count
    return slot == 0 ? member_count
                       : static_cast<size_t>(slot & tag_mask);
  }

  constexpr bool has_value() const noexcept {
    return slot != 0;
  }

  template <size_t I>
  constexpr decltype(auto) get(this auto &&self) noexcept {
    static_assert(I < member_count, "Index out of range");
    assert(self.index() == I && "get: member is not the active one");
    auto stored = self.slot;                                  // reading the slot value only, even for a const object
    auto addr = reinterpret_cast<void *>(stored & ~tag_mask); // 1 AND to clear the bits and restore the address
    // const does not propagate through reference members: return M& (like optional<T&>::get() const returning value_type&)
    return *static_cast<std::remove_reference_t<member_type<I>> *>(addr);
  }

  template <class U>
  constexpr decltype(auto) get(this auto &&self) noexcept {
    static_assert(base::template index_of<U> != member_count,
                  "get<U>: U must match exactly one member type");
    return self.template get<base::template index_of<U>>();
  }

  // get_if<I>: when engaged and index==I return a pointer to the get<I>() result, otherwise nullptr
  template <size_t I>
  constexpr auto get_if(this auto &&self) noexcept {
    static_assert(I < member_count, "Index out of range");
    using pointer = std::add_pointer_t<decltype(self.template get<I>())>;
    if (self.index() != I) return pointer{nullptr};
    return std::addressof(self.template get<I>());
  }

  template <class U>
  constexpr auto get_if(this auto &&self) noexcept {
    static_assert(base::template index_of<U> != member_count,
                  "get_if<U>: U must match exactly one member type");
    return self.template get_if<base::template index_of<U>>();
  }

  template <size_t I, class... Args>
  constexpr decltype(auto) construct(Args&&...args) noexcept {
    static_assert(I < member_count, "Index out of range");
    assert(!has_value() && "construct: storage already engaged");
    // Reference members: like optional<T&>, store the address after binding the argument; accept only lvalues to avoid dangling
    static_assert(sizeof...(Args) == 1 && (std::is_lvalue_reference_v<Args> && ...),
                  "construct for a reference member requires exactly one lvalue argument");
    static_assert(!(std::reference_constructs_from_temporary_v<member_type<I>, Args> && ...),
                  "construct for a reference member would bind to a temporary");
    auto addr = reinterpret_cast<std::uintptr_t>(std::addressof(std::forward<Args>(args)...));
    assert((addr & tag_mask) == 0);                           // alignment guarantee, defensive check
    slot = addr | I;                                          // 1 OR to write the variant index
    return (*this).template get<I>();
  }

  template <size_t I>
  constexpr void destroy() noexcept {
    static_assert(I < member_count, "Index out of range");
    assert(index() == I && "destroy: member is not the active one");
    slot = 0;                                                 // reference members have no destructor, setting 0 means disengaged
  }
#pragma endregion
};

// ---- bool niche version: exactly 1 bool member + remaining empty classes, 1-byte slot, no tag field ----
// Encoding: bool member = 0/1 (valid bool object representation, get returns a writable real bool&); j-th empty variant = 2+j; disengaged = member_count+1
// bool value range never shifts with the variant index (Rust semantics): two bool variants overlap -> cannot niche, fall back to the tagged version
template <class T>
  requires (storage_meta<T>::niche_bool_capable)
struct storage_base<T> : storage_meta<T>
{
  using base = storage_meta<T>;
  static constexpr size_t member_count = base::member_count;
  template <size_t I>
  using member_type = typename base::template member_type<I>;

  // bool member index (check guarantees exactly 1) and empty-variant member order/forward code tables (for index() reverse lookup and construct O(1))
  // Both taken directly from the base class metadata computed in one template for, no longer expanding the loop repeatedly
  static constexpr size_t bool_index = base::metadata.bool_index;
  static constexpr auto empty_members = base::metadata.empty_members;
  static constexpr auto empty_code = base::metadata.empty_code;
  static constexpr unsigned char disengaged = static_cast<unsigned char>(member_count + 1);

  // Read/write unsigned char directly: when the bool member is engaged a bool object is created via construct_at (byte always 0/1, never violates bool value rules)
  unsigned char slot = disengaged;

#pragma region
  constexpr std::size_t index() const noexcept {
    // 0/1 -> bool variant; 2..k -> empty variants (the (s-2)-th); k+1 -> disengaged (== member_count)
    if (slot <= 1) { return bool_index; }
    if (slot <= member_count) { return empty_members[slot - 2]; }
    return member_count;
  }

  constexpr bool has_value() const noexcept {
    return slot != disengaged;
  }

  template <size_t I>
  constexpr decltype(auto) get(this auto &&self) noexcept {
    static_assert(I < member_count, "Index out of range");
    assert(self.index() == I && "get: member is not the active one");
    if constexpr (I == bool_index) {
      // Bytes 0/1 are valid bool objects (created by construct_at), return a writable real reference; const does not propagate to the stored object
      using self_t = std::remove_reference_t<decltype(self)>;
      using bool_ptr_t = std::conditional_t<std::is_const_v<self_t>, const bool *, bool *>;
      return std::forward_like<decltype(self)>(*reinterpret_cast<bool_ptr_t>(&self.slot));
    } else {
      // Empty members have no object to reference: return by value (stateless, reads/writes meaningless; like Rust unit variants)
      return std::remove_cvref_t<member_type<I>>{};
    }
  }

  template <class U>
  constexpr decltype(auto) get(this auto &&self) noexcept {
    static_assert(base::template index_of<U> != member_count,
                  "get<U>: U must match exactly one member type");
    return self.template get<base::template index_of<U>>();
  }

  // get_if<I>: when engaged and index==I return a pointer to the get<I>() result, otherwise nullptr;
  // Empty members are returned by value with no storage to point to, rejected at compile time
  template <size_t I>
  constexpr auto get_if(this auto &&self) noexcept {
    static_assert(I < member_count, "Index out of range");
    static_assert(I == bool_index,
                  "get_if: empty members have no addressable storage; use get<I>()");
    using pointer = std::add_pointer_t<decltype(self.template get<I>())>;
    if (self.index() != I) return pointer{nullptr};
    return std::addressof(self.template get<I>());
  }

  template <class U>
  constexpr auto get_if(this auto &&self) noexcept {
    static_assert(base::template index_of<U> != member_count,
                  "get_if<U>: U must match exactly one member type");
    return self.template get_if<base::template index_of<U>>();
  }

  template <size_t I, class... Args>
  constexpr decltype(auto) construct(Args&&...args) noexcept {
    static_assert(I < member_count, "Index out of range");
    assert(!has_value() && "construct: storage already engaged");
    if constexpr (I == bool_index) {
      std::construct_at(reinterpret_cast<bool *>(&slot), std::forward<Args>(args)...);
    } else {
      // Empty variants (Rust unit semantics): no object, write the code byte directly; I is a compile-time constant, so the forward-table index folds to a constant
      static_assert(sizeof...(Args) == 0,
                    "construct for an empty member requires no arguments");
      slot = empty_code[I];
    }
    return (*this).template get<I>();
  }

  template <size_t I>
  constexpr void destroy() noexcept {
    static_assert(I < member_count, "Index out of range");
    assert(index() == I && "destroy: member is not the active one");
    if constexpr (I == bool_index) {
      std::destroy_at(reinterpret_cast<bool *>(&slot));       // bool is trivially destructible, only ends the lifetime
    }
    slot = disengaged;
  }
#pragma endregion

};

} // namespace stdex
