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

#include <array>     // std::array, std::to_array
#include <concepts>  // std::same_as
#include <cstddef>   // std::size_t
#include <cstdint>   // std::uint64_t
#include <memory>    // std::shared_ptr, std::unique_ptr
#include <ranges>    // std::ranges::contiguous_range, std::ranges::sized_range,
                     // std::ranges::range_value_t, std::ranges::data, std::ranges::size
#include <span>      // std::span, std::as_bytes
#include <string>
#include <string_view>
#include <type_traits> // std::remove_cvref_t, std::remove_cv_t, std::is_trivially_copyable_v

#include "bit_traits.hpp" // stdex::unsigned_equivalent_t, stdex::to_unsigned
#include "byte_utils.hpp" // stdex::byte_like, stdex::byte_value, stdex::read64_le,
                          // stdex::read32_le, stdex::little_endian_array
#include "int128.hpp" // stdex::umul128, stdex::umulh, stdex::mix, stdex::mum

namespace stdex
{

static_assert(CHAR_BIT == 8, "hash requires 8-bit bytes");
static_assert(sizeof(std::uint32_t) == 4, "hash requires 32-bit std::uint32_t");
static_assert(sizeof(std::uint64_t) == 8, "hash requires 64-bit std::uint64_t");

// clang-format off
struct hash_standard_t{};
struct hash_micro_t{};
struct hash_nano_t{};

inline constexpr hash_standard_t hash_standard {};
inline constexpr hash_micro_t hash_micro {};
inline constexpr hash_nano_t hash_nano {};
// clang-format on

template <class T>
concept hash_mode =
    std::same_as<std::remove_cvref_t<T>, hash_standard_t> ||
    std::same_as<std::remove_cvref_t<T>, hash_micro_t> ||
    std::same_as<std::remove_cvref_t<T>, hash_nano_t>;

namespace hash_detail
{
template <hash_mode Mode>
struct mode_traits;

template <>
struct mode_traits<hash_standard_t>
{
  static constexpr std::size_t stripe     = 112;
  static constexpr std::size_t lane_count = 7;
  static constexpr bool is_standard  = true;
  static constexpr bool is_micro     = false;
  static constexpr bool is_nano      = false;
};

template <>
struct mode_traits<hash_micro_t>
{
  static constexpr std::size_t stripe     = 80;
  static constexpr std::size_t lane_count = 5;
  static constexpr bool is_standard  = false;
  static constexpr bool is_micro     = true;
  static constexpr bool is_nano      = false;
};

template <>
struct mode_traits<hash_nano_t>
{
  static constexpr std::size_t stripe     = 48;
  static constexpr std::size_t lane_count = 3;
  static constexpr bool is_standard  = false;
  static constexpr bool is_micro     = false;
  static constexpr bool is_nano      = true;
};

inline constexpr auto default_secret = std::to_array<std::uint64_t>({
    0x2d'35'8d'cc'aa'6c'78'a5ULL,
    0x8b'b8'4b'93'96'2e'ac'c9ULL,
    0x4b'33'a6'2e'd4'33'd4'a3ULL,
    0x4d'5a'2d'a5'1d'e1'aa'47ULL,
    0xa0'76'1d'64'78'bd'64'2fULL,
    0xe7'03'7e'd1'a0'b4'28'dbULL,
    0x90'ed'17'65'28'1c'38'8cULL,
    0xaa'aa'aa'aa'aa'aa'aa'aaULL,
});

static_assert(default_secret.size() == 8);

template <typename T>
concept scalar_hashable = std::integral<std::remove_cv_t<T>> ||
                          std::is_enum_v<std::remove_cv_t<T>> ||
                          std::floating_point<std::remove_cv_t<T>>;

// -0.0 == +0.0 but bit_cast differs; NaN != NaN so no normalization needed.
template <typename T>
[[nodiscard]] constexpr unsigned_equivalent_t<T> to_hashable_unsigned(T const value) noexcept
{
  using U = std::remove_cv_t<T>;
  if constexpr (std::floating_point<U>) {
    if (value == static_cast<T>(0)) {
      return static_cast<unsigned_equivalent_t<T>>(0);
    }
  }
  return to_unsigned(value);
}

template <typename Range>
concept contiguous_byte_range = std::ranges::contiguous_range<Range> &&
                                std::ranges::sized_range<Range> &&
                                byte_like<std::ranges::range_value_t<Range>>;

template <typename Range>
concept contiguous_object_range = std::ranges::contiguous_range<Range> &&
                                  std::ranges::sized_range<Range> &&
                                  std::is_trivially_copyable_v<std::ranges::range_value_t<Range>> &&
                                  (!byte_like<std::ranges::range_value_t<Range>>);

template <byte_like Byte, hash_mode Mode>
[[nodiscard]] constexpr std::uint64_t hash_bytes_impl(Mode, std::span<Byte const> const input, std::uint64_t seed, std::array<std::uint64_t, 8> const &secret) noexcept
{
  std::size_t const length = input.size();
  seed ^= mix(seed ^ secret[2], secret[1]);
  std::uint64_t a {};
  std::uint64_t b {};
  std::size_t remaining = length;
  std::size_t offset {};

  if (length <= 16) [[likely]] {
    if (length >= 4) {
      seed ^= length;
      if (length >= 8) {
        a = read64_le(input, 0);
        b = read64_le(input, length - 8);
      } else {
        a = read32_le(input, 0);
        b = read32_le(input, length - 4);
      }
    } else if (length > 0) {
      a = (static_cast<std::uint64_t>(byte_value(input[0])) << 45U) | byte_value(input[length - 1]);
      b = byte_value(input[length >> 1U]);
    }
  } else {
    using traits = hash_detail::mode_traits<std::remove_cvref_t<Mode>>;

    if (remaining > traits::stripe) [[unlikely]] {
      std::array<std::uint64_t, 7> lane { seed, seed, seed, seed, seed, seed, seed };
      do {
        for (std::size_t index = 0; index < traits::lane_count; ++index) {
          lane[index] = mix(read64_le(input, offset + index * 16U) ^ secret[index], read64_le(input, offset + index * 16U + 8U) ^ lane[index]);
        }
        offset += traits::stripe;
        remaining -= traits::stripe;
      } while (remaining > traits::stripe);
      seed = lane[0];
      if constexpr (traits::is_standard) {
        seed ^= lane[1] ^ lane[2] ^ lane[3] ^ lane[4] ^ lane[5] ^ lane[6];
      } else if constexpr (traits::is_micro) {
        seed ^= lane[1] ^ lane[2] ^ lane[3] ^ lane[4];
      } else {
        seed ^= lane[1] ^ lane[2];
      }
    }

    if (remaining > 16) {
      seed = mix(read64_le(input, offset) ^ secret[2], read64_le(input, offset + 8) ^ seed);
      if (remaining > 32) {
        seed = mix(read64_le(input, offset + 16) ^ secret[2], read64_le(input, offset + 24) ^ seed);
        if constexpr (!traits::is_nano) {
          if (remaining > 48) {
            seed = mix(read64_le(input, offset + 32) ^ secret[1], read64_le(input, offset + 40) ^ seed);
            if (remaining > 64) {
              seed = mix(read64_le(input, offset + 48) ^ secret[1], read64_le(input, offset + 56) ^ seed);
              if constexpr (traits::is_standard) {
                if (remaining > 80) {
                  seed = mix(read64_le(input, offset + 64) ^ secret[2], read64_le(input, offset + 72) ^ seed);
                  if (remaining > 96) {
                    seed = mix(read64_le(input, offset + 80) ^ secret[1], read64_le(input, offset + 88) ^ seed);
                  }
                }
              }
            }
          }
        }
      }
    }
    a = read64_le(input, offset + remaining - 16) ^ remaining;
    b = read64_le(input, offset + remaining - 8);
  }

  a ^= secret[1];
  b ^= seed;
  mum(a, b);
  return mix(a ^ secret[7], b ^ secret[1] ^ remaining);
}
} // namespace hash_detail

template <byte_like Byte, hash_mode Mode = hash_standard_t>
[[nodiscard]] constexpr std::uint64_t hash_bytes(std::span<Byte const> const input, std::uint64_t const seed = 0) noexcept
{
  return hash_detail::hash_bytes_impl(Mode {}, input, seed, hash_detail::default_secret);
}

template <byte_like Byte, hash_mode Mode = hash_standard_t>
[[nodiscard]] constexpr std::uint64_t hash_bytes(std::span<Byte const> const input, std::uint64_t const seed, std::array<std::uint64_t, 8> const &secret) noexcept
{
  return hash_detail::hash_bytes_impl(Mode {}, input, seed, secret);
}

template <hash_mode Mode = hash_standard_t>
[[nodiscard]] constexpr std::uint64_t hash_bytes(void const *const data, std::size_t const size, std::uint64_t const seed = 0) noexcept
{
  return hash_bytes<std::byte, Mode>(std::span<std::byte const> { static_cast<std::byte const *>(data), size }, seed);
}

template <hash_detail::contiguous_byte_range Range, hash_mode Mode = hash_standard_t>
[[nodiscard]] constexpr std::uint64_t hash_range(Range const &range, std::uint64_t seed = 0) noexcept
{
  using byte_type = std::remove_cv_t<std::ranges::range_value_t<Range>>;
  return hash_bytes<byte_type, Mode>(std::span<byte_type const> { std::ranges::data(range), std::ranges::size(range) }, seed);
}

template <hash_detail::contiguous_object_range Range, hash_mode Mode = hash_standard_t>
[[nodiscard]] constexpr std::uint64_t hash_range(Range const &range, std::uint64_t seed = 0) noexcept
{
  using value_type = std::remove_cv_t<std::ranges::range_value_t<Range>>;
  if constexpr (hash_detail::scalar_hashable<value_type>) {
    // Per-element hash_value keeps this consistent with the scalar path on every
    // platform (little_endian_array, -0.0 folding); a raw as_bytes pass would bake
    // in native endianness. Structs without hash_value support take the branch below.
    std::uint64_t h = seed;
    for (value_type const v : range) {
      h = hash_value<value_type, Mode>(v, h);
    }
    return h;
  } else {
    static_assert(std::has_unique_object_representations_v<value_type>, "stdex::hash_range: value_type has padding or aliased representations; "
                                                                        "hashing raw bytes would violate a == b => hash(a) == hash(b). "
                                                                        "Provide a custom hash over members.");
    auto const objects = std::span<value_type const> { std::ranges::data(range), std::ranges::size(range) };
    return hash_bytes<std::byte, Mode>(std::as_bytes(objects), seed);
  }
}

template <hash_mode Mode = hash_standard_t>
[[nodiscard]] constexpr std::uint64_t hash_value(std::string_view text, std::uint64_t seed = 0) noexcept
{
  return hash_bytes<char, Mode>(std::span<char const> { text.data(), text.size() }, seed);
}

template <hash_detail::scalar_hashable T, hash_mode Mode = hash_standard_t>
[[nodiscard]] constexpr std::uint64_t hash_value(T const value, std::uint64_t const seed = 0) noexcept
{
  auto const bytes = little_endian_array(hash_detail::to_hashable_unsigned(value));
  using array_type = std::remove_cv_t<decltype(bytes)>;
  return hash_range<array_type, Mode>(bytes, seed);
}

template <class T>
struct hash;

template <hash_detail::scalar_hashable T>
struct hash<T>
{
  using argument_type = T;
  using result_type   = std::size_t;

