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

// test_bit_traits.cpp
//
// Verifies include/bit_traits.hpp: signed/unsigned reinterpretation, highest
// set bit, all-zero detection, bit reversal, and byte swapping across scalar
// and 128-bit paths.
#include <array>
#include <bit>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "bit_traits.hpp"
#include "int128.hpp"

namespace
{
int g_pass = 0;
int g_fail = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (cond) {                                                              \
      ++g_pass;                                                              \
    } else {                                                                 \
      ++g_fail;                                                              \
      if (g_fail < 20) {                                                     \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
      }                                                                      \
    }                                                                        \
  } while (0)

uint64_t rng_state = 0x9E37'79B9'7F4A'7C15ULL;
uint64_t next_rand()
{
  uint64_t z = (rng_state += 0x9E37'79B9'7F4A'7C15ULL);
  z = (z ^ (z >> 30U)) * 0xBF58'476D'1CE4'E5B9ULL;
  z = (z ^ (z >> 27U)) * 0x94D0'49BB'1331'11EBULL;
  return z ^ (z >> 31U);
}
} // namespace

constexpr std::uint64_t reverse_ref(std::uint64_t value) noexcept
{
  std::uint64_t result = 0;
  for (int i = 0; i < 64; ++i) {
    result = (result << 1U) | (value & 1U);
    value >>= 1U;
  }
  return result;
}

template <class UInt>
constexpr int popcount_ref(UInt value) noexcept
{
  int result = 0;
  for (; value != UInt {}; value = static_cast<UInt>(value >> 1U)) {
    result += static_cast<int>(value & UInt { 1 });
  }
  return result;
}

template <class UInt>
constexpr int countl_zero_ref(UInt value) noexcept
{
  constexpr int width = static_cast<int>(sizeof(UInt) * CHAR_BIT);
  if (value == UInt {}) {
    return width;
  }

  int result = 0;
  UInt mask = static_cast<UInt>(UInt { 1 } << (width - 1));
  while ((value & mask) == UInt {}) {
    ++result;
    mask = static_cast<UInt>(mask >> 1U);
  }
  return result;
}

template <class UInt>
constexpr int countr_zero_ref(UInt value) noexcept
{
  constexpr int width = static_cast<int>(sizeof(UInt) * CHAR_BIT);
  if (value == UInt {}) {
    return width;
  }

  int result = 0;
  while ((value & UInt { 1 }) == UInt {}) {
    ++result;
    value = static_cast<UInt>(value >> 1U);
  }
  return result;
}

template <class UInt>
constexpr int bit_width_ref(UInt value) noexcept
{
  constexpr int width = static_cast<int>(sizeof(UInt) * CHAR_BIT);
  return width - countl_zero_ref(value);
}

template <class UInt>
constexpr UInt bit_floor_ref(UInt value) noexcept
{
  if (value == UInt {}) {
    return UInt {};
  }
  return static_cast<UInt>(UInt { 1 } << (bit_width_ref(value) - 1));
}

template <class UInt>
constexpr UInt bit_ceil_ref(UInt value) noexcept
{
  constexpr int width = static_cast<int>(sizeof(UInt) * CHAR_BIT);
  if (value <= UInt { 1 }) {
    return UInt { 1 };
  }
  int const result_width = bit_width_ref(value);
  if (result_width >= width) {
    UInt const top = static_cast<UInt>(UInt { 1 } << (width - 1));
    return value == top ? value : UInt {};
  }
  return static_cast<UInt>(UInt { 1 } << result_width);
}

template <class UInt>
constexpr UInt shl_ref(UInt value, int count) noexcept
{
  constexpr int width = static_cast<int>(sizeof(UInt) * CHAR_BIT);
  if (count < 0) {
    if (-count >= width) {
      return UInt {};
    }
    return static_cast<UInt>(value >> -count);
  }
  if (count >= width) {
    return UInt {};
  }
  return static_cast<UInt>(value << count);
}

template <class UInt>
constexpr UInt shr_ref(UInt value, int count) noexcept
{
  constexpr int width = static_cast<int>(sizeof(UInt) * CHAR_BIT);
  if (count < 0) {
    if (-count >= width) {
      return UInt {};
    }
    return static_cast<UInt>(value << -count);
  }
  if (count >= width) {
    return UInt {};
  }
  return static_cast<UInt>(value >> count);
}

