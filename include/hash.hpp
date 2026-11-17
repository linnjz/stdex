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

#include <array>    // std::array, std::to_array
#include <bit>      // std::rotl
#include <concepts> // std::same_as
#include <cstddef>  // std::size_t
#include <cstdint>  // std::uint64_t
#include <memory>   // std::shared_ptr, std::unique_ptr
#include <ranges>   // std::ranges::contiguous_range, std::ranges::sized_range,
                    // std::ranges::range_value_t, std::ranges::data, std::ranges::size
#include <span>     // std::span, std::as_bytes
#include <string>
#include <string_view>
#include <type_traits> // std::remove_cvref_t, std::remove_cv_t, std::is_trivially_copyable_v

#include "bit.hpp" // stdex::unsigned_equivalent_t, stdex::to_unsigned
#include "int128.hpp"     // stdex::umul128, stdex::umul128_xor_fold

namespace stdex
{
// The hash functor deliberately does not perform avalanche multiplication.
// If the caller wants to improve avalanche resistance, they can selectively
// multiply the hash value returned by the functor call by one of the following four numbers.
inline constexpr std::uint64_t kPhiMixMultConstant   = 0x9E'37'79'B9'7F'4A'7C'15ULL; // [2^64 / 1.618033988749894...(std::numbers::phi) ]
inline constexpr std::uint64_t kPiMixMultConstant    = 0x51'7C'C1'B7'27'22'0A'95ULL; // [2^64 / 3.141592653589793...(std::numbers::pi) ]
inline constexpr std::uint32_t kPhiMixMultConstant32 = 0x9E'37'79'B9U;               // [2^32 / 1.618033988749894...(std::numbers::phi) ]
inline constexpr std::uint32_t kPiMixMultConstant32  = 0x51'7C'C1'B7U;               // [2^32 / 3.141592653589793...(std::numbers::pi) ]

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
  static constexpr bool is_standard       = true;
  static constexpr bool is_micro          = false;
  static constexpr bool is_nano           = false;
};

template <>
struct mode_traits<hash_micro_t>
{
  static constexpr std::size_t stripe     = 80;
  static constexpr std::size_t lane_count = 5;
  static constexpr bool is_standard       = false;
  static constexpr bool is_micro          = true;
  static constexpr bool is_nano           = false;
};

template <>
struct mode_traits<hash_nano_t>
{
  static constexpr std::size_t stripe     = 48;
  static constexpr std::size_t lane_count = 3;
  static constexpr bool is_standard       = false;
  static constexpr bool is_micro          = false;
  static constexpr bool is_nano           = true;
};

inline constexpr auto kDefaultSecret = std::to_array<std::uint64_t>({
    0x2D'35'8D'CC'AA'6C'78'A5ULL,
    0x8B'B8'4B'93'96'2E'AC'C9ULL,
    0x4B'33'A6'2E'D4'33'D4'A3ULL,
    0x4D'5A'2D'A5'1D'E1'AA'47ULL,
    0xA0'76'1D'64'78'BD'64'2FULL,
    0xE7'03'7E'D1'A0'B4'28'DBULL,
    0x90'ED'17'65'28'1C'38'8CULL,
    0xAA'AA'AA'AA'AA'AA'AA'AAULL,
});

static_assert(kDefaultSecret.size() == 8);

template <typename T>
concept scalar_hashable = std::integral<std::remove_cvref_t<T>> ||
                          std::is_enum_v<std::remove_cvref_t<T>> ||
                          std::floating_point<std::remove_cvref_t<T>>;

/// -0.0 == +0.0 but bit_cast differs; NaN != NaN so no normalization needed.
template <typename T>
[[nodiscard]] constexpr unsigned_equivalent_t<T> to_hashable_unsigned(T const value) noexcept
{
  if constexpr (std::floating_point<T>) {
    if (value == static_cast<T>(0)) {
      return static_cast<unsigned_equivalent_t<T>>(0);
    }
  }
  return to_unsigned(value);
}

/// /RTCc (SmallerTypeCheck) traps a narrowing cast when the value does not fit
/// the destination type; mask before narrowing in that configuration. This is
/// the same workaround Boost.Unordered uses in its mulx32.
[[nodiscard]] constexpr std::uint32_t fold_to_u32(std::uint64_t const value) noexcept
{
#if defined(__MSVC_RUNTIME_CHECKS)
  return static_cast<std::uint32_t>(value & 0xFFFF'FFFFU) ^ static_cast<std::uint32_t>(value >> 32U);
#else
  return static_cast<std::uint32_t>(value) ^ static_cast<std::uint32_t>(value >> 32U);
#endif
}

[[nodiscard]] constexpr std::size_t to_size_t(std::uint64_t const value) noexcept
{
  if constexpr (sizeof(std::size_t) == 8) {
    return static_cast<std::size_t>(value);
  } else {
    return static_cast<std::size_t>(fold_to_u32(value));
  }
}

/// One 32x32 -> 64 multiplication folded back to 32 bits.
[[nodiscard]] constexpr std::uint32_t mulx32(std::uint32_t const lhs, std::uint32_t const rhs) noexcept
{
  return fold_to_u32(static_cast<std::uint64_t>(lhs) * rhs);
}

/// One 64x64->128 multiply for a 128-bit key. XOR-ing both operands back in keeps
/// structured keys (notably high == 0) from collapsing to zero when either
/// multiplication operand is zero; rotating high makes the two words
/// order-sensitive.
[[nodiscard]] constexpr std::uint64_t mix_128_words(std::uint64_t const low, std::uint64_t const high, std::uint64_t const seed) noexcept
{
  std::uint64_t const low_seeded = low ^ seed;
  std::uint64_t const high_mixed = high ^ kPhiMixMultConstant;
  return umul128_xor_fold(low_seeded, high_mixed) ^ low_seeded ^ std::rotl(high_mixed, 32);
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
  seed ^= umul128_xor_fold(seed ^ secret[2], secret[1]);
  std::uint64_t a {};
  std::uint64_t b {};
  std::size_t remaining = length;
  std::size_t offset {};

  if (length <= 16) [[likely]] {
    if (length >= 4) {
      seed ^= length;
      if (length >= 8) {
        a = read64_le_unchecked(input, 0);
        b = read64_le_unchecked(input, length - 8);
      } else {
        a = read32_le_unchecked(input, 0);
        b = read32_le_unchecked(input, length - 4);
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
          lane[index] = umul128_xor_fold(read64_le_unchecked(input, offset + index * 16U) ^ secret[index], read64_le_unchecked(input, offset + index * 16U + 8U) ^ lane[index]);
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
      seed = umul128_xor_fold(read64_le_unchecked(input, offset) ^ secret[2], read64_le_unchecked(input, offset + 8) ^ seed);
      if (remaining > 32) {
        seed = umul128_xor_fold(read64_le_unchecked(input, offset + 16) ^ secret[2], read64_le_unchecked(input, offset + 24) ^ seed);
        if constexpr (!traits::is_nano) {
          if (remaining > 48) {
            seed = umul128_xor_fold(read64_le_unchecked(input, offset + 32) ^ secret[1], read64_le_unchecked(input, offset + 40) ^ seed);
            if (remaining > 64) {
              seed = umul128_xor_fold(read64_le_unchecked(input, offset + 48) ^ secret[1], read64_le_unchecked(input, offset + 56) ^ seed);
              if constexpr (traits::is_standard) {
                if (remaining > 80) {
                  seed = umul128_xor_fold(read64_le_unchecked(input, offset + 64) ^ secret[2], read64_le_unchecked(input, offset + 72) ^ seed);
                  if (remaining > 96) {
                    seed = umul128_xor_fold(read64_le_unchecked(input, offset + 80) ^ secret[1], read64_le_unchecked(input, offset + 88) ^ seed);
                  }
                }
              }
            }
          }
        }
      }
    }
    a = read64_le_unchecked(input, offset + remaining - 16) ^ remaining;
    b = read64_le_unchecked(input, offset + remaining - 8);
  }

  a ^= secret[1];
  b ^= seed;
  uint128_t const product { umul128(a, b) };
  a = product.lo();
  b = product.hi();
  return umul128_xor_fold(a ^ secret[7], b ^ secret[1] ^ remaining);
}
} // namespace hash_detail

template <byte_like Byte, hash_mode Mode = hash_standard_t>
[[nodiscard]] constexpr std::uint64_t hash_bytes(std::span<Byte const> const input, std::uint64_t const seed = 0) noexcept
{
  return hash_detail::hash_bytes_impl(Mode {}, input, seed, hash_detail::kDefaultSecret);
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
  static_assert(std::has_unique_object_representations_v<value_type>,
               "stdex::hash_range: value_type has padding or aliased representations; "
               "hashing raw bytes would violate a == b => hash(a) == hash(b). "
               "Provide a custom hash over members.");
  auto const objects = std::span<value_type const> { std::ranges::data(range), std::ranges::size(range) };
  return hash_bytes<std::byte, Mode>(std::as_bytes(objects), seed);
}

template <class T>
struct hash;

template <>
struct hash<uint128_t>
{
  using argument_type = uint128_t;
  using result_type   = std::size_t;

  [[nodiscard]] constexpr std::size_t operator() (argument_type const value, std::uint64_t const seed = 0) const noexcept
  {
    std::uint64_t const mixed = hash_detail::mix_128_words(value.lo(), value.hi(), seed);
    if constexpr (sizeof(std::size_t) == 8) {
      return static_cast<std::size_t>(mixed);
    } else {
      return static_cast<std::size_t>(hash_detail::fold_to_u32(mixed));
    }
  }
};

template <>
struct hash<int128_t>
{
  using argument_type = int128_t;
  using result_type   = std::size_t;

  [[nodiscard]] constexpr std::size_t operator() (argument_type const value, std::uint64_t const seed = 0) const noexcept
  {
    std::uint64_t const mixed = hash_detail::mix_128_words(value.lo(), value.hi(), seed);
    if constexpr (sizeof(std::size_t) == 8) {
      return static_cast<std::size_t>(mixed);
    } else {
      return static_cast<std::size_t>(hash_detail::fold_to_u32(mixed));
    }
  }
};

template <hash_detail::scalar_hashable T>
struct hash<T>
{
  using argument_type = T;
  using result_type   = std::size_t;

  [[nodiscard]] constexpr std::size_t operator() (argument_type const value, std::uint64_t const seed = 0) const noexcept
  {
    std::uint64_t const bits = static_cast<std::uint64_t>(hash_detail::to_hashable_unsigned(value));
    if constexpr (sizeof(std::size_t) == 8) {
      return hash_detail::to_size_t(umul128_xor_fold(bits ^ seed, kPhiMixMultConstant));
    } else {
      // Keep both halves of 64-bit scalar keys in play; the optional final
      // avalanche multiply is intentionally left to the caller.
      std::uint32_t x = hash_detail::fold_to_u32(bits) ^ hash_detail::fold_to_u32(seed);
      if constexpr (sizeof(T) > sizeof(std::uint32_t)) {
        x ^= hash_detail::mulx32(static_cast<std::uint32_t>(bits >> 32U), kPhiMixMultConstant32);
      }
      return static_cast<std::size_t>(hash_detail::mulx32(x, kPhiMixMultConstant32));
    }
  }
};

template <class CharT, class Allocator>
struct hash<std::basic_string<CharT, std::char_traits<CharT>, Allocator>>
{
  using argument_type  = std::basic_string<CharT, std::char_traits<CharT>, Allocator>;
  using result_type    = std::size_t;
  using is_transparent = void;

  template <hash_mode Mode = hash_standard_t>
  [[nodiscard]] constexpr std::size_t operator() (argument_type const &str, std::uint64_t seed = 0) const noexcept
  {
    return hash_detail::to_size_t(stdex::hash_range<argument_type, Mode>(str, seed));
  }

  template <hash_mode Mode = hash_standard_t>
  [[nodiscard]] constexpr std::size_t operator() (std::basic_string_view<CharT, std::char_traits<CharT>> str, std::uint64_t seed = 0) const noexcept
  {
    return hash_detail::to_size_t(stdex::hash_range<std::basic_string_view<CharT, std::char_traits<CharT>>, Mode>(str, seed));
  }

  template <hash_mode Mode = hash_standard_t>
  [[nodiscard]] constexpr std::size_t operator() (CharT const *str, std::uint64_t seed = 0) const noexcept
  {
    return hash_detail::to_size_t(stdex::hash_range<std::basic_string_view<CharT, std::char_traits<CharT>>, Mode>(
        std::basic_string_view<CharT, std::char_traits<CharT>> { str }, seed));
  }
};

template <class CharT>
struct hash<std::basic_string_view<CharT, std::char_traits<CharT>>>
{
  using argument_type  = std::basic_string_view<CharT, std::char_traits<CharT>>;
  using result_type    = std::size_t;
  using is_transparent = void;

  template <hash_mode Mode = hash_standard_t>
  [[nodiscard]] constexpr std::size_t operator() (argument_type sv, std::uint64_t seed = 0) const noexcept
  {
    return hash_detail::to_size_t(stdex::hash_range<argument_type, Mode>(sv, seed));
  }

  template <hash_mode Mode = hash_standard_t, class Allocator>
  [[nodiscard]] constexpr std::size_t operator() (std::basic_string<CharT, std::char_traits<CharT>, Allocator> const &str, std::uint64_t seed = 0) const noexcept
  {
    return hash_detail::to_size_t(stdex::hash_range<argument_type, Mode>(argument_type { str.data(), str.size() }, seed));
  }

  template <hash_mode Mode = hash_standard_t>
  [[nodiscard]] constexpr std::size_t operator() (CharT const *str, std::uint64_t seed = 0) const noexcept
  {
    return hash_detail::to_size_t(stdex::hash_range<argument_type, Mode>(argument_type { str }, seed));
  }
};

template <>
struct hash<std::nullptr_t>
{
  using argument_type = std::nullptr_t;
  using result_type   = std::size_t;

  [[nodiscard]] constexpr std::size_t operator() (std::nullptr_t, std::uint64_t seed = 0) const noexcept
  {
    return hash<std::uintptr_t> {}(0, seed);
  }
};

// reinterpret_cast from a function pointer is conditionally-supported; exclude function
// types so hash<void(*)()> fails on the constraint instead of inside reinterpret_cast.
template <class T>
requires (!std::is_function_v<T>)
struct hash<T *>
{
  using argument_type = T *;
  using result_type   = std::size_t;

  [[nodiscard]] std::size_t operator() (T *const ptr, std::uint64_t seed = 0) const noexcept
  {
    auto const address = reinterpret_cast<std::uintptr_t>(ptr);
    return hash<std::uintptr_t> {}(address, seed);
  }
};

template <class T>
struct hash<std::shared_ptr<T>>
{
  using argument_type = std::shared_ptr<T>;
  using result_type   = std::size_t;

  [[nodiscard]] std::size_t operator() (argument_type const &ptr, std::uint64_t seed = 0) const noexcept
  {
    return hash<T *> {}(ptr.get(), seed);
  }
};

template <class T, class Deleter>
struct hash<std::unique_ptr<T, Deleter>>
{
  using argument_type = std::unique_ptr<T, Deleter>;
  using result_type   = std::size_t;

  [[nodiscard]] std::size_t operator() (argument_type const &ptr, std::uint64_t seed = 0) const noexcept
  {
    return hash<T *> {}(ptr.get(), seed);
  }
};

template <class T>
struct seeded_hash;

template <>
struct seeded_hash<uint128_t>
{
  using argument_type = uint128_t;
  using result_type   = std::size_t;

  std::uint64_t seed {};

  [[nodiscard]] constexpr std::size_t operator() (argument_type const value) const noexcept
  {
    return hash<argument_type> {}(value, seed);
  }

  friend constexpr bool operator== (seeded_hash const &, seeded_hash const &) noexcept = default;
};

template <>
struct seeded_hash<int128_t>
{
  using argument_type = int128_t;
  using result_type   = std::size_t;

  std::uint64_t seed {};

  [[nodiscard]] constexpr std::size_t operator() (argument_type const value) const noexcept
  {
    return hash<argument_type> {}(value, seed);
  }

  friend constexpr bool operator== (seeded_hash const &, seeded_hash const &) noexcept = default;
};

template <hash_detail::scalar_hashable T>
struct seeded_hash<T>
{
  using argument_type = T;
  using result_type   = std::size_t;

  std::uint64_t seed {};

  [[nodiscard]] constexpr std::size_t operator() (argument_type const value) const noexcept
  {
    return hash<T> {}(value, seed);
  }

  friend constexpr bool operator== (seeded_hash const &, seeded_hash const &) noexcept = default;
};

template <class CharT, class Allocator>
struct seeded_hash<std::basic_string<CharT, std::char_traits<CharT>, Allocator>>
{
  using argument_type = std::basic_string<CharT, std::char_traits<CharT>, Allocator>;
  using result_type   = std::size_t;

  std::uint64_t seed {};

  template <hash_mode Mode = hash_standard_t>
  [[nodiscard]] constexpr std::size_t operator() (argument_type const &str) const noexcept
  {
    return hash<argument_type> {}.template operator()<Mode>(str, seed);
  }

  friend constexpr bool operator== (seeded_hash const &, seeded_hash const &) noexcept = default;
};

template <class CharT>
struct seeded_hash<std::basic_string_view<CharT, std::char_traits<CharT>>>
{
  using argument_type = std::basic_string_view<CharT, std::char_traits<CharT>>;
  using result_type   = std::size_t;

  std::uint64_t seed {};

  template <hash_mode Mode = hash_standard_t>
  [[nodiscard]] constexpr std::size_t operator() (argument_type const sv) const noexcept
  {
    return hash<argument_type> {}.template operator()<Mode>(sv, seed);
  }

  friend constexpr bool operator== (seeded_hash const &, seeded_hash const &) noexcept = default;
};

template <>
struct seeded_hash<std::nullptr_t>
{
  using argument_type = std::nullptr_t;
  using result_type   = std::size_t;

  std::uint64_t seed {};

  [[nodiscard]] constexpr std::size_t operator() (std::nullptr_t) const noexcept
  {
    return hash<std::nullptr_t> {}(nullptr, seed);
  }

  friend constexpr bool operator== (seeded_hash const &, seeded_hash const &) noexcept = default;
};

template <class T>
requires (!std::is_function_v<T>)
struct seeded_hash<T *>
{
  using argument_type = T *;
  using result_type   = std::size_t;

  std::uint64_t seed {};

  [[nodiscard]] std::size_t operator() (T *const ptr) const noexcept
  {
    return hash<T *> {}(ptr, seed);
  }

  friend constexpr bool operator== (seeded_hash const &, seeded_hash const &) noexcept = default;
};

template <class T>
struct seeded_hash<std::shared_ptr<T>>
{
  using argument_type = std::shared_ptr<T>;
  using result_type   = std::size_t;

  std::uint64_t seed {};

  [[nodiscard]] std::size_t operator() (argument_type const &ptr) const noexcept
  {
    return hash<T *> {}(ptr.get(), seed);
  }

  friend constexpr bool operator== (seeded_hash const &, seeded_hash const &) noexcept = default;
};

template <class T, class Deleter>
struct seeded_hash<std::unique_ptr<T, Deleter>>
{
  using argument_type = std::unique_ptr<T, Deleter>;
  using result_type   = std::size_t;

  std::uint64_t seed {};

  [[nodiscard]] std::size_t operator() (argument_type const &ptr) const noexcept
  {
    return hash<T *> {}(ptr.get(), seed);
  }

  friend constexpr bool operator== (seeded_hash const &, seeded_hash const &) noexcept = default;
};
} // namespace stdex
