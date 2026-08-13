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

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <meta>
#include <ranges>
#include <type_traits>
#include <utility>


namespace stdex { 
  
consteval bool is_pod(std::meta::info type_info) noexcept {
  return std::meta::is_trivially_destructible_type(type_info) ||
         std::meta::is_trivially_default_constructible_type(type_info);
}

template <class T>
struct storage_base 
{
  static constexpr auto nonstatic_data_members {
    std::define_static_array(std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()))
  };
  template<size_t I>
  using member_type = typename [: std::meta::type_of(nonstatic_data_members[I]) :];
  // using member_type = decltype(std::declval<T &>().[:nonstatic_data_members[I]:]) // Affected by access control
  
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

  static constexpr auto storage_traits = [] consteval noexcept {
    struct storage_traits {
      size_t max_size = 0;
      size_t max_align = 1;
    } traits;
    template for (constexpr auto I : std::views::iota(0zu, member_count)) {
      if (traits.max_size < sizeof(storage_type<I>)) {
        traits.max_size = sizeof(storage_type<I>);
      }
      if (traits.max_align < alignof(storage_type<I>)) { 
        traits.max_align = alignof(storage_type<I>);
      }
    }
    // Empty classes: zero-length arrays are illegal, degrade to 1 byte
    return (traits.max_size = traits.max_size ? traits.max_size : 1, traits);
  }();

  static constexpr auto nothrow_traits = [] consteval noexcept {
    struct nothrow_traits {
      bool are_all_mems_copy_assignment_nothrow = true;
      bool are_all_mems_copy_constructor_nothrow = true;
      bool are_all_mems_move_assignment_nothrow = true;
      bool are_all_mems_move_constructor_nothrow = true;
      bool are_all_mems_default_constructor_nothrow = true;
      bool are_all_mems_equality_comparison_nothrow = true;
    } traits;
    template for (constexpr auto I : std::views::iota(0zu, member_count)) {
      using storage_t = storage_type<I>;            // operations on reference members are really pointer copies, never throw
      using member_plain_t = std::remove_cvref_t<member_type<I>>; // comparison semantics apply to the referenced object
      traits.are_all_mems_move_constructor_nothrow &= 
          std::is_nothrow_move_constructible_v<storage_t>;
      traits.are_all_mems_default_constructor_nothrow &= 
          std::is_nothrow_default_constructible_v<storage_t>;
      traits.are_all_mems_copy_constructor_nothrow &= 
          std::is_nothrow_copy_constructible_v<storage_t>;
      traits.are_all_mems_copy_assignment_nothrow &= 
          std::is_nothrow_copy_assignable_v<storage_t>;
      traits.are_all_mems_move_assignment_nothrow &= 
          std::is_nothrow_move_assignable_v<storage_t>;
      traits.are_all_mems_equality_comparison_nothrow &= 
          std::equality_comparable<member_plain_t> && 
          requires(const member_plain_t& lhs, const member_plain_t& rhs) { { lhs == rhs } noexcept; };
    }
    return traits;
  }();

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

} // namespace stdex