template <class UInt>
constexpr UInt shr_signed_ref(UInt value, bool negative, int count) noexcept;

template <class UInt>
constexpr UInt shl_signed_ref(UInt value, bool negative, int count) noexcept
{
  constexpr int width = static_cast<int>(sizeof(UInt) * CHAR_BIT);
  if (count < 0) {
    if (-count >= width) {
      return negative ? static_cast<UInt>(~UInt {}) : UInt {};
    }
    return shr_signed_ref(value, negative, -count);
  }
  if (count >= width) {
    return UInt {};
  }
  return static_cast<UInt>(value << count);
}

template <class UInt>
constexpr UInt shr_signed_ref(UInt value, bool negative, int count) noexcept
{
  constexpr int width = static_cast<int>(sizeof(UInt) * CHAR_BIT);
  if (count < 0) {
    if (-count >= width) {
      return UInt {};
    }
    return shl_signed_ref(value, negative, -count);
  }
  if (count >= width) {
    return negative ? static_cast<UInt>(~UInt {}) : UInt {};
  }
  if (count == 0) {
    return value;
  }

  UInt result = static_cast<UInt>(value >> count);
  if (negative) {
    result = static_cast<UInt>(result | static_cast<UInt>(static_cast<UInt>(~UInt {}) << (width - count)));
  }
  return result;
}

template <class UInt>
constexpr UInt rotl_ref(UInt value, int count) noexcept
{
  constexpr int width = static_cast<int>(sizeof(UInt) * CHAR_BIT);
  int amount = count % width;
  if (amount < 0) {
    amount += width;
  }
  if (amount == 0) {
    return value;
  }
  return static_cast<UInt>((value << amount) | (value >> (width - amount)));
}

template <class UInt>
constexpr UInt rotr_ref(UInt value, int count) noexcept
{
  constexpr int width = static_cast<int>(sizeof(UInt) * CHAR_BIT);
  int amount = count % width;
  if (amount < 0) {
    amount += width;
  }
  if (amount == 0) {
    return value;
  }
  return static_cast<UInt>((value >> amount) | (value << (width - amount)));
}

template <class UInt>
constexpr UInt bit_repeat_ref(UInt pattern, int length) noexcept
{
  constexpr int width = static_cast<int>(sizeof(UInt) * CHAR_BIT);
  if (length <= 0) {
    return UInt {};
  }
  if (length >= width) {
    return pattern;
  }

  UInt result {};
  for (int i = 0; i < width; ++i) {
    result = static_cast<UInt>(result | static_cast<UInt>(((pattern >> (i % length)) & UInt { 1 }) << i));
  }
  return result;
}

template <class UInt>
constexpr UInt bit_compress_ref(UInt value, UInt mask) noexcept
{
  constexpr int width = static_cast<int>(sizeof(UInt) * CHAR_BIT);
  UInt result {};
  int j = 0;
  for (int i = 0; i < width; ++i) {
    if (((mask >> i) & UInt { 1 }) != UInt {}) {
      result = static_cast<UInt>(result | static_cast<UInt>(((value >> i) & UInt { 1 }) << j));
      ++j;
    }
  }
  return result;
}

template <class UInt>
constexpr UInt bit_expand_ref(UInt value, UInt mask) noexcept
{
  constexpr int width = static_cast<int>(sizeof(UInt) * CHAR_BIT);
  UInt result {};
  int j = 0;
  for (int i = 0; i < width; ++i) {
    if (((mask >> i) & UInt { 1 }) != UInt {}) {
      result = static_cast<UInt>(result | static_cast<UInt>(((value >> j) & UInt { 1 }) << i));
      ++j;
    }
  }
  return result;
}

template <class UInt>
constexpr int countl_one_ref(UInt value) noexcept
{
  return countl_zero_ref(static_cast<UInt>(~value));
}

template <class UInt>
constexpr int countr_one_ref(UInt value) noexcept
{
  return countr_zero_ref(static_cast<UInt>(~value));
}

template <class UInt>
constexpr bool has_single_bit_ref(UInt value) noexcept
{
  return popcount_ref(value) == 1;
}