  [[nodiscard]] constexpr std::size_t operator() (argument_type value) const noexcept
  {
    if constexpr (sizeof(void *) == 8) {
      return mix(static_cast<std::uint64_t>(hash_detail::to_hashable_unsigned(value)), 0x9E'37'79'B9'7F'4A'7C'15ull);
    } else {
      std::uint64_t product = static_cast<std::uint64_t>(hash_detail::to_hashable_unsigned(value)) * 0xE8'17'FB'2Du;
#if defined(__MSVC_RUNTIME_CHECKS)
      return static_cast<std::uint32_t>(product & UINT32_MAX) ^ static_cast<std::uint32_t>(product >> 32);
#else
      return static_cast<std::uint32_t>(product) ^ static_cast<std::uint32_t>(product >> 32);
#endif
    }
  }
};

template <class CharT, class Allocator>
struct hash<std::basic_string<CharT, std::char_traits<CharT>, Allocator>>
{
  using argument_type = std::basic_string<CharT, std::char_traits<CharT>, Allocator>;
  using result_type   = std::size_t;

  template <hash_mode Mode = hash_standard_t>
  [[nodiscard]] constexpr std::size_t operator() (argument_type const &str, std::uint64_t seed = 0) const noexcept
  {
    return stdex::hash_range<argument_type, Mode>(str, seed);
  }
};

template <class CharT>
struct hash<std::basic_string_view<CharT, std::char_traits<CharT>>>
{
  using argument_type = std::basic_string_view<CharT, std::char_traits<CharT>>;
  using result_type   = std::size_t;

  template <hash_mode Mode = hash_standard_t>
  [[nodiscard]] constexpr std::size_t operator() (argument_type sv, std::uint64_t seed = 0) const noexcept
  {
    return stdex::hash_range<argument_type, Mode>(sv, seed);
  }
};

template <>
struct hash<std::nullptr_t>
{
  using argument_type = std::nullptr_t;
  using result_type   = std::size_t;

  [[nodiscard]] constexpr std::size_t operator() (std::nullptr_t) const noexcept
  {
    return hash<std::uintptr_t> {}(0);
  }
};

// reinterpret_cast from a function pointer is conditionally-supported; exclude function
// types so hash<void(*)()> fails on the constraint instead of inside reinterpret_cast.
template <class T> requires (!std::is_function_v<T>)
struct hash<T *>
{
  using argument_type = T *;
  using result_type   = std::size_t;

  [[nodiscard]] std::size_t operator() (T *const ptr) const noexcept
  {
    auto const address = reinterpret_cast<std::uintptr_t>(ptr);
    return hash<std::uintptr_t> {}(address);
  }
};

template <class T>
struct hash<std::shared_ptr<T>>
{
  using argument_type = std::shared_ptr<T>;
  using result_type   = std::size_t;

  [[nodiscard]] std::size_t operator() (argument_type const &ptr) const noexcept
  {
    return hash<T *> {}(ptr.get());
  }
};

template <class T, class Deleter>
struct hash<std::unique_ptr<T, Deleter>>
{
  using argument_type = std::unique_ptr<T, Deleter>;
  using result_type   = std::size_t;

  [[nodiscard]] std::size_t operator() (argument_type const &ptr) const noexcept
  {
    return hash<T *> {}(ptr.get());
  }
};
} // namespace stdex
