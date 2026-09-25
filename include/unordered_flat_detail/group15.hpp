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

#include <cassert> // assert
#include <cstdint> // size_t
#include <memory>  // std::construct_at

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || (_M_IX86_FP >= 2)
#  include <emmintrin.h>
#elif (defined(__ARM_NEON__) || defined(__ARM_NEON)) && !defined(__ARM_BIG_ENDIAN)
#  include <arm_neon.h>
#endif

#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wattributes"
#elif defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wattributes"
#elif defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable : 5030)
#endif

namespace stdex::unordered_flat_detail
{
/*
 * fingerprint_hash_group15
 *   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *   |ofw|h14|h13|h13|h11|h10|h09|h08|h07|h06|h05|h04|h03|h02|h01|h00|
 *   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *
*/
struct group15
{
public:
  static constexpr unsigned char slot_available_mark = 0;
  static constexpr unsigned char slot_sentinel_mark  = 1;
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || (_M_IX86_FP >= 2) || \
    ((defined(__ARM_NEON__) || defined(__ARM_NEON)) && !defined(__ARM_BIG_ENDIAN))
  static constexpr bool is_simd_enabled = true;
#else
  static constexpr bool is_simd_enabled = false;
#endif
  /*
  static constexpr group15 make_dummy_group() noexcept
  {
    group15 g {};

    if constexpr (is_simd_enabled) {
      std::construct_at(&g.slot_fp_bytes_);
      g.slot_fp_bytes_[capacity() - 1] = slot_sentinel_mark;
    } else {
      std::construct_at(&g.slot_fp_words_);
      // The scalar layout interleaves slot bits across 16-bit lanes, so the
      // sentinel mark at slot N-1 lands on bit N-1 of the first word.
      g.slot_fp_words_[0] = uint64_t(slot_sentinel_mark) << (capacity() - 1);
      g.slot_fp_words_[1] = 0;
    }

    return g;
  }
  */

private:
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || (_M_IX86_FP >= 2)
  static constexpr uint32_t fingerprint_table[] = {
    0x08080808u,0x09090909u,0x02020202u,0x03030303u,0x04040404u,0x05050505u,
    0x06060606u,0x07070707u,0x08080808u,0x09090909u,0x0A0A0A0Au,0x0B0B0B0Bu,
    0x0C0C0C0Cu,0x0D0D0D0Du,0x0E0E0E0Eu,0x0F0F0F0Fu,0x10101010u,0x11111111u,
    0x12121212u,0x13131313u,0x14141414u,0x15151515u,0x16161616u,0x17171717u,
    0x18181818u,0x19191919u,0x1A1A1A1Au,0x1B1B1B1Bu,0x1C1C1C1Cu,0x1D1D1D1Du,
    0x1E1E1E1Eu,0x1F1F1F1Fu,0x20202020u,0x21212121u,0x22222222u,0x23232323u,
    0x24242424u,0x25252525u,0x26262626u,0x27272727u,0x28282828u,0x29292929u,
    0x2A2A2A2Au,0x2B2B2B2Bu,0x2C2C2C2Cu,0x2D2D2D2Du,0x2E2E2E2Eu,0x2F2F2F2Fu,
    0x30303030u,0x31313131u,0x32323232u,0x33333333u,0x34343434u,0x35353535u,
    0x36363636u,0x37373737u,0x38383838u,0x39393939u,0x3A3A3A3Au,0x3B3B3B3Bu,
    0x3C3C3C3Cu,0x3D3D3D3Du,0x3E3E3E3Eu,0x3F3F3F3Fu,0x40404040u,0x41414141u,
    0x42424242u,0x43434343u,0x44444444u,0x45454545u,0x46464646u,0x47474747u,
    0x48484848u,0x49494949u,0x4A4A4A4Au,0x4B4B4B4Bu,0x4C4C4C4Cu,0x4D4D4D4Du,
    0x4E4E4E4Eu,0x4F4F4F4Fu,0x50505050u,0x51515151u,0x52525252u,0x53535353u,
    0x54545454u,0x55555555u,0x56565656u,0x57575757u,0x58585858u,0x59595959u,
    0x5A5A5A5Au,0x5B5B5B5Bu,0x5C5C5C5Cu,0x5D5D5D5Du,0x5E5E5E5Eu,0x5F5F5F5Fu,
    0x60606060u,0x61616161u,0x62626262u,0x63636363u,0x64646464u,0x65656565u,
    0x66666666u,0x67676767u,0x68686868u,0x69696969u,0x6A6A6A6Au,0x6B6B6B6Bu,
    0x6C6C6C6Cu,0x6D6D6D6Du,0x6E6E6E6Eu,0x6F6F6F6Fu,0x70707070u,0x71717171u,
    0x72727272u,0x73737373u,0x74747474u,0x75757575u,0x76767676u,0x77777777u,
    0x78787878u,0x79797979u,0x7A7A7A7Au,0x7B7B7B7Bu,0x7C7C7C7Cu,0x7D7D7D7Du,
    0x7E7E7E7Eu,0x7F7F7F7Fu,0x80808080u,0x81818181u,0x82828282u,0x83838383u,
    0x84848484u,0x85858585u,0x86868686u,0x87878787u,0x88888888u,0x89898989u,
    0x8A8A8A8Au,0x8B8B8B8Bu,0x8C8C8C8Cu,0x8D8D8D8Du,0x8E8E8E8Eu,0x8F8F8F8Fu,
    0x90909090u,0x91919191u,0x92929292u,0x93939393u,0x94949494u,0x95959595u,
    0x96969696u,0x97979797u,0x98989898u,0x99999999u,0x9A9A9A9Au,0x9B9B9B9Bu,
    0x9C9C9C9Cu,0x9D9D9D9Du,0x9E9E9E9Eu,0x9F9F9F9Fu,0xA0A0A0A0u,0xA1A1A1A1u,
    0xA2A2A2A2u,0xA3A3A3A3u,0xA4A4A4A4u,0xA5A5A5A5u,0xA6A6A6A6u,0xA7A7A7A7u,
    0xA8A8A8A8u,0xA9A9A9A9u,0xAAAAAAAAu,0xABABABABu,0xACACACACu,0xADADADADu,
    0xAEAEAEAEu,0xAFAFAFAFu,0xB0B0B0B0u,0xB1B1B1B1u,0xB2B2B2B2u,0xB3B3B3B3u,
    0xB4B4B4B4u,0xB5B5B5B5u,0xB6B6B6B6u,0xB7B7B7B7u,0xB8B8B8B8u,0xB9B9B9B9u,
    0xBABABABAu,0xBBBBBBBBu,0xBCBCBCBCu,0xBDBDBDBDu,0xBEBEBEBEu,0xBFBFBFBFu,
    0xC0C0C0C0u,0xC1C1C1C1u,0xC2C2C2C2u,0xC3C3C3C3u,0xC4C4C4C4u,0xC5C5C5C5u,
    0xC6C6C6C6u,0xC7C7C7C7u,0xC8C8C8C8u,0xC9C9C9C9u,0xCACACACAu,0xCBCBCBCBu,
    0xCCCCCCCCu,0xCDCDCDCDu,0xCECECECEu,0xCFCFCFCFu,0xD0D0D0D0u,0xD1D1D1D1u,
    0xD2D2D2D2u,0xD3D3D3D3u,0xD4D4D4D4u,0xD5D5D5D5u,0xD6D6D6D6u,0xD7D7D7D7u,
    0xD8D8D8D8u,0xD9D9D9D9u,0xDADADADAu,0xDBDBDBDBu,0xDCDCDCDCu,0xDDDDDDDDu,
    0xDEDEDEDEu,0xDFDFDFDFu,0xE0E0E0E0u,0xE1E1E1E1u,0xE2E2E2E2u,0xE3E3E3E3u,
    0xE4E4E4E4u,0xE5E5E5E5u,0xE6E6E6E6u,0xE7E7E7E7u,0xE8E8E8E8u,0xE9E9E9E9u,
    0xEAEAEAEAu,0xEBEBEBEBu,0xECECECECu,0xEDEDEDEDu,0xEEEEEEEEu,0xEFEFEFEFu,
    0xF0F0F0F0u,0xF1F1F1F1u,0xF2F2F2F2u,0xF3F3F3F3u,0xF4F4F4F4u,0xF5F5F5F5u,
    0xF6F6F6F6u,0xF7F7F7F7u,0xF8F8F8F8u,0xF9F9F9F9u,0xFAFAFAFAu,0xFBFBFBFBu,
    0xFCFCFCFCu,0xFDFDFDFDu,0xFEFEFEFEu,0xFFFFFFFFu,
  };