constexpr stdex::int128_t int128_from_bits(stdex::uint128_t const bits) noexcept
{
  return stdex::int128_t::from_words(bits.lo(), bits.hi());
}

static_assert(stdex::to_unsigned(stdex::int128_t::from_i64(-1)) == ~stdex::uint128_t {});
static_assert(stdex::to_unsigned(std::int8_t { -1 }) == std::uint8_t { 0xFF });
static_assert(stdex::highest_one_bit_index(stdex::uint128_t {}) == 0);
static_assert(stdex::highest_one_bit_index(stdex::uint128_t { 0, 1 }) == 64);
static_assert(stdex::highest_one_bit_index(stdex::uint128_t { 0, 0x8000'0000'0000'0000ULL }) == 127);
// The constructor is (low, high); bit 123 lives in the 0x0FFF... high word.
static_assert(stdex::highest_one_bit_index(stdex::uint128_t { 0xFFFF'FFFF'FFFF'FFFFULL, 0x0FFF'FFFF'FFFF'FFFFULL }) == 123);
static_assert(stdex::highest_one_bit_index(stdex::uint128_t { 0x0FFF'FFFF'FFFF'FFFFULL, 0xFFFF'FFFF'FFFF'FFFFULL }) == 127);
static_assert(stdex::is_all_bits_zero(stdex::uint128_t {}));
static_assert(!stdex::is_all_bits_zero(stdex::uint128_t { 0, 1 }));
static_assert(stdex::bit_reverse<std::uint8_t>(0x01) == 0x80);
static_assert(stdex::bit_reverse<std::uint8_t>(0xF0) == 0x0F);
static_assert(stdex::bit_reverse<std::uint16_t>(0x00'01) == 0x80'00);
static_assert(stdex::bit_reverse<std::int8_t>(-128) == std::int8_t { 0x01 });
static_assert(stdex::bit_reverse<std::uint32_t>(0x0000'0001) == 0x8000'0000);
static_assert(stdex::bit_reverse<std::uint64_t>(0x0000'0000'0000'0001ULL) == 0x8000'0000'0000'0000ULL);
static_assert(stdex::bit_reverse(stdex::uint128_t { 1, 0 }) == (stdex::uint128_t { 0, 0x8000'0000'0000'0000ULL }));
static_assert(stdex::bit_reverse(stdex::uint128_t { 0, 1 }) == (stdex::uint128_t { 0x8000'0000'0000'0000ULL, 0 }));
static_assert(stdex::bit_reverse(stdex::int128_t { 1, 0 }) ==
              (stdex::int128_t { 0, 0x8000'0000'0000'0000ULL }));
static_assert(stdex::bit_reverse(stdex::int128_t::from_i64(-1)) == stdex::int128_t::from_i64(-1));
static_assert(stdex::byteswap(std::uint8_t { 0xAB }) == 0xAB);
static_assert(stdex::byteswap(std::uint16_t { 0x1234 }) == 0x3412);
static_assert(stdex::byteswap(std::uint32_t { 0x1234'5678 }) == 0x7856'3412);
static_assert(stdex::byteswap(std::uint64_t { 0x0123'4567'89AB'CDEFULL }) == 0xEFCD'AB89'6745'2301ULL);
static_assert(stdex::byteswap(std::int32_t { 0x1234'5678 }) == std::int32_t { 0x7856'3412 });
static_assert(stdex::byteswap(stdex::uint128_t { 0x0123'4567'89AB'CDEFULL, 0xFEDC'BA98'7654'3210ULL }) ==
              (stdex::uint128_t { 0x1032'5476'98BA'DCFEULL, 0xEFCD'AB89'6745'2301ULL }));
static_assert(stdex::byteswap(stdex::int128_t { 0x0123'4567'89AB'CDEFULL, 0xFEDC'BA98'7654'3210ULL }) ==
              (stdex::int128_t { 0x1032'5476'98BA'DCFEULL, 0xEFCD'AB89'6745'2301ULL }));
static_assert(stdex::byteswap(stdex::byteswap(
                  stdex::uint128_t { 0x0123'4567'89AB'CDEFULL, 0xFEDC'BA98'7654'3210ULL })) ==
              (stdex::uint128_t { 0x0123'4567'89AB'CDEFULL, 0xFEDC'BA98'7654'3210ULL }));
static_assert(stdex::byteswap(stdex::byteswap(
                  stdex::int128_t { 0x0123'4567'89AB'CDEFULL, 0xFEDC'BA98'7654'3210ULL })) ==
              (stdex::int128_t { 0x0123'4567'89AB'CDEFULL, 0xFEDC'BA98'7654'3210ULL }));
static_assert(!stdex::has_single_bit(std::uint8_t {}));
static_assert(stdex::has_single_bit(std::uint8_t { 0x80 }));
static_assert(stdex::bit_floor(std::uint8_t {}) == 0);
static_assert(stdex::bit_floor(std::uint8_t { 0xFF }) == 0x80);
static_assert(stdex::bit_ceil(std::uint8_t {}) == 1);
static_assert(stdex::bit_ceil(std::uint8_t { 0x80 }) == 0x80);
static_assert(stdex::bit_ceil(std::uint8_t { 0x81 }) == 0);
static_assert(stdex::bit_width(std::uint8_t {}) == 0);
static_assert(stdex::bit_width(std::uint8_t { 0xFF }) == 8);
static_assert(stdex::countl_zero(std::uint8_t { 1 }) == 7);
static_assert(stdex::countl_one(std::uint8_t { 0xF0 }) == 4);
static_assert(stdex::countr_zero(std::uint8_t { 0x80 }) == 7);
static_assert(stdex::countr_one(std::uint8_t { 0x0F }) == 4);
static_assert(stdex::popcount(std::uint8_t { 0xF0 }) == 4);
static_assert(stdex::lowest_one_bit_index(std::uint8_t {}) == 0);
static_assert(stdex::lowest_one_bit_index(std::uint8_t { 0x10 }) == 4);
static_assert(stdex::shl(std::uint8_t { 1 }, 7) == 0x80);
static_assert(stdex::shl(std::uint8_t { 1 }, 8) == 0);
static_assert(stdex::shl(std::uint8_t { 1 }, -1) == 0);
static_assert(stdex::shr(std::uint8_t { 2 }, -1) == 4);
static_assert(stdex::shl(std::int8_t { 1 }, 7) == std::int8_t { -128 });
static_assert(stdex::shl(std::int8_t { -1 }, -1) == -1);
static_assert(stdex::shl(std::int8_t { -1 }, -8) == -1);
static_assert(stdex::shl(std::int8_t { -1 }, -9) == -1);
static_assert(stdex::shr(std::int8_t { -8 }, 1) == -4);
static_assert(stdex::shr(std::int8_t { -1 }, 8) == -1);
static_assert(stdex::shr(std::int8_t { -1 }, 9) == -1);
static_assert(stdex::rotl(std::uint8_t { 0x81 }, 1) == 0x03);
static_assert(stdex::rotl(std::uint8_t { 0x81 }, -1) == 0xC0);
static_assert(stdex::rotr(std::uint8_t { 0x81 }, 1) == 0xC0);
static_assert(stdex::rotr(std::uint8_t { 0x81 }, -1) == 0x03);
static_assert(stdex::bit_repeat(std::uint8_t { 0x0C }, 4) == 0xCC);
static_assert(stdex::bit_compress(std::uint8_t { 0xAA }, std::uint8_t { 0x0F }) == 0x0A);
static_assert(stdex::bit_expand(std::uint8_t { 0x0F }, std::uint8_t { 0xAA }) == 0xAA);

constexpr stdex::uint128_t high_bit_128 = stdex::uint128_t { 0, 0x8000'0000'0000'0000ULL };
static_assert(stdex::has_single_bit(high_bit_128));
static_assert(stdex::bit_floor(high_bit_128) == high_bit_128);
static_assert(stdex::bit_ceil(high_bit_128) == high_bit_128);
static_assert(stdex::bit_ceil(stdex::uint128_t { 1 }) == stdex::uint128_t { 1 });
static_assert(stdex::bit_width(high_bit_128) == 128);
static_assert(stdex::countl_zero(stdex::uint128_t {}) == 128);
static_assert(stdex::countl_one(~stdex::uint128_t {}) == 128);
static_assert(stdex::countr_zero(stdex::uint128_t {}) == 128);
static_assert(stdex::countr_one(~stdex::uint128_t {}) == 128);
static_assert(stdex::popcount(~stdex::uint128_t {}) == 128);
static_assert(stdex::lowest_one_bit_index(high_bit_128) == 127);
static_assert(stdex::shl(stdex::uint128_t { 1 }, 127) == high_bit_128);
static_assert(stdex::shl(high_bit_128, 1) == stdex::uint128_t {});
static_assert(stdex::shr(high_bit_128, 127) == stdex::uint128_t { 1 });
static_assert(stdex::shr(high_bit_128, 128) == stdex::uint128_t {});
static_assert(stdex::shl(stdex::uint128_t { 1 }, -1) == stdex::uint128_t {});
static_assert(stdex::shr(stdex::uint128_t { 2 }, -1) == stdex::uint128_t { 4 });
static_assert(stdex::rotl(high_bit_128, 1) == stdex::uint128_t { 1 });
static_assert(stdex::rotr(stdex::uint128_t { 1 }, 1) == high_bit_128);
static_assert(stdex::bit_repeat(stdex::uint128_t { 0x0C }, 4) ==
              stdex::uint128_t::from_words(0xCCCC'CCCC'CCCC'CCCCULL, 0xCCCC'CCCC'CCCC'CCCCULL));

constexpr stdex::int128_t negative_one_128 = stdex::int128_t::from_i64(-1);
static_assert(stdex::countl_zero(negative_one_128) == 0);
static_assert(stdex::countl_one(negative_one_128) == 128);
static_assert(stdex::countr_zero(negative_one_128) == 0);
static_assert(stdex::countr_one(negative_one_128) == 128);
static_assert(stdex::popcount(negative_one_128) == 128);
static_assert(stdex::bit_width(negative_one_128) == 128);
static_assert(stdex::bit_floor(stdex::int128_t::from_i64(-8)) == stdex::int128_t::from_words(0, 0x8000'0000'0000'0000ULL));
static_assert(stdex::bit_ceil(stdex::int128_t::from_i64(-8)) == stdex::int128_t {});
static_assert(stdex::shl(negative_one_128, 127) == stdex::int128_t::from_words(0, 0x8000'0000'0000'0000ULL));
static_assert(stdex::shr(negative_one_128, 127) == negative_one_128);
static_assert(stdex::shr(negative_one_128, 128) == negative_one_128);
static_assert(stdex::shl(negative_one_128, 128) == stdex::int128_t {});
static_assert(stdex::shl(stdex::int128_t::from_i64(-8), -1) == stdex::int128_t::from_i64(-4));
static_assert(stdex::shr(stdex::int128_t::from_i64(-8), -1) == stdex::int128_t::from_i64(-16));
static_assert(stdex::rotl(stdex::int128_t::from_i64(-8), 1) == stdex::int128_t::from_i64(-15));
static_assert(stdex::lowest_one_bit_index(stdex::int128_t::from_i64(-8)) == 3);

bool test_bit_traits()
{
  int const fail_mark = g_fail;

  for (std::uint32_t i = 0; i < 256; ++i) {
    auto const v   = static_cast<std::uint8_t>(i);
    auto const rev = stdex::bit_reverse(v);
    CHECK(rev == static_cast<std::uint8_t>(reverse_ref(v) >> 56U));
    CHECK(stdex::bit_reverse(rev) == v);
  }
  for (std::uint32_t i = 0; i < 65536; i += 257) {
    auto const v   = static_cast<std::uint16_t>(i);
    auto const rev = stdex::bit_reverse(v);
    CHECK(rev == static_cast<std::uint16_t>(reverse_ref(v) >> 48U));
    CHECK(stdex::bit_reverse(rev) == v);
  }
  for (int i = 0; i < 2000; ++i) {
    std::uint64_t const v = next_rand();
    CHECK(stdex::bit_reverse(v) == reverse_ref(v));
    CHECK(stdex::bit_reverse(stdex::bit_reverse(v)) == v);

    stdex::uint128_t const w { next_rand(), next_rand() };
    stdex::uint128_t const expected { reverse_ref(w.hi()), reverse_ref(w.lo()) };
    CHECK(stdex::bit_reverse(w) == expected);
    CHECK(stdex::bit_reverse(stdex::bit_reverse(w)) == w);

    auto const bytes = std::bit_cast<std::array<std::byte, sizeof(w)>>(w);
    std::array<std::byte, sizeof(w)> reversed {};
    for (std::size_t j = 0; j < reversed.size(); ++j) {
      reversed[j] = bytes[bytes.size() - 1U - j];
    }
    CHECK(stdex::byteswap(w) == std::bit_cast<stdex::uint128_t>(reversed));
    CHECK(stdex::byteswap(stdex::byteswap(w)) == w);
  }
  for (int i = 0; i < 500; ++i) {
    std::uint64_t const hi = next_rand();
    std::uint64_t const lo = next_rand();
    std::size_t const expected =
        hi != 0 ? 64U + stdex::highest_one_bit_index(hi) : stdex::highest_one_bit_index(lo);
    CHECK(stdex::highest_one_bit_index(stdex::uint128_t { lo, hi }) == expected);
  }

  for (std::uint32_t i = 0; i < 256; ++i) {
    auto const v = static_cast<std::uint8_t>(i);
    CHECK(stdex::countl_zero(v) == countl_zero_ref(v));
    CHECK(stdex::countl_one(v) == countl_one_ref(v));
    CHECK(stdex::countr_zero(v) == countr_zero_ref(v));
    CHECK(stdex::countr_one(v) == countr_one_ref(v));
    CHECK(stdex::popcount(v) == popcount_ref(v));
    CHECK(stdex::has_single_bit(v) == has_single_bit_ref(v));
    CHECK(stdex::bit_width(v) == bit_width_ref(v));
    CHECK(stdex::bit_floor(v) == bit_floor_ref(v));
    CHECK(stdex::bit_ceil(v) == bit_ceil_ref(v));
    CHECK(stdex::lowest_one_bit_index(v) == (v == 0 ? 0U : static_cast<std::size_t>(countr_zero_ref(v))));

    for (std::uint32_t j = 0; j < 256; ++j) {
      auto const mask = static_cast<std::uint8_t>(j);
      CHECK(stdex::bit_compress(v, mask) == bit_compress_ref(v, mask));
      CHECK(stdex::bit_expand(v, mask) == bit_expand_ref(v, mask));
    }
    for (int length = -2; length <= 9; ++length) {
      CHECK(stdex::bit_repeat(v, length) == bit_repeat_ref(v, length));
    }
  }

  for (int i = 0; i < 2000; ++i) {
    std::uint64_t const v = next_rand();
    int const count       = static_cast<int>(next_rand() % 401U) - 200;

    CHECK(stdex::shl(v, count) == shl_ref(v, count));
    CHECK(stdex::shr(v, count) == shr_ref(v, count));
    CHECK(stdex::rotl(v, count) == rotl_ref(v, count));
    CHECK(stdex::rotr(v, count) == rotr_ref(v, count));

    auto const s = std::bit_cast<std::int64_t>(v);
    bool const negative = s < 0;
    CHECK(stdex::countl_zero(s) == countl_zero_ref(v));
    CHECK(stdex::countl_one(s) == countl_one_ref(v));
    CHECK(stdex::countr_zero(s) == countr_zero_ref(v));
    CHECK(stdex::countr_one(s) == countr_one_ref(v));
    CHECK(stdex::popcount(s) == popcount_ref(v));
    CHECK(stdex::has_single_bit(s) == has_single_bit_ref(v));
    CHECK(stdex::bit_width(s) == bit_width_ref(v));
    CHECK(stdex::bit_floor(s) == std::bit_cast<std::int64_t>(bit_floor_ref(v)));
    CHECK(stdex::bit_ceil(s) == std::bit_cast<std::int64_t>(bit_ceil_ref(v)));
    CHECK(stdex::lowest_one_bit_index(s) == (v == 0 ? 0U : static_cast<std::size_t>(countr_zero_ref(v))));
    CHECK(stdex::shl(s, count) == std::bit_cast<std::int64_t>(shl_signed_ref(v, negative, count)));
    CHECK(stdex::shr(s, count) == std::bit_cast<std::int64_t>(shr_signed_ref(v, negative, count)));
    CHECK(stdex::rotl(s, count) == std::bit_cast<std::int64_t>(rotl_ref(v, count)));
    CHECK(stdex::rotr(s, count) == std::bit_cast<std::int64_t>(rotr_ref(v, count)));
  }

  for (int i = 0; i < 1000; ++i) {
    stdex::uint128_t const v { next_rand(), next_rand() };
    int const count = static_cast<int>(next_rand() % 401U) - 200;

    CHECK(stdex::countl_zero(v) == countl_zero_ref(v));
    CHECK(stdex::countl_one(v) == countl_one_ref(v));
    CHECK(stdex::countr_zero(v) == countr_zero_ref(v));
    CHECK(stdex::countr_one(v) == countr_one_ref(v));
    CHECK(stdex::popcount(v) == popcount_ref(v));
    CHECK(stdex::has_single_bit(v) == has_single_bit_ref(v));
    CHECK(stdex::bit_width(v) == bit_width_ref(v));
    CHECK(stdex::bit_floor(v) == bit_floor_ref(v));
    CHECK(stdex::bit_ceil(v) == bit_ceil_ref(v));
    CHECK(stdex::lowest_one_bit_index(v) ==
          (v == stdex::uint128_t {} ? 0U : static_cast<std::size_t>(countr_zero_ref(v))));
    CHECK(stdex::shl(v, count) == shl_ref(v, count));
    CHECK(stdex::shr(v, count) == shr_ref(v, count));
    CHECK(stdex::rotl(v, count) == rotl_ref(v, count));
    CHECK(stdex::rotr(v, count) == rotr_ref(v, count));

    stdex::uint128_t const mask { next_rand(), next_rand() };
    CHECK(stdex::bit_compress(v, mask) == bit_compress_ref(v, mask));
    CHECK(stdex::bit_expand(v, mask) == bit_expand_ref(v, mask));

    int const length = static_cast<int>(next_rand() % 132U) - 2;
    CHECK(stdex::bit_repeat(v, length) == bit_repeat_ref(v, length));

    stdex::int128_t const s = int128_from_bits(v);
    bool const negative     = (v.hi() >> 63U) != 0;
    CHECK(stdex::countl_zero(s) == countl_zero_ref(v));
    CHECK(stdex::countl_one(s) == countl_one_ref(v));
    CHECK(stdex::countr_zero(s) == countr_zero_ref(v));
    CHECK(stdex::countr_one(s) == countr_one_ref(v));
    CHECK(stdex::popcount(s) == popcount_ref(v));
    CHECK(stdex::has_single_bit(s) == has_single_bit_ref(v));
    CHECK(stdex::bit_width(s) == bit_width_ref(v));
    CHECK(stdex::bit_floor(s) == int128_from_bits(bit_floor_ref(v)));
    CHECK(stdex::bit_ceil(s) == int128_from_bits(bit_ceil_ref(v)));
    CHECK(stdex::lowest_one_bit_index(s) ==
          (v == stdex::uint128_t {} ? 0U : static_cast<std::size_t>(countr_zero_ref(v))));
    CHECK(stdex::shl(s, count) == int128_from_bits(shl_signed_ref(v, negative, count)));
    CHECK(stdex::shr(s, count) == int128_from_bits(shr_signed_ref(v, negative, count)));
    CHECK(stdex::rotl(s, count) == int128_from_bits(rotl_ref(v, count)));
    CHECK(stdex::rotr(s, count) == int128_from_bits(rotr_ref(v, count)));
    CHECK(stdex::bit_compress(s, int128_from_bits(mask)) == int128_from_bits(bit_compress_ref(v, mask)));
    CHECK(stdex::bit_expand(s, int128_from_bits(mask)) == int128_from_bits(bit_expand_ref(v, mask)));
    CHECK(stdex::bit_repeat(s, length) == int128_from_bits(bit_repeat_ref(v, length)));
    CHECK(stdex::bit_reverse(s) ==
          int128_from_bits(stdex::uint128_t { reverse_ref(v.hi()), reverse_ref(v.lo()) }));
  }

  return g_fail == fail_mark;
}

int main()
{
  std::printf("\n=== bit traits ===\n");
  test_bit_traits();

  std::printf("\n===============================\n");
  std::printf("PASS: %d   FAIL: %d\n", g_pass, g_fail);
  std::printf("===============================\n");
  return g_fail == 0 ? 0 : 1;
}
