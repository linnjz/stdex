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

#include <algorithm>    // std::copy_n
#include <array>        // std::array, std::to_array
#include <bit>          // std::endian, std::byteswap, std::bit_cast
#include <climits>      // CHAR_BIT
#include <concepts>     // std::same_as, std::integral, std::floating_point
#include <cstddef>      // std::size_t, size_t, std::byte, std::to_integer
#include <cstdint>      // uint64_t, uint32_t, uint8_t
#include <ranges>       // std::ranges::contiguous_range, std::ranges::sized_range,
                        // std::ranges::range_value_t, std::ranges::data, std::ranges::size
#include <span>         // std::span, std::as_bytes
#include <string_view>  // std::string_view
#include <type_traits>  // std::remove_cvref_t, std::is_enum_v,
                        // std::is_trivially_copyable_v, std::make_unsigned_t,
                        // std::underlying_type_t, std::conditional_t,
                        // std::remove_cv_t, std::is_same_v
#include <utility>      // std::unreachable, std::to_underlying

#if defined(_MSC_VER)
#  include <intrin.h>
#  if defined(_M_X64) && !defined(_M_ARM64EC)
#    pragma intrinsic(_umul128)
#  endif
#  if defined(_M_ARM64) || defined(_M_ARM64EC)
#    pragma intrinsic(__umulh)
#  endif
#endif

#if defined(__clang__) || defined(__GNUC__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wattributes"
#endif

namespace stdex::hash
{
// clang-format off
struct standard_t{};
struct micro_t{};
struct nano_t{};

inline constexpr standard_t standard {};
inline constexpr micro_t micro {};
inline constexpr nano_t nano {};
// clang-format on

namespace hash_detail
{
template <class T>
concept hash_mode =
    std::same_as<std::remove_cvref_t<T>, standard_t> ||
    std::same_as<std::remove_cvref_t<T>, micro_t> ||
    std::same_as<std::remove_cvref_t<T>, nano_t>;

template <hash_mode Mode>
struct mode_traits;

template <>
struct mode_traits<standard_t>
{
  static constexpr size_t stripe     = 112;
  static constexpr size_t lane_count = 7;
  static constexpr bool is_standard  = true;
  static constexpr bool is_micro     = false;
  static constexpr bool is_nano      = false;
};

template <>
struct mode_traits<micro_t>
{
  static constexpr size_t stripe     = 80;
  static constexpr size_t lane_count = 5;
  static constexpr bool is_standard  = false;
  static constexpr bool is_micro     = true;
  static constexpr bool is_nano      = false;
};

template <>
struct mode_traits<nano_t>
{
  static constexpr size_t stripe     = 48;
  static constexpr size_t lane_count = 3;
  static constexpr bool is_standard  = false;
  static constexpr bool is_micro     = false;
  static constexpr bool is_nano      = true;
};

inline constexpr auto default_secret = std::to_array<uint64_t>({
    0x2d'35'8d'cc'aa'6c'78'a5ULL,
    0x8b'b8'4b'93'96'2e'ac'c9ULL,
    0x4b'33'a6'2e'd4'33'd4'a3ULL,
    0x4d'5a'2d'a5'1d'e1'aa'47ULL,
    0xa0'76'1d'64'78'bd'64'2fULL,
    0xe7'03'7e'd1'a0'b4'28'dbULL,
    0x90'ed'17'65'28'1c'38'8cULL,
    0xaa'aa'aa'aa'aa'aa'aa'aaULL,
});

static_assert(CHAR_BIT == 8, "stdexhash requires 8-bit bytes");
static_assert(sizeof(uint32_t) == 4);
static_assert(sizeof(uint64_t) == 8);
static_assert(default_secret.size() == 8);

template <typename T>
concept byte_like = std::same_as<std::remove_cv_t<T>, std::byte> ||
                    std::same_as<std::remove_cv_t<T>, char> ||
                    std::same_as<std::remove_cv_t<T>, signed char> ||
                    std::same_as<std::remove_cv_t<T>, unsigned char> ||
                    std::same_as<std::remove_cv_t<T>, uint8_t>;

template <typename T>
concept scalar_hashable = std::integral<std::remove_cv_t<T>> ||
                          std::is_enum_v<std::remove_cv_t<T>> ||
                          std::floating_point<std::remove_cv_t<T>>;

template <typename Range>
concept contiguous_byte_range = std::ranges::contiguous_range<Range> &&
                                std::ranges::sized_range<Range> &&
                                hash_detail::byte_like<std::ranges::range_value_t<Range>>;

template <typename Range>
concept contiguous_object_range = std::ranges::contiguous_range<Range> &&
                                  std::ranges::sized_range<Range> &&
                                  std::is_trivially_copyable_v<std::ranges::range_value_t<Range>> &&
                                  (!hash_detail::byte_like<std::ranges::range_value_t<Range>>);

template <typename T>
struct unsigned_equivalent;

template <>
struct unsigned_equivalent<bool>
{
  using type = uint8_t;
};

template <std::integral T>
struct unsigned_equivalent<T>
{
  using type = std::make_unsigned_t<T>;
};

template <typename T>
requires std::is_enum_v<T>
struct unsigned_equivalent<T>
{
  using type = std::make_unsigned_t<std::underlying_type_t<T>>;
};

template <std::floating_point T>
struct unsigned_equivalent<T>
{
  static_assert(sizeof(T) == 4 || sizeof(T) == 8, "stdexhash supports IEEE-width 32-bit and 64-bit floating types");
  using type = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;
};

template <typename T>
using unsigned_equivalent_t = typename unsigned_equivalent<std::remove_cv_t<T>>::type;

struct uint128_t
{
  uint64_t low;
  uint64_t high;