  static constexpr int broadcast_fingerprint(size_t hash) noexcept
  {
    return static_cast<int>(fingerprint_table[static_cast<unsigned char>(hash)]);
  }

  static constexpr unsigned char get_fingerprint_byte(size_t hash) noexcept
  {
    return static_cast<unsigned char>(broadcast_fingerprint(hash));
  }
#else
  static constexpr unsigned char fingerprint_table[] = {
    8,  9,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
    48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
    64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
    80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95,
    96, 97, 98, 99, 100,101,102,103,104,105,106,107,108,109,110,111,
    112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,
    128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,
    144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,
    160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,
    176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,
    192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,
    208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,
    224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,
    240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255,
  };

  static constexpr unsigned char get_fingerprint_byte(size_t hash) noexcept
  {
    return fingerprint_table[static_cast<unsigned char>(hash)];
  }
#endif

public:
  static constexpr size_t capacity() noexcept { return 15; }

#if !defined(DEBUG) && !defined(_DEBUG)
  [[nodiscard, msvc::forceinline, gnu::always_inline]]
#endif 
  constexpr int fingerprint_mask(size_t hash) const noexcept
  {
    if constexpr (is_simd_enabled) {
      if consteval {
        auto fp  = get_fingerprint_byte(hash);
        int mask = 0;
        for (size_t i = 0; i < capacity(); ++i) {
          if (slot_fp_bytes_[i] == fp) mask |= (1 << i);
        }
        return mask;
      } else {
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || (_M_IX86_FP >= 2)
        return _mm_movemask_epi8(_mm_cmpeq_epi8(load_fp_vector(), _mm_set1_epi32(broadcast_fingerprint(hash)))) & 0x7F'FF;
#elif (defined(__ARM_NEON__) || defined(__ARM_NEON)) && !defined(__ARM_BIG_ENDIAN)
        return simde_mm_movemask_epi8(vceqq_u8(load_fp_vector(), vdupq_n_u8(get_fingerprint_byte(hash)))) & 0x7F'FF;
#endif
      }
    } else {
      constexpr uint64_t fp_match_slot_mask[] = {
        0x00'00'00'00'00'00'00'00ull,
        0x00'00'00'00'00'00'ff'ffull,
        0x00'00'00'00'ff'ff'00'00ull,
        0x00'00'00'00'ff'ff'ff'ffull,
        0x00'00'ff'ff'00'00'00'00ull,
        0x00'00'ff'ff'00'00'ff'ffull,
        0x00'00'ff'ff'ff'ff'00'00ull,
        0x00'00'ff'ff'ff'ff'ff'ffull,
        0xff'ff'00'00'00'00'00'00ull,
        0xff'ff'00'00'00'00'ff'ffull,
        0xff'ff'00'00'ff'ff'00'00ull,
        0xff'ff'00'00'ff'ff'ff'ffull,
        0xff'ff'ff'ff'00'00'00'00ull,
        0xff'ff'ff'ff'00'00'ff'ffull,
        0xff'ff'ff'ff'ff'ff'00'00ull,
        0xff'ff'ff'ff'ff'ff'ff'ffull,
      };

      size_t code = static_cast<size_t>(get_fingerprint_byte(hash));

      auto const low_nibble  = code & 0xFu;
      auto const high_nibble = code >> 4;
      uint64_t masked_low    = slot_fp_words_[0] ^ fp_match_slot_mask[low_nibble];
      uint64_t masked_high   = slot_fp_words_[1] ^ fp_match_slot_mask[high_nibble];
      uint64_t combined      = ~(masked_high | masked_low);

      uint32_t folded = static_cast<uint32_t>(combined & (combined >> 32));
      folded &= folded >> 16;

      return folded & 0x7F'FF;
    }
  }

