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

#include <meta>


namespace stdex { 
  
consteval bool is_pod(std::meta::info type_info) noexcept {
  return std::meta::is_trivially_destructible_type(type_info) ||
         std::meta::is_trivially_default_constructible_type(type_info);
}
// Reflect members, take the largest type for alignment, and store uniformly in bytes without a union
// Construct every member, POD or not, with construct_at placement new
template <class T>
struct box 
{
  [[nodiscard]] constexpr decltype(auto) value(this auto &&self) noexcept {
    using Self = std::remove_reference_t<decltype(self)>;
    using Ptr = std::conditional_t<std::is_const_v<Self>, T const *, T *>;
    return std::forward_like<decltype(self)>(*reinterpret_cast<Ptr>(self.buffer));
  }
  
  template <class... Args>
  constexpr T &construct(Args&&...args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
    return *std::construct_at(reinterpret_cast<T *>(buffer), std::forward<Args>(args)...);
  }
  
  constexpr void destroy() noexcept {
    std::destroy_at(reinterpret_cast<T *>(buffer));
  }
  
  alignas(T) std::byte buffer[sizeof(T)];
};

template <class T>
struct storage_base_impl
{
  static constexpr auto nonstatic_data_members {
    std::define_static_array(std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::current()))
  };
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
  
  static constexpr auto nothrow_traits = [] consteval noexcept {
    struct nothrow_traits {
      bool are_all_mems_copy_assignment_nothrow = true;
      bool are_all_mems_copy_constructor_nothrow = true;
      bool are_all_mems_move_assignment_nothrow = true;
      bool are_all_mems_move_constructor_nothrow = true;
      bool are_all_mems_default_constructor_nothrow = true;
      bool are_all_mems_equality_comparison_nothrow = true;
    } traits;
    for (auto i = 0uz; i < member_count; ++i) {
        using member_type = decltype(std::declval<T &>().[:nonstatic_data_members[i]:]);
        using member_remove_cvref_t = std::remove_cvref_t<member_type>;
        traits.are_all_mems_move_constructor_nothrow &= 
            std::is_nothrow_move_constructible_v<member_remove_cvref_t>;
        traits.are_all_mems_default_constructor_nothrow &= 
            std::is_nothrow_default_constructible_v<member_remove_cvref_t>;
        traits.are_all_mems_copy_constructor_nothrow &= 
            std::is_nothrow_copy_constructible_v<member_remove_cvref_t>;
        traits.are_all_mems_copy_assignment_nothrow &= 
            std::is_nothrow_copy_assignable_v<member_remove_cvref_t>;
        traits.are_all_mems_move_assignment_nothrow &= 
            std::is_nothrow_move_assignable_v<member_remove_cvref_t>;
        traits.are_all_mems_equality_comparison_nothrow &= 
            std::equality_comparable<member_remove_cvref_t> && 
                noexcept(std::declval<const member_remove_cvref_t&>() == 
                         std::declval<const member_remove_cvref_t&>());
    }
    return traits;
  };
  
  constexpr std::size_t index() const noexcept {
    return static_cast<size_t>(tag);
  }
  
  constexpr bool has_value() const noexcept {
    return static_cast<size_t>(tag) != member_count;
  }
  
  template <size_t I>
  constexpr decltype(auto) get(this auto &&self) noexcept {
    static_assert(I < member_count, "Index out of range");
    if constexpr (is_pod(std::meta::type_of(nonstatic_data_members[i]))) {
      return std::forward_like<decltype(self)>(self.storage.[:nonstatic_data_members[I]:]);
    } else {
      return std::forward_like<decltype(self)>(self.storage.[:nonstatic_data_members[I]:].value());
    }
  }
  
  template <size_t I, class... Args>
  constexpr decltype(auto) construct(Args&&...args) noexcept(
      std::is_nothrow_constructible_v<
          std::remove_cvref_t<
              decltype(std::declval<std::meta::type_of([:nonstatic_data_members[I]:]) &>().[:nonstatic_data_members[I]:])
          >,
          Args...
      >
  ) {
    static_assert(I < member_count, "Index out of range");
    tag = static_cast<tag_type>(I);
    if constexpr (is_pod(std::meta::type_of(nonstatic_data_members[i]))) {
      return std::construct_at(&(storage.[:nonstatic_data_members[I]:]), std::forward<Args>(args)...);
    } else {
      return storage.[:nonstatic_data_members[I]:].construct(std::forward<Args>(args)...);
    }
  }
  
  template <size_t I>
  constexpr void destroy() noexcept {
    static_assert(I < member_count, "Index out of range");
    if constexpr (!is_pod(std::meta::type_of(nonstatic_data_members[i]))) {
      storage.[:nonstatic_data_members[I]:].destroy();
    }
    tag = static_cast<tag_type>(member_count);
  }  
   
  union storage_type;
  consteval { 
    std::meta::define_aggregate(^^storage_type, [] consteval noexcept {
      std::array<std::meta::info, member_count> members;
      for (auto i = 0uz, i < member_count; ++i) {
        auto type_info = std::meta::type_of(nonstatic_data_members[i]);
        auto member_wrap_info = is_pod(type_info)
                                ? type_info
                                : std::meta::substitute(^^box, { type_info });
        // 1. struct Rect {...} rect;
        // 2. struct {...} rect;
        members[i] = std::meta::data_member_spec(member_wrap_info, {
          .name = std::meta::identifier_of(std::meta::has_identifier(type_info)
                    ? type_info
                    : nonstatic_data_members[i]);
        });
      }
      return members;
    });
  }
  storage_type storage;
  tag_type tag { static_cast<tag_type>(member_count) };
};



template <class T>
struct storage_base 
     : storage_base_impl<T, storage_traits<T>::count, storage_traits<T>::is_niche>
{
};
  
template <class T>
class field_variant : public storage_base<T>
{
  constexpr field_variant() noexcept {
    
  }
  
  constexpr field_variant(field_variant const &other) noexcept(?) {
    
  }
  
  constexpr field_variant(field_variant &&other) noexcept {
    
  }
  
  template <class T>
  constexpr field_variant(T&& t) noexcept(?) {
    
  };
  
  template <class T, class... Args>
  constexpr explicit field_variant(std::in_place_type_t<T>, Args&&... args) noexcept(?) {
    
  };
  
  template <class T>
  constexpr field_variant(T&& t) noexcept(?) {
    
  };
  
  template <class T>
  constexpr field_variant(T&& t) noexcept(?) {
    
  }; 
      
  constexpr field_variant& operator=(field_variant const &rhs) noexcept(?) {
    
  }
  
  constexpr field_variant& operator=(field_variant &&rhs) noexcept {
    
  }
  
  template <class T>
  field_variant& operator=(T&& t) noexcept(?) {
    
  };  
}
  
} // namespace stdex