  friend constexpr bool operator== (uint128_t, uint128_t) noexcept = default;
};

[[nodiscard, msvc::forceinline, gnu::always_inline]] constexpr uint128_t umul128_portable(uint64_t const lhs, uint64_t const rhs) noexcept
{
  uint64_t const lhs_low  = static_cast<uint32_t>(lhs);
  uint64_t const lhs_high = lhs >> 32U;
  uint64_t const rhs_low  = static_cast<uint32_t>(rhs);
  uint64_t const rhs_high = rhs >> 32U;

  uint64_t const low_product  = lhs_low * rhs_low;
  uint64_t const middle1      = lhs_high * rhs_low;
  uint64_t const middle2      = rhs_high * lhs_low;
  uint64_t const high_product = lhs_high * rhs_high;
  uint64_t const carry_base   = low_product + (middle1 << 32U);
  uint64_t const carry1       = carry_base < low_product ? 1U : 0U;
  uint64_t const low          = carry_base + (middle2 << 32U);
  uint64_t const carry2       = low < carry_base ? 1U : 0U;
  uint64_t const high         = high_product + (middle1 >> 32U) + (middle2 >> 32U) + carry1 + carry2;
  return { low, high };
}

[[nodiscard, msvc::forceinline, gnu::always_inline]] constexpr uint64_t umulh_portable(uint64_t const lhs, uint64_t const rhs) noexcept
{
  return umul128_portable(lhs, rhs).high;
}

[[nodiscard, msvc::forceinline, gnu::always_inline]] constexpr uint128_t umul128(uint64_t const lhs, uint64_t const rhs) noexcept
{
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64) || defined(_M_ARM64EC))
  if consteval {
    return umul128_portable(lhs, rhs);
  } else {
#  if defined(_M_X64) && !defined(_M_ARM64EC)
    uint64_t high {};
    uint64_t const low = _umul128(lhs, rhs, &high);
    return { low, high };
#  else
    return { lhs * rhs, __umulh(lhs, rhs) };
#  endif
  }
#elif defined(__SIZEOF_INT128__)
  auto const product = static_cast<__uint128_t>(lhs) * static_cast<__uint128_t>(rhs);
  return { static_cast<uint64_t>(product), static_cast<uint64_t>(product >> 64U) };
#else
  return umul128_portable(lhs, rhs);
#endif
}