  constexpr void reset() noexcept
  {
    if constexpr (is_simd_enabled) {
      if consteval {
        for (size_t i = 0; i < (sizeof(slot_fp_bytes_) / sizeof(slot_fp_bytes_[0])); ++i) slot_fp_bytes_[i] = 0;
      } else {
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || (_M_IX86_FP >= 2)
        _mm_store_si128(reinterpret_cast<__m128i *>(slot_fp_bytes_), _mm_setzero_si128());
#elif (defined(__ARM_NEON__) || defined(__ARM_NEON)) && !defined(__ARM_BIG_ENDIAN)
        vst1q_u8(reinterpret_cast<uint8_t *>(slot_fp_bytes_), vdupq_n_u8(0));
#endif
      }
    } else {
      slot_fp_words_[0] = slot_fp_words_[1] = 0;
    }
  }

  constexpr void reset(size_t slot_index) noexcept
  {
    assert(slot_index < capacity() && "slot index out of range");
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || (_M_IX86_FP >= 2) || \
    ((defined(__ARM_NEON__) || defined(__ARM_NEON)) && !defined(__ARM_BIG_ENDIAN))
    slot_fp_bytes_[slot_index] = slot_available_mark;
#else
    set_fingerprint_hash_impl(slot_index, slot_available_mark);
#endif
  }

  static constexpr void reset(unsigned char *byte) noexcept
  {
    if constexpr (is_simd_enabled) {
      *byte = slot_available_mark;
    } else {
      if consteval {
        // The scalar layout spreads one slot over several 16-bit lanes, so
        // clearing it needs the enclosing group; deriving that group from a
        // bare address requires pointer->integer, which is not a constant
        // expression.  Consteval callers must clear via reset(group, slot).
        throw;
      } else {
        size_t pos = reinterpret_cast<uintptr_t>(byte) % sizeof(group15);
        reinterpret_cast<group15 *>(byte - pos)->reset(pos);
      }
    }
  }

#if !defined(DEBUG) && !defined(_DEBUG)
  [[nodiscard, msvc::forceinline, gnu::always_inline]]
#endif
  constexpr void set_fingerprint_hash(size_t slot_index, size_t hash) noexcept
  {
    assert(slot_index < capacity() && "slot index out of range");
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || (_M_IX86_FP >= 2) || \
    ((defined(__ARM_NEON__) || defined(__ARM_NEON)) && !defined(__ARM_BIG_ENDIAN))
    slot_fp_bytes_[slot_index] = get_fingerprint_byte(hash);
#else
    set_fingerprint_hash_impl(slot_index, get_fingerprint_byte(hash));
#endif
  }

#if !defined(DEBUG) && !defined(_DEBUG)
  [[nodiscard, msvc::forceinline, gnu::always_inline]]
#endif
  constexpr void set_overflow_mark(size_t hash) noexcept
  {
    if constexpr (is_simd_enabled) {
      overflow() |= static_cast<unsigned char>(1u << (hash % 8));
    } else {
      if consteval {
        // Scalar layout: the overflow flag of lane j = hash % 8 lives at bit 15
        // of the j-th 16-bit lane, i.e. bit 15 + 16 * (j % 4) of word j / 4.
        auto const lane = uint64_t(0x80'00u) << (16 * (hash % 4));
        if ((hash % 8) < 4)
          slot_fp_words_[0] |= lane;
        else
          slot_fp_words_[1] |= lane;
      } else {
        reinterpret_cast<uint16_t *>(slot_fp_words_)[hash % 8] |= 0x80'00u;
      }
    }
  }

#if !defined(DEBUG) && !defined(_DEBUG)
  [[nodiscard, msvc::forceinline, gnu::always_inline]]
#endif
  constexpr bool is_overflow_by_hash(size_t hash) const noexcept
  {
    if constexpr (is_simd_enabled) {
      constexpr unsigned char shift[] = { 1, 2, 4, 8, 16, 32, 64, 128 };
      return (overflow() & shift[hash % 8]) != 0;
    } else {
      if consteval {
        auto const lane = uint64_t(0x80'00u) << (16 * (hash % 4));
        return (((hash % 8) < 4 ? slot_fp_words_[0] : slot_fp_words_[1]) & lane) != 0;
      } else {
        return reinterpret_cast<uint16_t const *>(slot_fp_words_)[hash % 8] & 0x80'00u;
      }
    }
  }

  constexpr bool is_overflow_by_slot(size_t slot_index) const noexcept
  {
    if constexpr (is_simd_enabled) {
      return is_overflow_by_hash(static_cast<size_t>(slot_fp_bytes_[slot_index]));
    } else {
      if consteval {
        auto const lane = uint64_t(0x80'00u) << (16 * (slot_index % 4));
        return (((slot_index % 8) < 4 ? slot_fp_words_[0] : slot_fp_words_[1]) & lane) != 0;
      } else {
        return reinterpret_cast<uint16_t const *>(slot_fp_words_)[slot_index % 8] & 0x80'00u;
      }
    }
  }

  /*
  static bool is_overflow(unsigned char const *byte)
  {
    size_t pos = reinterpret_cast<uintptr_t>(byte) % sizeof(group15);
    auto *ptr  = reinterpret_cast<group15 const *>(byte - pos);
    if constexpr (is_simd_enabled) {
      return ptr->is_overflow_by_hash(static_cast<size_t>(*byte));
    } else {
      uint64_t x = ((ptr->slot_fp_words_[0]) >> pos) & 0x00'01'00'01'00'01ull;
      uint32_t y = static_cast<uint32_t>(x | (x >> 15) | (x >> 30));
      return ptr->is_overflow_by_hash(static_cast<size_t>(y));
    }
  }
  */

  constexpr void set_sentinel_mark() noexcept
  {
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || (_M_IX86_FP >= 2) || \
    ((defined(__ARM_NEON__) || defined(__ARM_NEON)) && !defined(__ARM_BIG_ENDIAN))
    slot_fp_bytes_[capacity() - 1] = slot_sentinel_mark;
#else
    set_fingerprint_hash_impl(capacity() - 1, slot_sentinel_mark);
#endif
  }

  constexpr bool is_sentinel_by_slot(size_t slot_index) const noexcept
  {
    assert(slot_index < capacity() && "slot index out of range");
    if constexpr (is_simd_enabled) {
      return slot_index == capacity() - 1 && slot_fp_bytes_[slot_index] == slot_sentinel_mark;
    } else {
      return slot_index == capacity() - 1 &&
             (slot_fp_words_[0] & uint64_t(0x40'00'40'00'40'00'40'00ull)) == uint64_t(0x40'00ull) &&
             (slot_fp_words_[1] & uint64_t(0x40'00'40'00'40'00'40'00ull)) == 0;
    }
  }

  /*
  static constexpr bool is_sentinel(unsigned char const *byte)
  {
    return *byte == slot_sentinel_mark;
  }
  */

  constexpr bool is_occupied_by_slot(size_t slot_index) const noexcept
  {
    assert(slot_index < capacity() && "slot index out of range");
    if constexpr (is_simd_enabled) {
      return slot_fp_bytes_[slot_index] != slot_available_mark;
    } else {
      return ((slot_fp_words_[0] | slot_fp_words_[1]) & (0x00'01'00'01'00'01'00'01ull << slot_index)) != 0;
    }
  }

  /*
  static constexpr bool is_occupied(unsigned char const *byte)
  {
    return *byte != slot_available_mark;
  }
  */

  static constexpr int occupied_mask_excluding_sentinel(group15 const *curr, group15 const *sentinel_group) noexcept
  {
    // excluding the sentinel
    return curr->occupied_mask() & ~(static_cast<int>(curr == sentinel_group) << (capacity() - 1));
  }

#if !defined(DEBUG) && !defined(_DEBUG)
  [[nodiscard, msvc::forceinline, gnu::always_inline]]
#endif 
  constexpr int available_mask() const noexcept
  {
    if constexpr (is_simd_enabled) {
      if consteval {
        int mask = 0;
        for (size_t i = 0; i < capacity(); ++i) {
          if (slot_fp_bytes_[i] == slot_available_mark) mask |= (1 << i);
        }
        return mask;
      } else {
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || (_M_IX86_FP >= 2)
        return _mm_movemask_epi8(_mm_cmpeq_epi8(load_fp_vector(), _mm_setzero_si128())) & 0x7F'FF;
#elif (defined(__ARM_NEON__) || defined(__ARM_NEON)) && !defined(__ARM_BIG_ENDIAN)
        return simde_mm_movemask_epi8(vceqq_u8(load_fp_vector(), vdupq_n_u8(0))) & 0x7F'FF;
#endif
      }
    } else {
      uint64_t x = ~(slot_fp_words_[0] | slot_fp_words_[1]);
      uint32_t y = static_cast<uint32_t>(x & (x >> 32));
      y &= y >> 16;
      return y & 0x7F'FF;
    }
  }

  constexpr int occupied_mask() const noexcept
  {
    if constexpr (is_simd_enabled) {
      if consteval {
        int mask = 0;
        for (size_t i = 0; i < capacity(); ++i) {
          if (slot_fp_bytes_[i] != slot_available_mark) mask |= (1 << i);
        }
        return mask;
      } else {
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || (_M_IX86_FP >= 2)
        return (~available_mask()) & 0x7F'FF;
#elif (defined(__ARM_NEON__) || defined(__ARM_NEON)) && !defined(__ARM_BIG_ENDIAN)
        return simde_mm_movemask_epi8(vcgtq_u8(load_fp_vector(), vdupq_n_u8(0))) & 0x7F'FF;
#endif
      }
    } else {
      uint64_t x = slot_fp_words_[0] | slot_fp_words_[1];
      uint32_t y = static_cast<uint32_t>(x | (x >> 32));
      y |= y >> 16;
      return y & 0x7F'FF;
    }
  }

private:
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || (_M_IX86_FP >= 2)
  __m128i load_fp_vector() const
  {
    return _mm_load_si128(reinterpret_cast<__m128i const *>(slot_fp_bytes_));
  }
#elif (defined(__ARM_NEON__) || defined(__ARM_NEON)) && !defined(__ARM_BIG_ENDIAN)
  uint8x16_t load_fp_vector() const
  {
    return vld1q_u8(reinterpret_cast<uint8_t const *>(slot_fp_bytes_));
  }

  static int simde_mm_movemask_epi8(uint8x16_t input_vec)
  {
    static constexpr uint8_t bit_weights[16] = {
      1 << 0, 1 << 1, 1 << 2, 1 << 3, 1 << 4, 1 << 5, 1 << 6, 1 << 7,
      1 << 0, 1 << 1, 1 << 2, 1 << 3, 1 << 4, 1 << 5, 1 << 6, 1 << 7,
    };

    uint8x16_t weighted_values = vandq_u8(vld1q_u8(bit_weights), input_vec);

    uint8x8x2_t interleaved_pairs = vzip_u8(vget_low_u8(weighted_values), vget_high_u8(weighted_values));

    uint16x8_t combined_u16 = vreinterpretq_u16_u8(vcombine_u8(interleaved_pairs.val[0], interleaved_pairs.val[1]));

#  if defined(__ARM_ARCH_ISA_A64)
    return vaddvq_u16(combined_u16);
#  else
    uint64x2_t summed_u64 = vpaddlq_u32(vpaddlq_u16(combined_u16));
    return static_cast<int>(vgetq_lane_u64(summed_u64, 0)) + static_cast<int>(vgetq_lane_u64(summed_u64, 1));
#  endif
  }
#else

  // The scalar bit layout is expressed with plain integer masks so that the
  // constant-evaluated and runtime paths share one representation on platforms
  // without SIMD.
  constexpr void set_fingerprint_hash_impl(size_t slot_index, unsigned char fp_hash) noexcept
  {
    constexpr uint64_t write_masks[] = {
      0x00'00'00'00'00'00'00'00ull,
      0x00'00'00'00'00'00'00'01ull,
      0x00'00'00'00'00'01'00'00ull,
      0x00'00'00'00'00'01'00'01ull,
      0x00'00'00'01'00'00'00'00ull,
      0x00'00'00'01'00'00'00'01ull,
      0x00'00'00'01'00'01'00'00ull,
      0x00'00'00'01'00'01'00'01ull,
      0x00'01'00'00'00'00'00'00ull,
      0x00'01'00'00'00'00'00'01ull,
      0x00'01'00'00'00'01'00'00ull,
      0x00'01'00'00'00'01'00'01ull,
      0x00'01'00'01'00'00'00'00ull,
      0x00'01'00'01'00'00'00'01ull,
      0x00'01'00'01'00'01'00'00ull,
      0x00'01'00'01'00'01'00'01ull,
    };
    constexpr uint64_t clear_masks[] = {
      0x00'01'00'01'00'01'00'01ull,
      0x00'01'00'01'00'01'00'00ull,
      0x00'01'00'01'00'00'00'01ull,
      0x00'01'00'01'00'00'00'00ull,
      0x00'01'00'00'00'01'00'01ull,
      0x00'01'00'00'00'01'00'00ull,
      0x00'01'00'00'00'00'00'01ull,
      0x00'01'00'00'00'00'00'00ull,
      0x00'00'00'01'00'01'00'01ull,
      0x00'00'00'01'00'01'00'00ull,
      0x00'00'00'01'00'00'00'01ull,
      0x00'00'00'01'00'00'00'00ull,
      0x00'00'00'00'00'01'00'01ull,
      0x00'00'00'00'00'01'00'00ull,
      0x00'00'00'00'00'00'00'01ull,
      0x00'00'00'00'00'00'00'00ull,
    };
    auto const low_nibble  = fp_hash & 0xFu;
    auto const high_nibble = fp_hash >> 4;

    slot_fp_words_[0] |= (write_masks[low_nibble] << slot_index);
    slot_fp_words_[0] &= ~(clear_masks[low_nibble] << slot_index);

    slot_fp_words_[1] |= (write_masks[high_nibble] << slot_index);
    slot_fp_words_[1] &= ~(clear_masks[high_nibble] << slot_index);
  }
#endif

  constexpr unsigned char &overflow() noexcept { return slot_fp_bytes_[15]; }

  constexpr unsigned char const &overflow() const noexcept { return slot_fp_bytes_[15]; }

  union
  {
    alignas(16) unsigned char slot_fp_bytes_[16];
    alignas(16) uint64_t slot_fp_words_[2];
  };
};
} // namespace stdex::unordered_flat_detail

#if defined(__clang__)
#  pragma clang diagnostic pop
#elif defined(__GNUC__)
#  pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#  pragma warning(pop)
#endif