[[nodiscard, msvc::forceinline, gnu::always_inline]] constexpr uint64_t umulh(uint64_t const lhs, uint64_t const rhs) noexcept
{
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64) || defined(_M_ARM64EC))
  if consteval {
    return umulh_portable(lhs, rhs);
  } else {
#  if defined(_M_X64) && !defined(_M_ARM64EC)
    uint64_t high {};
    (void) _umul128(lhs, rhs, &high);
    return high;
#  else
    return __umulh(lhs, rhs);
#  endif
  }
#elif defined(__SIZEOF_INT128__)
  return static_cast<uint64_t>((static_cast<__uint128_t>(lhs) * static_cast<__uint128_t>(rhs)) >> 64U);
#else
  return umulh_portable(lhs, rhs);
#endif
}

[[nodiscard, msvc::forceinline, gnu::always_inline]] constexpr uint64_t mix(uint64_t const lhs, uint64_t const rhs) noexcept
{
  auto const [low, high] = umul128(lhs, rhs);
  return low ^ high;
}

[[msvc::forceinline, gnu::always_inline]] constexpr void mum(uint64_t &lhs, uint64_t &rhs) noexcept
{
  auto const product = umul128(lhs, rhs);
  lhs                = product.low;
  rhs                = product.high;
}

template <byte_like T>
[[nodiscard]] constexpr uint8_t byte_value(T const value) noexcept
{
  if constexpr (std::same_as<std::remove_cv_t<T>, std::byte>) {
    return std::to_integer<uint8_t>(value);
  } else {
    return static_cast<uint8_t>(value);
  }
}

template <byte_like Byte>
[[nodiscard, msvc::forceinline, gnu::always_inline]] constexpr uint64_t read64_le(std::span<Byte const> const input, size_t const offset) noexcept
{
  std::array<Byte, 8> buf;
  std::copy_n(input.begin() + offset, buf.size(), buf.begin());
  if constexpr (std::endian::native == std::endian::big) {
    return std::byteswap(std::bit_cast<uint64_t>(buf));
  } else if constexpr (std::endian::native == std::endian::little) {
    return std::bit_cast<uint64_t>(buf);
  } else {
    std::unreachable();
  }
}

template <byte_like Byte>
[[nodiscard, msvc::forceinline, gnu::always_inline]] constexpr uint32_t read32_le(std::span<Byte const> const input, size_t const offset) noexcept
{
  std::array<Byte, 4> buf;
  std::copy_n(input.begin() + offset, buf.size(), buf.begin());

  if constexpr (std::endian::native == std::endian::big) {
    return std::byteswap(std::bit_cast<uint32_t>(buf));
  } else if constexpr (std::endian::native == std::endian::little) {
    return std::bit_cast<uint32_t>(buf);
  } else {
    std::unreachable();
  }
}

template <std::unsigned_integral UInt>
[[nodiscard]] constexpr auto little_endian_array(UInt const value) noexcept
{
  if constexpr (std::endian::native == std::endian::big) {
    return std::bit_cast<std::array<std::byte, sizeof(UInt)>>(std::byteswap(value));
  } else if constexpr (std::endian::native == std::endian::little) {
    return std::bit_cast<std::array<std::byte, sizeof(UInt)>>(value);
  } else {
    std::unreachable();
  }
}

template <byte_like Byte, hash_detail::hash_mode Mode>
[[nodiscard]] constexpr uint64_t hash_bytes_impl(Mode, std::span<Byte const> const input, uint64_t seed, std::array<uint64_t, 8> const &secret) noexcept
{
  size_t const length = input.size();
  seed ^= mix(seed ^ secret[2], secret[1]);
  uint64_t a {};
  uint64_t b {};
  size_t remaining = length;
  size_t offset {};

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
      a = (static_cast<uint64_t>(byte_value(input[0])) << 45U) | byte_value(input[length - 1]);
      b = byte_value(input[length >> 1U]);
    }
  } else {
    using traits = hash_detail::mode_traits<std::remove_cvref_t<Mode>>;

    if (remaining > traits::stripe) [[unlikely]] {
      std::array<uint64_t, 7> lane { seed, seed, seed, seed, seed, seed, seed };
      do {
        for (size_t index = 0; index < traits::lane_count; ++index) {
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

template <hash_detail::byte_like Byte, hash_detail::hash_mode Mode = standard_t>
[[nodiscard]] constexpr uint64_t hash_bytes(std::span<Byte const> const input, uint64_t const seed = 0, Mode = {}) noexcept
{
  [[assume(input.data() != nullptr)]];
  return hash_detail::hash_bytes_impl(Mode {}, input, seed, hash_detail::default_secret);
}

template <hash_detail::byte_like Byte, hash_detail::hash_mode Mode = standard_t>
[[nodiscard]] constexpr uint64_t hash_bytes(std::span<Byte const> const input, uint64_t const seed, std::array<uint64_t, 8> const &secret, Mode = {}) noexcept
{
  [[assume(input.data() != nullptr)]];
  return hash_detail::hash_bytes_impl(Mode {}, input, seed, secret);
}

template <hash_detail::byte_like Byte, hash_detail::hash_mode Mode = standard_t>
[[nodiscard]] constexpr uint64_t hash_bytes(void const *const data, size_t const size, uint64_t const seed = 0, Mode = {}) noexcept
{
  [[assume(data != nullptr)]];
  return hash_bytes(Mode {}, std::span<std::byte const> { static_cast<std::byte const *>(data), size }, seed);
}

template <hash_detail::contiguous_byte_range Range, hash_detail::hash_mode Mode = standard_t>
[[nodiscard]] constexpr uint64_t hash_range(Range const &range, uint64_t const seed = 0, Mode = {}) noexcept
{
  using byte_type = std::remove_cv_t<std::ranges::range_value_t<Range>>;
  return hash_bytes(Mode {}, std::span<byte_type const> { std::ranges::data(range), std::ranges::size(range) }, seed);
}

template <hash_detail::contiguous_object_range Range, hash_detail::hash_mode Mode = standard_t>
[[nodiscard]] constexpr uint64_t hash_range(Range const &range, uint64_t const seed = 0, Mode = {}) noexcept
{
  using value_type   = std::remove_cv_t<std::ranges::range_value_t<Range>>;
  auto const objects = std::span<value_type const> { std::ranges::data(range), std::ranges::size(range) };
  return hash_bytes(Mode {}, std::as_bytes(objects), seed);
}

template <hash_detail::hash_mode Mode = standard_t>
[[nodiscard]] constexpr uint64_t hash(std::string_view const text, uint64_t const seed = 0) noexcept
{
  return hash_bytes(Mode {}, std::span<char const> { text.data(), text.size() }, seed);
}

template <hash_detail::scalar_hashable T, hash_detail::hash_mode Mode = standard_t>
[[nodiscard]] constexpr uint64_t hash(T const value, uint64_t const seed = 0, Mode = {}) noexcept
{
  using clean_type    = std::remove_cv_t<T>;
  using unsigned_type = hash_detail::unsigned_equivalent_t<clean_type>;
  unsigned_type bits {};
  if constexpr (std::floating_point<clean_type>) {
    bits = std::bit_cast<unsigned_type>(value);
  } else if constexpr (std::is_enum_v<clean_type>) {
    bits = static_cast<unsigned_type>(std::to_underlying(value));
  } else {
    bits = static_cast<unsigned_type>(value);
  }
  auto const bytes = hash_detail::little_endian_array(bits);
  return hash_range(Mode {}, bytes, seed);
}
} // namespace stdex::hash

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif
